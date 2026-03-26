#pragma once

namespace {

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP Pressure Transducer</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f2f2f2;
      --panel: #ffffff;
      --panel-soft: #f8f8f8;
      --ink: #333333;
      --muted: #6a6a6a;
      --accent: #f39c12;
      --accent-strong: #d98200;
      --accent-soft: #fff4df;
      --line: #d8d8d8;
      --warn: #b45309;
      --shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
      --radius: 12px;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      font-family: Verdana, Arial, sans-serif;
      color: var(--ink);
      background: var(--bg);
    }

    .wrap {
      width: min(980px, calc(100% - 20px));
      margin: 12px auto 24px;
    }

    .hero,
    .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
    }

    .hero {
      padding: 16px;
      display: grid;
      gap: 14px;
    }

    h1,
    h2 {
      margin: 0;
      font-weight: 700;
    }

    h1 {
      font-size: clamp(1.4rem, 2.3vw, 2rem);
    }

    h2 {
      font-size: 1rem;
      margin-bottom: 12px;
      color: #444444;
    }

    .hero-header {
      text-align: center;
    }

    .stats {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
      gap: 10px;
    }

    .stat {
      padding: 12px;
      border-radius: 10px;
      background: var(--panel-soft);
      border: 1px solid var(--line);
      text-align: center;
    }

    .stat-label {
      font-size: 0.76rem;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      color: var(--muted);
    }

    .stat-value {
      margin-top: 6px;
      font-size: clamp(1.1rem, 3vw, 1.8rem);
      font-weight: 700;
      color: #2d2d2d;
    }

    .grid {
      margin-top: 12px;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
      gap: 12px;
    }

    .panel {
      padding: 16px;
    }

    .panel-wide {
      grid-column: 1 / -1;
    }

    form {
      display: grid;
      gap: 10px;
    }

    label {
      display: grid;
      gap: 4px;
      font-size: 0.88rem;
      font-weight: 600;
    }

    input,
    select {
      width: 100%;
      border: 1px solid #cfcfcf;
      border-radius: 8px;
      padding: 10px 12px;
      background: #ffffff;
      color: var(--ink);
      font: inherit;
      min-height: 42px;
    }

    .password-field {
      position: relative;
    }

    .password-field input {
      padding-right: 52px;
    }

    .password-toggle {
      position: absolute;
      top: 50%;
      right: 8px;
      width: 36px;
      height: 36px;
      padding: 0;
      border-radius: 999px;
      border: 1px solid #d5d5d5;
      background: #f7f7f7;
      color: #555555;
      transform: translateY(-50%);
      transition: transform 180ms ease, background-color 180ms ease, border-color 180ms ease, color 180ms ease;
    }

    .password-toggle:hover {
      background: #fff4df;
      border-color: #cf8400;
      transform: translateY(-50%) scale(1.05);
    }

    .eye-icon {
      position: relative;
      display: block;
      width: 18px;
      height: 12px;
      margin: 0 auto;
      border: 2px solid currentColor;
      border-radius: 12px / 8px;
      transition: transform 180ms ease;
    }

    .eye-icon::before {
      content: "";
      position: absolute;
      top: 50%;
      left: 50%;
      width: 4px;
      height: 4px;
      border-radius: 50%;
      background: currentColor;
      transform: translate(-50%, -50%);
    }

    .eye-icon::after {
      content: "";
      position: absolute;
      top: -4px;
      left: 7px;
      width: 2px;
      height: 18px;
      background: currentColor;
      border-radius: 999px;
      transform: rotate(35deg) scaleY(0);
      transform-origin: center;
      opacity: 0;
      transition: transform 180ms ease, opacity 180ms ease;
    }

    .password-toggle.revealed {
      background: #fff4df;
      border-color: #cf8400;
      color: #b36d00;
    }

    .password-toggle.revealed .eye-icon {
      transform: scale(1.05);
    }

    .password-toggle.revealed .eye-icon::after {
      transform: rotate(35deg) scaleY(1);
      opacity: 1;
    }

    input[type="checkbox"] {
      width: 18px;
      height: 18px;
    }

    .check {
      display: flex;
      gap: 10px;
      align-items: center;
      font-weight: 600;
      padding: 6px 0;
    }

    .row {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }

    button {
      border: 0;
      border-radius: 8px;
      padding: 10px 16px;
      font: inherit;
      font-weight: 700;
      color: white;
      background: linear-gradient(180deg, var(--accent), var(--accent-strong));
      cursor: pointer;
      transition: filter 120ms ease, transform 120ms ease;
      box-shadow: none;
    }

    button:hover {
      filter: brightness(1.02);
    }

    button.secondary {
      color: var(--ink);
      background: #e9e9e9;
      box-shadow: none;
      border: 1px solid #cfcfcf;
    }

    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-top: 8px;
    }

    .inline-actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
    }

    .inline-actions button {
      flex: 0 0 auto;
    }

    .scan-status {
      color: var(--muted);
      font-size: 0.86rem;
      font-weight: 600;
    }

    .status-list {
      display: grid;
      gap: 6px;
      margin-top: 8px;
    }

    .firmware-summary {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 10px;
      margin-bottom: 14px;
    }

    .firmware-card {
      padding: 12px;
      border-radius: 10px;
      background: var(--panel-soft);
      border: 1px solid var(--line);
    }

    .firmware-card strong {
      display: block;
      margin-top: 4px;
      font-size: 1rem;
    }

    .firmware-list {
      display: grid;
      gap: 8px;
      margin-top: 12px;
    }

    .firmware-item {
      display: grid;
      grid-template-columns: auto 1fr auto;
      gap: 10px;
      align-items: center;
      padding: 12px;
      border: 1px solid var(--line);
      border-radius: 10px;
      background: #fbfbfb;
    }

    .firmware-meta {
      display: grid;
      gap: 4px;
    }

    .firmware-title {
      font-weight: 700;
    }

    .firmware-subtitle {
      font-size: 0.82rem;
      color: var(--muted);
    }

    .badge-row {
      display: flex;
      flex-wrap: wrap;
      gap: 6px;
      justify-content: flex-end;
    }

    .badge {
      display: inline-flex;
      align-items: center;
      min-height: 22px;
      padding: 0 8px;
      border-radius: 999px;
      font-size: 0.76rem;
      font-weight: 700;
      background: #ececec;
      color: #555555;
    }

    .badge.new {
      background: #fff2cc;
      color: #9a6700;
    }

    .badge.current {
      background: #dcfce7;
      color: #166534;
    }

    .badge.latest {
      background: #dbeafe;
      color: #1d4ed8;
    }

    .status-item {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      border-bottom: 1px solid #efefef;
      padding: 4px 0 8px;
      font-size: 0.92rem;
    }

    .note {
      margin-top: 10px;
      font-size: 0.84rem;
      color: var(--warn);
      line-height: 1.4;
    }

    #message {
      min-height: 1.4em;
      font-weight: 700;
    }

    .tabs {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin-bottom: 16px;
      padding-bottom: 10px;
      border-bottom: 1px solid #ececec;
    }

    .tab-button {
      color: #505050;
      background: #eeeeee;
      box-shadow: none;
      border: 1px solid #d5d5d5;
      min-width: 110px;
    }

    .tab-button[aria-selected="true"] {
      color: white;
      background: linear-gradient(180deg, var(--accent), var(--accent-strong));
      border-color: #cf8400;
    }

    .tab-button:focus-visible,
    input:focus-visible,
    button:focus-visible {
      outline: 2px solid #4d90fe;
      outline-offset: 1px;
    }

    .tab-panel {
      display: none;
    }

    .tab-panel.active {
      display: block;
      animation: fadeIn 140ms ease;
    }

    @keyframes fadeIn {
      from {
        opacity: 0;
      }
      to {
        opacity: 1;
      }
    }

    @media (max-width: 640px) {
      .wrap {
        width: min(100% - 12px, 980px);
        margin-top: 6px;
      }

      .row {
        grid-template-columns: 1fr;
      }

      .hero,
      .panel {
        padding: 12px;
      }

      .tabs {
        display: grid;
        grid-template-columns: repeat(2, minmax(0, 1fr));
      }

      .tab-button {
        width: 100%;
        min-width: 0;
      }

      .stat-value {
        font-size: 1.25rem;
      }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <div class="hero-header">
        <h1 id="deviceTitle">ESP8266 Pressure Monitor</h1>
      </div>

      <div class="stats">
        <div class="stat">
          <div class="stat-label">Pressure</div>
          <div class="stat-value" id="pressureValue">--.- kPa</div>
        </div>
        <div class="stat">
          <div class="stat-label">Pressure</div>
          <div class="stat-value" id="barValue">--.-- bar</div>
        </div>
        <div class="stat">
          <div class="stat-label">Sensor Voltage</div>
          <div class="stat-value" id="sensorVoltage">-.---</div>
        </div>
        <div class="stat">
          <div class="stat-label">Connection</div>
          <div class="stat-value" id="connectionState">Checking...</div>
        </div>
      </div>
    </section>

    <div class="grid">
      <section class="panel panel-wide">
        <div class="tabs" role="tablist" aria-label="Configuration tabs">
          <button class="tab-button" type="button" role="tab" aria-selected="true" aria-controls="tab-device" data-tab="device">Info</button>
          <button class="tab-button" type="button" role="tab" aria-selected="false" aria-controls="tab-wifi" data-tab="wifi">Wi-Fi</button>
          <button class="tab-button" type="button" role="tab" aria-selected="false" aria-controls="tab-mqtt" data-tab="mqtt">MQTT</button>
          <button class="tab-button" type="button" role="tab" aria-selected="false" aria-controls="tab-calibration" data-tab="calibration">Calibration</button>
          <button class="tab-button" type="button" role="tab" aria-selected="false" aria-controls="tab-oled" data-tab="oled">OLED</button>
          <button class="tab-button" type="button" role="tab" aria-selected="false" aria-controls="tab-firmware" data-tab="firmware">Firmware</button>
        </div>

        <div id="tab-device" class="tab-panel active" role="tabpanel">
          <h2>Info</h2>
          <div class="status-list">
            <div class="status-item"><span>Wi-Fi</span><strong id="wifiStatus">-</strong></div>
            <div class="status-item"><span>IP Address</span><strong id="ipAddress">-</strong></div>
            <div class="status-item"><span>Access Point</span><strong id="apInfo">-</strong></div>
            <div class="status-item"><span>MQTT</span><strong id="mqttStatus">-</strong></div>
            <div class="status-item"><span>ADC Raw</span><strong id="rawAdc">-</strong></div>
            <div class="status-item"><span>Alarm</span><strong id="alarmStatus">-</strong></div>
            <div class="status-item"><span>Uptime</span><strong id="uptime">-</strong></div>
          </div>
          <p class="note">If pressure reads wrong, adjust the sensor voltage range and max pressure in Calibration.</p>
        </div>

        <div id="tab-wifi" class="tab-panel" role="tabpanel">
          <h2>Wi-Fi</h2>
          <form id="networkForm">
            <div class="row">
              <label>Wi-Fi SSID
                <input id="wifiSsid" autocomplete="off">
              </label>
              <label>Wi-Fi Password
                <div class="password-field">
                  <input id="wifiPassword" type="password" autocomplete="new-password">
                  <button class="password-toggle" type="button" data-target="wifiPassword" aria-label="Show Wi-Fi password">
                    <span class="eye-icon" aria-hidden="true"></span>
                  </button>
                </div>
              </label>
            </div>
            <div class="inline-actions">
              <button id="scanWifiBtn" type="button" class="secondary">Search Wi-Fi</button>
              <span id="scanStatus" class="scan-status"></span>
            </div>
            <label>Found Networks
              <select id="wifiNetworkList">
                <option value="">Select a scanned SSID</option>
              </select>
            </label>
            <div class="row">
              <label>Access Point SSID
                <input id="apSsid" autocomplete="off">
              </label>
              <label>Access Point Password
                <div class="password-field">
                  <input id="apPassword" type="password" autocomplete="new-password">
                  <button class="password-toggle" type="button" data-target="apPassword" aria-label="Show access point password">
                    <span class="eye-icon" aria-hidden="true"></span>
                  </button>
                </div>
              </label>
            </div>
          </form>
        </div>

        <div id="tab-mqtt" class="tab-panel" role="tabpanel">
          <h2>MQTT</h2>
          <form id="mqttForm">
            <label class="check"><input id="mqttEnabled" type="checkbox"> Enable MQTT publishing</label>
            <label>MQTT Host
              <input id="mqttHost" autocomplete="off" placeholder="192.168.1.10">
            </label>
            <div class="row">
              <label>MQTT Port
                <input id="mqttPort" type="number" min="1" max="65535">
              </label>
              <label>Publish Interval (s)
                <input id="publishIntervalSeconds" type="number" min="1" max="3600">
              </label>
            </div>
            <div class="row">
              <label>MQTT Username
                <input id="mqttUser" autocomplete="off">
              </label>
              <label>MQTT Password
                <div class="password-field">
                  <input id="mqttPassword" type="password" autocomplete="new-password">
                  <button class="password-toggle" type="button" data-target="mqttPassword" aria-label="Show MQTT password">
                    <span class="eye-icon" aria-hidden="true"></span>
                  </button>
                </div>
              </label>
            </div>
            <label>Base Topic
              <input id="mqttBaseTopic" autocomplete="off" placeholder="home/pressure-device">
            </label>
            <label class="check"><input id="mqttDiscoveryEnabled" type="checkbox"> Publish Home Assistant discovery entities</label>
            <label>Discovery Prefix
              <input id="mqttDiscoveryPrefix" autocomplete="off" placeholder="homeassistant">
            </label>
          </form>
        </div>

        <div id="tab-calibration" class="tab-panel" role="tabpanel">
          <h2>Calibration</h2>
          <form id="deviceForm">
            <div class="row">
              <label>Device Name
                <input id="deviceName" autocomplete="off">
              </label>
              <label>Max Pressure (kPa)
                <input id="sensorMaxPressureKPa" type="number" step="0.1">
              </label>
            </div>
            <div class="row">
              <label>Sensor Min Voltage (V)
                <input id="sensorMinVoltage" type="number" step="0.01">
              </label>
              <label>Sensor Max Voltage (V)
                <input id="sensorMaxVoltage" type="number" step="0.01">
              </label>
            </div>
            <div class="row">
              <label>Alarm Threshold (kPa)
                <input id="buzzerAlarmThresholdKPa" type="number" step="0.1">
              </label>
              <label class="check"><input id="buzzerEnabled" type="checkbox"> Enable buzzer alarm</label>
            </div>
            <div class="row">
              <label>Sensor Filter
                <select id="sensorFilterPreset">
                  <option value="none">None</option>
                  <option value="light">Light</option>
                  <option value="soft">Soft</option>
                  <option value="hard">Hard</option>
                  <option value="strong">Strong</option>
                </select>
              </label>
            </div>
          </form>
        </div>

        <div id="tab-oled" class="tab-panel" role="tabpanel">
          <h2>OLED Configuration</h2>
          <form id="oledForm">
            <div class="row">
              <label>Display Unit
                <select id="oledPressureUnit">
                  <option value="bar">bar</option>
                  <option value="psi">PSI</option>
                  <option value="mpa">MPa</option>
                </select>
              </label>
              <label>OLED Contrast
                <input id="oledContrast" type="number" min="1" max="255">
              </label>
              <label class="check"><input id="oledFlip" type="checkbox"> Rotate OLED 180 deg</label>
            </div>
            <div class="row">
              <label>Top Row
                <select id="oledTopRowMode">
                  <option value="ip">IP / AP IP</option>
                  <option value="wifi">Wi-Fi / AP name</option>
                  <option value="device">Device name</option>
                  <option value="topic">MQTT topic</option>
                </select>
              </label>
              <label>Bottom Row
                <select id="oledBottomRowMode">
                  <option value="mqtt">MQTT status</option>
                  <option value="wifi">Wi-Fi status</option>
                  <option value="kpa">Pressure in kPa</option>
                  <option value="raw">Raw ADC</option>
                  <option value="blank">Blank</option>
                </select>
              </label>
            </div>
            <div class="row">
              <label>Value Vertical Offset
                <input id="oledValueYOffset" type="number" min="-12" max="12" step="1">
              </label>
            </div>
            <p class="note">Choose what the OLED shows on the top and bottom rows, adjust the main value position, and switch the pressure unit between bar, PSI, and MPa.</p>
          </form>
        </div>

        <div id="tab-firmware" class="tab-panel" role="tabpanel">
          <h2>Firmware</h2>
          <div class="firmware-summary">
            <div class="firmware-card">
              <div class="stat-label">Current Firmware</div>
              <strong id="currentFirmwareVersion">-</strong>
            </div>
            <div class="firmware-card">
              <div class="stat-label">Latest Release</div>
              <strong id="latestFirmwareVersion">-</strong>
            </div>
            <div class="firmware-card">
              <div class="stat-label">Update Status</div>
              <strong id="firmwareStatusText">Idle</strong>
            </div>
          </div>
          <div class="inline-actions">
            <button id="refreshFirmwareBtn" type="button" class="secondary">Check Releases</button>
            <button id="updateFirmwareBtn" type="button">Update Selected</button>
            <span id="firmwareSelectionLabel" class="scan-status">No firmware selected</span>
          </div>
          <div id="firmwareList" class="firmware-list"></div>
          <p class="note">OTA updates download release assets from GitHub. The device updates LittleFS first when available, then flashes the selected firmware release.</p>
        </div>

        <div class="actions">
          <button id="saveBtn" type="button">Save Configuration</button>
          <button id="restartBtn" type="button" class="secondary">Restart Device</button>
        </div>
        <p id="message"></p>
      </section>
    </div>
  </div>

  <script>
    const fields = [
      "deviceName", "apSsid", "apPassword", "wifiSsid", "wifiPassword",
      "mqttHost", "mqttPort", "mqttUser", "mqttPassword", "mqttBaseTopic",
      "mqttDiscoveryPrefix", "sensorMinVoltage",
      "sensorMaxVoltage", "sensorMaxPressureKPa", "buzzerAlarmThresholdKPa",
      "sensorFilterPreset",
      "publishIntervalSeconds", "oledContrast", "oledPressureUnit",
      "oledTopRowMode", "oledBottomRowMode", "oledValueYOffset"
    ];

    const checkboxes = ["mqttEnabled", "mqttDiscoveryEnabled", "buzzerEnabled", "oledFlip"];
    const tabButtons = Array.from(document.querySelectorAll(".tab-button"));
    const tabPanels = Array.from(document.querySelectorAll(".tab-panel"));
    const wifiNetworkList = document.getElementById("wifiNetworkList");
    const passwordToggles = Array.from(document.querySelectorAll(".password-toggle"));
    const firmwareList = document.getElementById("firmwareList");
    let firmwareBusy = false;

    function activateTab(tabName) {
      tabButtons.forEach((button) => {
        const isActive = button.dataset.tab === tabName;
        button.setAttribute("aria-selected", isActive ? "true" : "false");
      });

      tabPanels.forEach((panel) => {
        panel.classList.toggle("active", panel.id === `tab-${tabName}`);
      });

      if (tabName === "firmware") {
        refreshFirmwareInfo().catch((error) => setMessage(error.message, true));
      }
    }

    async function fetchJson(url, options) {
      const response = await fetch(url, options);
      if (!response.ok) {
        let errorText = response.statusText;
        try {
          const payload = await response.json();
          errorText = payload.error || payload.message || errorText;
        } catch (_) {
        }
        throw new Error(errorText);
      }
      return response.json();
    }

    function setMessage(text, isError = false) {
      const message = document.getElementById("message");
      message.textContent = text;
      message.style.color = isError ? "#b91c1c" : "#115e59";
    }

    function setScanStatus(text, isError = false) {
      const status = document.getElementById("scanStatus");
      status.textContent = text;
      status.style.color = isError ? "#b91c1c" : "";
    }

    function renderWifiNetworks(networks) {
      wifiNetworkList.innerHTML = "";

      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = networks.length ? "Select a scanned SSID" : "No networks found";
      wifiNetworkList.appendChild(placeholder);

      networks.forEach((network) => {
        if (!network.ssid) {
          return;
        }

        const option = document.createElement("option");
        option.value = network.ssid;
        option.textContent = `${network.ssid} (${network.rssi} dBm${network.encrypted ? ", locked" : ", open"})`;
        wifiNetworkList.appendChild(option);
      });
    }

    function loadForm(config) {
      fields.forEach((id) => {
        const element = document.getElementById(id);
        if (element) {
          element.value = config[id] ?? "";
        }
      });
      checkboxes.forEach((id) => {
        const element = document.getElementById(id);
        if (element) {
          element.checked = Boolean(config[id]);
        }
      });
      document.getElementById("deviceTitle").textContent = "ESP8266 Pressure Monitor";
    }

    function selectedFirmwareVersion() {
      const selected = document.querySelector('input[name="firmwareVersion"]:checked');
      return selected ? selected.value : "";
    }

    function updateFirmwareSelectionLabel() {
      const selected = selectedFirmwareVersion();
      document.getElementById("firmwareSelectionLabel").textContent = selected ? `Selected: ${selected}` : "No firmware selected";
    }

    function renderFirmwareList(releases, currentVersion, latestVersion, selectedVersion) {
      firmwareList.innerHTML = "";

      if (!releases.length) {
        firmwareList.innerHTML = '<div class="note">No GitHub firmware releases found.</div>';
        updateFirmwareSelectionLabel();
        return;
      }

      releases.forEach((release, index) => {
        const item = document.createElement("label");
        item.className = "firmware-item";

        const radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "firmwareVersion";
        radio.value = release.tag;
        radio.checked = Boolean(
          (selectedVersion && release.tag === selectedVersion) ||
          (!selectedVersion && (release.isLatest || (!latestVersion && index === 0)))
        );
        radio.addEventListener("change", updateFirmwareSelectionLabel);

        const meta = document.createElement("div");
        meta.className = "firmware-meta";

        const title = document.createElement("div");
        title.className = "firmware-title";
        title.textContent = release.name || release.tag;

        const subtitle = document.createElement("div");
        subtitle.className = "firmware-subtitle";
        subtitle.textContent = `${release.tag} - ${release.publishedAt || "unknown date"}${release.hasFilesystem ? " - firmware + LittleFS" : " - firmware only"}`;

        meta.appendChild(title);
        meta.appendChild(subtitle);

        const badges = document.createElement("div");
        badges.className = "badge-row";

        if (release.isCurrent || release.tag === currentVersion) {
          const badge = document.createElement("span");
          badge.className = "badge current";
          badge.textContent = "Installed";
          badges.appendChild(badge);
        }

        if (release.isLatest || release.tag === latestVersion) {
          const badge = document.createElement("span");
          badge.className = `badge ${release.isNew ? "new" : "latest"}`;
          badge.textContent = release.isNew ? "New" : "Latest";
          badges.appendChild(badge);
        }

        if (release.prerelease) {
          const badge = document.createElement("span");
          badge.className = "badge";
          badge.textContent = "Pre-release";
          badges.appendChild(badge);
        }

        item.appendChild(radio);
        item.appendChild(meta);
        item.appendChild(badges);
        firmwareList.appendChild(item);
      });

      updateFirmwareSelectionLabel();
    }

    function updateFirmwarePanel(info) {
      const currentVersion = info.currentVersion || "-";
      const latestVersion = info.latestVersion || "No release";
      document.getElementById("currentFirmwareVersion").textContent = currentVersion;
      document.getElementById("latestFirmwareVersion").textContent = latestVersion;
      document.getElementById("firmwareStatusText").textContent = info.updateStatus || "Idle";
      firmwareBusy = Boolean(info.updateBusy);
      renderFirmwareList(Array.isArray(info.releases) ? info.releases : [], currentVersion, info.latestVersion || "", info.selectedVersion || "");
      if (info.error) {
        setMessage(info.error, true);
      }
    }

    function readForm() {
      const payload = {};
      fields.forEach((id) => {
        payload[id] = document.getElementById(id).value;
      });
      ["mqttPort", "publishIntervalSeconds"].forEach((id) => {
        payload[id] = Number(payload[id]);
      });
      ["sensorMinVoltage", "sensorMaxVoltage", "sensorMaxPressureKPa", "buzzerAlarmThresholdKPa", "oledContrast", "oledValueYOffset"].forEach((id) => {
        payload[id] = Number(payload[id]);
      });
      checkboxes.forEach((id) => {
        payload[id] = document.getElementById(id).checked;
      });
      return payload;
    }

    function formatUptime(seconds) {
      const days = Math.floor(seconds / 86400);
      const hours = Math.floor((seconds % 86400) / 3600);
      const minutes = Math.floor((seconds % 3600) / 60);
      return `${days}d ${hours}h ${minutes}m`;
    }

    function updateStatus(status) {
      document.getElementById("pressureValue").textContent = `${status.pressureKPa} kPa`;
      document.getElementById("barValue").textContent = `${status.pressureBar} bar`;
      document.getElementById("sensorVoltage").textContent = `${status.sensorVoltage} V`;
      document.getElementById("connectionState").textContent = status.wifiConnected ? "Wi-Fi linked" : "AP only";
      document.getElementById("wifiStatus").textContent = status.wifiConnected ? status.wifiSsid : "Not connected";
      document.getElementById("ipAddress").textContent = status.ipAddress;
      document.getElementById("apInfo").textContent = `${status.apSsid} @ ${status.apIp}`;
      document.getElementById("mqttStatus").textContent = status.mqttConnected ? `Connected to ${status.mqttHost || "broker"}` : "Disconnected";
      document.getElementById("rawAdc").textContent = status.rawAdc;
      document.getElementById("alarmStatus").textContent = status.alarmActive ? "Active" : "Normal";
      document.getElementById("uptime").textContent = formatUptime(status.uptimeSeconds);
    }

    async function refreshConfig() {
      const config = await fetchJson("/api/config");
      loadForm(config);
    }

    async function refreshStatus() {
      const status = await fetchJson("/api/status");
      updateStatus(status);
    }

    async function refreshFirmwareInfo() {
      const info = await fetchJson("/api/firmware");
      updateFirmwarePanel(info);
    }

    async function scanWifiNetworks() {
      const button = document.getElementById("scanWifiBtn");
      button.disabled = true;
      setScanStatus("Searching...");

      try {
        const response = await fetchJson("/api/wifi/scan");
        const networks = Array.isArray(response.networks) ? response.networks : [];
        renderWifiNetworks(networks);
        setScanStatus(networks.length ? `Found ${networks.length} network(s)` : "No networks found");
      } catch (error) {
        setScanStatus(error.message, true);
      } finally {
        button.disabled = false;
      }
    }

    async function saveConfiguration() {
      setMessage("Saving configuration...");
      try {
        const payload = readForm();
        const response = await fetchJson("/api/config", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(payload)
        });
        setMessage(response.message || "Configuration saved.");
      } catch (error) {
        setMessage(error.message, true);
      }
    }

    async function restartDevice() {
      setMessage("Restarting device...");
      try {
        const response = await fetchJson("/api/restart", { method: "POST" });
        setMessage(response.message || "Restart scheduled.");
      } catch (error) {
        setMessage(error.message, true);
      }
    }

    async function updateFirmware() {
      const version = selectedFirmwareVersion();
      if (!version) {
        setMessage("Select a firmware release first.", true);
        return;
      }

      setMessage(`Starting OTA update to ${version}...`);

      try {
        const response = await fetchJson("/api/firmware/update", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ version })
        });
        setMessage(response.message || `Update queued for ${version}.`);
        await refreshFirmwareInfo();
      } catch (error) {
        setMessage(error.message, true);
      }
    }

    document.getElementById("saveBtn").addEventListener("click", saveConfiguration);
    document.getElementById("restartBtn").addEventListener("click", restartDevice);
    document.getElementById("scanWifiBtn").addEventListener("click", scanWifiNetworks);
    document.getElementById("refreshFirmwareBtn").addEventListener("click", () => refreshFirmwareInfo().catch((error) => setMessage(error.message, true)));
    document.getElementById("updateFirmwareBtn").addEventListener("click", updateFirmware);
    wifiNetworkList.addEventListener("change", () => {
      if (wifiNetworkList.value) {
        document.getElementById("wifiSsid").value = wifiNetworkList.value;
      }
    });
    tabButtons.forEach((button) => button.addEventListener("click", () => activateTab(button.dataset.tab)));
    passwordToggles.forEach((button) => {
      button.addEventListener("click", () => {
        const input = document.getElementById(button.dataset.target);
        const reveal = input.type === "password";
        input.type = reveal ? "text" : "password";
        button.classList.toggle("revealed", reveal);
      });
    });

    refreshConfig().catch((error) => setMessage(error.message, true));
    refreshStatus().catch((error) => setMessage(error.message, true));
    refreshFirmwareInfo().catch(() => {});
    setInterval(() => refreshStatus().catch(() => {}), 3000);
    setInterval(() => {
      if (firmwareBusy || document.getElementById("tab-firmware").classList.contains("active")) {
        refreshFirmwareInfo().catch(() => {});
      }
    }, 5000);
  </script>
</body>
</html>
)rawliteral";

}  // namespace