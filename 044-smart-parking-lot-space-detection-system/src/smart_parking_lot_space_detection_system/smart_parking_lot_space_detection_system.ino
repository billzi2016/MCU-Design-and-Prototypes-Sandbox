#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Wire.h>

// 智能停车场车位检测系统：
// 使用多路超声波检测车位占用情况，统计剩余车位，
// 并根据入口请求控制道闸开闭和 LED 指示。

const int PIN_GATE_SERVO = 3;
const int PIN_ENTRY_BUTTON = 4;
const int PIN_EXIT_BUTTON = 5;
const int PIN_LED_GREEN = 6;
const int PIN_LED_RED = 7;

const int PIN_TRIG_1 = 8;
const int PIN_ECHO_1 = 9;
const int PIN_TRIG_2 = 10;
const int PIN_ECHO_2 = 11;
const int PIN_TRIG_3 = 12;
const int PIN_ECHO_3 = 13;

const int OCCUPIED_DISTANCE_CM = 18;
const unsigned long SAMPLE_INTERVAL_MS = 500;
const unsigned long GATE_OPEN_MS = 3000;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gateServo;

int slotDistance[3] = {200, 200, 200};
bool slotOccupied[3] = {false, false, false};
int freeSlots = 3;
unsigned long lastSampleTime = 0;
unsigned long gateOpenedTime = 0;
bool gateOpen = false;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_ENTRY_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EXIT_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  pinMode(PIN_TRIG_1, OUTPUT);
  pinMode(PIN_ECHO_1, INPUT);
  pinMode(PIN_TRIG_2, OUTPUT);
  pinMode(PIN_ECHO_2, INPUT);
  pinMode(PIN_TRIG_3, OUTPUT);
  pinMode(PIN_ECHO_3, INPUT);

  gateServo.attach(PIN_GATE_SERVO);
  closeGate();

  lcd.init();
  lcd.backlight();
}

void loop() {
  sampleSlotsIfNeeded();
  handleGateLogic();
  updateIndicators();
  updateDisplay();
}

void sampleSlotsIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  slotDistance[0] = readDistanceCm(PIN_TRIG_1, PIN_ECHO_1);
  slotDistance[1] = readDistanceCm(PIN_TRIG_2, PIN_ECHO_2);
  slotDistance[2] = readDistanceCm(PIN_TRIG_3, PIN_ECHO_3);

  freeSlots = 0;
  for (int i = 0; i < 3; ++i) {
    // 小于阈值说明有车停在传感器前方，因此该车位视为已占用。
    slotOccupied[i] = slotDistance[i] < OCCUPIED_DISTANCE_CM;
    if (!slotOccupied[i]) {
      freeSlots++;
    }
  }
}

void handleGateLogic() {
  static bool lastEntryButton = HIGH;
  static bool lastExitButton = HIGH;
  bool entryButton = digitalRead(PIN_ENTRY_BUTTON);
  bool exitButton = digitalRead(PIN_EXIT_BUTTON);

  if (lastEntryButton == HIGH && entryButton == LOW && freeSlots > 0) {
    openGate();
  }

  // 出口按钮无论车位是否满都允许开闸，模拟车辆离场场景。
  if (lastExitButton == HIGH && exitButton == LOW) {
    openGate();
  }

  if (gateOpen && millis() - gateOpenedTime > GATE_OPEN_MS) {
    closeGate();
  }

  lastEntryButton = entryButton;
  lastExitButton = exitButton;
}

void openGate() {
  gateServo.write(95);
  gateOpenedTime = millis();
  gateOpen = true;
}

void closeGate() {
  gateServo.write(10);
  gateOpen = false;
}

void updateIndicators() {
  // 绿灯表示仍有车位可进，红灯表示满位或入口受限。
  digitalWrite(PIN_LED_GREEN, freeSlots > 0 ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, freeSlots == 0 ? HIGH : LOW);
}

int readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return 200;
  }
  return static_cast<int>(duration * 0.0343f / 2.0f);
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Free:");
  lcd.print(freeSlots);
  lcd.print(" Gate:");
  lcd.print(gateOpen ? "ON" : "OFF");
  lcd.setCursor(0, 1);
  for (int i = 0; i < 3; ++i) {
    lcd.print(slotOccupied[i] ? "X" : "O");
    if (i < 2) {
      lcd.print(' ');
    }
  }
}
