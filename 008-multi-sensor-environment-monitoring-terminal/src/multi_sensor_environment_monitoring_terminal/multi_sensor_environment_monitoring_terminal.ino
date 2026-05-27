#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

// 多传感器环境监测终端：
// 在 ESP32 上同时采集温湿度、气压、烟雾和光照，并通过 OLED 分页展示。
// 重点不是“把值读出来”，而是做基础状态评估和多页面信息组织。

const int PIN_DHT = 4;
const int PIN_MQ2 = 34;
const int PIN_LIGHT = 35;
const int DHT_TYPE = DHT22;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long DISPLAY_PAGE_MS = 3500;
const float ANALOG_FILTER_ALPHA = 0.18f;

const float COMFORT_TEMP_LOW = 18.0f;
const float COMFORT_TEMP_HIGH = 30.0f;
const float COMFORT_HUMIDITY_LOW = 35.0f;
const float COMFORT_HUMIDITY_HIGH = 75.0f;
const int MQ2_WARNING_THRESHOLD = 1800;
const int MQ2_DANGER_THRESHOLD = 2800;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BMP280 bmp;

// 两路模拟量使用平滑值显示，避免 OLED 上数字跳动过快影响可读性。
float temperatureC = 0.0f;
float humidity = 0.0f;
float pressureHpa = 0.0f;
float mq2Filtered = 0.0f;
float lightFiltered = 0.0f;

bool dhtReady = false;
bool bmpReady = false;
unsigned long lastSensorRead = 0;
unsigned long lastPageSwitch = 0;
int currentPage = 0;

void setup() {
  Wire.begin();
  // ESP32 ADC 使用 12 位分辨率，和 0~4095 的显示条形图范围保持一致。
  analogReadResolution(12);

  dht.begin();
  dhtReady = true;

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ESP32 Env Node");
    display.println("Init sensors...");
    display.display();
  } else {
    while (true) {
    }
  }

  bmpReady = bmp.begin(0x76) || bmp.begin(0x77);

  int mq2Raw = analogRead(PIN_MQ2);
  int lightRaw = analogRead(PIN_LIGHT);
  mq2Filtered = static_cast<float>(mq2Raw);
  lightFiltered = static_cast<float>(lightRaw);
}

void loop() {
  // 这里不做复杂状态切换，逻辑集中在“采样 + 分页绘制”两件事上。
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

  // DHT22 失败时保留上次有效值，但把状态标记为故障，页面层再给出提示。
  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperatureC = newTemperature;
  } else {
    dhtReady = false;
  }

  if (!bmpReady) {
    // BMP280 如果上电初始化失败，后续仍周期性重试，避免必须手动复位。
    bmpReady = bmp.begin(0x76) || bmp.begin(0x77);
  }

  if (bmpReady) {
    pressureHpa = bmp.readPressure() / 100.0f;
  }

  int mq2Raw = analogRead(PIN_MQ2);
  int lightRaw = analogRead(PIN_LIGHT);

  mq2Filtered = ANALOG_FILTER_ALPHA * static_cast<float>(mq2Raw) +
                (1.0f - ANALOG_FILTER_ALPHA) * mq2Filtered;
  lightFiltered = ANALOG_FILTER_ALPHA * static_cast<float>(lightRaw) +
                  (1.0f - ANALOG_FILTER_ALPHA) * lightFiltered;

  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    dhtReady = true;
  }
}

void updateDisplayPage() {
  // 采用自动轮播而不是按键翻页，适合作为长期运行的环境看板。
  if (millis() - lastPageSwitch >= DISPLAY_PAGE_MS) {
    currentPage = (currentPage + 1) % 4;
    lastPageSwitch = millis();
  }
}

void drawPage() {
  // 每页都从头绘制，避免旧页面内容残留。
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (currentPage == 0) {
    drawSummaryPage();
  } else if (currentPage == 1) {
    drawAirLightPage();
  } else if (currentPage == 2) {
    drawComfortPage();
  } else {
    drawNodeInfoPage();
  }

  display.display();
}

void drawSummaryPage() {
  // 总览页优先展示最核心的温湿度、气压和总状态。
  display.println("Multi Env Summary");

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
    display.println("BMP280 error");
  }

  display.print("State:");
  display.println(overallStateLabel());
  drawAnalogBar(0, 54, static_cast<int>(mq2Filtered), 4095, "Smoke");
}

void drawAirLightPage() {
  // 这一页突出模拟量输入，便于现场调阈值或观察传感器响应。
  display.println("Smoke and Light");
  display.print("MQ2 Raw : ");
  display.println(static_cast<int>(mq2Filtered));
  display.print("LightRaw: ");
  display.println(static_cast<int>(lightFiltered));
  display.print("AirRisk : ");
  display.println(smokeLevelLabel());
  drawAnalogBar(0, 54, static_cast<int>(lightFiltered), 4095, "Light");
}

void drawComfortPage() {
  // 舒适度页把多个指标压缩成标签，便于快速判断是否处于合理区间。
  display.println("Comfort Check");
  display.print("Temp : ");
  display.println(comfortTemperatureLabel());
  display.print("Hum  : ");
  display.println(comfortHumidityLabel());
  display.print("Press: ");
  display.println(bmpReady ? "OK" : "ERR");
  display.print("Alarm: ");
  display.println(overallStateLabel());
}

void drawNodeInfoPage() {
  display.println("Node Information");
  display.println("ESP32 + DHT22");
  display.println("MQ2 + BMP280");
  display.print("Sample: ");
  display.println("2s");
  display.print("Pages : ");
  display.println("4");
}

const char* smokeLevelLabel() {
  if (static_cast<int>(mq2Filtered) >= MQ2_DANGER_THRESHOLD) {
    return "DANGER";
  }
  if (static_cast<int>(mq2Filtered) >= MQ2_WARNING_THRESHOLD) {
    return "WARN";
  }
  return "NORMAL";
}

const char* comfortTemperatureLabel() {
  if (!dhtReady) {
    return "ERR";
  }
  if (temperatureC < COMFORT_TEMP_LOW) {
    return "LOW";
  }
  if (temperatureC > COMFORT_TEMP_HIGH) {
    return "HIGH";
  }
  return "OK";
}

const char* comfortHumidityLabel() {
  if (!dhtReady) {
    return "ERR";
  }
  if (humidity < COMFORT_HUMIDITY_LOW) {
    return "DRY";
  }
  if (humidity > COMFORT_HUMIDITY_HIGH) {
    return "WET";
  }
  return "OK";
}

const char* overallStateLabel() {
  // 总体状态采用“故障或强风险优先”的策略，避免轻微异常掩盖严重问题。
  bool dhtFault = !dhtReady;
  bool smokeDanger = static_cast<int>(mq2Filtered) >= MQ2_DANGER_THRESHOLD;
  bool smokeWarn = static_cast<int>(mq2Filtered) >= MQ2_WARNING_THRESHOLD;
  bool comfortWarning = false;

  if (dhtReady) {
    comfortWarning = temperatureC < COMFORT_TEMP_LOW ||
                     temperatureC > COMFORT_TEMP_HIGH ||
                     humidity < COMFORT_HUMIDITY_LOW ||
                     humidity > COMFORT_HUMIDITY_HIGH;
  }

  if (dhtFault || smokeDanger) {
    return "DANGER";
  }
  if (smokeWarn || comfortWarning || !bmpReady) {
    return "WARN";
  }
  return "NORMAL";
}

void drawAnalogBar(int x, int y, int value, int maxValue, const char* label) {
  // 条形图只负责可视化，不参与任何告警判定。
  int barWidth = map(constrain(value, 0, maxValue), 0, maxValue, 0, 80);

  display.setCursor(x, y - 8);
  display.print(label);
  display.drawRect(x + 34, y - 10, 82, 8, SSD1306_WHITE);
  display.fillRect(x + 35, y - 9, barWidth, 6, SSD1306_WHITE);
}
