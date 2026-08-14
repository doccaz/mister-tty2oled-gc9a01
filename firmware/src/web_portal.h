#pragma once

// Small WiFi-bootstrap web UI - independent of the WebSerial-based web/
// companion app, which handles marquee art. This one's only job is
// getting the device onto a WiFi network (AP-mode captive portal setup
// form) and showing basic status once connected. Modeled directly on
// chromecast-esp32/src/web_server.h's CastWebServer (same route shapes,
// same embedded-PROGMEM-HTML approach, no LittleFS/SPIFFS), trimmed to
// WiFi setup only - no device-control routes, those don't apply here.

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "wifi_config.h"
#include "mqtt_config.h"

class WebPortal {
 public:
  // apMode selects which _handleRoot() renders: the setup form (AP mode,
  // no WiFi configured yet) or the status page (STA mode, connected).
  void begin(WifiConfigStore *config, bool apMode);

  // STA mode only: adds the MQTT broker settings form to the status page
  // and the /mqtt-save route. Called once from wifi_manager's
  // enterConnected(), after begin(..., false) - not passed to begin()
  // itself since AP mode's setup form has no use for it.
  void setMqttConfig(MqttConfigStore *mqttConfig);

  // AP mode only: starts the captive-portal DNS wildcard redirect.
  void startCaptiveDns(IPAddress apIp);

  void loop();

 private:
  void handleRoot();
  void handleSave();
  void handleReset();
  void handleStatus();
  void handleScan(); // AP mode only: WiFi.scanNetworks(), returns JSON for the setup form's dropdown
  void handleWifiTest(); // AP mode only: "Test connection" button - probes the form's current SSID/pass, doesn't save
  void handleMqttSave();
  void handleMqttTest(); // "Test connection" button - tries the form's current values, doesn't save

  WebServer _server{80};
  DNSServer _dns;
  WifiConfigStore *_config = nullptr;
  MqttConfigStore *_mqttConfig = nullptr;
  bool _apMode = false;
  bool _dnsActive = false;
};
