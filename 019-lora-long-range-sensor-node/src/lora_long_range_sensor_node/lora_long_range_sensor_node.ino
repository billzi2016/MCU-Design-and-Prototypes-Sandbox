#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>

// LoRa 远距离传感器节点：
// 采集环境温湿度数据，通过 LoRa 周期性广播发送，并在本地 OLED 显示节点状态。
// 该实现偏向发射端节点，适合作为农业或仓储环境监测的远距离采集端。

const int PIN_DHT = 4;
const int DHT_TYPE = DHT22;

const int PIN_LORA_SS = 5;
const int PIN_LORA_RST = 14;
const int PIN_LORA_DIO0 = 2;

const long LORA_FREQUENCY = 433E6;
const char NODE_ID[] = "NODE-01";

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long SEND_INTERVAL_MS = 10000;
const unsigned long DISPLAY_REFRESH_MS = 250;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 发射节点更关心“传感器是否有效”“无线是否就绪”“已经发了多少包”。
float temperatureC = 0.0f;
float humidity = 0.0f;
bool sensorReady = false;
bool loraReady = false;
unsigned long packetCounter = 0;
unsigned long lastSensorRead = 0;
unsigned long lastSendTime = 0;
unsigned long lastDisplayRefresh = 0;

void setup() {
  Wire.begin();
  SPI.begin();
  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("LoRa Sensor Node");
  display.println("Init radio...");
  display.display();

  LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);
  loraReady = LoRa.begin(LORA_FREQUENCY);
}

void loop() {
  // 传感器采样和 LoRa 发包节奏分离，方便后续做低功耗调度。
  updateSensorData();
  sendPayloadIfNeeded();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    drawScreen();
    lastDisplayRefresh = millis();
  }
}

void updateSensorData() {
  if (millis() - lastSensorRead < SENSOR_READ_MS) {
    return;
  }

  lastSensorRead = millis();
  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  // 传感器无效时停止发包，避免无线链路上传播垃圾数据。
  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperatureC = newTemperature;
    sensorReady = true;
  } else {
    sensorReady = false;
  }
}

void sendPayloadIfNeeded() {
  // LoRa 节点按固定周期广播，适合作为远距离采集端。
  if (!loraReady || !sensorReady || millis() - lastSendTime < SEND_INTERVAL_MS) {
    return;
  }

  lastSendTime = millis();

  // 这里用简单 JSON 字符串，便于网关侧直接解析。
  String payload = "{";
  payload += "\"node\":\"";
  payload += NODE_ID;
  payload += "\",\"temperature\":";
  payload += String(temperatureC, 2);
  payload += ",\"humidity\":";
  payload += String(humidity, 2);
  payload += ",\"packet\":";
  payload += String(packetCounter + 1);
  payload += "}";

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  packetCounter++;
}

void drawScreen() {
  // 本地 OLED 主要用于查看无线状态、测量值和累计发包数。
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("LoRa Long Range");
  display.print("Node: ");
  display.println(NODE_ID);
  display.print("Radio: ");
  display.println(loraReady ? "READY" : "FAULT");

  if (sensorReady) {
    display.print("T:");
    display.print(temperatureC, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.println("%");
  } else {
    display.println("Sensor read error");
  }

  display.print("Packets: ");
  display.println(packetCounter);
}
