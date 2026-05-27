#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// 智能家居安防原型系统：
// 集成温湿度、烟雾、门磁和人体红外检测，支持布防/撤防、环境告警和入侵告警。
// 该版本重点体现多源传感器融合、报警分级和本地显示逻辑。

const int PIN_DHT = 4;
const int PIN_MQ2 = 34;
const int PIN_DOOR = 18;
const int PIN_PIR = 19;
const int PIN_BUZZER = 25;
const int PIN_RELAY = 26;
const int PIN_ARM_BUTTON = 27;
const int PIN_ACK_BUTTON = 14;

const int DHT_TYPE = DHT22;
// MQ-2 与温度阈值都需要按具体环境重新标定，这里给的是原型默认值。
const int SMOKE_THRESHOLD = 2000;
const float TEMP_HIGH_THRESHOLD = 35.0f;
const unsigned long SAMPLE_INTERVAL_MS = 1200;
const unsigned long DISPLAY_ROTATE_MS = 2400;
const unsigned long INTRUSION_LATCH_MS = 15000;

DHT dht(PIN_DHT, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

enum SecurityMode {
  MODE_DISARMED,
  MODE_ARMED,
  MODE_ALARM
};

SecurityMode securityMode = MODE_DISARMED;

// envAlarm 和 intrusionAlarm 分别表示“环境风险”和“入侵风险”，
// 它们最终共同影响 securityMode 和声光联动，但来源不同、解除方式也不同。
float temperatureC = 0.0f;
float humidity = 0.0f;
int smokeRaw = 0;
bool doorOpen = false;
bool motionDetected = false;
bool envAlarm = false;
bool intrusionAlarm = false;

unsigned long lastSampleTime = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastIntrusionTime = 0;
bool showSensorPage = true;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_ARM_BUTTON, INPUT_PULLUP);
  pinMode(PIN_ACK_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_RELAY, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  updateDisplay();
}

void loop() {
  handleButtons();
  sampleSensorsIfNeeded();
  evaluateAlarms();
  updateOutputs();
  updateDisplay();
}

void handleButtons() {
  static bool lastArmButton = HIGH;
  static bool lastAckButton = HIGH;

  bool armButton = digitalRead(PIN_ARM_BUTTON);
  bool ackButton = digitalRead(PIN_ACK_BUTTON);

  if (lastArmButton == HIGH && armButton == LOW) {
    // 布防按键是整套系统的人机入口，布防切换时要顺手清理部分临时状态。
    securityMode = securityMode == MODE_DISARMED ? MODE_ARMED : MODE_DISARMED;
    if (securityMode == MODE_DISARMED) {
      intrusionAlarm = false;
    }
  }

  if (lastAckButton == HIGH && ackButton == LOW) {
    // 确认键优先用于消除入侵报警；若环境异常还在，系统不应被错误切回安全态。
    intrusionAlarm = false;
    if (!envAlarm) {
      securityMode = (securityMode == MODE_ALARM) ? MODE_ARMED : securityMode;
    }
  }

  lastArmButton = armButton;
  lastAckButton = ackButton;
}

void sampleSensorsIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  float newTemp = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  // DHT 失败时保留上一次有效值，避免偶发读数失败把屏幕和逻辑全部打成 0。
  if (!isnan(newTemp)) {
    temperatureC = newTemp;
  }
  if (!isnan(newHumidity)) {
    humidity = newHumidity;
  }

  smokeRaw = analogRead(PIN_MQ2);
  doorOpen = digitalRead(PIN_DOOR) == LOW;
  motionDetected = digitalRead(PIN_PIR) == HIGH;
}

void evaluateAlarms() {
  envAlarm = smokeRaw > SMOKE_THRESHOLD || temperatureC > TEMP_HIGH_THRESHOLD;

  // 只有在布防状态下，门磁和人体红外才参与入侵判定，避免住户活动时频繁误报。
  if (securityMode == MODE_ARMED && (doorOpen || motionDetected)) {
    intrusionAlarm = true;
    lastIntrusionTime = millis();
  }

  if (intrusionAlarm || envAlarm) {
    securityMode = MODE_ALARM;
  } else if (securityMode != MODE_DISARMED) {
    securityMode = MODE_ARMED;
  }

  // 入侵报警至少保持一段时间，避免门刚打开就立刻被用户误消掉。
  if (intrusionAlarm && millis() - lastIntrusionTime < INTRUSION_LATCH_MS) {
    intrusionAlarm = true;
  }
}

void updateOutputs() {
  // 继电器可以外接警灯、排风或切断执行回路，这里统一由告警总状态驱动。
  digitalWrite(PIN_RELAY, securityMode == MODE_ALARM ? HIGH : LOW);
  updateBuzzerPattern();
}

void updateBuzzerPattern() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;
  unsigned long interval = 0;

  if (intrusionAlarm) {
    interval = 120;
  } else if (envAlarm) {
    interval = 320;
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastToggle < interval) {
    return;
  }
  lastToggle = millis();
  buzzerOn = !buzzerOn;
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
}

void updateDisplay() {
  // 一页看环境数值，一页看安防状态，避免 16x2 屏幕内容过度拥挤。
  if (millis() - lastDisplaySwitch > DISPLAY_ROTATE_MS) {
    lastDisplaySwitch = millis();
    showSensorPage = !showSensorPage;
  }

  lcd.clear();
  if (showSensorPage) {
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperatureC, 1);
    lcd.print(" H:");
    lcd.print(humidity, 0);
    lcd.setCursor(0, 1);
    lcd.print("Smoke:");
    lcd.print(smokeRaw);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Mode:");
    lcd.print(modeName());
    lcd.setCursor(0, 1);
    lcd.print(doorOpen ? "Door " : "Door-");
    lcd.print(motionDetected ? " PIR" : " ---");
  }
}

const char *modeName() {
  if (securityMode == MODE_ARMED) {
    return "ARMED";
  }
  if (securityMode == MODE_ALARM) {
    return "ALARM";
  }
  return "SAFE";
}
