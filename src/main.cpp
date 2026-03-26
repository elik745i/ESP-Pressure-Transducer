#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include "web_ui.h"

namespace {

constexpr char CONFIG_PATH[] = "/config.json";
constexpr uint8_t BUZZER_PIN = D5;
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;
constexpr uint16_t DNS_PORT = 53;
constexpr float ADC_PIN_MAX_VOLTAGE = 3.2f;
constexpr float DIVIDER_R1_KOHM = 10.0f;
constexpr float DIVIDER_R2_KOHM = 33.0f;
constexpr unsigned long SENSOR_SAMPLE_INTERVAL_MS = 250;
constexpr unsigned long DISPLAY_INTERVAL_MS = 500;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 10000;
constexpr unsigned long BOOT_INDICATION_MS = 4000;

struct AppConfig {
  String deviceName;
  String apSsid;
  String apPassword;
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  uint16_t mqttPort;
  String mqttUser;
  String mqttPassword;
  String mqttBaseTopic;
  String mqttDiscoveryPrefix;
  bool mqttEnabled;
  bool mqttDiscoveryEnabled;
  float sensorMinVoltage;
  float sensorMaxVoltage;
  float sensorMaxPressureKPa;
  float buzzerAlarmThresholdKPa;
  bool buzzerEnabled;
  uint32_t publishIntervalSeconds;
  uint8_t oledContrast;
  bool oledFlip;
};

struct SensorState {
  uint16_t rawAdc = 0;
  float a0Voltage = 0.0f;
  float sensorVoltage = 0.0f;
  float pressureKPa = 0.0f;
  float pressureBar = 0.0f;
  bool alarmActive = false;
};

AppConfig config;
SensorState sensorState;
ESP8266WebServer server(80);
DNSServer dnsServer;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

unsigned long lastSensorSampleMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastWifiReconnectAttemptMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastAlarmToneMs = 0;
bool restartRequested = false;
unsigned long restartAtMs = 0;
bool mqttDiscoveryPublished = false;
bool wasWifiConnected = false;

enum class LedPattern {
  Booting,
  ApOnly,
  WifiConnecting,
  WifiConnected,
  WifiAndMqttConnected,
  RestartPending,
};

String defaultDeviceName() {
  return String("pressure-") + String(ESP.getChipId(), HEX);
}

String defaultApSsid() {
  return String("PressureConfig-") + String(ESP.getChipId(), HEX);
}

void setDefaults() {
  config.deviceName = defaultDeviceName();
  config.apSsid = defaultApSsid();
  config.apPassword = "12345678";
  config.wifiSsid = "";
  config.wifiPassword = "";
  config.mqttHost = "";
  config.mqttPort = 1883;
  config.mqttUser = "";
  config.mqttPassword = "";
  config.mqttBaseTopic = String("home/") + config.deviceName;
  config.mqttDiscoveryPrefix = "homeassistant";
  config.mqttEnabled = false;
  config.mqttDiscoveryEnabled = true;
  config.sensorMinVoltage = 0.5f;
  config.sensorMaxVoltage = 4.5f;
  config.sensorMaxPressureKPa = 1200.0f;
  config.buzzerAlarmThresholdKPa = 1100.0f;
  config.buzzerEnabled = true;
  config.publishIntervalSeconds = 15;
  config.oledContrast = 200;
  config.oledFlip = false;
}

bool saveConfig() {
  JsonDocument doc;
  doc["deviceName"] = config.deviceName;
  doc["apSsid"] = config.apSsid;
  doc["apPassword"] = config.apPassword;
  doc["wifiSsid"] = config.wifiSsid;
  doc["wifiPassword"] = config.wifiPassword;
  doc["mqttHost"] = config.mqttHost;
  doc["mqttPort"] = config.mqttPort;
  doc["mqttUser"] = config.mqttUser;
  doc["mqttPassword"] = config.mqttPassword;
  doc["mqttBaseTopic"] = config.mqttBaseTopic;
  doc["mqttDiscoveryPrefix"] = config.mqttDiscoveryPrefix;
  doc["mqttEnabled"] = config.mqttEnabled;
  doc["mqttDiscoveryEnabled"] = config.mqttDiscoveryEnabled;
  doc["sensorMinVoltage"] = config.sensorMinVoltage;
  doc["sensorMaxVoltage"] = config.sensorMaxVoltage;
  doc["sensorMaxPressureKPa"] = config.sensorMaxPressureKPa;
  doc["buzzerAlarmThresholdKPa"] = config.buzzerAlarmThresholdKPa;
  doc["buzzerEnabled"] = config.buzzerEnabled;
  doc["publishIntervalSeconds"] = config.publishIntervalSeconds;
  doc["oledContrast"] = config.oledContrast;
  doc["oledFlip"] = config.oledFlip;

  File file = LittleFS.open(CONFIG_PATH, "w");
  if (!file) {
    return false;
  }

  const bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

bool loadConfig() {
  setDefaults();

  if (!LittleFS.exists(CONFIG_PATH)) {
    return saveConfig();
  }

  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    return false;
  }

  config.deviceName = doc["deviceName"] | config.deviceName;
  config.apSsid = doc["apSsid"] | config.apSsid;
  config.apPassword = doc["apPassword"] | config.apPassword;
  config.wifiSsid = doc["wifiSsid"] | config.wifiSsid;
  config.wifiPassword = doc["wifiPassword"] | config.wifiPassword;
  config.mqttHost = doc["mqttHost"] | config.mqttHost;
  config.mqttPort = doc["mqttPort"] | config.mqttPort;
  config.mqttUser = doc["mqttUser"] | config.mqttUser;
  config.mqttPassword = doc["mqttPassword"] | config.mqttPassword;
  config.mqttBaseTopic = doc["mqttBaseTopic"] | config.mqttBaseTopic;
  config.mqttDiscoveryPrefix = doc["mqttDiscoveryPrefix"] | config.mqttDiscoveryPrefix;
  config.mqttEnabled = doc["mqttEnabled"] | config.mqttEnabled;
  config.mqttDiscoveryEnabled = doc["mqttDiscoveryEnabled"] | config.mqttDiscoveryEnabled;
  config.sensorMinVoltage = doc["sensorMinVoltage"] | config.sensorMinVoltage;
  config.sensorMaxVoltage = doc["sensorMaxVoltage"] | config.sensorMaxVoltage;
  config.sensorMaxPressureKPa = doc["sensorMaxPressureKPa"] | config.sensorMaxPressureKPa;
  config.buzzerAlarmThresholdKPa = doc["buzzerAlarmThresholdKPa"] | config.buzzerAlarmThresholdKPa;
  config.buzzerEnabled = doc["buzzerEnabled"] | config.buzzerEnabled;
  config.publishIntervalSeconds = doc["publishIntervalSeconds"] | config.publishIntervalSeconds;
  config.oledContrast = doc["oledContrast"] | config.oledContrast;
  config.oledFlip = doc["oledFlip"] | config.oledFlip;
  return true;
}

void applyDisplaySettings() {
  display.setContrast(config.oledContrast);
  display.setFlipMode(config.oledFlip ? 1 : 0);
}

String localIpString() {
  return WiFi.isConnected() ? WiFi.localIP().toString() : String("not connected");
}

String mqttClientId() {
  String clientId = config.deviceName;
  clientId.replace(" ", "-");
  return clientId;
}

String stateTopic() {
  return config.mqttBaseTopic + "/state";
}

String availabilityTopic() {
  return config.mqttBaseTopic + "/availability";
}

String uniqueIdBase() {
  String id = config.deviceName;
  id.replace(" ", "_");
  id.toLowerCase();
  return id;
}

void fillDiscoveryDevice(JsonObject device) {
  JsonArray ids = device["ids"].to<JsonArray>();
  ids.add(uniqueIdBase());
  device["name"] = config.deviceName;
  device["mf"] = "DIY";
  device["mdl"] = "Wemos D1 mini pressure transducer";
  device["sw"] = "1.0.0";
}

float clampFloat(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

void beep(uint16_t frequency, uint16_t durationMs) {
  if (!config.buzzerEnabled) {
    return;
  }

  tone(BUZZER_PIN, frequency, durationMs);
  delay(durationMs + 30);
  noTone(BUZZER_PIN);
}

void beepImmediate(uint16_t frequency, uint16_t durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
  delay(durationMs + 30);
  noTone(BUZZER_PIN);
}

void playTonePair(uint16_t firstFrequency, uint16_t secondFrequency, uint16_t durationMs, uint16_t gapMs) {
  beep(firstFrequency, durationMs);
  delay(gapMs);
  beep(secondFrequency, durationMs);
}

void setStatusLed(bool on) {
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
}

LedPattern currentLedPattern(unsigned long now) {
  if (restartRequested) {
    return LedPattern::RestartPending;
  }

  if (now < BOOT_INDICATION_MS) {
    return LedPattern::Booting;
  }

  if (!WiFi.isConnected()) {
    return config.wifiSsid.isEmpty() ? LedPattern::ApOnly : LedPattern::WifiConnecting;
  }

  if (config.mqttEnabled && mqttClient.connected()) {
    return LedPattern::WifiAndMqttConnected;
  }

  return LedPattern::WifiConnected;
}

bool ledStateForPattern(LedPattern pattern, unsigned long now) {
  switch (pattern) {
    case LedPattern::Booting:
      return (now % 240UL) < 120UL;
    case LedPattern::ApOnly:
      return (now % 2000UL) < 120UL;
    case LedPattern::WifiConnecting: {
      const unsigned long phase = now % 1200UL;
      return phase < 100UL || (phase >= 220UL && phase < 320UL);
    }
    case LedPattern::WifiConnected:
      return (now % 3000UL) < 40UL;
    case LedPattern::WifiAndMqttConnected: {
      const unsigned long phase = now % 3000UL;
      return phase < 40UL || (phase >= 160UL && phase < 200UL);
    }
    case LedPattern::RestartPending:
      return (now % 160UL) < 80UL;
  }

  return false;
}

void updateStatusLed(unsigned long now) {
  setStatusLed(ledStateForPattern(currentLedPattern(now), now));
}

void scheduleRestart(unsigned long delayMs) {
  restartRequested = true;
  restartAtMs = millis() + delayMs;
}

bool isIpAddress(const String &value) {
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value.charAt(index);
    if ((ch < '0' || ch > '9') && ch != '.') {
      return false;
    }
  }
  return true;
}

bool captivePortalRedirect() {
  const String host = server.hostHeader();
  if (host.isEmpty() || isIpAddress(host) || host == WiFi.softAPIP().toString()) {
    return false;
  }

  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
  return true;
}

void sendConfigPage() {
  if (captivePortalRedirect()) {
    return;
  }

  File file = LittleFS.open("/index.html.gz", "r");
  if (file) {
    server.sendHeader("Content-Encoding", "gzip");
    server.streamFile(file, "text/html");
    file.close();
    return;
  }

  file = LittleFS.open("/index.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close();
    return;
  }

  server.send_P(200, "text/html", INDEX_HTML);
}

void connectWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname(config.deviceName.c_str());
  WiFi.softAP(config.apSsid.c_str(), config.apPassword.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  if (!config.wifiSsid.isEmpty()) {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  }
}

void mqttPublish(const String &topic, const String &payload, bool retained) {
  mqttClient.publish(topic.c_str(), payload.c_str(), retained);
}

void publishDiscovery() {
  if (!config.mqttDiscoveryEnabled || config.mqttDiscoveryPrefix.isEmpty()) {
    return;
  }

  JsonDocument pressureDoc;
  pressureDoc["name"] = config.deviceName + " Pressure";
  pressureDoc["uniq_id"] = uniqueIdBase() + "_pressure";
  pressureDoc["stat_t"] = stateTopic();
  pressureDoc["avty_t"] = availabilityTopic();
  pressureDoc["unit_of_meas"] = "kPa";
  pressureDoc["dev_cla"] = "pressure";
  pressureDoc["val_tpl"] = "{{ value_json.pressure_kpa }}";
  pressureDoc["exp_aft"] = config.publishIntervalSeconds * 3;
  fillDiscoveryDevice(pressureDoc["dev"].to<JsonObject>());

  String pressurePayload;
  serializeJson(pressureDoc, pressurePayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/sensor/" + uniqueIdBase() + "/pressure/config", pressurePayload, true);

  JsonDocument voltageDoc;
  voltageDoc["name"] = config.deviceName + " Sensor Voltage";
  voltageDoc["uniq_id"] = uniqueIdBase() + "_sensor_voltage";
  voltageDoc["stat_t"] = stateTopic();
  voltageDoc["avty_t"] = availabilityTopic();
  voltageDoc["unit_of_meas"] = "V";
  voltageDoc["dev_cla"] = "voltage";
  voltageDoc["val_tpl"] = "{{ value_json.sensor_voltage }}";
  voltageDoc["exp_aft"] = config.publishIntervalSeconds * 3;
  fillDiscoveryDevice(voltageDoc["dev"].to<JsonObject>());

  String voltagePayload;
  serializeJson(voltageDoc, voltagePayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/sensor/" + uniqueIdBase() + "/sensor_voltage/config", voltagePayload, true);

  JsonDocument signalDoc;
  signalDoc["name"] = config.deviceName + " WiFi RSSI";
  signalDoc["uniq_id"] = uniqueIdBase() + "_wifi_rssi";
  signalDoc["stat_t"] = stateTopic();
  signalDoc["avty_t"] = availabilityTopic();
  signalDoc["unit_of_meas"] = "dBm";
  signalDoc["dev_cla"] = "signal_strength";
  signalDoc["val_tpl"] = "{{ value_json.wifi_rssi }}";
  signalDoc["exp_aft"] = config.publishIntervalSeconds * 3;
  fillDiscoveryDevice(signalDoc["dev"].to<JsonObject>());

  String signalPayload;
  serializeJson(signalDoc, signalPayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/sensor/" + uniqueIdBase() + "/wifi_rssi/config", signalPayload, true);

  mqttDiscoveryPublished = true;
}

bool connectMqtt() {
  if (!config.mqttEnabled || config.mqttHost.isEmpty() || !WiFi.isConnected()) {
    return false;
  }

  mqttClient.setServer(config.mqttHost.c_str(), config.mqttPort);
  const bool connected = config.mqttUser.isEmpty()
                             ? mqttClient.connect(mqttClientId().c_str(), availabilityTopic().c_str(), 0, true, "offline")
                             : mqttClient.connect(mqttClientId().c_str(), config.mqttUser.c_str(), config.mqttPassword.c_str(), availabilityTopic().c_str(), 0, true, "offline");

  if (connected) {
    mqttPublish(availabilityTopic(), "online", true);
    publishDiscovery();
    beep(2600, 80);
  }

  return connected;
}

void sampleSensor() {
  uint32_t total = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    total += analogRead(A0);
    delay(2);
  }

  sensorState.rawAdc = total / 8;
  sensorState.a0Voltage = static_cast<float>(sensorState.rawAdc) * ADC_PIN_MAX_VOLTAGE / 1023.0f;
  sensorState.sensorVoltage = sensorState.a0Voltage * ((DIVIDER_R1_KOHM + DIVIDER_R2_KOHM) / DIVIDER_R2_KOHM);

  const float normalized = (sensorState.sensorVoltage - config.sensorMinVoltage) /
                           (config.sensorMaxVoltage - config.sensorMinVoltage);
  sensorState.pressureKPa = clampFloat(normalized, 0.0f, 1.0f) * config.sensorMaxPressureKPa;
  sensorState.pressureBar = sensorState.pressureKPa / 100.0f;
  sensorState.alarmActive = config.buzzerEnabled && sensorState.pressureKPa >= config.buzzerAlarmThresholdKPa;
}

void publishState() {
  if (!mqttClient.connected()) {
    return;
  }

  JsonDocument doc;
  doc["device_name"] = config.deviceName;
  doc["pressure_kpa"] = serialized(String(sensorState.pressureKPa, 1));
  doc["pressure_bar"] = serialized(String(sensorState.pressureBar, 2));
  doc["sensor_voltage"] = serialized(String(sensorState.sensorVoltage, 3));
  doc["a0_voltage"] = serialized(String(sensorState.a0Voltage, 3));
  doc["raw_adc"] = sensorState.rawAdc;
  doc["wifi_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["ip"] = localIpString();
  doc["alarm"] = sensorState.alarmActive;
  doc["uptime_seconds"] = millis() / 1000;

  String payload;
  serializeJson(doc, payload);
  mqttPublish(stateTopic(), payload, false);
}

void drawDisplay() {
  const String ipText = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  display.clearBuffer();
  display.setFont(u8g2_font_6x13_tf);
  display.setCursor(0, 11);
  display.print(WiFi.isConnected() ? "IP " : "AP ");
  display.print(ipText);

  display.setFont(u8g2_font_logisoso24_tf);
  display.setCursor(0, 41);
  display.print(sensorState.pressureBar, 2);

  display.setFont(u8g2_font_6x13_tf);
  display.setCursor(96, 41);
  display.print("bar");

  display.setCursor(0, 64);
  display.print(mqttClient.connected() ? "MQTT online" : "MQTT offline");
  display.sendBuffer();
}

void sendJsonConfig() {
  JsonDocument doc;
  doc["deviceName"] = config.deviceName;
  doc["apSsid"] = config.apSsid;
  doc["apPassword"] = config.apPassword;
  doc["wifiSsid"] = config.wifiSsid;
  doc["wifiPassword"] = config.wifiPassword;
  doc["mqttHost"] = config.mqttHost;
  doc["mqttPort"] = config.mqttPort;
  doc["mqttUser"] = config.mqttUser;
  doc["mqttPassword"] = config.mqttPassword;
  doc["mqttBaseTopic"] = config.mqttBaseTopic;
  doc["mqttDiscoveryPrefix"] = config.mqttDiscoveryPrefix;
  doc["mqttEnabled"] = config.mqttEnabled;
  doc["mqttDiscoveryEnabled"] = config.mqttDiscoveryEnabled;
  doc["sensorMinVoltage"] = config.sensorMinVoltage;
  doc["sensorMaxVoltage"] = config.sensorMaxVoltage;
  doc["sensorMaxPressureKPa"] = config.sensorMaxPressureKPa;
  doc["buzzerAlarmThresholdKPa"] = config.buzzerAlarmThresholdKPa;
  doc["buzzerEnabled"] = config.buzzerEnabled;
  doc["publishIntervalSeconds"] = config.publishIntervalSeconds;
  doc["oledContrast"] = config.oledContrast;
  doc["oledFlip"] = config.oledFlip;

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void sendStatus() {
  JsonDocument doc;
  doc["deviceName"] = config.deviceName;
  doc["wifiConnected"] = WiFi.isConnected();
  doc["wifiSsid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  doc["ipAddress"] = localIpString();
  doc["apSsid"] = config.apSsid;
  doc["apIp"] = WiFi.softAPIP().toString();
  doc["mqttConnected"] = mqttClient.connected();
  doc["mqttHost"] = config.mqttHost;
  doc["rawAdc"] = sensorState.rawAdc;
  doc["a0Voltage"] = serialized(String(sensorState.a0Voltage, 3));
  doc["sensorVoltage"] = serialized(String(sensorState.sensorVoltage, 3));
  doc["pressureKPa"] = serialized(String(sensorState.pressureKPa, 1));
  doc["pressureBar"] = serialized(String(sensorState.pressureBar, 2));
  doc["alarmActive"] = sensorState.alarmActive;
  doc["uptimeSeconds"] = millis() / 1000;

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void sendWifiScanResults() {
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();

  const int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount >= 0) {
    for (int index = 0; index < networkCount; ++index) {
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = WiFi.SSID(index);
      network["rssi"] = WiFi.RSSI(index);
      network["encrypted"] = WiFi.encryptionType(index) != ENC_TYPE_NONE;
    }
    WiFi.scanDelete();
  }

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

bool updateConfigFromRequest() {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return false;
  }

  config.deviceName = String(static_cast<const char *>(doc["deviceName"] | config.deviceName.c_str()));
  config.apSsid = String(static_cast<const char *>(doc["apSsid"] | config.apSsid.c_str()));
  config.apPassword = String(static_cast<const char *>(doc["apPassword"] | config.apPassword.c_str()));
  config.wifiSsid = String(static_cast<const char *>(doc["wifiSsid"] | config.wifiSsid.c_str()));
  config.wifiPassword = String(static_cast<const char *>(doc["wifiPassword"] | config.wifiPassword.c_str()));
  config.mqttHost = String(static_cast<const char *>(doc["mqttHost"] | config.mqttHost.c_str()));
  config.mqttPort = doc["mqttPort"] | config.mqttPort;
  config.mqttUser = String(static_cast<const char *>(doc["mqttUser"] | config.mqttUser.c_str()));
  config.mqttPassword = String(static_cast<const char *>(doc["mqttPassword"] | config.mqttPassword.c_str()));
  config.mqttBaseTopic = String(static_cast<const char *>(doc["mqttBaseTopic"] | config.mqttBaseTopic.c_str()));
  config.mqttDiscoveryPrefix = String(static_cast<const char *>(doc["mqttDiscoveryPrefix"] | config.mqttDiscoveryPrefix.c_str()));
  config.mqttEnabled = doc["mqttEnabled"] | config.mqttEnabled;
  config.mqttDiscoveryEnabled = doc["mqttDiscoveryEnabled"] | config.mqttDiscoveryEnabled;
  config.sensorMinVoltage = doc["sensorMinVoltage"] | config.sensorMinVoltage;
  config.sensorMaxVoltage = doc["sensorMaxVoltage"] | config.sensorMaxVoltage;
  config.sensorMaxPressureKPa = doc["sensorMaxPressureKPa"] | config.sensorMaxPressureKPa;
  config.buzzerAlarmThresholdKPa = doc["buzzerAlarmThresholdKPa"] | config.buzzerAlarmThresholdKPa;
  config.buzzerEnabled = doc["buzzerEnabled"] | config.buzzerEnabled;
  config.publishIntervalSeconds = doc["publishIntervalSeconds"] | config.publishIntervalSeconds;
  config.oledContrast = doc["oledContrast"] | config.oledContrast;
  config.oledFlip = doc["oledFlip"] | config.oledFlip;

  if (config.apPassword.length() < 8) {
    server.send(400, "application/json", "{\"error\":\"AP password must be at least 8 characters\"}");
    return false;
  }

  if (config.mqttPort == 0) {
    config.mqttPort = 1883;
  }

  if (config.publishIntervalSeconds == 0) {
    config.publishIntervalSeconds = 15;
  }

  if (config.oledContrast > 255) {
    config.oledContrast = 255;
  }

  if (config.sensorMaxVoltage <= config.sensorMinVoltage) {
    server.send(400, "application/json", "{\"error\":\"Sensor max voltage must be greater than min voltage\"}");
    return false;
  }

  if (!saveConfig()) {
    server.send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
    return false;
  }

  return true;
}

void handleConfigPost() {
  if (!updateConfigFromRequest()) {
    return;
  }

  applyDisplaySettings();
  beep(2000, 120);
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Saved. Device will restart.\"}");
  scheduleRestart(1200);
}

void handleRestartPost() {
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Restart scheduled\"}");
  scheduleRestart(1000);
}

void handleNotFound() {
  if (captivePortalRedirect()) {
    return;
  }

  if (server.method() == HTTP_GET) {
    sendConfigPage();
    return;
  }

  server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

void configureWebServer() {
  server.on("/", HTTP_GET, sendConfigPage);
  server.on("/hotspot-detect.html", HTTP_GET, sendConfigPage);
  server.on("/generate_204", HTTP_GET, sendConfigPage);
  server.on("/gen_204", HTTP_GET, sendConfigPage);
  server.on("/connecttest.txt", HTTP_GET, sendConfigPage);
  server.on("/ncsi.txt", HTTP_GET, sendConfigPage);
  server.on("/success.txt", HTTP_GET, sendConfigPage);
  server.on("/library/test/success.html", HTTP_GET, sendConfigPage);

  server.on("/api/config", HTTP_GET, sendJsonConfig);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/wifi/scan", HTTP_GET, sendWifiScanResults);
  server.on("/api/restart", HTTP_POST, handleRestartPost);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupApp() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(false);

  Serial.begin(115200);
  delay(100);
  beepImmediate(2200, 40);

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  }

  if (!loadConfig()) {
    Serial.println("Using default config");
    setDefaults();
    saveConfig();
  }

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_6x13_tf);
  display.drawStr(0, 14, "Pressure monitor");
  display.drawStr(0, 30, "Booting...");
  display.sendBuffer();
  applyDisplaySettings();

  connectWifi();
  mqttClient.setBufferSize(768);
  configureWebServer();
}

void loopApp() {
  dnsServer.processNextRequest();
  server.handleClient();

  const unsigned long now = millis();
  const bool wifiConnected = WiFi.isConnected();
  updateStatusLed(now);

  if (wifiConnected && !wasWifiConnected) {
    playTonePair(1800, 2400, 120, 90);
  } else if (!wifiConnected && wasWifiConnected) {
    playTonePair(2600, 1500, 120, 90);
  }
  wasWifiConnected = wifiConnected;

  if (now - lastSensorSampleMs >= SENSOR_SAMPLE_INTERVAL_MS) {
    lastSensorSampleMs = now;
    sampleSensor();
  }

  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = now;
    drawDisplay();
  }

  if (!wifiConnected && !config.wifiSsid.isEmpty() && now - lastWifiReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiReconnectAttemptMs = now;
    WiFi.disconnect();
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  }

  if (wifiConnected && config.mqttEnabled) {
    if (!mqttClient.connected() && now - lastMqttReconnectAttemptMs >= MQTT_RECONNECT_INTERVAL_MS) {
      lastMqttReconnectAttemptMs = now;
      connectMqtt();
    }
    mqttClient.loop();
  }

  if (mqttClient.connected() && now - lastPublishMs >= config.publishIntervalSeconds * 1000UL) {
    lastPublishMs = now;
    publishState();
  }

  if (sensorState.alarmActive && now - lastAlarmToneMs >= 5000) {
    lastAlarmToneMs = now;
    beep(3200, 180);
  }

  if (restartRequested && static_cast<long>(now - restartAtMs) >= 0) {
    ESP.restart();
  }
}

}  // namespace

void setup() {
  setupApp();
}

void loop() {
  loopApp();
}
