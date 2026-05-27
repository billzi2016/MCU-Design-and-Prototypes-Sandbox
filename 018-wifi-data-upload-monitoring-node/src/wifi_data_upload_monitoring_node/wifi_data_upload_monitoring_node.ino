#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>

// WiFi 数据上传监测节点：
// 采集 DHT22 温湿度数据，连接 WiFi 后定时向 HTTP 服务上传 JSON 数据，
// 并用 OLED 显示连接状态、上传结果和当前测量值。

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
const char UPLOAD_URL[] = "http://example.com/api/sensor";

const int PIN_DHT = 4;
const int DHT_TYPE = DHT22;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long UPLOAD_INTERVAL_MS = 10000;
const unsigned long DISPLAY_REFRESH_MS = 250;
const unsigned long WIFI_RETRY_MS = 6000;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float temperatureC = 0.0f;
float humidity = 0.0f;
bool sensorReady = false;
bool uploadOk = false;
int lastHttpCode = 0;
unsigned long lastSensorRead = 0;
unsigned long lastUploadTime = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastWifiRetry = 0;

void setup() {
  Wire.begin();
  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Upload Node");
  display.println("Connecting...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  ensureWifiConnected();
  updateSensorData();
  uploadDataIfNeeded();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    drawScreen();
    lastDisplayRefresh = millis();
  }
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWifiRetry >= WIFI_RETRY_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiRetry = millis();
  }
}

void updateSensorData() {
  if (millis() - lastSensorRead < SENSOR_READ_MS) {
    return;
  }

  lastSensorRead = millis();
  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperatureC = newTemperature;
    sensorReady = true;
  } else {
    sensorReady = false;
  }
}

void uploadDataIfNeeded() {
  if (!sensorReady || WiFi.status() != WL_CONNECTED || millis() - lastUploadTime < UPLOAD_INTERVAL_MS) {
    return;
  }

  lastUploadTime = millis();

  HTTPClient http;
  http.begin(UPLOAD_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"temperature\":";
  payload += String(temperatureC, 2);
  payload += ",\"humidity\":";
  payload += String(humidity, 2);
  payload += ",\"device\":\"esp32-node\"}";

  lastHttpCode = http.POST(payload);
  uploadOk = lastHttpCode > 0 && lastHttpCode < 300;
  http.end();
}

void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Monitoring");

  display.print("WiFi: ");
  display.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  if (sensorReady) {
    display.print("T:");
    display.print(temperatureC, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.println("%");
  } else {
    display.println("Sensor read error");
  }

  display.print("HTTP: ");
  display.println(lastHttpCode);
  display.print("Upload:");
  display.println(uploadOk ? "SUCCESS" : "WAIT/FAIL");
}
