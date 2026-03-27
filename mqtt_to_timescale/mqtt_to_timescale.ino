#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

const char* mqtt_server = "192.168.0.220"; // MQTT broker IP

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  client.setServer(mqtt_server, 1883);
}

void reconnect() {
  while (!client.connected()) {
    client.connect("ESP32_client");
  }
}

void loop() {

  if (!client.connected()) reconnect();

  client.loop();

  float temp = random(200,300)/10.0;

  String payload =
    "{\"device\":\"ESP32_01\",\"temp\":" + String(temp) + "}";

  client.publish("factory/sensor", payload.c_str());

  delay(1000);
}
