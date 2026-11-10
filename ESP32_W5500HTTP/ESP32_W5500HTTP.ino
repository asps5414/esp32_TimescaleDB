#include <SPI.h>
#include <Ethernet2.h>
#include <ModbusMaster.h>

// ===== W5500 =====
#define W5500_CS 6
#define W5500_RST 14

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress server(192,168,0,220); // 你的電腦IP

EthernetClient client;

// ===== RS485 =====
#define RS_TX_PIN 5
#define RS_RX_PIN 4
#define RS_ENAMBLE_232_PIN 21
#define RS_ENAMBLE_422_PIN 16
#define RS_ENAMBLE_485_PIN 15

#define RS_BAUDRATE 9600

ModbusMaster node;

// ===== Modbus callback =====
void preTransmission() {
  while (Serial2.available()) Serial2.read();
}

void postTransmission() {
  Serial2.flush();
  delayMicroseconds(100);
  while (Serial2.available()) Serial2.read();
}

// ===== setup =====
void setup() {
  Serial.begin(115200);

  // ===== W5500 reset =====
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(20);
  digitalWrite(W5500_RST, HIGH);
  delay(100);

  // ⚠️ 用你成功的 SPI 腳位
  SPI.begin(12,13,11,W5500_CS);
  Ethernet.init(W5500_CS);

  IPAddress ip(192,168,0,50);
  IPAddress gateway(192,168,0,1);
  IPAddress subnet(255,255,255,0);

  Ethernet.begin(mac, ip, gateway, gateway, subnet);

  Serial.print("IP: ");
  Serial.println(Ethernet.localIP());

  // ===== RS485 =====
  pinMode(RS_ENAMBLE_232_PIN, OUTPUT);
  pinMode(RS_ENAMBLE_422_PIN, OUTPUT);
  pinMode(RS_ENAMBLE_485_PIN, OUTPUT);

  digitalWrite(RS_ENAMBLE_232_PIN, LOW);
  digitalWrite(RS_ENAMBLE_422_PIN, LOW);
  digitalWrite(RS_ENAMBLE_485_PIN, HIGH);

  Serial2.begin(RS_BAUDRATE, SERIAL_8N1, RS_RX_PIN, RS_TX_PIN);

  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("System Ready");
}

// ===== loop =====
void loop() {

  uint8_t result;
  uint16_t raw;

  // ✅ 用你成功的地址
  result = node.readHoldingRegisters(43, 1);

  if (result == node.ku8MBSuccess) {

    raw = node.getResponseBuffer(0);

    float temperature = raw / 10.0;

    Serial.print("Temp: ");
    Serial.println(temperature);

    // ===== HTTP 傳送 =====
    if (client.connect(server, 5000)) {

      String json =
        "{\"device\":\"CH4_SENSOR\",\"temp\":" + String(temperature) + "}";

      client.println("POST /sensor HTTP/1.1");
      client.println("Host: 192.168.0.220");
      client.println("Content-Type: application/json");
      client.print("Content-Length: ");
      client.println(json.length());
      client.println();
      client.println(json);

      Serial.println("Data sent!");

      client.stop();
    } else {
      Serial.println("HTTP failed");
    }

  } else {
    Serial.print("Modbus error: ");
    Serial.println(result);
  }

  delay(2000);
}
