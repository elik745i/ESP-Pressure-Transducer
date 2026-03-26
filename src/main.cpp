#include <Arduino.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include <Updater.h>
#include <WiFiClientSecureBearSSL.h>
#include "web_ui.h"

namespace {

constexpr char CONFIG_PATH[] = "/config.json";
constexpr char FIRMWARE_VERSION[] = "v1.1.4";
constexpr char GITHUB_OWNER[] = "elik745i";
constexpr char GITHUB_REPO[] = "ESP-Pressure-Transducer";
constexpr char GITHUB_TAGS_API_URL[] = "https://api.github.com/repos/elik745i/ESP-Pressure-Transducer/tags?per_page=10";
constexpr char GITHUB_FIRMWARE_ASSET_NAME[] = "firmware.bin";
constexpr char GITHUB_FILESYSTEM_ASSET_NAME[] = "littlefs.bin";
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
constexpr size_t EEPROM_SIZE_BYTES = 1024;
constexpr uint32_t PERSISTENT_CONFIG_MAGIC = 0x50545231UL;
constexpr uint16_t PERSISTENT_CONFIG_VERSION = 1;

struct PersistentConfigData {
  uint32_t magic;
  uint16_t version;
  char deviceName[64];
  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[65];
  char mqttPassword[65];
  char mqttBaseTopic[96];
  char mqttDiscoveryPrefix[32];
  uint8_t mqttEnabled;
  uint8_t mqttDiscoveryEnabled;
  uint32_t checksum;
};

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
  String sensorFilterPreset;
  float buzzerAlarmThresholdKPa;
  bool buzzerEnabled;
  uint32_t publishIntervalSeconds;
  uint8_t oledContrast;
  bool oledFlip;
  String oledPressureUnit;
  String oledTopRowMode;
  String oledBottomRowMode;
  int8_t oledValueYOffset;
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
bool wasMqttConnected = false;
bool accessPointEnabled = false;
bool otaUpdateRequested = false;
bool otaUpdateRunning = false;
uint8_t otaProgressPercent = 0;
String otaStatusMessage = "Idle";
String otaUpdateVersion = "";
String otaFirmwareUrl = "";
String otaCurrentPhase = "";
size_t otaProgressCurrentBytes = 0;
size_t otaProgressTotalBytes = 0;

enum class LedPattern {
  Booting,
  ApOnly,
  WifiConnecting,
  WifiConnected,
  WifiAndMqttConnected,
  RestartPending,
};

void scheduleRestart(unsigned long delayMs);

String defaultDeviceName() {
  return String("pressure-") + String(ESP.getChipId(), HEX);
}

String defaultApSsid() {
  return String("PressureConfig-") + String(ESP.getChipId(), HEX);
}

uint32_t persistentConfigChecksum(const PersistentConfigData &data) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&data);
  const size_t checksumOffset = offsetof(PersistentConfigData, checksum);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < checksumOffset; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

void writeFixedString(char *target, size_t size, const String &value) {
  if (size == 0) {
    return;
  }
  memset(target, 0, size);
  value.substring(0, size - 1).toCharArray(target, size);
}

String readFixedString(const char *value, size_t size) {
  size_t length = 0;
  while (length < size && value[length] != '\0') {
    ++length;
  }
  return String(value).substring(0, length);
}

bool loadPersistentConfig() {
  PersistentConfigData stored{};
  EEPROM.get(0, stored);

  if (stored.magic != PERSISTENT_CONFIG_MAGIC ||
      stored.version != PERSISTENT_CONFIG_VERSION ||
      stored.checksum != persistentConfigChecksum(stored)) {
    return false;
  }

  config.deviceName = readFixedString(stored.deviceName, sizeof(stored.deviceName));
  config.wifiSsid = readFixedString(stored.wifiSsid, sizeof(stored.wifiSsid));
  config.wifiPassword = readFixedString(stored.wifiPassword, sizeof(stored.wifiPassword));
  config.mqttHost = readFixedString(stored.mqttHost, sizeof(stored.mqttHost));
  config.mqttPort = stored.mqttPort;
  config.mqttUser = readFixedString(stored.mqttUser, sizeof(stored.mqttUser));
  config.mqttPassword = readFixedString(stored.mqttPassword, sizeof(stored.mqttPassword));
  config.mqttBaseTopic = readFixedString(stored.mqttBaseTopic, sizeof(stored.mqttBaseTopic));
  config.mqttDiscoveryPrefix = readFixedString(stored.mqttDiscoveryPrefix, sizeof(stored.mqttDiscoveryPrefix));
  config.mqttEnabled = stored.mqttEnabled != 0;
  config.mqttDiscoveryEnabled = stored.mqttDiscoveryEnabled != 0;

  if (config.deviceName.isEmpty()) {
    config.deviceName = defaultDeviceName();
  }
  if (config.mqttPort == 0) {
    config.mqttPort = 1883;
  }
  if (config.mqttBaseTopic.isEmpty()) {
    config.mqttBaseTopic = String("home/") + config.deviceName;
  }
  if (config.mqttDiscoveryPrefix.isEmpty()) {
    config.mqttDiscoveryPrefix = "homeassistant";
  }
  return true;
}

bool savePersistentConfig() {
  PersistentConfigData stored{};
  stored.magic = PERSISTENT_CONFIG_MAGIC;
  stored.version = PERSISTENT_CONFIG_VERSION;
  writeFixedString(stored.deviceName, sizeof(stored.deviceName), config.deviceName);
  writeFixedString(stored.wifiSsid, sizeof(stored.wifiSsid), config.wifiSsid);
  writeFixedString(stored.wifiPassword, sizeof(stored.wifiPassword), config.wifiPassword);
  writeFixedString(stored.mqttHost, sizeof(stored.mqttHost), config.mqttHost);
  stored.mqttPort = config.mqttPort;
  writeFixedString(stored.mqttUser, sizeof(stored.mqttUser), config.mqttUser);
  writeFixedString(stored.mqttPassword, sizeof(stored.mqttPassword), config.mqttPassword);
  writeFixedString(stored.mqttBaseTopic, sizeof(stored.mqttBaseTopic), config.mqttBaseTopic);
  writeFixedString(stored.mqttDiscoveryPrefix, sizeof(stored.mqttDiscoveryPrefix), config.mqttDiscoveryPrefix);
  stored.mqttEnabled = config.mqttEnabled ? 1 : 0;
  stored.mqttDiscoveryEnabled = config.mqttDiscoveryEnabled ? 1 : 0;
  stored.checksum = persistentConfigChecksum(stored);

  EEPROM.put(0, stored);
  return EEPROM.commit();
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
  config.sensorFilterPreset = "none";
  config.buzzerAlarmThresholdKPa = 1100.0f;
  config.buzzerEnabled = true;
  config.publishIntervalSeconds = 15;
  config.oledContrast = 200;
  config.oledFlip = false;
  config.oledPressureUnit = "bar";
  config.oledTopRowMode = "ip";
  config.oledBottomRowMode = "mqtt";
  config.oledValueYOffset = 3;
}

bool saveConfig() {
  JsonDocument doc;
  doc["apSsid"] = config.apSsid;
  doc["apPassword"] = config.apPassword;
  doc["sensorMinVoltage"] = config.sensorMinVoltage;
  doc["sensorMaxVoltage"] = config.sensorMaxVoltage;
  doc["sensorMaxPressureKPa"] = config.sensorMaxPressureKPa;
  doc["sensorFilterPreset"] = config.sensorFilterPreset;
  doc["buzzerAlarmThresholdKPa"] = config.buzzerAlarmThresholdKPa;
  doc["buzzerEnabled"] = config.buzzerEnabled;
  doc["publishIntervalSeconds"] = config.publishIntervalSeconds;
  doc["oledContrast"] = config.oledContrast;
  doc["oledFlip"] = config.oledFlip;
  doc["oledPressureUnit"] = config.oledPressureUnit;
  doc["oledTopRowMode"] = config.oledTopRowMode;
  doc["oledBottomRowMode"] = config.oledBottomRowMode;
  doc["oledValueYOffset"] = config.oledValueYOffset;

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
  const bool hasPersistentConfig = loadPersistentConfig();

  if (!LittleFS.exists(CONFIG_PATH)) {
    if (!hasPersistentConfig) {
      savePersistentConfig();
    }
    return saveConfig();
  }

  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) {
    return hasPersistentConfig ? saveConfig() : false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    return hasPersistentConfig ? saveConfig() : false;
  }

  config.apSsid = doc["apSsid"] | config.apSsid;
  config.apPassword = doc["apPassword"] | config.apPassword;
  config.sensorMinVoltage = doc["sensorMinVoltage"] | config.sensorMinVoltage;
  config.sensorMaxVoltage = doc["sensorMaxVoltage"] | config.sensorMaxVoltage;
  config.sensorMaxPressureKPa = doc["sensorMaxPressureKPa"] | config.sensorMaxPressureKPa;
  config.sensorFilterPreset = doc["sensorFilterPreset"] | config.sensorFilterPreset;
  config.buzzerAlarmThresholdKPa = doc["buzzerAlarmThresholdKPa"] | config.buzzerAlarmThresholdKPa;
  config.buzzerEnabled = doc["buzzerEnabled"] | config.buzzerEnabled;
  config.publishIntervalSeconds = doc["publishIntervalSeconds"] | config.publishIntervalSeconds;
  config.oledContrast = doc["oledContrast"] | config.oledContrast;
  config.oledFlip = doc["oledFlip"] | config.oledFlip;
  config.oledPressureUnit = doc["oledPressureUnit"] | config.oledPressureUnit;
  config.oledTopRowMode = doc["oledTopRowMode"] | config.oledTopRowMode;
  config.oledBottomRowMode = doc["oledBottomRowMode"] | config.oledBottomRowMode;
  config.oledValueYOffset = doc["oledValueYOffset"] | config.oledValueYOffset;

  if (!hasPersistentConfig) {
    config.deviceName = doc["deviceName"] | config.deviceName;
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
    savePersistentConfig();
    saveConfig();
  }
  return true;
}

void applyDisplaySettings() {
  display.setContrast(config.oledContrast);
  display.setFlipMode(config.oledFlip ? 1 : 0);
}

String localIpString() {
  return WiFi.isConnected() ? WiFi.localIP().toString() : String("not connected");
}

String devicePageUrl() {
  if (WiFi.isConnected()) {
    return String("http://") + WiFi.localIP().toString();
  }
  if (accessPointEnabled) {
    return String("http://") + WiFi.softAPIP().toString();
  }
  return "";
}

float displayedPressureValue() {
  if (config.oledPressureUnit == "psi") {
    return sensorState.pressureKPa * 0.1450377f;
  }
  if (config.oledPressureUnit == "mpa") {
    return sensorState.pressureKPa / 1000.0f;
  }
  return sensorState.pressureBar;
}

uint8_t displayedPressureDecimals() {
  if (config.oledPressureUnit == "psi") {
    return 1;
  }
  if (config.oledPressureUnit == "mpa") {
    return 3;
  }
  return 2;
}

String displayedPressureLabel() {
  if (config.oledPressureUnit == "psi") {
    return "psi";
  }
  if (config.oledPressureUnit == "mpa") {
    return "MPa";
  }
  return "bar";
}

String oledTopRowText() {
  if (config.oledTopRowMode == "wifi") {
    return WiFi.isConnected() ? String("WiFi ") + WiFi.SSID() : String("AP ") + config.apSsid;
  }
  if (config.oledTopRowMode == "device") {
    return config.deviceName;
  }
  if (config.oledTopRowMode == "topic") {
    return config.mqttBaseTopic;
  }
  return String(WiFi.isConnected() ? "IP " : "AP ") +
         (WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
}

String oledBottomRowText() {
  if (config.oledBottomRowMode == "blank") {
    return "";
  }
  if (config.oledBottomRowMode == "wifi") {
    return WiFi.isConnected() ? String("WiFi ") + WiFi.SSID() : "WiFi offline";
  }
  if (config.oledBottomRowMode == "kpa") {
    return String(sensorState.pressureKPa, 1) + " kPa";
  }
  if (config.oledBottomRowMode == "raw") {
    return String("ADC ") + sensorState.rawAdc;
  }
  return mqttClient.connected() ? "MQTT online" : "MQTT offline";
}

float sensorFilterAlpha() {
  if (config.sensorFilterPreset == "none") {
    return 1.0f;
  }
  if (config.sensorFilterPreset == "light") {
    return 0.55f;
  }
  if (config.sensorFilterPreset == "hard") {
    return 0.18f;
  }
  if (config.sensorFilterPreset == "strong") {
    return 0.10f;
  }
  return 0.30f;
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

String githubUserAgent() {
  return String("ESP-Pressure-Transducer/") + FIRMWARE_VERSION;
}

String githubReleaseAssetUrl(const String &tagName, const char *assetName) {
  return String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
         "/releases/download/" + tagName + "/" + assetName;
}

bool fetchGithubTags(JsonDocument &doc, String &errorMessage) {
  if (!WiFi.isConnected()) {
    errorMessage = "Connect to Wi-Fi to check GitHub releases.";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient https;
  https.setReuse(false);
  https.setTimeout(15000);
  https.setUserAgent(githubUserAgent());

  if (!https.begin(client, GITHUB_TAGS_API_URL)) {
    errorMessage = "Failed to open GitHub tags API.";
    return false;
  }

  https.addHeader("Accept", "application/vnd.github+json");
  https.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int statusCode = https.GET();
  if (statusCode != HTTP_CODE_OK) {
    errorMessage = String("GitHub API error: HTTP ") + statusCode;
    https.end();
    return false;
  }

  JsonDocument filter;
  JsonObject tagFilter = filter.add<JsonObject>();
  tagFilter["name"] = true;

  doc.clear();
  DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
  https.end();

  if (error) {
    errorMessage = String("GitHub tags parse failed: ") + error.c_str();
    return false;
  }

  return true;
}

bool resolveReleaseAssets(const String &tagName, String &firmwareUrl, String &filesystemUrl, String &errorMessage) {
  JsonDocument tags;
  if (!fetchGithubTags(tags, errorMessage)) {
    return false;
  }

  for (JsonObject tag : tags.as<JsonArray>()) {
    const String releaseTag = String(static_cast<const char *>(tag["name"] | ""));
    if (releaseTag != tagName) {
      continue;
    }

    firmwareUrl = githubReleaseAssetUrl(releaseTag, GITHUB_FIRMWARE_ASSET_NAME);
    filesystemUrl = githubReleaseAssetUrl(releaseTag, GITHUB_FILESYSTEM_ASSET_NAME);
    return true;
  }

  errorMessage = "Selected firmware tag was not found on GitHub.";
  return false;
}

void fillDiscoveryDevice(JsonObject device) {
  JsonArray ids = device["ids"].to<JsonArray>();
  ids.add(uniqueIdBase());
  device["name"] = config.deviceName;
  device["mf"] = "DIY";
  device["mdl"] = "Wemos D1 mini pressure transducer";
  device["sw"] = FIRMWARE_VERSION;
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

void playMelody(const uint16_t *frequencies, const uint16_t *durations, size_t noteCount, bool honorMute) {
  if (honorMute && !config.buzzerEnabled) {
    return;
  }

  for (size_t index = 0; index < noteCount; ++index) {
    const uint16_t frequency = frequencies[index];
    const uint16_t duration = durations[index];

    if (frequency == 0) {
      delay(duration);
      continue;
    }

    if (honorMute) {
      beep(frequency, duration);
    } else {
      beepImmediate(frequency, duration);
    }
  }
}

void playBootMelody() {
  constexpr uint16_t frequencies[] = {1960, 2470, 2940};
  constexpr uint16_t durations[] = {90, 90, 140};
  playMelody(frequencies, durations, 3, false);
}

void playWifiConnectedMelody() {
  constexpr uint16_t frequencies[] = {1760, 2200, 2620};
  constexpr uint16_t durations[] = {90, 90, 130};
  playMelody(frequencies, durations, 3, true);
}

void playWifiDisconnectedMelody() {
  constexpr uint16_t frequencies[] = {2620, 1960, 1470};
  constexpr uint16_t durations[] = {100, 100, 140};
  playMelody(frequencies, durations, 3, true);
}

void playMqttConnectedMelody() {
  constexpr uint16_t frequencies[] = {1560, 2080, 1560, 3120};
  constexpr uint16_t durations[] = {70, 70, 70, 150};
  playMelody(frequencies, durations, 4, true);
}

void serviceBackgroundTasksDuringOta() {
  if (accessPointEnabled) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  yield();
}

bool runHttpUpdate(const String &url, int updateCommand, const String &phaseLabel) {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  client.setBufferSizes(512, 512);

  HTTPClient https;
  https.setReuse(false);
  https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  https.setTimeout(30000);
  https.setUserAgent(githubUserAgent());

  otaCurrentPhase = phaseLabel;
  otaProgressCurrentBytes = 0;
  otaProgressTotalBytes = 0;
  otaProgressPercent = 0;
  otaStatusMessage = phaseLabel + " update started...";

  if (!https.begin(client, url)) {
    otaStatusMessage = phaseLabel + " update failed: could not open URL.";
    return false;
  }

  const int statusCode = https.GET();
  if (statusCode != HTTP_CODE_OK) {
    otaStatusMessage = phaseLabel + " update failed: HTTP " + String(statusCode);
    https.end();
    return false;
  }

  const int totalBytes = https.getSize();
  if (totalBytes <= 0) {
    otaStatusMessage = phaseLabel + " update failed: unknown content length.";
    https.end();
    return false;
  }

  otaProgressTotalBytes = static_cast<size_t>(totalBytes);
  WiFiClient *stream = https.getStreamPtr();
  if (stream == nullptr) {
    otaStatusMessage = phaseLabel + " update failed: no download stream.";
    https.end();
    return false;
  }

  if (!Update.begin(static_cast<size_t>(totalBytes), updateCommand)) {
    otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
    https.end();
    return false;
  }

  uint8_t buffer[1024];
  while (https.connected() && (otaProgressCurrentBytes < otaProgressTotalBytes)) {
    const size_t available = stream->available();
    if (available == 0) {
      serviceBackgroundTasksDuringOta();
      delay(1);
      continue;
    }

    const size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
    const size_t bytesRead = stream->readBytes(buffer, toRead);
    if (bytesRead == 0) {
      serviceBackgroundTasksDuringOta();
      continue;
    }

    const size_t written = Update.write(buffer, bytesRead);
    if (written != bytesRead) {
      Update.end();
      otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
      https.end();
      return false;
    }

    otaProgressCurrentBytes += written;
    otaProgressPercent = otaProgressTotalBytes > 0
                             ? static_cast<uint8_t>((otaProgressCurrentBytes * 100U) / otaProgressTotalBytes)
                             : 0;
    otaStatusMessage = phaseLabel + " update " + String(otaProgressPercent) + "%";
    serviceBackgroundTasksDuringOta();
  }

  if (!Update.end()) {
    otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
    https.end();
    return false;
  }

  if (!Update.isFinished()) {
    otaStatusMessage = phaseLabel + " update failed: incomplete write.";
    https.end();
    return false;
  }

  otaProgressCurrentBytes = otaProgressTotalBytes;
  otaProgressPercent = 100;
  otaStatusMessage = phaseLabel + " update completed.";
  https.end();
  return true;
}

void performQueuedOtaUpdate() {
  if (!otaUpdateRequested || otaUpdateRunning) {
    return;
  }

  if (!WiFi.isConnected()) {
    otaUpdateRequested = false;
    otaStatusMessage = "Update canceled because Wi-Fi is disconnected.";
    return;
  }

  otaUpdateRequested = false;
  otaUpdateRunning = true;
  otaStatusMessage = "Preparing update " + otaUpdateVersion + "...";
  otaProgressPercent = 0;
  otaProgressCurrentBytes = 0;
  otaProgressTotalBytes = 0;

  if (!runHttpUpdate(otaFirmwareUrl, U_FLASH, "Firmware")) {
    otaUpdateRunning = false;
    return;
  }

  otaUpdateRunning = false;
  otaProgressPercent = 100;
  otaCurrentPhase = "";
  otaProgressCurrentBytes = otaProgressTotalBytes;
  otaStatusMessage = "Firmware " + otaUpdateVersion + " installed. Restarting...";
  scheduleRestart(1500);
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

void startAccessPoint() {
  if (accessPointEnabled) {
    return;
  }

  WiFi.softAP(config.apSsid.c_str(), config.apPassword.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  accessPointEnabled = true;
}

void stopAccessPoint() {
  if (!accessPointEnabled) {
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  accessPointEnabled = false;
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

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
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

  addNoCacheHeaders();

  server.send_P(200, "text/html", INDEX_HTML);
}

void connectWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname(config.deviceName.c_str());
  startAccessPoint();

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
  pressureDoc["unit_of_meas"] = "bar";
  pressureDoc["dev_cla"] = "pressure";
  pressureDoc["val_tpl"] = "{{ value_json.pressure_bar }}";
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
  }

  return connected;
}

void sampleSensor() {
  uint32_t total = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    total += analogRead(A0);
    delay(2);
  }

  const uint16_t rawAdc = total / 8;
  const float a0Voltage = static_cast<float>(rawAdc) * ADC_PIN_MAX_VOLTAGE / 1023.0f;
  const float sensorVoltage = a0Voltage * ((DIVIDER_R1_KOHM + DIVIDER_R2_KOHM) / DIVIDER_R2_KOHM);
  const float alpha = sensorFilterAlpha();

  sensorState.rawAdc = rawAdc;
  sensorState.a0Voltage += alpha * (a0Voltage - sensorState.a0Voltage);
  sensorState.sensorVoltage += alpha * (sensorVoltage - sensorState.sensorVoltage);

  const float normalized = (sensorState.sensorVoltage - config.sensorMinVoltage) /
                           (config.sensorMaxVoltage - config.sensorMinVoltage);
  const float pressureKPa = clampFloat(normalized, 0.0f, 1.0f) * config.sensorMaxPressureKPa;
  sensorState.pressureKPa += alpha * (pressureKPa - sensorState.pressureKPa);
  sensorState.pressureBar = sensorState.pressureKPa / 100.0f;
  sensorState.alarmActive = config.buzzerEnabled && sensorState.pressureKPa >= config.buzzerAlarmThresholdKPa;
}

void publishState() {
  if (!mqttClient.connected()) {
    return;
  }

  JsonDocument doc;
  doc["device_name"] = config.deviceName;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["pressure_bar"] = serialized(String(sensorState.pressureBar, 2));
  doc["pressure_kpa"] = serialized(String(sensorState.pressureKPa, 1));
  doc["sensor_voltage"] = serialized(String(sensorState.sensorVoltage, 3));
  doc["a0_voltage"] = serialized(String(sensorState.a0Voltage, 3));
  doc["raw_adc"] = sensorState.rawAdc;
  doc["wifi_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  doc["ip"] = localIpString();
  doc["ui_url"] = devicePageUrl();
  doc["alarm"] = sensorState.alarmActive;
  doc["uptime_seconds"] = millis() / 1000;

  String payload;
  serializeJson(doc, payload);
  mqttPublish(stateTopic(), payload, false);
}

void drawDisplay() {
  const int pressureBaseline = 38 + config.oledValueYOffset;
  const String pressureLabel = displayedPressureLabel();

  display.clearBuffer();
  display.setFont(u8g2_font_6x13_tf);
  display.setCursor(0, 11);
  display.print(oledTopRowText());

  display.setFont(u8g2_font_logisoso24_tf);
  display.setCursor(0, pressureBaseline);
  display.print(displayedPressureValue(), displayedPressureDecimals());

  display.setFont(u8g2_font_6x13_tf);
  display.setCursor(96, pressureBaseline);
  display.print(pressureLabel);

  display.setCursor(0, 64);
  display.print(oledBottomRowText());
  display.sendBuffer();
}

void sendJsonConfig() {
  addNoCacheHeaders();
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
  doc["sensorFilterPreset"] = config.sensorFilterPreset;
  doc["buzzerAlarmThresholdKPa"] = config.buzzerAlarmThresholdKPa;
  doc["buzzerEnabled"] = config.buzzerEnabled;
  doc["publishIntervalSeconds"] = config.publishIntervalSeconds;
  doc["oledContrast"] = config.oledContrast;
  doc["oledFlip"] = config.oledFlip;
  doc["oledPressureUnit"] = config.oledPressureUnit;
  doc["oledTopRowMode"] = config.oledTopRowMode;
  doc["oledBottomRowMode"] = config.oledBottomRowMode;
  doc["oledValueYOffset"] = config.oledValueYOffset;

  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

void sendStatus() {
  addNoCacheHeaders();
  JsonDocument doc;
  doc["deviceName"] = config.deviceName;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
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
  addNoCacheHeaders();
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

void sendFirmwareInfo() {
  addNoCacheHeaders();
  const bool includeReleases = server.hasArg("refresh") && server.arg("refresh") == "1";
  JsonDocument response;
  response["currentVersion"] = FIRMWARE_VERSION;
  response["updateStatus"] = otaStatusMessage;
  response["updateBusy"] = otaUpdateRunning || otaUpdateRequested;
  response["updateProgress"] = otaProgressPercent;
  response["updatePhase"] = otaCurrentPhase;
  response["updateBytes"] = otaProgressCurrentBytes;
  response["updateTotalBytes"] = otaProgressTotalBytes;
  response["selectedVersion"] = otaUpdateVersion;

  JsonArray releasesOut = response["releases"].to<JsonArray>();
  if (!includeReleases) {
    String payload;
    serializeJson(response, payload);
    server.send(200, "application/json", payload);
    return;
  }

  JsonDocument tags;
  String errorMessage;
  if (!fetchGithubTags(tags, errorMessage)) {
    response["error"] = errorMessage;
    String payload;
    serializeJson(response, payload);
    server.send(200, "application/json", payload);
    return;
  }

  String latestVersion;
  for (JsonObject tag : tags.as<JsonArray>()) {
    const String tagName = String(static_cast<const char *>(tag["name"] | ""));
    if (tagName.isEmpty()) {
      continue;
    }

    if (latestVersion.isEmpty()) {
      latestVersion = tagName;
    }

    JsonObject item = releasesOut.add<JsonObject>();
    item["tag"] = tagName;
    item["name"] = tagName;
    item["publishedAt"] = "";
    item["prerelease"] = false;
    item["hasFilesystem"] = true;
    item["isCurrent"] = tagName == FIRMWARE_VERSION;
    item["isLatest"] = false;
    item["isNew"] = false;
  }

  response["latestVersion"] = latestVersion;
  response["updateAvailable"] = !latestVersion.isEmpty() && latestVersion != FIRMWARE_VERSION;

  for (JsonObject item : releasesOut) {
    const String tagName = String(static_cast<const char *>(item["tag"] | ""));
    const bool isLatest = tagName == latestVersion;
    item["isLatest"] = isLatest;
    item["isNew"] = isLatest && tagName != FIRMWARE_VERSION;
  }

  String payload;
  serializeJson(response, payload);
  server.send(200, "application/json", payload);
}

void handleFirmwareUpdatePost() {
  if (otaUpdateRunning || otaUpdateRequested) {
    server.send(409, "application/json", "{\"error\":\"Another update is already in progress\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const String requestedVersion = String(static_cast<const char *>(doc["version"] | ""));
  if (requestedVersion.isEmpty()) {
    server.send(400, "application/json", "{\"error\":\"Firmware version is required\"}");
    return;
  }

  String firmwareUrl;
  String filesystemUrl;
  String errorMessage;
  if (!resolveReleaseAssets(requestedVersion, firmwareUrl, filesystemUrl, errorMessage)) {
    JsonDocument response;
    response["error"] = errorMessage;
    String payload;
    serializeJson(response, payload);
    server.send(502, "application/json", payload);
    return;
  }

  otaUpdateVersion = requestedVersion;
  otaFirmwareUrl = firmwareUrl;
  otaUpdateRequested = true;
  otaUpdateRunning = false;
  otaProgressPercent = 0;
  otaCurrentPhase = "";
  otaStatusMessage = filesystemUrl.isEmpty()
                         ? "Update queued for " + requestedVersion + "."
                         : "Update queued for " + requestedVersion + " (firmware only, settings preserved).";

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Starting OTA update to " + requestedVersion;
  String payload;
  serializeJson(response, payload);
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
  config.sensorFilterPreset = String(static_cast<const char *>(doc["sensorFilterPreset"] | config.sensorFilterPreset.c_str()));
  config.buzzerAlarmThresholdKPa = doc["buzzerAlarmThresholdKPa"] | config.buzzerAlarmThresholdKPa;
  config.buzzerEnabled = doc["buzzerEnabled"] | config.buzzerEnabled;
  config.publishIntervalSeconds = doc["publishIntervalSeconds"] | config.publishIntervalSeconds;
  config.oledContrast = doc["oledContrast"] | config.oledContrast;
  config.oledFlip = doc["oledFlip"] | config.oledFlip;
  config.oledPressureUnit = String(static_cast<const char *>(doc["oledPressureUnit"] | config.oledPressureUnit.c_str()));
  config.oledTopRowMode = String(static_cast<const char *>(doc["oledTopRowMode"] | config.oledTopRowMode.c_str()));
  config.oledBottomRowMode = String(static_cast<const char *>(doc["oledBottomRowMode"] | config.oledBottomRowMode.c_str()));
  config.oledValueYOffset = doc["oledValueYOffset"] | config.oledValueYOffset;

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

  if (config.oledValueYOffset < -12) {
    config.oledValueYOffset = -12;
  } else if (config.oledValueYOffset > 12) {
    config.oledValueYOffset = 12;
  }

  if (config.oledPressureUnit != "bar" && config.oledPressureUnit != "psi" && config.oledPressureUnit != "mpa") {
    config.oledPressureUnit = "bar";
  }

  if (config.oledTopRowMode != "ip" && config.oledTopRowMode != "wifi" &&
      config.oledTopRowMode != "device" && config.oledTopRowMode != "topic") {
    config.oledTopRowMode = "ip";
  }

  if (config.oledBottomRowMode != "mqtt" && config.oledBottomRowMode != "wifi" &&
      config.oledBottomRowMode != "kpa" && config.oledBottomRowMode != "raw" &&
      config.oledBottomRowMode != "blank") {
    config.oledBottomRowMode = "mqtt";
  }

  if (config.sensorMaxVoltage <= config.sensorMinVoltage) {
    server.send(400, "application/json", "{\"error\":\"Sensor max voltage must be greater than min voltage\"}");
    return false;
  }

  if (config.sensorFilterPreset != "none" && config.sensorFilterPreset != "light" &&
      config.sensorFilterPreset != "soft" &&
      config.sensorFilterPreset != "hard" && config.sensorFilterPreset != "strong") {
    config.sensorFilterPreset = "none";
  }

  if (!savePersistentConfig()) {
    server.send(500, "application/json", "{\"error\":\"Failed to save persistent configuration\"}");
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
  server.on("/api/firmware", HTTP_GET, sendFirmwareInfo);
  server.on("/api/firmware/update", HTTP_POST, handleFirmwareUpdatePost);
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
  playBootMelody();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
  }

  EEPROM.begin(EEPROM_SIZE_BYTES);

  if (!loadConfig()) {
    Serial.println("Using default config");
    setDefaults();
    savePersistentConfig();
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
  if (accessPointEnabled) {
    dnsServer.processNextRequest();
  }

  performQueuedOtaUpdate();
  server.handleClient();

  const unsigned long now = millis();
  const bool wifiConnected = WiFi.isConnected();
  const bool mqttConnected = mqttClient.connected();
  updateStatusLed(now);

  if (wifiConnected && !wasWifiConnected) {
    stopAccessPoint();
    playWifiConnectedMelody();
  } else if (!wifiConnected && wasWifiConnected) {
    startAccessPoint();
    playWifiDisconnectedMelody();
  }
  wasWifiConnected = wifiConnected;

  if (mqttConnected && !wasMqttConnected) {
    playMqttConnectedMelody();
  }
  wasMqttConnected = mqttConnected;

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

  if (mqttConnected && now - lastPublishMs >= config.publishIntervalSeconds * 1000UL) {
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
