#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Wire.h>

// 智能垃圾桶：
// 通过红外或接近信号检测用户靠近后自动开盖，
// 使用超声波检测桶内剩余空间，并在接近满载时进行蜂鸣提示。

const int PIN_IR_APPROACH = 2;
const int PIN_TRIG = 12;
const int PIN_ECHO = 13;
const int PIN_SERVO_LID = 9;
const int PIN_BUZZER = 8;

const int LID_OPEN_ANGLE = 105;
const int LID_CLOSE_ANGLE = 15;
const int FULL_THRESHOLD_CM = 10;
const unsigned long LID_HOLD_MS = 3500;
const unsigned long SAMPLE_INTERVAL_MS = 400;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lidServo;

bool lidOpen = false;
bool fullAlarm = false;
int fillDistanceCm = 200;
unsigned long lidOpenedTime = 0;
unsigned long lastSampleTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_IR_APPROACH, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  lidServo.attach(PIN_SERVO_LID);
  closeLid();

  lcd.init();
  lcd.backlight();
}

void loop() {
  sampleTrashLevelIfNeeded();
  handleApproachTrigger();
  updateLidState();
  updateBuzzer();
  updateDisplay();
}

void sampleTrashLevelIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  fillDistanceCm = readDistanceCm();
  fullAlarm = fillDistanceCm <= FULL_THRESHOLD_CM;
}

void handleApproachTrigger() {
  // 有人靠近时自动开盖，但如果已经满载，只保留提示不再反复打开，避免垃圾溢出。
  if (digitalRead(PIN_IR_APPROACH) == HIGH && !fullAlarm) {
    openLid();
  }
}

void updateLidState() {
  if (lidOpen && millis() - lidOpenedTime > LID_HOLD_MS) {
    closeLid();
  }
}

void openLid() {
  lidServo.write(LID_OPEN_ANGLE);
  lidOpen = true;
  lidOpenedTime = millis();
}

void closeLid() {
  lidServo.write(LID_CLOSE_ANGLE);
  lidOpen = false;
}

void updateBuzzer() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;

  if (!fullAlarm) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastToggle < 280) {
    return;
  }
  lastToggle = millis();
  buzzerOn = !buzzerOn;
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
}

int readDistanceCm() {
  // 超声波安装在桶盖上方向下看，距离越小说明垃圾越接近满载位置。
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duration == 0) {
    return 200;
  }
  return static_cast<int>(duration * 0.0343f / 2.0f);
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Lid:");
  lcd.print(lidOpen ? "OPEN " : "CLOSE");
  lcd.setCursor(0, 1);
  lcd.print("Fill:");
  lcd.print(fillDistanceCm);
  lcd.print(fullAlarm ? " FULL" : " OK");
}
