#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

// 室内空气质量监测系统：
// 结合 MQ-135 与 DHT22 获取空气质量、温度和湿度，并在 OLED 上轮播显示。
// 本地蜂鸣器用于提示空气质量危险或传感器故障。

const int PIN_DHT = 4;
const int PIN_MQ135 = 34;
const int PIN_BUZZER = 18;
const int DHT_TYPE = DHT22;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long DISPLAY_PAGE_MS = 3200;
const unsigned long BUZZER_TOGGLE_MS = 180;
const float FILTER_ALPHA = 0.18f;

const int AQI_WARNING_THRESHOLD = 1700;
const int AQI_DANGER_THRESHOLD = 2600;
const float TEMP_COMFORT_HIGH = 30.0f;
const float HUMIDITY_COMFORT_HIGH = 75.0f;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum AirState {
  AIR_NORMAL,
  AIR_WARNING,
  AIR_DANGER,
  AIR_SENSOR_FAULT
};

float temperatureC = 0.0f;
float humidity = 0.0f;
float mq135Filtered = 0.0f;
bool dhtReady = false;
bool buzzerState = false;
int currentPage = 0;
AirState airState = AIR_SENSOR_FAULT;

unsigned long lastSensorRead = 0;
unsigned long lastPageSwitch = 0;
unsigned long lastBuzzerToggle = 0;

void setup() {
  Wire.begin();
  analogReadResolution(12);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  dht.begin();
  mq135Filtered = static_cast<float>(analogRead(PIN_MQ135));

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Air Quality Node");
  display.println("Init sensors...");
  display.display();
}

void loop() {
  updateSensorData();
  updateAirState();
  updateBuzzer();
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

  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperatureC = newTemperature;
    dhtReady = true;
  } else {
    dhtReady = false;
  }

  int mq135Raw = analogRead(PIN_MQ135);
  mq135Filtered = FILTER_ALPHA * static_cast<float>(mq135Raw) +
                  (1.0f - FILTER_ALPHA) * mq135Filtered;
}

void updateAirState() {
  if (!dhtReady) {
    airState = AIR_SENSOR_FAULT;
    return;
  }

  if (static_cast<int>(mq135Filtered) >= AQI_DANGER_THRESHOLD) {
    airState = AIR_DANGER;
  } else if (static_cast<int>(mq135Filtered) >= AQI_WARNING_THRESHOLD ||
             temperatureC >= TEMP_COMFORT_HIGH ||
             humidity >= HUMIDITY_COMFORT_HIGH) {
    airState = AIR_WARNING;
  } else {
    airState = AIR_NORMAL;
  }
}

void updateBuzzer() {
  if (airState != AIR_DANGER && airState != AIR_SENSOR_FAULT) {
    buzzerState = false;
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastBuzzerToggle >= BUZZER_TOGGLE_MS) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void updateDisplayPage() {
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
    drawAirSensorPage();
  } else {
    drawComfortPage();
  }

  display.display();
}

void drawSummaryPage() {
  display.println("Indoor Air Quality");
  if (dhtReady) {
    display.print("T:");
    display.print(temperatureC, 1);
    display.print("C H:");
    display.print(humidity, 0);
    display.println("%");
  } else {
    display.println("DHT22 read error");
  }

  display.print("MQ135:");
  display.println(static_cast<int>(mq135Filtered));
  display.print("State:");
  display.println(stateLabel());
  drawBar(0, 56, static_cast<int>(mq135Filtered), 4095);
}

void drawAirSensorPage() {
  display.println("Air Sensor Page");
  display.print("Warn : ");
  display.println(AQI_WARNING_THRESHOLD);
  display.print("Danger: ");
  display.println(AQI_DANGER_THRESHOLD);
  display.print("Buzzer: ");
  display.println(buzzerState ? "ON" : "OFF");
  display.print("Risk  : ");
  display.println(stateLabel());
}

void drawComfortPage() {
  display.println("Comfort Check");
  display.print("Temp : ");
  display.println(temperatureC >= TEMP_COMFORT_HIGH ? "HOT" : "OK");
  display.print("Hum  : ");
  display.println(humidity >= HUMIDITY_COMFORT_HIGH ? "WET" : "OK");
  display.print("DHT  : ");
  display.println(dhtReady ? "READY" : "FAULT");
  display.print("AQI  : ");
  display.println(stateLabel());
}

const char* stateLabel() {
  if (airState == AIR_NORMAL) {
    return "NORMAL";
  }
  if (airState == AIR_WARNING) {
    return "WARNING";
  }
  if (airState == AIR_DANGER) {
    return "DANGER";
  }
  return "FAULT";
}

void drawBar(int x, int y, int value, int maxValue) {
  int width = map(constrain(value, 0, maxValue), 0, maxValue, 0, 100);
  display.drawRect(x, y - 8, 102, 8, SSD1306_WHITE);
  display.fillRect(x + 1, y - 7, width, 6, SSD1306_WHITE);
}
