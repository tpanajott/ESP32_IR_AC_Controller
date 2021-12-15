#include "Arduino.h"
#include <WiFi.h>
#include <LITTLEFS.h>
#include "FS.h"
#include "PubSubClient.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
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
AsyncWebSocket updateStatusWebSocket("/update_status");

// Status variables
bool isLearning = false;

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

void startLearning() {
  Serial.println("[AC] Start learning process.");
  isLearning = true;
  irrecv.enableIRIn();
}

void stopLearning() {
  Serial.println("[AC] Stop learning process.");
  isLearning = false;
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

void reconnectToMqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    client.setServer(mqttConfig.server, mqttConfig.port);
    Serial.println("[MQTT] Attempting to connect...");
    
    // Attempt to connect
    if(mqttConfig.use_auth) {
      client.connect(wifiConfig.hostname, mqttConfig.username, mqttConfig.password);
    } else {
      client.connect(wifiConfig.hostname);
    }

    if (client.connected()) {
      Serial.println("[MQTT] Connected");
      // Once connected, publish an announcement...
      // client.publish("outTopic", "hello world");
      // ... and resubscribe
      // client.subscribe("inTopic");
    } else {
      Serial.print("[MQTT] failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void respondCurrentStatus(AsyncWebServerRequest *request) {
  // Respond with current AC status.
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument doc(1024);
  // Break out all the easy values, more complex values are handled below
  doc["ac_protocol"] = typeToString(ac.next.protocol);
  doc["ac_model"] = ac.next.model;
  doc["ac_power"] = ac.next.power ? "On" : "Off";
  doc["ac_degrees"] = ac.next.degrees;
  doc["ac_celsius"] = ac.next.celsius ? "Celsius" : "Fahrenheit";
  doc["ac_quiet"] = ac.next.quiet ? "Quiet" : "Normal";
  doc["ac_turbo"] = ac.next.turbo ? "Turbo" : "Normal";
  doc["ac_econo"] = ac.next.econo ? "Econo" : "Normal";
  doc["ac_light"] = ac.next.light ? "Light" : "Dark";
  doc["ac_filter"] = ac.next.filter ? "Clear" : "Normal";
  doc["ac_clean"] = ac.next.clean ? "Clear" : "Normal";
  doc["ac_beep"] = ac.next.beep ? "Beep" : "Silent";
  doc["ac_learning"] = isLearning;

  // AC Mode status
  switch (ac.next.mode)
  {
    case stdAc::opmode_t::kAuto:
      doc["ac_mode"] = "Auto";
      break;
    case stdAc::opmode_t::kCool:
      doc["ac_mode"] = "Cool";
      break;
    case stdAc::opmode_t::kDry:
      doc["ac_mode"] = "Dry";
      break;
    case stdAc::opmode_t::kFan:
      doc["ac_mode"] = "Fan";
      break;
    case stdAc::opmode_t::kHeat:
      doc["ac_mode"] = "Heat";
      break;
    case stdAc::opmode_t::kOff:
      doc["ac_mode"] = "Off";
      break;
    
    default:
      doc["ac_mode"] = "-UNKNOWN-";
      break;
  }
  // AC Fan speed status
  switch (ac.next.fanspeed)
  {
    case stdAc::fanspeed_t::kAuto:
      doc["ac_fanspeed"] = "Auto";
      break;
    case stdAc::fanspeed_t::kMax:
      doc["ac_fanspeed"] = "Max";
      break;
    case stdAc::fanspeed_t::kHigh:
      doc["ac_fanspeed"] = "High";
      break;
    case stdAc::fanspeed_t::kMedium:
      doc["ac_fanspeed"] = "Medium";
      break;
    case stdAc::fanspeed_t::kLow:
      doc["ac_fanspeed"] = "Low";
      break;
    case stdAc::fanspeed_t::kMin:
      doc["ac_fanspeed"] = "Min";
      break;
    
    default:
      doc["ac_fanspeed"] = "-UNKNOWN-";
      break;
  }
  // Swing Vertical status
  switch (ac.next.swingv)
  {
    case stdAc::swingv_t::kAuto:
      doc["ac_swingv"] = "Auto";
      break;
    case stdAc::swingv_t::kHighest:
      doc["ac_swingv"] = "Highest";
      break;
    case stdAc::swingv_t::kHigh:
      doc["ac_swingv"] = "High";
      break;
    case stdAc::swingv_t::kMiddle:
      doc["ac_swingv"] = "Middle";
      break;
    case stdAc::swingv_t::kLow:
      doc["ac_swingv"] = "Low";
      break;
    case stdAc::swingv_t::kLowest:
      doc["ac_swingv"] = "Lowest";
      break;
    case stdAc::swingv_t::kOff:
      doc["ac_swingv"] = "Off";
      break;
    
    default:
      doc["ac_swingv"] = "-UNKNOWN-";
      break;
  }
  // Swing Horizontal status
  switch (ac.next.swingh)
  {
    case stdAc::swingh_t::kOff:
      doc["ac_swingh"] = "Off";
      break;
    case stdAc::swingh_t::kAuto:
      doc["ac_swingh"] = "Auto";
      break;
    case stdAc::swingh_t::kLeftMax:
      doc["ac_swingh"] = "Left Max";
      break;
    case stdAc::swingh_t::kLeft:
      doc["ac_swingh"] = "Left";
      break;
    case stdAc::swingh_t::kMiddle:
      doc["ac_swingh"] = "Middle";
      break;
    case stdAc::swingh_t::kRight:
      doc["ac_swingh"] = "Right";
      break;
    case stdAc::swingh_t::kRightMax:
      doc["ac_swingh"] = "Right Max";
      break;
    case stdAc::swingh_t::kWide:
      doc["ac_swingh"] = "Wide";
      break;
    
    default:
      doc["ac_swingh"] = "-UNKNOWN-";
      break;
  }

  // Other status items
  if(client.connected()) {
    doc["mqtt_status_tag"] = "Connected";
  } else {
    String error_string = "Error: ";
    error_string.concat(client.state());
    doc["mqtt_status_tag"] = error_string.c_str();
  }
  doc["mqtt_status_tag_classes"] = client.connected() ? "tag is-success" : "tag is-danger";

  doc["wifi_status_tag"] = WiFi.isConnected() ? "Connected" : "Disconnected";
  doc["wifi_status_tag_classes"] = WiFi.isConnected() ? "tag is-success" : "tag is-danger";

  serializeJson(doc, *response);
  request->send(response);
}

void actFromWeb(AsyncWebServerRequest *request) {
  // Given that a button was pressed in the Web GUI, act on the value sent.
  if (request->hasParam("mode")) {
    switch(request->getParam("mode")->value().toInt()) {
      case -1:
        ac.next.mode = stdAc::opmode_t::kOff;
        request->send(200, "OK");
        return;
        break;
      case 0:
        ac.next.mode = stdAc::opmode_t::kAuto;
        request->send(200, "OK");
        return;
        break;
      case 1:
        ac.next.mode = stdAc::opmode_t::kCool;
        request->send(200, "OK");
        return;
        break;
      case 2:
        ac.next.mode = stdAc::opmode_t::kHeat;
        request->send(200, "OK");
        return;
        break;
      case 3:
        ac.next.mode = stdAc::opmode_t::kDry;
        request->send(200, "OK");
        return;
        break;
      case 4:
        ac.next.mode = stdAc::opmode_t::kFan;
        request->send(200, "OK");
        return;
        break;
    }
  } else if (request->hasParam("celsius")) {
    ac.next.celsius = !ac.next.celsius;
    request->send(200, "OK");
    return;
  } else if(request->hasParam("degrees")) {
    if(request->getParam("degrees")->value() == "1") {
      ac.next.degrees++;
      request->send(200, "OK");
      return;
    } else {
      ac.next.degrees--;
      request->send(200, "OK");
      return;
    }
  } else if (request->hasParam("fanspeed")) {
    switch(request->getParam("fanspeed")->value().toInt()) {
      case 0:
        ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
        request->send(200, "OK");
        return;
        break;
      case 1:
        ac.next.fanspeed = stdAc::fanspeed_t::kMin;
        request->send(200, "OK");
        return;
        break;
      case 2:
        ac.next.fanspeed = stdAc::fanspeed_t::kLow;
        request->send(200, "OK");
        return;
        break;
      case 3:
        ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
        request->send(200, "OK");
        return;
        break;
      case 4:
        ac.next.fanspeed = stdAc::fanspeed_t::kHigh;
        request->send(200, "OK");
        return;
        break;
      case 5:
        ac.next.fanspeed = stdAc::fanspeed_t::kMax;
        request->send(200, "OK");
        return;
        break;
    }
  } else if(request->hasParam("swingv")) {
    switch(request->getParam("swingv")->value().toInt()) {
      case -1:
        ac.next.swingv = stdAc::swingv_t::kOff;
        request->send(200, "OK");
        return;
        break;
      case 0:
        ac.next.swingv = stdAc::swingv_t::kAuto;
        request->send(200, "OK");
        return;
        break;
      case 1:
        ac.next.swingv = stdAc::swingv_t::kHighest;
        request->send(200, "OK");
        return;
        break;
      case 2:
        ac.next.swingv = stdAc::swingv_t::kHigh;
        request->send(200, "OK");
        return;
        break;
      case 3:
        ac.next.swingv = stdAc::swingv_t::kMiddle;
        request->send(200, "OK");
        return;
        break;
      case 4:
        ac.next.swingv = stdAc::swingv_t::kLow;
        request->send(200, "OK");
        return;
        break;
      case 5:
        ac.next.swingv = stdAc::swingv_t::kLowest;
        request->send(200, "OK");
        return;
        break;
    }
  } else if (request->hasParam("swingh")) {
    switch(request->getParam("swingh")->value().toInt()) {
      case -1:
        ac.next.swingh = stdAc::swingh_t::kOff;
        request->send(200, "OK");
        return;
        break;
      case 0:
        ac.next.swingh = stdAc::swingh_t::kAuto;
        request->send(200, "OK");
        return;
        break;
      case 1:
        ac.next.swingh = stdAc::swingh_t::kLeftMax;
        request->send(200, "OK");
        return;
        break;
      case 2:
        ac.next.swingh = stdAc::swingh_t::kLeft;
        request->send(200, "OK");
        return;
        break;
      case 3:
        ac.next.swingh = stdAc::swingh_t::kMiddle;
        request->send(200, "OK");
        return;
        break;
      case 4:
        ac.next.swingh = stdAc::swingh_t::kRight;
        request->send(200, "OK");
        return;
        break;
      case 5:
        ac.next.swingh = stdAc::swingh_t::kRightMax;
        request->send(200, "OK");
        return;
        break;
      case 6:
        ac.next.swingh = stdAc::swingh_t::kWide;
        request->send(200, "OK");
        return;
        break;
    }
  } else if(request->hasParam("light")) {
    ac.next.light = !ac.next.light;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("beep")) {
    ac.next.beep = !ac.next.beep;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("econo")) {
    ac.next.econo = !ac.next.econo;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("filter")) {
    ac.next.filter = !ac.next.filter;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("turbo")) {
    ac.next.turbo = !ac.next.turbo;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("quiet")) {
    ac.next.quiet = !ac.next.quiet;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("clean")) {
    ac.next.clean = !ac.next.clean;
    request->send(200, "OK");
    return;
  }else if(request->hasParam("power")) {
    if(request->getParam("power")->value().toInt() == 0) {
      ac.next.power = false;
      request->send(200, "OK");
      return;
    } else if(request->getParam("power")->value().toInt() == 1) {
      ac.next.power = true;
      request->send(200, "OK");
      return;
    }
  } else if(request->hasParam("ac_learning")) {
    if(request->getParam("ac_learning")->value().toInt() == 0) {
      stopLearning();
      request->send(200, "OK");
      return;
    } else if(request->getParam("ac_learning")->value().toInt() == 1) {
      startLearning();
      request->send(200, "OK");
      return;
    }
  }

  // Nothing was done.
  request->send(500, "text/plain", "Unkonwn action");
}

void setupWebServer() {
  server.serveStatic("/static", LITTLEFS, "/static/");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LITTLEFS, "/index.html");
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    respondCurrentStatus(request);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
    actFromWeb(request);
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
  if(isLearning) {
    if (irrecv.decode(&results)) {
      // Check if we got an IR message that was to big for our capture buffer.
      if (!results.overflow) {
        // Decode result and save into the standard AC object.
        IRAcUtils::decodeToState(&results, &ac.next);
        stopLearning();
        yield();  // Feed the WDT as the text output can take a while to print.
      } else {
        Serial.println("[ERROR] Buffer overflow!");
      }
    }
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
  reconnectToMqtt(); // (Re)connect to MQTT if not already connected

  client.loop(); // Update MQTT
}