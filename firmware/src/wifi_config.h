#pragma once

// NVS-backed WiFi credential storage. Modeled directly on the sibling
// chromecast-esp32 ("KnobCast") project's config.h ConfigStore, trimmed to
// just what this project's WiFi bootstrap needs - no device-control state,
// that lives entirely in protocol.cpp/display.cpp already.

#include <Arduino.h>
#include <Preferences.h>

struct WifiConfig {
  char wifiSsid[64];
  char wifiPass[64];
  bool configured; // true once a WiFi setup form has been submitted

  void setDefaults() {
    wifiSsid[0] = '\0';
    wifiPass[0] = '\0';
    configured = false;
  }
};

class WifiConfigStore {
 public:
  WifiConfig cfg;

  void begin() {
    cfg.setDefaults();
    _prefs.begin("tty2oled", false);
    load();
  }

  void load() {
    cfg.configured = _prefs.getBool("configured", false);
    _prefs.getString("wifiSsid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
    _prefs.getString("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));
  }

  void save() {
    _prefs.putBool("configured", cfg.configured);
    _prefs.putString("wifiSsid", cfg.wifiSsid);
    _prefs.putString("wifiPass", cfg.wifiPass);
  }

  void reset() {
    cfg.setDefaults();
    _prefs.clear();
  }

 private:
  Preferences _prefs;
};
