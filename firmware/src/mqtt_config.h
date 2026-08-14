#pragma once

// NVS-backed MQTT broker config. Modeled directly on wifi_config.h's
// WifiConfigStore, but its own Preferences namespace - MQTT settings are
// a separate concern from WiFi credentials, matching the split this
// project already established between wifi_config.h/web_portal.h/etc.

#include <Arduino.h>
#include <Preferences.h>

struct MqttConfig {
  bool enabled;
  char host[64];
  uint16_t port;
  char username[32]; // empty = no auth
  char password[32];
  char topicPrefix[48]; // empty = built from deviceName at connect time
  uint16_t durationMs;  // how long a notification shows before reverting

  void setDefaults() {
    enabled = false;
    host[0] = '\0';
    port = 1883;
    username[0] = '\0';
    password[0] = '\0';
    topicPrefix[0] = '\0';
    durationMs = 8000;
  }
};

class MqttConfigStore {
 public:
  MqttConfig cfg;

  void begin() {
    cfg.setDefaults();
    _prefs.begin("tty2oled_mqtt", false);
    load();
  }

  void load() {
    cfg.enabled = _prefs.getBool("enabled", false);
    _prefs.getString("host", cfg.host, sizeof(cfg.host));
    cfg.port = _prefs.getUShort("port", 1883);
    _prefs.getString("username", cfg.username, sizeof(cfg.username));
    _prefs.getString("password", cfg.password, sizeof(cfg.password));
    _prefs.getString("topicPrefix", cfg.topicPrefix, sizeof(cfg.topicPrefix));
    cfg.durationMs = _prefs.getUShort("durationMs", 8000);
  }

  void save() {
    _prefs.putBool("enabled", cfg.enabled);
    _prefs.putString("host", cfg.host);
    _prefs.putUShort("port", cfg.port);
    _prefs.putString("username", cfg.username);
    _prefs.putString("password", cfg.password);
    _prefs.putString("topicPrefix", cfg.topicPrefix);
    _prefs.putUShort("durationMs", cfg.durationMs);
  }

  void reset() {
    cfg.setDefaults();
    _prefs.clear();
  }

 private:
  Preferences _prefs;
};
