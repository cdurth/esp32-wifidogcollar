#include <WiFi.h>
#include <WebServer.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <secrets.h>
// settings
bool httpEnabled = false;
bool telegramEnabled = true;
bool debugMode = true;

// Wi-Fi credentials
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

// Telegram credentials
const char* botToken = SECRET_BOTTOKEN;
const char* chatID = SECRET_CHATID;

// esp32 hardware watchdog stuff
#define WDT_TIMEOUT 5000
#define CONFIG_FREERTOS_NUMBER_OF_CORES 2

esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT,
        .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,    // Bitmask of all cores
        .trigger_panic = true,
    };

// Define the GPIO pin for the BC337 transistors
#define BEEP_PIN 8
#define VIBRATE_PIN 18
#define SHOCK_PIN 13

#define BEEP_DELAY 500
#define VIBRATE_DELAY 750
#define SHOCK_DELAY 500

// Create objects for the web server and Telegram bot
WebServer server(80);
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Timer variables for IP logging
unsigned long lastDebugTime = 0;
const unsigned long debugInterval = 5000; // Log every 5 seconds

unsigned long bot_lasttime;   // Last time Telegram bot was polled
const unsigned long bot_interval = 1000; // Polling interval in milliseconds

typedef void (*CommandHandler)(String chat_id);

void handleBeep(String chat_id) {
  digitalWrite(BEEP_PIN, HIGH); // Turn transistor ON
  bot.sendMessage(chat_id, "Sending BEEP command", "");
  delay(BEEP_DELAY); // Keep it ON for 1 second
  digitalWrite(BEEP_PIN, LOW); // Turn transistor OFF
  bot.sendMessage(chat_id, "BEEP sequence has finished", "");
}

void handleVibrate(String chat_id) {
  digitalWrite(VIBRATE_PIN, HIGH); // Turn transistor ON
  bot.sendMessage(chat_id, "Sending VIBRATE command", "");
  delay(VIBRATE_DELAY); 
  digitalWrite(VIBRATE_PIN, LOW); 
  bot.sendMessage(chat_id, "VIBRATE sequence has finished", "");
}

void handleShock(String chat_id) {
  digitalWrite(SHOCK_PIN, HIGH); // Turn transistor ON
  bot.sendMessage(chat_id, "Sending SHOCK command", "");
  delay(SHOCK_DELAY); 
  digitalWrite(SHOCK_PIN, LOW); 
  bot.sendMessage(chat_id, "SHOCK sequence has finished", "");
}

void handleStatus(String chat_id) {
  // String status = (digitalRead(TRANSISTOR_PIN) == HIGH) ? "ON" : "OFF";
  // bot.sendMessage(chat_id, "Transistor is " + status, "");
}

// Struct to map commands to their handlers
struct Command {
  String command;
  CommandHandler handler;
};

// Define the commands and their handlers
Command commands[] = {
    {"/beep", handleBeep},
    {"/vibrate", handleVibrate},
    {"/shock", handleShock},
    {"/status", handleStatus}
};

const int numCommands = sizeof(commands) / sizeof(commands[0]);

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    debugPrint("Received message: " + text);
    // Only process messages from the specific chat ID
    if (chat_id != chatID) {
      continue; // Skip processing if the chat ID does not match
    }
    // Find the appropriate handler for the command
    bool commandFound = false;
    for (int j = 0; j < numCommands; j++) {
      if (text == commands[j].command) {
        commands[j].handler(chat_id);
        commandFound = true;
        break;
      }
    }

    // If no command matched, send an invalid command message
    if (!commandFound && text.startsWith("/")) {
      bot.sendMessage(chat_id, "Invalid command. Use /on, /off, or /status.", "");
    }
  }
}

void debugPrint(const String& message) {
  if (debugMode) {
    Serial.println(message);
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  debugPrint("Configuring WDT...");
  esp_task_wdt_deinit(); //wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&twdt_config); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

  // Set up the transistor pin as an output
  pinMode(BEEP_PIN, OUTPUT);
  digitalWrite(BEEP_PIN, LOW); // Default OFF
  pinMode(VIBRATE_PIN, OUTPUT);
  digitalWrite(VIBRATE_PIN, LOW); // Default OFF
  pinMode(SHOCK_PIN, OUTPUT);
  digitalWrite(SHOCK_PIN, LOW); // Default OFF

  // Connect to Wi-Fi
  debugPrint("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  #ifdef ESP32
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  #endif
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debugPrint(".");
  }
  debugPrint("\nConnected to Wi-Fi");

  // Print the assigned IP address
  debugPrint("IP Address: " + WiFi.localIP().toString());

  // Define web server routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "Control the transistor using /on or /off endpoints.");
  });

  server.on("/on", HTTP_GET, []() {
    digitalWrite(BEEP_PIN, HIGH); // Turn transistor ON
    server.send(200, "text/plain", "Transistor is ON.");
    delay(1000); // Keep it ON for 1 second
    digitalWrite(BEEP_PIN, LOW); // Turn transistor OFF
    esp_task_wdt_reset();
  });

  server.on("/off", HTTP_GET, []() {
    digitalWrite(BEEP_PIN, LOW); // Turn transistor OFF
    server.send(200, "text/plain", "Transistor is OFF.");
    esp_task_wdt_reset();
  });

  // Start the web server
  server.begin();
  debugPrint("Web server started");
}

void loop() {
  if (httpEnabled) {
    server.handleClient();
  }
  if (telegramEnabled && (millis() - bot_lasttime > bot_interval)) {
    debugPrint("WDT reset after Telegram command");
    esp_task_wdt_reset();
    debugPrint("Polling telegram API for messages...");
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    // Process new messages only if there are any
    if (numNewMessages > 0) {
      handleNewMessages(numNewMessages);
      bot.last_message_received = bot.messages[numNewMessages - 1].update_id;
    }
    bot_lasttime = millis();
  }
}