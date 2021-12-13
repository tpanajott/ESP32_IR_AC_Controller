#include "Arduino.h"
#include <WiFi.h>
#include <LITTLEFS.h>
#include "FS.h"
#include "PubSubClient.h"
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// AC IR
IRrecv irrecv(34, 1024, 50, true);
decode_results results;  // Somewhere to store the results
IRac ac(4); // IR Sender

// Web
AsyncWebServer server(80);

struct ACConfig {
  int ac_protocol;
  int ac_model;
  int ac_mode;
  bool ac_celsius;
  int ac_degrees;
  int ac_fanspeed;
  int ac_swingv;
  int ac_swingh;
  bool ac_light;
  bool ac_beep;
  bool ac_econo;
  bool ac_filter;
  bool ac_turbo;
  bool ac_quiet;
  int ac_sleep;
  bool ac_clean;
  int ac_clock;
  bool ac_power;
};

struct WiFiConfig {
  char ssid[64];
  char psk[64];
  char hostname[64];
};

struct MQTTConfig {
  char server[64];
  char base_topic[64];
  char username[64];
  char password[64];
  bool use_auth;
  int port;
};

// Create "instances" of structs to store config values read at startup.
ACConfig acConfig;
WiFiConfig wifiConfig;
MQTTConfig mqttConfig;

bool initLittleFS() {
  if(!LITTLEFS.begin(false)){
    Serial.println("[ERROR] LITTLEFS Mount Failed. Device needs to be re-flashed.");
    return false;
  }
  return true;
}

void loadConfigFromLittleFS() {
  File file = LITTLEFS.open("/config.json", "r");
  if(!file){
      Serial.println("[ERROR] Failed to open config file.");
      return;
  }

  // Read file into JSON object
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("[ERROR] Failed to read config file!");
    file.close();
    return;
  }
  
  // We have now read the file. Close it.
  file.close();
  
  // Read values for WiFi
  strlcpy(wifiConfig.hostname, doc["wifi_hostname"], sizeof(wifiConfig.hostname));
  strlcpy(wifiConfig.ssid, doc["wifi_ssid"], sizeof(wifiConfig.ssid));
  strlcpy(wifiConfig.psk, doc["wifi_psk"], sizeof(wifiConfig.psk));
  
  // Read values for MQTT
  strlcpy(mqttConfig.server, doc["mqtt_server"], sizeof(mqttConfig.server));
  strlcpy(mqttConfig.username, doc["mqtt_username"], sizeof(mqttConfig.username));
  strlcpy(mqttConfig.password, doc["mqtt_password"], sizeof(mqttConfig.password));
  strlcpy(mqttConfig.base_topic, doc["mqtt_base_topic"], sizeof(mqttConfig.base_topic));
  mqttConfig.use_auth = doc["mqtt_use_auth"];
  mqttConfig.port = doc["mqtt_port"];

  // Read AC Config values from config file.
  ac.next.protocol = static_cast<IRREMOTEESP8266_H_::decode_type_t>(doc["ac_protocol"]);
  ac.next.model = doc["ac_model"];
  ac.next.power = doc["ac_power"];
  ac.next.mode = static_cast<stdAc::opmode_t>(doc["ac_mode"].as<int>());
  ac.next.degrees = doc["ac_degrees"];
  ac.next.celsius = doc["ac_celsius"];
  ac.next.fanspeed = static_cast<stdAc::fanspeed_t>(doc["ac_fanspeed"].as<int>());
  ac.next.swingv = static_cast<stdAc::swingv_t>(doc["ac_swingv"].as<int>());
  ac.next.swingh = static_cast<stdAc::swingh_t>(doc["ac_swingh"].as<int>());
  ac.next.quiet = doc["ac_quiet"];
  ac.next.turbo = doc["ac_turbo"];
  ac.next.econo = doc["ac_econo"];
  ac.next.light = doc["ac_light"];
  ac.next.filter = doc["ac_filter"];
  ac.next.clean = doc["ac_clean"];
  ac.next.beep = doc["beep"];
  ac.next.sleep = -1;
  ac.next.clock = -1;

  Serial.println("[INFO] Config loaded.");
}

void setupACLibrary() {
  irrecv.setUnknownThreshold(12);
  irrecv.setTolerance(25);
}

void startReceivingIR() {
  irrecv.enableIRIn();
}

void stopReceivingIR() {
  irrecv.disableIRIn();
}

void connectToWiFi() {
  if(!WiFi.isConnected()) {
    WiFi.setHostname(wifiConfig.hostname);
    WiFi.begin(wifiConfig.ssid, wifiConfig.psk);
    WiFi.setAutoConnect(true);

    Serial.print("[WiFi] Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print('.');
      delay(1000);
    }
    Serial.println("");

    if(WiFi.isConnected()) {
      // A connection was successful, print info
      Serial.printf("[WiFi] WiFi connected.\n");
      Serial.printf("[WiFi] IP Address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("[WiFi] Netmask   : %s\n", WiFi.subnetMask().toString().c_str());
      Serial.printf("[WiFi] Gateway   : %s\n", WiFi.gatewayIP().toString().c_str());
      Serial.printf("[WiFi] DNS       : %s\n", WiFi.dnsIP().toString().c_str());
      Serial.printf("[WiFi] Hostname  : %s\n", WiFi.getHostname());
    }
  }
}

void setupWebServer() {
  server.serveStatic("/static", LITTLEFS, "/static/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LITTLEFS, "/index.html");
  });

  server.on("/configure", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LITTLEFS, "/configure.html");
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LITTLEFS, "/update.html");
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "File not found!");
  });

  server.begin();
}

void decodeIR() {
  if (irrecv.decode(&results)) {
    // Check if we got an IR message that was to big for our capture buffer.
    if (results.overflow)
      Serial.println("[ERROR] Buffer overflow!");
    
    // Display the basic output of what we found.
    Serial.print(resultToHumanReadableBasic(&results));
    // Display any extra A/C info if we have it.
    String description = IRAcUtils::resultAcToString(&results);
    yield();  // Feed the WDT as the text output can take a while to print.
    // Output the results as source code
    Serial.println(resultToSourceCode(&results));
    Serial.println();    // Blank line between entries
    yield();             // Feed the WDT (again)
  }
}

void setup() {
  Serial.begin(115200);
  initLittleFS();
  loadConfigFromLittleFS();
  connectToWiFi(); // Connect to WiFi
  setupWebServer();

  Serial.println("[INFO] Setup done.");
}

void loop() {
  decodeIR(); // Decode IR if decoding is enabled
}