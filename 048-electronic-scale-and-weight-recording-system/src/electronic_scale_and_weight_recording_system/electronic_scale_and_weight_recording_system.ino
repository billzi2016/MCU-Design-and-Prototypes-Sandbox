#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// 电子秤与称重记录系统：
// 使用 HX711 读取称重传感器数据，支持去皮、校准系数调节和称重记录计数。
// 该版本通过 LCD 显示当前重量、校准系数和已记录次数。

const int PIN_HX711_DT = 3;
const int PIN_HX711_SCK = 2;
const int PIN_TARE_BUTTON = 8;
const int PIN_LOG_BUTTON = 9;
const int PIN_CAL_UP_BUTTON = 10;
const int PIN_CAL_DOWN_BUTTON = 11;

const unsigned long SAMPLE_INTERVAL_MS = 200;
const int MAX_LOG_COUNT = 99;

HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2);

float calibrationFactor = 2280.0f;
float currentWeight = 0.0f;
float tareOffset = 0.0f;
int logCount = 0;
unsigned long lastSampleTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TARE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LOG_BUTTON, INPUT_PULLUP);
  pinMode(PIN_CAL_UP_BUTTON, INPUT_PULLUP);
  pinMode(PIN_CAL_DOWN_BUTTON, INPUT_PULLUP);

  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_scale(calibrationFactor);
  scale.tare();

  lcd.init();
  lcd.backlight();
}

void loop() {
  handleButtons();
  sampleWeightIfNeeded();
  updateDisplay();
}

void handleButtons() {
  static bool lastTare = HIGH;
  static bool lastLog = HIGH;
  static bool lastCalUp = HIGH;
  static bool lastCalDown = HIGH;

  bool tareButton = digitalRead(PIN_TARE_BUTTON);
  bool logButton = digitalRead(PIN_LOG_BUTTON);
  bool calUpButton = digitalRead(PIN_CAL_UP_BUTTON);
  bool calDownButton = digitalRead(PIN_CAL_DOWN_BUTTON);

  if (lastTare == HIGH && tareButton == LOW) {
    // 去皮直接重置当前零点，适合空容器或托盘扣重。
    scale.tare();
    tareOffset = 0.0f;
  }

  if (lastLog == HIGH && logButton == LOW) {
    logCount = min(logCount + 1, MAX_LOG_COUNT);
    Serial.print("LOG,");
    Serial.println(currentWeight, 2);
  }

  if (lastCalUp == HIGH && calUpButton == LOW) {
    calibrationFactor += 10.0f;
    scale.set_scale(calibrationFactor);
  }

  if (lastCalDown == HIGH && calDownButton == LOW) {
    calibrationFactor -= 10.0f;
    scale.set_scale(calibrationFactor);
  }

  lastTare = tareButton;
  lastLog = logButton;
  lastCalUp = calUpButton;
  lastCalDown = calDownButton;
}

void sampleWeightIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  // 取多次平均值能降低称重抖动，适合演示环境下的稳态读数。
  currentWeight = scale.get_units(5) - tareOffset;
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("W:");
  lcd.print(currentWeight, 2);
  lcd.print(" g");
  lcd.setCursor(0, 1);
  lcd.print("C:");
  lcd.print(calibrationFactor, 0);
  lcd.print(" L:");
  lcd.print(logCount);
}
