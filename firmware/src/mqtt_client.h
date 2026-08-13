#pragma once

// MQTT notifications: text banners and image-by-URL, both one-shot and
// auto-reverting after a configurable duration. See mqtt_config.h for
// the broker/topic config and CLAUDE.md's "MQTT notifications" section
// for the full design (why image payloads are URLs, not raw bytes; the
// widened picture-buffer guard shared with protocol.cpp/ws_protocol.cpp).

void mqtt_client_init(); // called once from wifi_manager's enterConnected() - STA-only
void mqtt_client_loop();  // reconnect/poll + the notification-revert timer; no-op before init() or if disabled
