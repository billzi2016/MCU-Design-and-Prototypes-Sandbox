#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

// 简易气象站：
// 采集温湿度、气压、降雨和风速数据，并在 OLED 上以分页形式显示。
// 风速使用脉冲方式统计，降雨同时保留模拟强度和是否下雨两种信息。

const int PIN_DHT = 4;
const int PIN_RAIN_ANALOG = 34;
const int PIN_RAIN_DIGITAL = 27;
const int PIN_WIND_PULSE = 26;
const int DHT_TYPE = DHT22;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long DISPLAY_PAGE_MS = 3000;
const float FILTER_ALPHA = 0.18f;
const float WIND_FACTOR_KMH = 2.40f;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

volatile unsigned long windPulseCount = 0;

// 不同传感器的结果分开保存，便于页面按需组合展示。
float temperatureC = 0.0f;
float humidity = 0.0f;
float pressureHpa = 0.0f;
float rainFiltered = 0.0f;
float windSpeedKmh = 0.0f;
bool dhtReady = false;
bool bmpReady = false;
bool raining = false;
int currentPage = 0;
unsigned long lastSensorRead = 0;
unsigned long lastPageSwitch = 0;
unsigned long lastWindPulseSnapshot = 0;
unsigned long lastWindCalcTime = 0;

void IRAM_ATTR onWindPulse() {
  windPulseCount++;
}

void setup() {
  Wire.begin();
  analogReadResolution(12);

  pinMode(PIN_RAIN_DIGITAL, INPUT);
  pinMode(PIN_WIND_PULSE, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_WIND_PULSE), onWindPulse, FALLING);

  dht.begin();
  bmpReady = bmp.begin(0x76) || bmp.begin(0x77);

  rainFiltered = static_cast<float>(analogRead(PIN_RAIN_ANALOG));
  lastWindCalcTime = millis();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Simple Weather");
  display.println("Init sensors...");
  display.display();
}

void loop() {
  // 气象站主循环保持简单：采样、翻页、显示。
  updateSensorData();
  updateDisplayPage();
  drawPage();
}

void updateSensorData() {
  if (millis() - lastSensorRead < SENSOR_READ_MS) {
    return;
  }

  lastSensorRead = millis();

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();
  // DHT22 失败时只标记状态，不直接清空旧值，便于观察最近一次有效采样。
  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperatureC = newTemperature;
    dhtReady = true;
  } else {
    dhtReady = false;
  }

  if (!bmpReady) {
    // BMP280 初始化失败后继续重试，避免必须重新上电。
    bmpReady = bmp.begin(0x76) || bmp.begin(0x77);
  }
  if (bmpReady) {
    pressureHpa = bmp.readPressure() / 100.0f;
  }

  int rainRaw = analogRead(PIN_RAIN_ANALOG);
  // 雨滴模拟量做平滑，避免水滴抖动让页面读数跳得太快。
  rainFiltered = FILTER_ALPHA * static_cast<float>(rainRaw) +
                 (1.0f - FILTER_ALPHA) * rainFiltered;
  raining = digitalRead(PIN_RAIN_DIGITAL) == LOW;

  updateWindSpeed();
}

void updateWindSpeed() {
  // 风速使用固定时间窗内的脉冲增量计算，适合基础原型验证。
  unsigned long now = millis();
  unsigned long elapsedMs = now - lastWindCalcTime;
  if (elapsedMs == 0) {
    return;
  }

  noInterrupts();
  unsigned long pulseSnapshot = windPulseCount;
  interrupts();

  unsigned long pulseDelta = pulseSnapshot - lastWindPulseSnapshot;
  float pulsesPerSecond = static_cast<float>(pulseDelta) * 1000.0f / static_cast<float>(elapsedMs);
  windSpeedKmh = pulsesPerSecond * WIND_FACTOR_KMH;

  lastWindPulseSnapshot = pulseSnapshot;
  lastWindCalcTime = now;
}

void updateDisplayPage() {
  // 通过自动轮播把温湿度、降雨和风速拆到不同页面显示。
  if (millis() - lastPageSwitch >= DISPLAY_PAGE_MS) {
    currentPage = (currentPage + 1) % 3;
    lastPageSwitch = millis();
  }
}

void drawPage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (currentPage == 0) {
    drawSummaryPage();
  } else if (currentPage == 1) {
    drawRainPage();
  } else {
    drawWindPage();
  }

  display.display();
}

void drawSummaryPage() {
  // 总览页优先展示最核心的气象信息。
  display.println("Weather Summary");
  if (dhtReady) {
    display.print("T:");
    display.print(temperatureC, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.println("%");
  } else {
    display.println("DHT22 read error");
  }

  display.print("P:");
  if (bmpReady) {
    display.print(pressureHpa, 1);
    display.println(" hPa");
  } else {
    display.println("BMP280 fault");
  }

  display.print("Rain:");
  display.println(raining ? "YES" : "NO");
  display.print("Wind:");
  display.print(windSpeedKmh, 1);
  display.println("km/h");
}

void drawRainPage() {
  // 雨滴页更适合观察传感器阈值和雨量强弱趋势。
  display.println("Rain Condition");
  display.print("Detected: ");
  display.println(raining ? "YES" : "NO");
  display.print("Analog  : ");
  display.println(static_cast<int>(rainFiltered));
  display.print("Trend   : ");
  if (static_cast<int>(rainFiltered) < 1300) {
    display.println("HEAVY");
  } else if (static_cast<int>(rainFiltered) < 2400) {
    display.println("LIGHT");
  } else {
    display.println("DRY");
  }
}

void drawWindPage() {
  // 风速页同时显示累计脉冲，方便校准风速换算因子。
  display.println("Wind Station");
  display.print("Speed: ");
  display.print(windSpeedKmh, 1);
  display.println("km/h");
  display.print("Pulse: ");
  display.println(lastWindPulseSnapshot);
  display.print("Sensors:");
  display.println((dhtReady && bmpReady) ? " OK" : " WARN");
}
