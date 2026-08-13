#include "web_portal.h"
#include "version.h"
#include <WiFi.h>

namespace {

// Minimal inline CSS/markup - same "embedded PROGMEM, no filesystem"
// approach as chromecast-esp32/src/web_server.h's _handleRoot(). Kept
// deliberately small: this page's only job is WiFi setup, not the full
// marquee editor (that's web/, over WebSerial). Dark theme with the same
// purple/teal accent pairing as the web app's own header
// (web/src/style.css's gradient title), so the two feel like the same
// project instead of a stock plain-HTML form.
const char PAGE_HEAD[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>tty2oled setup</title>"
    "<style>"
    ":root{--bg:#14141c;--card:#1e1e2a;--border:#33334a;--text:#e8e8f0;"
    "--muted:#9494ab;--accent:#8b6bff;--accent2:#3fd9c7;--danger:#ff6b6b}"
    "*{box-sizing:border-box}"
    "body{font-family:-apple-system,'Segoe UI',Roboto,sans-serif;max-width:440px;"
    "margin:2em auto;padding:0 1em;background:var(--bg);color:var(--text)}"
    "h1{font-size:1.3em;margin:1.4em 0 .4em;background:linear-gradient(90deg,var(--accent),var(--accent2));"
    "-webkit-background-clip:text;background-clip:text;color:transparent;font-weight:700}"
    "h1:first-of-type{margin-top:0}"
    "label{display:block;margin-top:.7em;font-size:.85em;color:var(--muted)}"
    "input,select{width:100%;padding:.55em;margin-top:.25em;background:var(--card);"
    "border:1px solid var(--border);border-radius:6px;color:var(--text);font-size:1em}"
    "input:focus,select:focus{outline:none;border-color:var(--accent)}"
    "input[type=checkbox]{width:auto;margin:0 .4em 0 0;vertical-align:middle}"
    "button{padding:.65em 1.2em;margin-top:1em;background:var(--accent);color:#fff;"
    "border:none;border-radius:6px;font-size:1em;cursor:pointer;transition:background .15s}"
    "button:hover{background:#7857f5}"
    "button.danger{background:var(--danger)}"
    "button.danger:hover{background:#e85555}"
    "p{line-height:1.5}"
    "code{background:var(--card);border:1px solid var(--border);border-radius:4px;"
    "padding:.1em .4em;font-size:.9em;color:var(--accent2)}"
    ".muted{color:var(--muted);font-size:.9em}"
    "</style></head><body>";

const char PAGE_TAIL[] PROGMEM = "</body></html>";

String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

} // namespace

void WebPortal::begin(WifiConfigStore *config, bool apMode) {
  _config = config;
  _apMode = apMode;

  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/save", HTTP_POST, [this]() { handleSave(); });
  _server.on("/reset", HTTP_POST, [this]() { handleReset(); });
  _server.on("/status", HTTP_GET, [this]() { handleStatus(); });
  if (_apMode) {
    _server.on("/scan", HTTP_GET, [this]() { handleScan(); });
    _server.onNotFound([this]() { handleRoot(); }); // captive portal
  }
  _server.begin();
}

void WebPortal::setMqttConfig(MqttConfigStore *mqttConfig) {
  _mqttConfig = mqttConfig;
  _server.on("/mqtt-save", HTTP_POST, [this]() { handleMqttSave(); });
}

void WebPortal::startCaptiveDns(IPAddress apIp) {
  _dns.start(53, "*", apIp);
  _dnsActive = true;
}

void WebPortal::loop() {
  if (_dnsActive) _dns.processNextRequest();
  _server.handleClient();
}

void WebPortal::handleRoot() {
  String body;
  body.reserve(1024);
  body += FPSTR(PAGE_HEAD);

  if (_apMode) {
    body += "<h1>tty2oled WiFi setup</h1>"
            "<p class='muted'>Join your home WiFi network so this device "
            "is reachable at its own address instead of running its own "
            "access point.</p>"
            "<button type='button' onclick='doScan()'>Scan for networks</button>"
            "<select id='ssidList' style='width:100%;margin:.3em 0;display:none'></select>"
            "<form method='POST' action='/save'>"
            "<label>WiFi network name (SSID)</label>"
            "<input name='ssid' id='ssid' maxlength='63' required>"
            "<label>Password</label>"
            "<input name='pass' type='password' maxlength='63'>"
            "<button type='submit'>Save &amp; connect</button>"
            "</form>"
            "<script>"
            "function doScan(){"
            "var sel=document.getElementById('ssidList');"
            "sel.style.display='block';"
            "sel.innerHTML='<option>Scanning...</option>';"
            "fetch('/scan').then(function(r){return r.json()}).then(function(j){"
            "sel.innerHTML='<option value=\"\">Select a network...</option>';"
            "j.networks.forEach(function(n){"
            "var opt=document.createElement('option');"
            "opt.value=n.ssid;"
            "opt.textContent=(n.open?'':'\\uD83D\\uDD12 ')+n.ssid+' ('+n.rssi+'dBm)';"
            "sel.appendChild(opt)"
            "});"
            "}).catch(function(){sel.innerHTML='<option>Scan failed</option>'})"
            "}"
            "document.getElementById('ssidList').addEventListener('change',function(){"
            "if(this.value)document.getElementById('ssid').value=this.value"
            "});"
            "</script>";
  } else {
    body += "<h1>tty2oled status</h1>"
            "<p>Connected to <b>" + htmlEscape(_config->cfg.wifiSsid) + "</b></p>"
            "<p>IP: " + WiFi.localIP().toString() + "</p>"
            "<p class='muted'>Firmware " FW_VERSION " &middot; " REPO_URL "</p>"
            "<form method='POST' action='/reset' onsubmit=\"return confirm('Forget WiFi and restart into setup mode?');\">"
            "<button type='submit' class='danger'>Forget WiFi</button>"
            "</form>";

    if (_mqttConfig) {
      const MqttConfig &m = _mqttConfig->cfg;
      body += "<h1>MQTT notifications</h1>"
              "<p class='muted'>Subscribe to <code>&lt;prefix&gt;/text</code> and "
              "<code>&lt;prefix&gt;/image</code> (an image URL) on a broker - e.g. "
              "Home Assistant's Mosquitto add-on.</p>"
              "<form method='POST' action='/mqtt-save'>"
              "<label><input type='checkbox' name='enabled' value='1'"
              + String(m.enabled ? " checked" : "") + "> Enabled</label>"
              "<label>Broker host</label>"
              "<input name='host' maxlength='63' value='" + htmlEscape(m.host) + "'>"
              "<label>Port</label>"
              "<input name='port' type='number' min='1' max='65535' value='" + String(m.port) + "'>"
              "<label>Username (optional)</label>"
              "<input name='username' maxlength='31' value='" + htmlEscape(m.username) + "'>"
              "<label>Password (optional)</label>"
              "<input name='password' type='password' maxlength='31' value='" + htmlEscape(m.password) + "'>"
              "<label>Topic prefix (blank = tty2oled/&lt;device name&gt;)</label>"
              "<input name='topicPrefix' maxlength='47' value='" + htmlEscape(m.topicPrefix) + "'>"
              "<label>Notification duration (ms)</label>"
              "<input name='durationMs' type='number' min='1000' max='60000' value='" + String(m.durationMs) + "'>"
              "<button type='submit'>Save MQTT settings</button>"
              "</form>";
    }
  }

  body += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html", body);
}

void WebPortal::handleSave() {
  String ssid = _server.arg("ssid");
  String pass = _server.arg("pass");
  if (ssid.length() == 0 || ssid.length() >= sizeof(_config->cfg.wifiSsid)) {
    _server.send(400, "text/plain", "Invalid SSID");
    return;
  }
  strncpy(_config->cfg.wifiSsid, ssid.c_str(), sizeof(_config->cfg.wifiSsid) - 1);
  _config->cfg.wifiSsid[sizeof(_config->cfg.wifiSsid) - 1] = '\0';
  strncpy(_config->cfg.wifiPass, pass.c_str(), sizeof(_config->cfg.wifiPass) - 1);
  _config->cfg.wifiPass[sizeof(_config->cfg.wifiPass) - 1] = '\0';
  _config->cfg.configured = true;
  _config->save();

  String body;
  body += FPSTR(PAGE_HEAD);
  body += "<h1>Saved</h1><p>Restarting and connecting to "
          + htmlEscape(ssid) + "&hellip;</p>";
  body += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html", body);

  delay(300); // let the response actually flush before restarting
  ESP.restart();
}

void WebPortal::handleReset() {
  _config->reset();
  String body;
  body += FPSTR(PAGE_HEAD);
  body += "<h1>WiFi forgotten</h1><p>Restarting into setup mode&hellip;</p>";
  body += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html", body);

  delay(300);
  ESP.restart();
}

void WebPortal::handleMqttSave() {
  MqttConfig &m = _mqttConfig->cfg;
  m.enabled = _server.hasArg("enabled");
  strncpy(m.host, _server.arg("host").c_str(), sizeof(m.host) - 1);
  m.host[sizeof(m.host) - 1] = '\0';
  m.port = (uint16_t)_server.arg("port").toInt();
  if (m.port == 0) m.port = 1883;
  strncpy(m.username, _server.arg("username").c_str(), sizeof(m.username) - 1);
  m.username[sizeof(m.username) - 1] = '\0';
  strncpy(m.password, _server.arg("password").c_str(), sizeof(m.password) - 1);
  m.password[sizeof(m.password) - 1] = '\0';
  strncpy(m.topicPrefix, _server.arg("topicPrefix").c_str(), sizeof(m.topicPrefix) - 1);
  m.topicPrefix[sizeof(m.topicPrefix) - 1] = '\0';
  m.durationMs = (uint16_t)_server.arg("durationMs").toInt();
  if (m.durationMs < 1000) m.durationMs = 8000;
  _mqttConfig->save();

  String body;
  body += FPSTR(PAGE_HEAD);
  body += "<h1>MQTT settings saved</h1><p>Restarting to apply&hellip;</p>";
  body += FPSTR(PAGE_TAIL);
  _server.send(200, "text/html", body);

  // Simplest reliable way to apply new broker settings - mqtt_client.cpp
  // only reads MqttConfigStore once, at mqtt_client_init(). A restart is
  // cheap here (device is already on WiFi, reconnects immediately) and
  // avoids adding a second "re-init the MQTT client live" code path.
  delay(300);
  ESP.restart();
}

void WebPortal::handleScan() {
  // Synchronous WiFi.scanNetworks() - same pattern as chromecast-esp32's
  // /wifiscan. Blocks a few seconds, which is fine here: this only runs
  // in AP mode, on-demand from a button click on the setup form, not on
  // any hot path.
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + htmlEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();
  _server.send(200, "application/json", json);
}

void WebPortal::handleStatus() {
  String json = "{";
  json += "\"apMode\":" + String(_apMode ? "true" : "false") + ",";
  json += "\"ssid\":\"" + htmlEscape(_apMode ? WiFi.softAPSSID() : String(_config->cfg.wifiSsid)) + "\",";
  json += "\"ip\":\"" + (_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
  json += "}";
  _server.send(200, "application/json", json);
}
