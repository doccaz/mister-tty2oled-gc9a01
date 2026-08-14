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

// Clears WiFi + MQTT NVS config and restarts - never returns. Called from
// protocol.cpp's protocol_button_check() after a 10s continuous hold of
// the wake button (GPIO9) during normal operation (not at power-on: this
// pin is a strapping pin sampled by the ROM bootloader at reset, so
// holding it low *across* an actual reset/power-cycle enters UART
// download mode instead of running the app at all - a hold has to start
// after boot has already completed to be observable by app code).
void wifi_manager_factory_reset();

// Redraws whatever screen this module considers "current" - used to
// undo a factory-reset countdown that was cancelled (released) before
// completing. See wifi_manager.cpp's wifi_manager_redraw_screen() for
// which states this actually covers.
void wifi_manager_redraw_screen();

// Synchronous WiFi.begin() probe with the given credentials - backs the
// AP-mode setup form's "Test connection" button so a typo'd password can
// be caught before committing via /save (which saves + restarts on the
// assumption the credentials work). AP mode already runs WIFI_AP_STA
// (see enterApMode()'s comment - needed for /scan too), so a STA connect
// attempt here doesn't drop the AP/captive portal. Blocks up to ~10s;
// disconnects STA again afterward either way, since this is only a
// probe - the real, persistent connect happens post-/save-and-restart.
// On failure, errorOut gets a short reason.
bool wifi_manager_test_connection(const String &ssid, const String &pass, String &errorOut);

// "tty2oled-XXXX", built from the last 2 MAC bytes - used as both the AP
// SSID and (STA mode) the mDNS hostname, so a device is identifiable the
// same way in either mode.
const String &wifi_manager_device_name();

// Short human-readable line for oled_status.cpp's dashboard: current IP
// (STA/CONNECTED) or "AP: <name>" (AP_MODE) or "WiFi..." (CONNECTING).
String wifi_manager_status_line();
