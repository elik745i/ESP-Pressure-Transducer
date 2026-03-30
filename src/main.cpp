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
constexpr char FIRMWARE_VERSION[] = "v1.1.7";
constexpr char GITHUB_OWNER[] = "elik745i";
constexpr char GITHUB_REPO[] = "ESP-Pressure-Transducer";
constexpr char GITHUB_RELEASES_API_URL[] = "https://api.github.com/repos/elik745i/ESP-Pressure-Transducer/releases?per_page=10";
constexpr char GITHUB_TAGS_API_URL[] = "https://api.github.com/repos/elik745i/ESP-Pressure-Transducer/tags?per_page=10";
constexpr char GITHUB_FIRMWARE_ASSET_NAME[] = "firmware.bin";
constexpr char GITHUB_FILESYSTEM_ASSET_NAME[] = "littlefs.bin";
constexpr char OTA_DIRECT_FIRMWARE_PATH[] = "firmware.bin";
constexpr uint8_t BUZZER_PIN = D5;
constexpr uint8_t TOUCH_SENSOR_PIN = D6;
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
  String otaBaseUrl;
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
  float buzzerAlarmMinPressureKPa;
  float buzzerAlarmMaxPressureKPa;
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
bool localFirmwareUploadOk = false;
bool localFirmwareUploadStarted = false;
bool localFirmwareUploadHadData = false;
String localFirmwareUploadError = "";
String localFirmwareUploadFilename = "";
bool touchSensorPressed = false;
unsigned long touchSensorDebounceDeadlineMs = 0;
const uint16_t *buzzerMelodyFrequencies = nullptr;
const uint16_t *buzzerMelodyDurations = nullptr;
size_t buzzerMelodyCount = 0;
size_t buzzerMelodyIndex = 0;
bool buzzerMelodyActive = false;
bool buzzerTonePhase = false;
unsigned long buzzerPhaseEndsAtMs = 0;
constexpr uint16_t BOOT_MELODY_FREQUENCIES[] = {1960, 2470, 2940};
constexpr uint16_t BOOT_MELODY_DURATIONS[] = {90, 90, 140};
constexpr uint16_t WIFI_CONNECTED_MELODY_FREQUENCIES[] = {1760, 2200, 2620};
constexpr uint16_t WIFI_CONNECTED_MELODY_DURATIONS[] = {90, 90, 130};
constexpr uint16_t WIFI_DISCONNECTED_MELODY_FREQUENCIES[] = {2620, 1960, 1470};
constexpr uint16_t WIFI_DISCONNECTED_MELODY_DURATIONS[] = {100, 100, 140};
constexpr uint16_t MQTT_CONNECTED_MELODY_FREQUENCIES[] = {1560, 2080, 1560, 3120};
constexpr uint16_t MQTT_CONNECTED_MELODY_DURATIONS[] = {70, 70, 70, 150};

enum class LedPattern {
  Booting,
  ApOnly,
  WifiConnecting,
  WifiConnected,
  WifiAndMqttConnected,
  RestartPending,
};

void scheduleRestart(unsigned long delayMs);
float clampFloat(float value, float low, float high);
void publishState();
void drawDisplay();

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
  config.otaBaseUrl = "";
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
  config.buzzerAlarmMinPressureKPa = 0.0f;
  config.buzzerAlarmMaxPressureKPa = 1100.0f;
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
  doc["otaBaseUrl"] = config.otaBaseUrl;
  doc["sensorMinVoltage"] = config.sensorMinVoltage;
  doc["sensorMaxVoltage"] = config.sensorMaxVoltage;
  doc["sensorMaxPressureKPa"] = config.sensorMaxPressureKPa;
  doc["sensorFilterPreset"] = config.sensorFilterPreset;
  doc["buzzerAlarmMinPressureKPa"] = config.buzzerAlarmMinPressureKPa;
  doc["buzzerAlarmMaxPressureKPa"] = config.buzzerAlarmMaxPressureKPa;
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
  config.otaBaseUrl = doc["otaBaseUrl"] | config.otaBaseUrl;
  config.sensorMinVoltage = doc["sensorMinVoltage"] | config.sensorMinVoltage;
  config.sensorMaxVoltage = doc["sensorMaxVoltage"] | config.sensorMaxVoltage;
  config.sensorMaxPressureKPa = doc["sensorMaxPressureKPa"] | config.sensorMaxPressureKPa;
  config.sensorFilterPreset = doc["sensorFilterPreset"] | config.sensorFilterPreset;
  config.buzzerAlarmMinPressureKPa = doc["buzzerAlarmMinPressureKPa"] | config.buzzerAlarmMinPressureKPa;
  config.buzzerAlarmMaxPressureKPa = doc["buzzerAlarmMaxPressureKPa"] | config.buzzerAlarmMaxPressureKPa;
  config.buzzerAlarmThresholdKPa = doc["buzzerAlarmThresholdKPa"] | config.buzzerAlarmThresholdKPa;
  config.buzzerEnabled = doc["buzzerEnabled"] | config.buzzerEnabled;
  config.publishIntervalSeconds = doc["publishIntervalSeconds"] | config.publishIntervalSeconds;
  config.oledContrast = doc["oledContrast"] | config.oledContrast;
  config.oledFlip = doc["oledFlip"] | config.oledFlip;
  config.oledPressureUnit = doc["oledPressureUnit"] | config.oledPressureUnit;
  config.oledTopRowMode = doc["oledTopRowMode"] | config.oledTopRowMode;
  config.oledBottomRowMode = doc["oledBottomRowMode"] | config.oledBottomRowMode;
  config.oledValueYOffset = doc["oledValueYOffset"] | config.oledValueYOffset;

  if (doc["buzzerAlarmMinPressureKPa"].isNull() && doc["buzzerAlarmMaxPressureKPa"].isNull()) {
    config.buzzerAlarmMinPressureKPa = 0.0f;
    config.buzzerAlarmMaxPressureKPa = config.buzzerAlarmThresholdKPa;
  }

  config.buzzerAlarmMinPressureKPa = clampFloat(config.buzzerAlarmMinPressureKPa, 0.0f, config.sensorMaxPressureKPa);
  config.buzzerAlarmMaxPressureKPa = clampFloat(config.buzzerAlarmMaxPressureKPa, config.buzzerAlarmMinPressureKPa, config.sensorMaxPressureKPa);

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

float kPaToBar(float pressureKPa) {
  return pressureKPa / 100.0f;
}

float barToKPa(float pressureBar) {
  return pressureBar * 100.0f;
}

String alarmMinPressureStateTopic() {
  return config.mqttBaseTopic + "/alarm_min_pressure/state";
}

String alarmMinPressureCommandTopic() {
  return config.mqttBaseTopic + "/alarm_min_pressure/set";
}

String alarmMaxPressureStateTopic() {
  return config.mqttBaseTopic + "/alarm_max_pressure/state";
}

String alarmMaxPressureCommandTopic() {
  return config.mqttBaseTopic + "/alarm_max_pressure/set";
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

void otaDebug(const String &message) {
  Serial.print(F("[OTA] "));
  Serial.println(message);
}

String githubReleaseAssetUrl(const String &tagName, const char *assetName) {
  return String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
         "/releases/download/" + tagName + "/" + assetName;
}

String normalizedOtaBaseUrl() {
  String baseUrl = config.otaBaseUrl;
  baseUrl.trim();
  while (baseUrl.endsWith("/")) {
    baseUrl.remove(baseUrl.length() - 1);
  }
  return baseUrl;
}

bool otaBaseUrlConfigured() {
  return !normalizedOtaBaseUrl().isEmpty();
}

String otaDirectDownloadUrl(const String &tagName, const char *assetName) {
  const String baseUrl = normalizedOtaBaseUrl();
  if (baseUrl.isEmpty()) {
    return "";
  }
  return baseUrl + "/" + tagName + "/" + assetName;
}

String chooseReleaseAssetUrl(const JsonVariantConst &assets, const char *assetName) {
  if (!assets.is<JsonArrayConst>()) {
    return "";
  }

  for (JsonObjectConst asset : assets.as<JsonArrayConst>()) {
    const String name = String(static_cast<const char *>(asset["name"] | ""));
    const String url = String(static_cast<const char *>(asset["browser_download_url"] | ""));
    if (name == assetName && !url.isEmpty()) {
      return url;
    }
  }
  return "";
}

bool fetchGithubTags(JsonDocument &doc, String &errorMessage) {
  if (!WiFi.isConnected()) {
    errorMessage = "Connect to Wi-Fi to check GitHub releases.";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, GITHUB_TAGS_API_URL)) {
    errorMessage = "Could not open GitHub tags API.";
    return false;
  }

  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", githubUserAgent());
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    errorMessage = String("GitHub API error: HTTP ") + statusCode;
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject tagFilter = filter[0].to<JsonObject>();
  tagFilter["name"] = true;

  doc.clear();
  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    errorMessage = String("GitHub tags parse failed: ") + error.c_str();
    return false;
  }
  if (!doc.is<JsonArray>()) {
    errorMessage = "GitHub tags response format invalid.";
    return false;
  }

  return true;
}

bool fetchGithubReleases(JsonDocument &doc, String &errorMessage) {
  if (!WiFi.isConnected()) {
    errorMessage = "Connect to Wi-Fi to check GitHub releases.";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, GITHUB_RELEASES_API_URL)) {
    errorMessage = "Could not open GitHub releases API.";
    return false;
  }

  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", githubUserAgent());
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    errorMessage = String("GitHub API error: HTTP ") + statusCode;
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject releaseFilter = filter[0].to<JsonObject>();
  releaseFilter["tag_name"] = true;
  releaseFilter["name"] = true;
  releaseFilter["draft"] = true;
  releaseFilter["prerelease"] = true;
  releaseFilter["published_at"] = true;
  JsonObject assetFilter = releaseFilter["assets"][0].to<JsonObject>();
  assetFilter["name"] = true;
  assetFilter["browser_download_url"] = true;

  doc.clear();
  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    errorMessage = String("GitHub response parse failed: ") + error.c_str();
    return false;
  }
  if (!doc.is<JsonArray>()) {
    errorMessage = "GitHub response format invalid.";
    return false;
  }

  return true;
}

bool resolveReleaseAssets(const String &tagName, String &firmwareUrl, String &filesystemUrl, String &errorMessage) {
  JsonDocument releases;
  if (fetchGithubReleases(releases, errorMessage)) {
    for (JsonObjectConst release : releases.as<JsonArrayConst>()) {
      const bool draft = release["draft"] | false;
      const bool prerelease = release["prerelease"] | false;
      if (draft || prerelease) {
        continue;
      }

      const String releaseTag = String(static_cast<const char *>(release["tag_name"] | ""));
      if (releaseTag != tagName) {
        continue;
      }

      firmwareUrl = chooseReleaseAssetUrl(release["assets"], GITHUB_FIRMWARE_ASSET_NAME);
      filesystemUrl = chooseReleaseAssetUrl(release["assets"], GITHUB_FILESYSTEM_ASSET_NAME);
      if (firmwareUrl.isEmpty()) {
        errorMessage = "Selected release has no firmware.bin asset.";
        return false;
      }
      return true;
    }
  }

  JsonDocument tags;
  String tagsError;
  if (!fetchGithubTags(tags, tagsError)) {
    if (errorMessage.isEmpty()) {
      errorMessage = tagsError;
    } else {
      errorMessage += " Fallback failed: " + tagsError;
    }
    return false;
  }

  for (JsonObjectConst tag : tags.as<JsonArrayConst>()) {
    const String releaseTag = String(static_cast<const char *>(tag["name"] | ""));
    if (releaseTag != tagName) {
      continue;
    }

    firmwareUrl = githubReleaseAssetUrl(releaseTag, GITHUB_FIRMWARE_ASSET_NAME);
    filesystemUrl = githubReleaseAssetUrl(releaseTag, GITHUB_FILESYSTEM_ASSET_NAME);
    return true;
  }

  errorMessage = "Selected firmware release was not found on GitHub.";
  return false;
}

String resolveFirmwareDownloadUrl(const String &tagName, const String &githubAssetUrl) {
  const String directUrl = otaDirectDownloadUrl(tagName, OTA_DIRECT_FIRMWARE_PATH);
  if (!directUrl.isEmpty()) {
    return directUrl;
  }
  return githubAssetUrl;
}

void fillDiscoveryDevice(JsonObject device) {
  JsonArray ids = device["ids"].to<JsonArray>();
  ids.add(uniqueIdBase());
  device["name"] = config.deviceName;
  device["mf"] = "DIY";
  device["mdl"] = "Wemos D1 mini pressure transducer";
  device["sw"] = FIRMWARE_VERSION;
  const String configUrl = devicePageUrl();
  if (!configUrl.isEmpty()) {
    device["configuration_url"] = configUrl;
  }
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

void startMelody(const uint16_t *frequencies, const uint16_t *durations, size_t noteCount, bool honorMute) {
  if (honorMute && !config.buzzerEnabled) {
    return;
  }

  buzzerMelodyFrequencies = frequencies;
  buzzerMelodyDurations = durations;
  buzzerMelodyCount = noteCount;
  buzzerMelodyIndex = 0;
  buzzerMelodyActive = noteCount > 0;
  buzzerTonePhase = false;
  buzzerPhaseEndsAtMs = 0;
  noTone(BUZZER_PIN);
}

void serviceBuzzer(unsigned long now) {
  if (!buzzerMelodyActive) {
    return;
  }
  if (static_cast<long>(now - buzzerPhaseEndsAtMs) < 0) {
    return;
  }

  if (buzzerTonePhase) {
    noTone(BUZZER_PIN);
    buzzerTonePhase = false;
    ++buzzerMelodyIndex;
    buzzerPhaseEndsAtMs = now + 30;
    return;
  }

  if (buzzerMelodyIndex >= buzzerMelodyCount) {
    buzzerMelodyActive = false;
    noTone(BUZZER_PIN);
    return;
  }

  const uint16_t frequency = buzzerMelodyFrequencies[buzzerMelodyIndex];
  const uint16_t duration = buzzerMelodyDurations[buzzerMelodyIndex];
  if (frequency == 0 || duration == 0) {
    noTone(BUZZER_PIN);
    ++buzzerMelodyIndex;
    buzzerPhaseEndsAtMs = now + duration;
    return;
  }

  tone(BUZZER_PIN, frequency, duration);
  buzzerTonePhase = true;
  buzzerPhaseEndsAtMs = now + duration;
}

void beep(uint16_t frequency, uint16_t durationMs) {
  static uint16_t singleFrequency[1];
  static uint16_t singleDuration[1];
  singleFrequency[0] = frequency;
  singleDuration[0] = durationMs;
  startMelody(singleFrequency, singleDuration, 1, true);
}

void playMelody(const uint16_t *frequencies, const uint16_t *durations, size_t noteCount, bool honorMute) {
  startMelody(frequencies, durations, noteCount, honorMute);
}

void playBootMelody() {
  playMelody(BOOT_MELODY_FREQUENCIES, BOOT_MELODY_DURATIONS, 3, false);
}

void playWifiConnectedMelody() {
  playMelody(WIFI_CONNECTED_MELODY_FREQUENCIES, WIFI_CONNECTED_MELODY_DURATIONS, 3, true);
}

void playWifiDisconnectedMelody() {
  playMelody(WIFI_DISCONNECTED_MELODY_FREQUENCIES, WIFI_DISCONNECTED_MELODY_DURATIONS, 3, true);
}

void playMqttConnectedMelody() {
  playMelody(MQTT_CONNECTED_MELODY_FREQUENCIES, MQTT_CONNECTED_MELODY_DURATIONS, 4, true);
}

void handleTouchSensor(unsigned long now) {
  if (static_cast<long>(now - touchSensorDebounceDeadlineMs) < 0) {
    return;
  }

  const bool isPressed = digitalRead(TOUCH_SENSOR_PIN) == HIGH;
  if (isPressed == touchSensorPressed) {
    return;
  }

  touchSensorPressed = isPressed;
  touchSensorDebounceDeadlineMs = now + 150;
  if (!isPressed) {
    return;
  }

  beep(2600, 70);
  config.oledPressureUnit = (config.oledPressureUnit == "psi") ? "bar" : "psi";
  drawDisplay();
}

bool hasBinExtension(const String &filename) {
  String lowercase = filename;
  lowercase.toLowerCase();
  return lowercase.endsWith(".bin");
}

void resetLocalFirmwareUploadState() {
  localFirmwareUploadOk = false;
  localFirmwareUploadStarted = false;
  localFirmwareUploadHadData = false;
  localFirmwareUploadError = "";
  localFirmwareUploadFilename = "";
}

void failLocalFirmwareUpload(const String &message) {
  localFirmwareUploadError = message;
  otaUpdateRunning = false;
  otaProgressPercent = 0;
  otaProgressCurrentBytes = 0;
  otaProgressTotalBytes = 0;
  otaCurrentPhase = "";
  otaStatusMessage = message;
}

bool runHttpUpdate(const String &url, int updateCommand, const String &phaseLabel) {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  client.setBufferSizes(1024, 1024);

  otaCurrentPhase = phaseLabel;
  otaProgressCurrentBytes = 0;
  otaProgressTotalBytes = 0;
  otaProgressPercent = 0;
  otaStatusMessage = phaseLabel + " update started...";
  otaDebug("Starting " + phaseLabel + " OTA from: " + url);
  otaDebug("Wi-Fi RSSI: " + String(WiFi.RSSI()) + " dBm");
  otaDebug("Redirect mode: HTTPC_FORCE_FOLLOW_REDIRECTS");

  HTTPClient http;
  const char *headerKeys[] = {"Content-Type", "Location"};
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent(githubUserAgent());
  http.useHTTP10(true);
  http.collectHeaders(headerKeys, 2);
  if (!http.begin(client, url)) {
    otaDebug("HTTP begin failed.");
    otaStatusMessage = phaseLabel + " update failed: could not open URL.";
    return false;
  }

  http.addHeader("Accept", "application/octet-stream");
  const int code = http.GET();
  otaDebug("HTTP GET code: " + String(code));
  otaDebug("Requested URL: " + url);
  otaDebug("Redirect location header: " + http.header("Location"));
  if (code < 200 || code >= 300) {
    otaDebug("HTTP GET failed, response text: " + http.getString());
    otaStatusMessage = phaseLabel + " update failed: HTTP " + String(code);
    http.end();
    return false;
  }

  const int totalLen = http.getSize();
  otaDebug("Content-Length: " + String(totalLen));
  otaDebug("Content-Type: " + http.header("Content-Type"));
  if (totalLen <= 0) {
    otaStatusMessage = phaseLabel + " update failed: unknown content length.";
    http.end();
    return false;
  }

  WiFiClient &stream = http.getStream();
  stream.setTimeout(3000);
  otaDebug("Stream object acquired.");
  otaDebug("HTTP connected immediately after headers: " + String(http.connected() ? "yes" : "no"));

  uint8_t headerProbe[4] = {0};
  const size_t peeked = stream.peekBytes(headerProbe, sizeof(headerProbe));
  if (peeked > 0) {
    char headerHex[16];
    snprintf(headerHex, sizeof(headerHex), "%02X %02X %02X %02X",
             headerProbe[0],
             peeked > 1 ? headerProbe[1] : 0,
             peeked > 2 ? headerProbe[2] : 0,
             peeked > 3 ? headerProbe[3] : 0);
    otaDebug("First bytes: " + String(headerHex));
  } else {
    otaDebug("Could not peek first bytes from OTA stream.");
  }

  if (!Update.begin(static_cast<size_t>(totalLen), updateCommand)) {
    otaDebug("Update.begin failed: " + Update.getErrorString());
    otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
    http.end();
    return false;
  }

  uint8_t buffer[1024];
  int remaining = totalLen;
  uint8_t emptyReadCount = 0;
  bool sawBodyData = false;
  while (remaining > 0) {
    const size_t available = static_cast<size_t>(stream.available());
    const size_t toRead = available > 0
                              ? (available > sizeof(buffer) ? sizeof(buffer) : available)
                              : (remaining > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : static_cast<size_t>(remaining));
    const int bytesRead = stream.readBytes(buffer, toRead);
    if (bytesRead <= 0) {
      ++emptyReadCount;
      otaDebug("Stream read returned 0, connected=" + String(http.connected() ? "yes" : "no") +
               ", available=" + String(stream.available()) +
               ", remaining=" + String(remaining) +
               ", empty_reads=" + String(emptyReadCount));
      if (emptyReadCount >= 5) {
        otaStatusMessage = sawBodyData
                               ? phaseLabel + " update failed: stream stalled before completion."
                               : phaseLabel + " update failed: empty OTA body.";
        otaDebug("Rejecting OTA because the body stream did not provide data.");
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    emptyReadCount = 0;
    sawBodyData = true;
    otaDebug("Read chunk: " + String(bytesRead) + " bytes, connected=" +
             String(http.connected() ? "yes" : "no") +
             ", available=" + String(stream.available()) +
             ", remaining_before=" + String(remaining));

    if (Update.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
      otaDebug("Update.write failed at " + String(otaProgressCurrentBytes) + " bytes: " + Update.getErrorString());
      otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
      http.end();
      return false;
    }

    otaProgressCurrentBytes += static_cast<size_t>(bytesRead);
    if (remaining > 0) {
      remaining -= bytesRead;
    }
    if (totalLen > 0) {
      otaProgressTotalBytes = static_cast<size_t>(totalLen);
      otaProgressPercent = static_cast<uint8_t>(
          min<size_t>(100U, (otaProgressCurrentBytes * 100U) / static_cast<size_t>(totalLen)));
      otaStatusMessage = phaseLabel + " update " + String(otaProgressPercent) + "%";
    }
    delay(0);
  }

  http.end();
  otaDebug("HTTP download finished, received bytes: " + String(otaProgressCurrentBytes));
  if (!sawBodyData) {
    otaStatusMessage = phaseLabel + " update failed: empty OTA body.";
    otaDebug("Rejecting OTA because zero body bytes were read.");
    return false;
  }

  otaProgressPercent = 100;
  otaCurrentPhase = phaseLabel;
  otaStatusMessage = phaseLabel + " finalizing...";
  if (!Update.end(true)) {
    otaDebug("Update.end(true) failed: " + Update.getErrorString());
    otaStatusMessage = phaseLabel + " update failed: " + Update.getErrorString();
    return false;
  }

  otaProgressCurrentBytes = otaProgressTotalBytes;
  otaDebug(phaseLabel + " OTA completed successfully.");
  otaStatusMessage = phaseLabel + " update completed.";
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

bool saveRuntimeConfig() {
  return savePersistentConfig() && saveConfig();
}

void publishAlarmSettings() {
  if (!mqttClient.connected()) {
    return;
  }

  mqttPublish(alarmMinPressureStateTopic(), String(kPaToBar(config.buzzerAlarmMinPressureKPa), 2), true);
  mqttPublish(alarmMaxPressureStateTopic(), String(kPaToBar(config.buzzerAlarmMaxPressureKPa), 2), true);
}

void publishDiscovery() {
  if (!config.mqttDiscoveryEnabled || config.mqttDiscoveryPrefix.isEmpty()) {
    return;
  }

  mqttPublish(config.mqttDiscoveryPrefix + "/sensor/" + uniqueIdBase() + "/pressure/config", "", true);

  JsonDocument pressureDoc;
  pressureDoc["name"] = "Pressure";
  pressureDoc["uniq_id"] = uniqueIdBase() + "_pressure_bar";
  pressureDoc["stat_t"] = stateTopic();
  pressureDoc["avty_t"] = availabilityTopic();
  pressureDoc["unit_of_meas"] = "bar";
  pressureDoc["dev_cla"] = "pressure";
  pressureDoc["val_tpl"] = "{{ value_json.pressure_bar }}";
  pressureDoc["sug_dsp_prc"] = 2;
  pressureDoc["exp_aft"] = config.publishIntervalSeconds * 3;
  fillDiscoveryDevice(pressureDoc["dev"].to<JsonObject>());

  String pressurePayload;
  serializeJson(pressureDoc, pressurePayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/sensor/" + uniqueIdBase() + "/pressure_bar/config", pressurePayload, true);

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

  JsonDocument alarmMinDoc;
  alarmMinDoc["name"] = "Min";
  alarmMinDoc["uniq_id"] = uniqueIdBase() + "_alarm_min_pressure";
  alarmMinDoc["stat_t"] = alarmMinPressureStateTopic();
  alarmMinDoc["cmd_t"] = alarmMinPressureCommandTopic();
  alarmMinDoc["avty_t"] = availabilityTopic();
  alarmMinDoc["unit_of_meas"] = "bar";
  alarmMinDoc["mode"] = "box";
  alarmMinDoc["min"] = 0;
  alarmMinDoc["max"] = serialized(String(kPaToBar(config.sensorMaxPressureKPa), 2));
  alarmMinDoc["step"] = 0.01;
  fillDiscoveryDevice(alarmMinDoc["dev"].to<JsonObject>());

  String alarmMinPayload;
  serializeJson(alarmMinDoc, alarmMinPayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/number/" + uniqueIdBase() + "/alarm_min_pressure/config", alarmMinPayload, true);

  JsonDocument alarmMaxDoc;
  alarmMaxDoc["name"] = "Max";
  alarmMaxDoc["uniq_id"] = uniqueIdBase() + "_alarm_max_pressure";
  alarmMaxDoc["stat_t"] = alarmMaxPressureStateTopic();
  alarmMaxDoc["cmd_t"] = alarmMaxPressureCommandTopic();
  alarmMaxDoc["avty_t"] = availabilityTopic();
  alarmMaxDoc["unit_of_meas"] = "bar";
  alarmMaxDoc["mode"] = "box";
  alarmMaxDoc["min"] = 0;
  alarmMaxDoc["max"] = serialized(String(kPaToBar(config.sensorMaxPressureKPa), 2));
  alarmMaxDoc["step"] = 0.01;
  fillDiscoveryDevice(alarmMaxDoc["dev"].to<JsonObject>());

  String alarmMaxPayload;
  serializeJson(alarmMaxDoc, alarmMaxPayload);
  mqttPublish(config.mqttDiscoveryPrefix + "/number/" + uniqueIdBase() + "/alarm_max_pressure/config", alarmMaxPayload, true);

  publishAlarmSettings();
  mqttDiscoveryPublished = true;
}

void handleMqttMessage(char *topic, byte *payload, unsigned int length) {
  if (topic == nullptr || payload == nullptr) {
    return;
  }

  String topicString(topic);
  String payloadString;
  payloadString.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    payloadString += static_cast<char>(payload[index]);
  }
  payloadString.trim();
  if (payloadString.isEmpty()) {
    return;
  }

  char *endPtr = nullptr;
  const float requestedValue = strtof(payloadString.c_str(), &endPtr);
  if (endPtr == payloadString.c_str() || (endPtr != nullptr && *endPtr != '\0')) {
    return;
  }

  const float requestedKPa = barToKPa(requestedValue);

  if (topicString == alarmMinPressureCommandTopic()) {
    config.buzzerAlarmMinPressureKPa = clampFloat(requestedKPa, 0.0f, config.buzzerAlarmMaxPressureKPa);
  } else if (topicString == alarmMaxPressureCommandTopic()) {
    config.buzzerAlarmMaxPressureKPa = clampFloat(requestedKPa, config.buzzerAlarmMinPressureKPa, config.sensorMaxPressureKPa);
  } else {
    return;
  }

  if (!saveRuntimeConfig()) {
    return;
  }

  publishAlarmSettings();
  publishState();
  publishDiscovery();
}

bool connectMqtt() {
  if (!config.mqttEnabled || config.mqttHost.isEmpty() || !WiFi.isConnected()) {
    return false;
  }

  mqttClient.setServer(config.mqttHost.c_str(), config.mqttPort);
  mqttClient.setCallback(handleMqttMessage);
  const bool connected = config.mqttUser.isEmpty()
                             ? mqttClient.connect(mqttClientId().c_str(), availabilityTopic().c_str(), 0, true, "offline")
                             : mqttClient.connect(mqttClientId().c_str(), config.mqttUser.c_str(), config.mqttPassword.c_str(), availabilityTopic().c_str(), 0, true, "offline");

  if (connected) {
    mqttPublish(availabilityTopic(), "online", true);
    mqttClient.subscribe(alarmMinPressureCommandTopic().c_str());
    mqttClient.subscribe(alarmMaxPressureCommandTopic().c_str());
    publishDiscovery();
  }

  return connected;
}

void sampleSensor() {
  uint32_t total = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    total += analogRead(A0);
    yield();
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
  sensorState.alarmActive = config.buzzerEnabled &&
                            (sensorState.pressureKPa <= config.buzzerAlarmMinPressureKPa ||
                             sensorState.pressureKPa >= config.buzzerAlarmMaxPressureKPa);
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
  doc["alarm_min_kpa"] = serialized(String(config.buzzerAlarmMinPressureKPa, 1));
  doc["alarm_max_kpa"] = serialized(String(config.buzzerAlarmMaxPressureKPa, 1));
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
  doc["otaBaseUrl"] = config.otaBaseUrl;
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
  doc["buzzerAlarmMinPressureKPa"] = config.buzzerAlarmMinPressureKPa;
  doc["buzzerAlarmMaxPressureKPa"] = config.buzzerAlarmMaxPressureKPa;
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
  response["otaBaseUrlConfigured"] = otaBaseUrlConfigured();
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

  String latestVersion;
  JsonDocument releases;
  String errorMessage;
  if (fetchGithubReleases(releases, errorMessage)) {
    for (JsonObjectConst release : releases.as<JsonArrayConst>()) {
      const bool draft = release["draft"] | false;
      const bool prerelease = release["prerelease"] | false;
      if (draft || prerelease) {
        continue;
      }

      const String tagName = String(static_cast<const char *>(release["tag_name"] | ""));
      const String firmwareUrl = chooseReleaseAssetUrl(release["assets"], GITHUB_FIRMWARE_ASSET_NAME);
      const String filesystemUrl = chooseReleaseAssetUrl(release["assets"], GITHUB_FILESYSTEM_ASSET_NAME);
      if (tagName.isEmpty() || firmwareUrl.isEmpty()) {
        continue;
      }

      if (latestVersion.isEmpty()) {
        latestVersion = tagName;
      }

      JsonObject item = releasesOut.add<JsonObject>();
      item["tag"] = tagName;
      item["name"] = String(static_cast<const char *>(release["name"] | tagName.c_str()));
      item["publishedAt"] = String(static_cast<const char *>(release["published_at"] | ""));
      item["prerelease"] = prerelease;
      item["hasFilesystem"] = !filesystemUrl.isEmpty();
      item["downloadUrl"] = resolveFirmwareDownloadUrl(tagName, firmwareUrl);
      item["isCurrent"] = tagName == FIRMWARE_VERSION;
      item["isLatest"] = false;
      item["isNew"] = false;
    }
  } else {
    JsonDocument tags;
    String tagsError;
    if (!fetchGithubTags(tags, tagsError)) {
      response["error"] = errorMessage.isEmpty() ? tagsError : errorMessage;
      String payload;
      serializeJson(response, payload);
      server.send(200, "application/json", payload);
      return;
    }

    for (JsonObjectConst tag : tags.as<JsonArrayConst>()) {
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
      item["downloadUrl"] = resolveFirmwareDownloadUrl(tagName, githubReleaseAssetUrl(tagName, GITHUB_FIRMWARE_ASSET_NAME));
      item["isCurrent"] = tagName == FIRMWARE_VERSION;
      item["isLatest"] = false;
      item["isNew"] = false;
    }
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
    otaDebug("Failed to resolve assets for " + requestedVersion + ": " + errorMessage);
    JsonDocument response;
    response["error"] = errorMessage;
    String payload;
    serializeJson(response, payload);
    server.send(502, "application/json", payload);
    return;
  }

  otaUpdateVersion = requestedVersion;
  otaFirmwareUrl = resolveFirmwareDownloadUrl(requestedVersion, firmwareUrl);
  otaDebug("Resolved GitHub firmware asset for " + requestedVersion + ": " + firmwareUrl);
  otaDebug("Selected OTA download URL for " + requestedVersion + ": " + otaFirmwareUrl);
  otaUpdateRequested = true;
  otaUpdateRunning = false;
  otaProgressPercent = 0;
  otaCurrentPhase = "";
  otaStatusMessage = otaBaseUrlConfigured()
                         ? "Update queued for " + requestedVersion + " from OTA host."
                         : "Update queued for " + requestedVersion + " from GitHub.";

  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Starting OTA update to " + requestedVersion;
  String payload;
  serializeJson(response, payload);
  server.send(200, "application/json", payload);
}

void handleFirmwareUploadPost() {
  JsonDocument response;
  if (!localFirmwareUploadError.isEmpty()) {
    response["error"] = localFirmwareUploadError;
    String payload;
    serializeJson(response, payload);
    server.send(400, "application/json", payload);
    return;
  }

  if (!localFirmwareUploadOk) {
    response["error"] = "Firmware upload did not complete.";
    String payload;
    serializeJson(response, payload);
    server.send(400, "application/json", payload);
    return;
  }

  response["ok"] = true;
  response["message"] = "Local firmware uploaded successfully. Restarting...";
  String payload;
  serializeJson(response, payload);
  server.send(200, "application/json", payload);
}

void handleFirmwareUploadData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    resetLocalFirmwareUploadState();
    if (otaUpdateRunning || otaUpdateRequested) {
      localFirmwareUploadError = "Another update is already in progress.";
      return;
    }

    localFirmwareUploadFilename = upload.filename;
    if (!hasBinExtension(upload.filename)) {
      localFirmwareUploadError = "Select a .bin firmware image.";
      return;
    }

    localFirmwareUploadStarted = true;
    otaUpdateRunning = true;
    otaUpdateRequested = false;
    otaUpdateVersion = "local";
    otaCurrentPhase = "Upload";
    otaProgressPercent = 0;
    otaProgressCurrentBytes = 0;
    otaProgressTotalBytes = upload.totalSize;
    otaStatusMessage = "Uploading local firmware...";

    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000U) & 0xFFFFF000U;
    if (!Update.begin(maxSketchSpace, U_FLASH)) {
      failLocalFirmwareUpload(String("Local firmware update failed: ") + Update.getErrorString());
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!localFirmwareUploadError.isEmpty() || !localFirmwareUploadStarted) {
      return;
    }

    if (!localFirmwareUploadHadData) {
      localFirmwareUploadHadData = upload.currentSize > 0;
      if (!localFirmwareUploadHadData || upload.buf[0] != 0xE9) {
        failLocalFirmwareUpload("Uploaded file is not a valid ESP8266 firmware image.");
        return;
      }
    }

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failLocalFirmwareUpload(String("Local firmware update failed: ") + Update.getErrorString());
      return;
    }

    otaProgressCurrentBytes += upload.currentSize;
    if (otaProgressTotalBytes > 0) {
      otaProgressPercent = static_cast<uint8_t>((otaProgressCurrentBytes * 100U) / otaProgressTotalBytes);
    }
    otaStatusMessage = "Uploading local firmware... " + String(otaProgressPercent) + "%";
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!localFirmwareUploadError.isEmpty() || !localFirmwareUploadStarted) {
      otaUpdateRunning = false;
      return;
    }

    if (!localFirmwareUploadHadData) {
      failLocalFirmwareUpload("Uploaded firmware did not contain data.");
      return;
    }

    if (!Update.end(true)) {
      const String updateError = Update.getErrorString();
      failLocalFirmwareUpload(updateError == "No Error"
                                  ? "Local firmware update failed: uploaded image could not be finalized."
                                  : String("Local firmware update failed: ") + updateError);
      return;
    }

    if (!Update.isFinished()) {
      failLocalFirmwareUpload("Local firmware update failed: incomplete write.");
      return;
    }

    localFirmwareUploadOk = true;
    otaUpdateRunning = false;
    otaProgressCurrentBytes = otaProgressTotalBytes;
    otaProgressPercent = 100;
    otaCurrentPhase = "";
    otaStatusMessage = "Local firmware uploaded. Restarting...";
    scheduleRestart(1500);
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    failLocalFirmwareUpload("Local firmware upload was canceled.");
  }
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
  config.otaBaseUrl = String(static_cast<const char *>(doc["otaBaseUrl"] | config.otaBaseUrl.c_str()));
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
  config.buzzerAlarmMinPressureKPa = doc["buzzerAlarmMinPressureKPa"] | config.buzzerAlarmMinPressureKPa;
  config.buzzerAlarmMaxPressureKPa = doc["buzzerAlarmMaxPressureKPa"] | config.buzzerAlarmMaxPressureKPa;
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

  config.otaBaseUrl.trim();
  while (config.otaBaseUrl.endsWith("/")) {
    config.otaBaseUrl.remove(config.otaBaseUrl.length() - 1);
  }
  if (!config.otaBaseUrl.isEmpty() &&
      !config.otaBaseUrl.startsWith("http://") &&
      !config.otaBaseUrl.startsWith("https://")) {
    server.send(400, "application/json", "{\"error\":\"OTA Base URL must start with http:// or https://\"}");
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

  if (config.buzzerAlarmMinPressureKPa < 0.0f || config.buzzerAlarmMaxPressureKPa < 0.0f) {
    server.send(400, "application/json", "{\"error\":\"Alarm pressures must be zero or higher\"}");
    return false;
  }

  if (config.buzzerAlarmMaxPressureKPa < config.buzzerAlarmMinPressureKPa) {
    server.send(400, "application/json", "{\"error\":\"Alarm max pressure must be greater than or equal to alarm min pressure\"}");
    return false;
  }

  if (config.buzzerAlarmMaxPressureKPa > config.sensorMaxPressureKPa) {
    server.send(400, "application/json", "{\"error\":\"Alarm max pressure cannot exceed sensor max pressure\"}");
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
  if (mqttClient.connected()) {
    publishAlarmSettings();
    publishDiscovery();
    publishState();
  }
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
  server.on("/api/firmware/upload", HTTP_POST, handleFirmwareUploadPost, handleFirmwareUploadData);
  server.on("/api/restart", HTTP_POST, handleRestartPost);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setupApp() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(TOUCH_SENSOR_PIN, INPUT);
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
  serviceBuzzer(now);
  handleTouchSensor(now);
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
