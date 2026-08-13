#include "wifi_manager.h"
#include "wifi_config.h"
#include "web_portal.h"
#include "display.h"
#include <WiFi.h>
#include <ESPmDNS.h>

namespace {

enum class WifiState { INIT, AP_MODE, CONNECTING, CONNECTED };

WifiConfigStore configStore;
WebPortal portal;
WifiState state = WifiState::INIT;
String deviceName; // "tty2oled-XXXX"
unsigned long stateEnteredMs = 0;
bool portalStarted = false;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000; // same as chromecast-esp32's reference

String buildDeviceName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "tty2oled-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

void enterApMode() {
  // AP_STA, not plain AP: WiFi.scanNetworks() (the setup form's "Scan for
  // networks" button, see web_portal.cpp's handleScan()) needs the STA
  // interface active to scan at all - a pure AP-mode scan just fails.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(deviceName.c_str()); // open network, confirmed with user
  delay(100);
  IPAddress apIp = WiFi.softAPIP();

  portal.begin(&configStore, true);
  portal.startCaptiveDns(apIp);
  portalStarted = true;

  display_show_ap_mode(deviceName, apIp.toString());

  state = WifiState::AP_MODE;
  stateEnteredMs = millis();
}

void enterConnecting() {
  display_show_connecting_wifi(configStore.cfg.wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(configStore.cfg.wifiSsid, configStore.cfg.wifiPass);
  state = WifiState::CONNECTING;
  stateEnteredMs = millis();
}

void enterConnected() {
  MDNS.begin(deviceName.c_str());
  MDNS.addService("http", "tcp", 80);

  portal.begin(&configStore, false);
  portalStarted = true;

  // Real-hardware finding (2026-08-13): the connect itself succeeded well
  // under the 20s timeout on the first end-to-end test, but the display
  // never got a corresponding redraw here - it kept showing the stale
  // "Connecting to <ssid>" text indefinitely even though WiFi/mDNS/the
  // portal were all already working. Confirmed via the mDNS/HTTP checks
  // (not the screen) before this call was added.
  display_show_wifi_connected(configStore.cfg.wifiSsid, WiFi.localIP().toString(), deviceName + ".local");

  state = WifiState::CONNECTED;
  stateEnteredMs = millis();
}

} // namespace

void wifi_manager_init() {
  configStore.begin();
  deviceName = buildDeviceName();

  if (!configStore.cfg.configured || configStore.cfg.wifiSsid[0] == '\0') {
    enterApMode();
  } else {
    enterConnecting();
  }
}

void wifi_manager_loop() {
  switch (state) {
    case WifiState::INIT:
      // wifi_manager_init() always leaves this immediately; kept only so
      // the enum has a defined starting value.
      break;

    case WifiState::AP_MODE:
      if (portalStarted) portal.loop();
      break;

    case WifiState::CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        enterConnected();
      } else if (millis() - stateEnteredMs > WIFI_CONNECT_TIMEOUT_MS) {
        enterApMode();
      }
      break;

    case WifiState::CONNECTED:
      if (portalStarted) portal.loop();
      break;
  }
}

bool wifi_manager_is_ap_mode() {
  return state == WifiState::AP_MODE;
}

bool wifi_manager_is_connected() {
  return state == WifiState::CONNECTED;
}

const String &wifi_manager_device_name() {
  return deviceName;
}

String wifi_manager_status_line() {
  switch (state) {
    case WifiState::AP_MODE:
      return "AP: " + deviceName;
    case WifiState::CONNECTING:
      return "WiFi...";
    case WifiState::CONNECTED:
      return WiFi.localIP().toString();
    default:
      return "";
  }
}
