#include <SPI.h>
#include <Ethernet2.h>
#include <ModbusMaster.h>
#include <mbedtls/md5.h>

// ===== W5500 =====
#define W5500_CS    6
#define W5500_RST   14
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress dbServer(192, 168, 0, 112);  // 你電腦的 IP
EthernetClient client;

// ===== RS485 =====
#define RS_TX_PIN          5
#define RS_RX_PIN          4
#define RS_ENAMBLE_232_PIN 21
#define RS_ENAMBLE_422_PIN 16
#define RS_ENAMBLE_485_PIN 15
#define RS_BAUDRATE        9600
ModbusMaster node;

// ===== DB 設定 =====
const char* DB_NAME = "iot_db";
const char* DB_USER = "postgres";
const char* DB_PASS = "123456";
// ─────────────────────────────────────────────
// PostgreSQL wire protocol helpers
// ─────────────────────────────────────────────

// 讀掉一個完整的 server message，回傳 type byte
char pgReadMessage() {
  unsigned long t = millis();
  while (!client.available()) {
    if (millis() - t > 5000) { Serial.println("PG timeout"); return 0; }
    delay(10);
  }
  char msgType = (char)client.read();

  // 讀 4 bytes 長度
  uint8_t lb[4] = {0};
  int got = 0;
  t = millis();
  while (got < 4) {
    if (client.available()) lb[got++] = client.read();
    if (millis() - t > 2000) break;
  }
  int bodyLen = (((int)lb[0]<<24)|((int)lb[1]<<16)|((int)lb[2]<<8)|(int)lb[3]) - 4;

  // 如果是錯誤訊息，印出來
  if (msgType == 'E' && bodyLen > 0 && bodyLen < 400) {
    uint8_t body[400];
    got = 0; t = millis();
    while (got < bodyLen) {
      if (client.available()) body[got++] = client.read();
      if (millis() - t > 2000) break;
    }
    // PG 錯誤格式：一堆 "field_type + string\0" 組合
    // 印出所有可讀字元
    Serial.print("PG Error: ");
    for (int i = 0; i < got; i++) {
      if (body[i] >= 32 && body[i] < 127) Serial.print((char)body[i]);
      else if (body[i] == 0) Serial.print(" | ");
    }
    Serial.println();
    return msgType;
  }

  // 其他訊息：讀掉 body
  got = 0; t = millis();
  while (got < bodyLen) {
    if (client.available()) { client.read(); got++; }
    if (millis() - t > 2000) break;
  }
  return msgType;
}

// 送出有 type byte 的 message（一般訊息用）
void pgSendMessage(char type, const uint8_t* body, int bodyLen) {
  int totalLen = bodyLen + 4;
  uint8_t hdr[5];
  hdr[0] = (uint8_t)type;
  hdr[1] = (totalLen >> 24) & 0xFF;
  hdr[2] = (totalLen >> 16) & 0xFF;
  hdr[3] = (totalLen >>  8) & 0xFF;
  hdr[4] = (totalLen      ) & 0xFF;
  client.write(hdr, 5);
  if (bodyLen > 0) client.write(body, bodyLen);
}

// 等到收到 ReadyForQuery ('Z')
void pgWaitReady() {
  char r = 0;
  for (int i = 0; i < 15 && r != 'Z'; i++) r = pgReadMessage();
}

// ─────────────────────────────────────────────
// 連線到 PostgreSQL（Startup → Auth → Ready）
// ─────────────────────────────────────────────
bool pgConnect() {
  Serial.print("TCP -> DB...");
    //if (!client.connect(dbServer, 5433)) {

  if (!client.connect(dbServer, 5432)) {
    Serial.println("FAIL");
    return false;
  }
  Serial.println("OK");

  // Startup message
  uint8_t buf[128];
  int pos = 0;
  buf[pos++]=0x00; buf[pos++]=0x03; buf[pos++]=0x00; buf[pos++]=0x00;
  memcpy(buf+pos,"user",4); pos+=4; buf[pos++]=0;
  int ul=strlen(DB_USER); memcpy(buf+pos,DB_USER,ul); pos+=ul; buf[pos++]=0;
  memcpy(buf+pos,"database",8); pos+=8; buf[pos++]=0;
  int dl=strlen(DB_NAME); memcpy(buf+pos,DB_NAME,dl); pos+=dl; buf[pos++]=0;
  buf[pos++]=0;

  int totalLen = pos + 4;
  uint8_t lenBytes[4] = {
    (uint8_t)(totalLen>>24),(uint8_t)(totalLen>>16),
    (uint8_t)(totalLen>>8), (uint8_t)(totalLen)
  };
  client.write(lenBytes, 4);
  client.write(buf, pos);

  // 讀 auth request，取得 salt
  unsigned long t = millis();
  while (!client.available()) { delay(10); if(millis()-t>5000) return false; }
  char msgType = client.read();

  uint8_t lb[4]; int got=0; t=millis();
  while(got<4){ if(client.available()) lb[got++]=client.read(); if(millis()-t>2000) break; }
  int bodyLen = (((int)lb[0]<<24)|((int)lb[1]<<16)|((int)lb[2]<<8)|(int)lb[3]) - 4;

  uint8_t authBody[32]; got=0; t=millis();
  while(got<bodyLen && got<32){
    if(client.available()) authBody[got++]=client.read();
    if(millis()-t>2000) break;
  }

  Serial.printf("Startup resp: '%c' authType=%d\n", msgType, (int)authBody[3]);

  // authBody[0..3] = auth type (5 = md5, 0 = ok/trust)
  int authType = authBody[3];

  if (authType == 5) {
    // MD5 認證：salt 在 authBody[4..7]
    uint8_t salt[4];
    salt[0]=authBody[4]; salt[1]=authBody[5];
    salt[2]=authBody[6]; salt[3]=authBody[7];

    uint8_t hash1[16], hash2[16];
    char hex1[33], hex2[33];

    // 第一步：md5(password + user)
    char pwuser[128];
    snprintf(pwuser, sizeof(pwuser), "%s%s", DB_PASS, DB_USER);
    mbedtls_md5((uint8_t*)pwuser, strlen(pwuser), hash1);
    for(int i=0;i<16;i++) sprintf(hex1+i*2,"%02x",hash1[i]);
    hex1[32]=0;

    // 第二步：md5(hex1 + salt)
    char hex1salt[40];
    memcpy(hex1salt, hex1, 32);
    memcpy(hex1salt+32, salt, 4);
    mbedtls_md5((uint8_t*)hex1salt, 36, hash2);
    for(int i=0;i<16;i++) sprintf(hex2+i*2,"%02x",hash2[i]);
    hex2[32]=0;

    // 送出 "md5" + hex2
    char pwmsg[40];
    snprintf(pwmsg, sizeof(pwmsg), "md5%s", hex2);
    int pLen = strlen(pwmsg)+1;
    pgSendMessage('p', (uint8_t*)pwmsg, pLen);

    pgWaitReady();

  } else if (authType == 0) {
    // trust / AuthOK
    pgWaitReady();
  } else {
    Serial.printf("Unsupported auth type: %d\n", authType);
    client.stop();
    return false;
  }

  Serial.println("DB ready!");
  return true;
}

// ─────────────────────────────────────────────
// 執行 INSERT SQL
// ─────────────────────────────────────────────
bool pgInsert(float val, int machine_id, int tag_addr) {
  char sql[256];
  snprintf(sql, sizeof(sql),
    "INSERT INTO sensor_data (time, device_id, temperature) "
    "VALUES (NOW(), 'ESP32_01', %.2f);",
    val
  );

  Serial.println(sql);  // Debug 

  int sLen = strlen(sql);
  uint8_t body[256];
  memcpy(body, sql, sLen);
  body[sLen] = 0;

  pgSendMessage('Q', body, sLen + 1);

  char resp = pgReadMessage();
  Serial.printf("Insert resp: '%c'\n", resp);

  if (resp == 'E') {
    Serial.println("SQL Error!");
    pgWaitReady();
    return false;
  }

  pgWaitReady();
  return true;
}

// ─────────────────────────────────────────────
// Modbus callbacks
// ─────────────────────────────────────────────
void preTransmission() {
  while (Serial2.available()) Serial2.read();
}
void postTransmission() {
  Serial2.flush();
  delayMicroseconds(100);
  while (Serial2.available()) Serial2.read();
}

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // W5500 reset
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);  delay(20);
  digitalWrite(W5500_RST, HIGH); delay(100);

  SPI.begin(12, 13, 11, W5500_CS);
  Ethernet.init(W5500_CS);
  IPAddress ip(192, 168, 0, 50);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  Ethernet.begin(mac, ip, gateway, gateway, subnet);
  Serial.print("IP: "); Serial.println(Ethernet.localIP());

  // RS485
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
  pgConnect();
}

void loop() {
  uint8_t result = node.readHoldingRegisters(43, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t raw = node.getResponseBuffer(0);
    float val = raw / 10.0;
    Serial.print("Temp: "); Serial.println(val);

    // DB 連線斷掉就重連
    if (!client.connected()) {
      Serial.println("Reconnecting...");
      pgConnect();
    }

    // machine_id=10, tag_addr=43 ( Modbus register)
    if (pgInsert(val, 10, 43)) {
      Serial.println("Saved to DB!");
    }
  } else {
    Serial.print("Modbus error: 0x");
    Serial.println(result, HEX);
  }
  delay(2000);
}
