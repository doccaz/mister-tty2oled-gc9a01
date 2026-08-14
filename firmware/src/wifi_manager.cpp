#include "wifi_manager.h"
#include "wifi_config.h"
#include "mqtt_config.h"
#include "web_portal.h"
#include "ws_protocol.h"
#include "mqtt_client.h"
#include "display.h"
#include <WiFi.h>
#include <ESPmDNS.h>

namespace {

enum class WifiState { INIT, AP_MODE, CONNECTING, CONNECTED };

WifiConfigStore configStore;
// Separate from mqtt_client.cpp's own MqttConfigStore instance - this one
// only backs the portal's settings form. Both read the same NVS
// namespace; handleMqttSave() restarts the device to apply changes
// (see its comment), so the two never need to stay in sync live.
MqttConfigStore mqttConfigStore;
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
  // Advertised separately from "http" so the web app's WiFi transport
  // (or avahi-browse/dns-sd) can discover the command-protocol WebSocket
  // port specifically, not just infer it from the fixed port 81 constant
  // in ws_protocol.cpp/wifiLink.ts. "ws"/"tcp" isn't an IANA-registered
  // service name, but is the common convention other WebSocket-based IoT
  // devices already use for exactly this purpose.
  MDNS.addService("ws", "tcp", 81);

  portal.begin(&configStore, false);
  mqttConfigStore.begin();
  portal.setMqttConfig(&mqttConfigStore);
  portalStarted = true;

  // Real-hardware finding (2026-08-13): the connect itself succeeded well
  // under the 20s timeout on the first end-to-end test, but the display
  // never got a corresponding redraw here - it kept showing the stale
  // "Connecting to <ssid>" text indefinitely even though WiFi/mDNS/the
  // portal were all already working. Confirmed via the mDNS/HTTP checks
  // (not the screen) before this call was added.
  display_show_wifi_connected(configStore.cfg.wifiSsid, WiFi.localIP().toString(), deviceName + ".local");

  // WS command protocol is STA-mode only (see CLAUDE.md "Wire protocol
  // over WiFi") - AP mode's only job is WiFi bootstrapping.
  ws_protocol_init();

  // Same STA-only scope cut - MQTT notifications only matter once on the
  // real network. No-ops internally if not configured/enabled.
  mqtt_client_init();

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

void wifi_manager_factory_reset() {
  configStore.reset();
  // A local, separate Preferences instance rather than reusing the
  // file-scope mqttConfigStore - that one may already be begin()'d (see
  // enterConnected()), and NVS handles are cheap/independent, so this
  // avoids reasoning about double-begin() on the shared instance.
  MqttConfigStore mqttConfig;
  mqttConfig.begin();
  mqttConfig.reset();
  delay(500); // let display_show_factory_reset_done() actually show before restarting
  ESP.restart();
}

// Redraws whatever this module considers "the current screen" - used by
// protocol.cpp's long-press-cancel path to undo a factory-reset countdown
// that didn't reach the full hold. Only AP_MODE has a screen protocol.cpp
// doesn't already know how to restore itself (protocol_redisplay_current()
// covers CONNECTED's marquee/corename content); CONNECTING's screen is
// static text with nothing to preserve either way.
void wifi_manager_redraw_screen() {
  if (state == WifiState::AP_MODE) {
    display_show_ap_mode(deviceName, WiFi.softAPIP().toString());
  }
}

bool wifi_manager_test_connection(const String &ssid, const String &pass, String &errorOut) {
  if (ssid.length() == 0) {
    errorOut = "No SSID given";
    return false;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  wl_status_t status;
  while ((status = WiFi.status()) != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }
  bool ok = (status == WL_CONNECTED);
  if (!ok) {
    switch (status) {
      case WL_NO_SSID_AVAIL: errorOut = "Network not found"; break;
      case WL_CONNECT_FAILED: errorOut = "Wrong password or auth rejected"; break;
      default: errorOut = "Timed out connecting"; break;
    }
  }
  WiFi.disconnect(false, false); // drop the test association; AP/captive portal stays up either way
  return ok;
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
