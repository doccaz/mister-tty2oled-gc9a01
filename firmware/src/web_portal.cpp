#include "web_portal.h"
#include "version.h"
#include <WiFi.h>

namespace {

// Minimal inline CSS/markup - same "embedded PROGMEM, no filesystem"
// approach as chromecast-esp32/src/web_server.h's _handleRoot(). Kept
// deliberately small: this page's only job is WiFi setup, not the full
// marquee editor (that's web/, over WebSerial).
const char PAGE_HEAD[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>tty2oled setup</title>"
    "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
    "h1{font-size:1.2em}input{width:100%;box-sizing:border-box;padding:.5em;margin:.3em 0}"
    "button{padding:.6em 1em;margin-top:.5em}"
    ".muted{color:#666;font-size:.9em}</style></head><body>";

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
            "<button type='submit'>Forget WiFi</button>"
            "</form>";
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
