#pragma once

#include <Arduino.h>

// WiFi bring-up state machine - AP mode (own hotspot + setup portal) when
// no WiFi is configured, otherwise connect as a station and fall back to
// AP mode on failure. Modeled directly on chromecast-esp32/src/main.cpp's
// AppState handling (INIT/AP_MODE/WIFI_CONNECTING/WIFI_CONNECTED), see
// CLAUDE.md's "WiFi/AP feature" section for the full design rationale.

void wifi_manager_init();
void wifi_manager_loop();

bool wifi_manager_is_ap_mode();
bool wifi_manager_is_connected();

// "tty2oled-XXXX", built from the last 2 MAC bytes - used as both the AP
// SSID and (STA mode) the mDNS hostname, so a device is identifiable the
// same way in either mode.
const String &wifi_manager_device_name();

// Short human-readable line for oled_status.cpp's dashboard: current IP
// (STA/CONNECTED) or "AP: <name>" (AP_MODE) or "WiFi..." (CONNECTING).
String wifi_manager_status_line();
