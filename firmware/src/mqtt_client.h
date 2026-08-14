#pragma once

#include <Arduino.h>

// MQTT notifications: text banners and image-by-URL, both one-shot and
// auto-reverting after a configurable duration. See mqtt_config.h for
// the broker/topic config and CLAUDE.md's "MQTT notifications" section
// for the full design (why image payloads are URLs, not raw bytes; the
// widened picture-buffer guard shared with protocol.cpp/ws_protocol.cpp).

void mqtt_client_init(); // called once from wifi_manager's enterConnected() - STA-only
void mqtt_client_loop();  // reconnect/poll + the notification-revert timer; no-op before init() or if disabled

// Synchronous one-shot connect attempt against the given broker, using a
// throwaway WiFiClient/PubSubClient - entirely separate from the
// persistent client above, so it can't disturb an already-connected
// session. Backs web_portal.cpp's "Test connection" button, so users can
// verify broker reachability/credentials before saving+restarting.
// Blocks for up to a few seconds; on failure, errorOut gets a short
// human-readable reason (PubSubClient::state() translated).
bool mqtt_client_test(const String &host, uint16_t port, const String &username, const String &password, String &errorOut);
