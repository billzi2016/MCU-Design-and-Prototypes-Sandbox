#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

// 小型温控箱系统：
// 采集箱内温湿度，根据设定目标温度和回差控制加热片与风扇，
// 同时支持本地按键调节设定值和自动 / 手动模式切换。

const int PIN_DHT = 4;
const int PIN_HEATER_RELAY = 5;
const int PIN_FAN_RELAY = 6;
const int PIN_MODE_BUTTON = 8;
const int PIN_UP_BUTTON = 9;
const int PIN_DOWN_BUTTON = 10;

const int DHT_TYPE = DHT22;
const float DEFAULT_TARGET_TEMP = 28.0f;
const float TEMP_HYSTERESIS = 1.0f;
const unsigned long SAMPLE_INTERVAL_MS = 1500;
const unsigned long SCREEN_ROTATE_MS = 2200;

DHT dht(PIN_DHT, DHT_TYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum ControlMode {
  MODE_AUTO,
  MODE_HEAT_ONLY,
  MODE_FAN_ONLY,
  MODE_IDLE
};

ControlMode controlMode = MODE_AUTO;

float temperatureC = 0.0f;
float humidity = 0.0f;
float targetTemp = DEFAULT_TARGET_TEMP;
bool sensorFault = false;
bool heaterOn = false;
bool fanOn = false;
bool showStatusPage = true;
unsigned long lastSampleTime = 0;
unsigned long lastScreenSwitch = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_HEATER_RELAY, OUTPUT);
  pinMode(PIN_FAN_RELAY, OUTPUT);
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_UP_BUTTON, INPUT_PULLUP);
  pinMode(PIN_DOWN_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_HEATER_RELAY, LOW);
  digitalWrite(PIN_FAN_RELAY, LOW);

  dht.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void loop() {
  handleButtons();
  sampleEnvironmentIfNeeded();
  updateControlLogic();
  applyOutputs();
  updateDisplay();
}

void handleButtons() {
  static bool lastMode = HIGH;
  static bool lastUp = HIGH;
  static bool lastDown = HIGH;

  bool modeButton = digitalRead(PIN_MODE_BUTTON);
  bool upButton = digitalRead(PIN_UP_BUTTON);
  bool downButton = digitalRead(PIN_DOWN_BUTTON);

  if (lastMode == HIGH && modeButton == LOW) {
    controlMode = static_cast<ControlMode>((controlMode + 1) % 4);
  }

  if (lastUp == HIGH && upButton == LOW) {
    targetTemp = min(targetTemp + 0.5f, 45.0f);
  }

  if (lastDown == HIGH && downButton == LOW) {
    targetTemp = max(targetTemp - 0.5f, 5.0f);
  }

  lastMode = modeButton;
  lastUp = upButton;
  lastDown = downButton;
}

void sampleEnvironmentIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  float newTemp = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  sensorFault = isnan(newTemp) || isnan(newHumidity);
  if (!sensorFault) {
    temperatureC = newTemp;
    humidity = newHumidity;
  }
}

void updateControlLogic() {
  // 自动模式使用目标温度和回差控制，避免温度刚好压线时继电器频繁抖动。
  if (sensorFault) {
    heaterOn = false;
    fanOn = false;
    return;
  }

  switch (controlMode) {
    case MODE_AUTO:
      if (temperatureC < targetTemp - TEMP_HYSTERESIS) {
        heaterOn = true;
        fanOn = false;
      } else if (temperatureC > targetTemp + TEMP_HYSTERESIS) {
        heaterOn = false;
        fanOn = true;
      }
      break;

    case MODE_HEAT_ONLY:
      heaterOn = true;
      fanOn = false;
      break;

    case MODE_FAN_ONLY:
      heaterOn = false;
      fanOn = true;
      break;

    case MODE_IDLE:
      heaterOn = false;
      fanOn = false;
      break;
  }

  // 自动模式下，当温度回到目标附近时关闭执行器，保持箱内环境稳定。
  if (controlMode == MODE_AUTO && abs(temperatureC - targetTemp) <= TEMP_HYSTERESIS * 0.4f) {
    heaterOn = false;
    fanOn = false;
  }
}

void applyOutputs() {
  digitalWrite(PIN_HEATER_RELAY, heaterOn ? HIGH : LOW);
  digitalWrite(PIN_FAN_RELAY, fanOn ? HIGH : LOW);
}

void updateDisplay() {
  if (millis() - lastScreenSwitch > SCREEN_ROTATE_MS) {
    lastScreenSwitch = millis();
    showStatusPage = !showStatusPage;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (showStatusPage) {
    display.setCursor(0, 0);
    display.print("Temp:");
    display.print(temperatureC, 1);
    display.print("C");
    display.setCursor(0, 14);
    display.print("Hum :");
    display.print(humidity, 0);
    display.print("%");
    display.setCursor(0, 28);
    display.print("Target:");
    display.print(targetTemp, 1);
    display.setCursor(0, 42);
    display.print("Mode:");
    display.print(controlMode);
  } else {
    display.setCursor(0, 0);
    display.print("Heat:");
    display.print(heaterOn ? "ON" : "OFF");
    display.setCursor(0, 14);
    display.print("Fan :");
    display.print(fanOn ? "ON" : "OFF");
    display.setCursor(0, 28);
    display.print(sensorFault ? "Sensor Fault" : "Sensor OK");
    display.setCursor(0, 42);
    display.print("Band:");
    display.print(TEMP_HYSTERESIS, 1);
  }

  display.display();
}
