#include "mqtt_client.h"
#include "mqtt_config.h"
#include "protocol.h"
#include "display.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <PubSubClient.h>

namespace {

MqttConfigStore configStore;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
bool initialized = false;

String topicText;
String topicImage;

bool notificationActive = false;
unsigned long notificationShownAt = 0;

constexpr unsigned long RECONNECT_INTERVAL_MS = 5000;
unsigned long lastReconnectAttempt = 0;

// Notifications should wake a blanked display and count as real activity,
// same as any dispatched command - otherwise a notification could arrive
// invisibly behind a blanked screensaver, or get immediately re-blanked
// on the next protocol_saver_check() tick.
void wakeForNotification() {
  display_on();
  protocol_note_activity();
}

void handleTextMessage(const uint8_t *payload, unsigned int length) {
  String text;
  text.reserve(length);
  for (unsigned int i = 0; i < length; i++) text += (char)payload[i];

  wakeForNotification();
  display_show_notification_text(text);
  notificationActive = true;
  notificationShownAt = millis();
}

// Minimal hand-rolled HTTP/1.1 GET over plain WiFiClient - deliberately
// not HTTPClient: that pulls in WiFiClientSecure unconditionally (even
// for a plain http:// URL, since HTTPClient.cpp references its methods
// itself, not gated by the runtime URL scheme), and this framework's
// mbedtls config has PSK cipher suites disabled - ssl_client.cpp's real
// implementation is compiled out behind an #if guard that isn't
// satisfied, compiling to a stub with none of the symbols
// WiFiClientSecure.cpp calls, which fails at link time. We only ever
// need plain http:// for local-network image URLs (Home Assistant, a
// self-hosted server), so a small hand-rolled GET - in the same spirit
// as this project's other hand-rolled wire parsing (protocol.cpp's
// splitField(), cast_message.h's protobuf encoder in the sibling
// chromecast-esp32 project) - sidesteps the whole problem. No https://
// support; see CLAUDE.md's MQTT notifications section.
bool httpGet(const String &url, String &outHost, long &outContentLength, WiFiClient &client) {
  if (!url.startsWith("http://")) return false; // no https:// - see comment above
  String rest = url.substring(7);
  int slashIdx = rest.indexOf('/');
  String hostPort = slashIdx == -1 ? rest : rest.substring(0, slashIdx);
  String path = slashIdx == -1 ? "/" : rest.substring(slashIdx);
  int colonIdx = hostPort.indexOf(':');
  String host = colonIdx == -1 ? hostPort : hostPort.substring(0, colonIdx);
  int port = colonIdx == -1 ? 80 : hostPort.substring(colonIdx + 1).toInt();
  outHost = host;

  if (!client.connect(host.c_str(), port)) return false;
  client.print("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");

  unsigned long start = millis();
  while (client.connected() && !client.available()) {
    if (millis() - start > 5000) { client.stop(); return false; }
    delay(2);
  }

  long contentLength = -1;
  bool statusOk = false;
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break; // blank line ends the headers
    if (line.startsWith("HTTP/")) {
      statusOk = line.indexOf(" 200 ") != -1;
    } else if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(16).toInt();
    }
  }
  if (!statusOk || contentLength <= 0) {
    client.stop();
    return false;
  }
  outContentLength = contentLength;
  return true;
}

void handleImageMessage(const uint8_t *payload, unsigned int length) {
  String url;
  url.reserve(length);
  for (unsigned int i = 0; i < length; i++) url += (char)payload[i];

  WiFiClient client;
  String host;
  long total = 0;
  if (!httpGet(url, host, total, client)) {
    wakeForNotification();
    display_show_notification_text("MQTT image fetch failed");
    notificationActive = true;
    notificationShownAt = millis();
    return;
  }

  String name = url.substring(url.lastIndexOf('/') + 1, url.lastIndexOf('.'));
  if (!protocol_ws_xfer_begin(name, 3 /* wipe top->bottom */, 500, (size_t)total)) {
    client.stop();
    wakeForNotification();
    display_show_notification_text("MQTT image: bad size");
    notificationActive = true;
    notificationShownAt = millis();
    return;
  }

  uint8_t chunk[512];
  bool ok = true;
  long received = 0;
  unsigned long start = millis();
  while (received < total && millis() - start < 8000) {
    size_t avail = client.available();
    if (avail == 0) {
      if (!client.connected()) break;
      delay(2);
      continue;
    }
    size_t toRead = min(avail, sizeof(chunk));
    int n = client.readBytes(chunk, toRead);
    if (n <= 0) break;
    if (!protocol_ws_xfer_append(chunk, (size_t)n)) {
      ok = false;
      break;
    }
    received += n;
  }
  client.stop();

  if (ok && protocol_ws_xfer_complete()) {
    wakeForNotification();
    protocol_ws_xfer_finish_transient();
    notificationActive = true;
    notificationShownAt = millis();
  } else {
    protocol_ws_xfer_abort();
    wakeForNotification();
    display_show_notification_text("MQTT image: incomplete fetch");
    notificationActive = true;
    notificationShownAt = millis();
  }
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  String t(topic);
  if (t == topicText) {
    handleTextMessage(payload, length);
  } else if (t == topicImage) {
    handleImageMessage(payload, length);
  }
}

void reconnect() {
  // wifi_manager_device_name() already returns "tty2oled-XXXX" - no
  // extra prefix needed (found doubled to "tty2oled-tty2oled-XXXX" in
  // testing, see wifi_manager.cpp's buildDeviceName()).
  String clientId = wifi_manager_device_name();
  bool ok;
  if (configStore.cfg.username[0] != '\0') {
    ok = mqtt.connect(clientId.c_str(), configStore.cfg.username, configStore.cfg.password);
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  if (ok) {
    mqtt.subscribe(topicText.c_str());
    mqtt.subscribe(topicImage.c_str());
  }
}

// PubSubClient::state() codes -> short human-readable reason, for the
// test-connection button's result message.
const char *mqttStateStr(int state) {
  switch (state) {
    case -4: return "Timed out";
    case -3: return "Connection lost";
    case -2: return "Connect failed (host/port unreachable?)";
    case -1: return "Disconnected";
    case 1: return "Bad protocol version";
    case 2: return "Client ID rejected";
    case 3: return "Broker unavailable";
    case 4: return "Bad username/password";
    case 5: return "Not authorized";
    default: return "Unknown error";
  }
}

} // namespace

bool mqtt_client_test(const String &host, uint16_t port, const String &username, const String &password, String &errorOut) {
  if (host.length() == 0) {
    errorOut = "No host given";
    return false;
  }

  WiFiClient testWifiClient;
  PubSubClient testMqtt(testWifiClient);
  testMqtt.setServer(host.c_str(), port);
  testMqtt.setSocketTimeout(3); // seconds - keep the web request responsive

  String clientId = wifi_manager_device_name() + "-test";
  bool ok = username.length() != 0
                ? testMqtt.connect(clientId.c_str(), username.c_str(), password.c_str())
                : testMqtt.connect(clientId.c_str());

  if (ok) {
    testMqtt.disconnect();
    return true;
  }
  errorOut = mqttStateStr(testMqtt.state());
  return false;
}

void mqtt_client_init() {
  configStore.begin();
  if (!configStore.cfg.enabled || configStore.cfg.host[0] == '\0') return;

  String prefix = configStore.cfg.topicPrefix[0] != '\0'
                      ? String(configStore.cfg.topicPrefix)
                      : "tty2oled/" + wifi_manager_device_name();
  topicText = prefix + "/text";
  topicImage = prefix + "/image";

  mqtt.setServer(configStore.cfg.host, configStore.cfg.port);
  mqtt.setCallback(onMqttMessage);
  initialized = true;
}

void mqtt_client_loop() {
  if (!initialized) return;

  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
      lastReconnectAttempt = now;
      reconnect();
    }
  } else {
    mqtt.loop();
  }

  if (notificationActive && millis() - notificationShownAt > configStore.cfg.durationMs) {
    notificationActive = false;
    protocol_redisplay_current(0);
  }
}
