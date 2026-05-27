#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include <Wire.h>

// 无人小车感知与控制平台：
// 融合超声波、三路红外循迹、编码器测速和视觉侧串口偏差输入，
// 通过模式切换统一演示避障、巡线、速度控制和视觉辅助驾驶四类能力。

const int PIN_TRIG = 12;
const int PIN_ECHO = 13;
const int PIN_IR_LEFT = A0;
const int PIN_IR_CENTER = A1;
const int PIN_IR_RIGHT = A2;
const int PIN_ENCODER = 2;
const int PIN_MODE_BUTTON = 4;

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const int LINE_THRESHOLD = 500;
const int SAFE_DISTANCE_CM = 25;
const int BASE_SPEED = 145;
const int TARGET_RPM = 90;
const unsigned long SENSOR_INTERVAL_MS = 80;
const unsigned long RPM_INTERVAL_MS = 500;
const unsigned long VISION_TIMEOUT_MS = 500;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum DriveMode {
  MODE_LINE_FOLLOW,
  MODE_OBSTACLE_AVOID,
  MODE_SPEED_HOLD,
  MODE_VISION_ASSIST
};

DriveMode driveMode = MODE_LINE_FOLLOW;

volatile unsigned long encoderPulses = 0;
unsigned long lastSensorTime = 0;
unsigned long lastRpmTime = 0;
unsigned long lastVisionTime = 0;

int distanceCm = 200;
int lineStateLeft = 0;
int lineStateCenter = 0;
int lineStateRight = 0;
int visionError = 0;
float currentRpm = 0.0f;
int speedPwm = BASE_SPEED;

char serialBuffer[32];
byte serialIndex = 0;

void onEncoderPulse() {
  encoderPulses++;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER), onEncoderPulse, RISING);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void loop() {
  handleModeButton();
  readVisionFrame();
  sampleSensorsIfNeeded();
  updateRpmIfNeeded();
  runCurrentMode();
  updateDisplay();
}

void handleModeButton() {
  static bool lastButton = HIGH;
  bool currentButton = digitalRead(PIN_MODE_BUTTON);

  // 模式切换集中到一个按键，便于同一台底盘轮流演示不同功能。
  if (lastButton == HIGH && currentButton == LOW) {
    driveMode = static_cast<DriveMode>((driveMode + 1) % 4);
    stopCar();
  }
  lastButton = currentButton;
}

void readVisionFrame() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseVisionFrame(serialBuffer);
      serialIndex = 0;
      continue;
    }
    if (serialIndex < sizeof(serialBuffer) - 1) {
      serialBuffer[serialIndex++] = incoming;
    } else {
      serialIndex = 0;
    }
  }
}

void parseVisionFrame(char *frame) {
  // 串口协议：V,error
  // 视觉端只下发一维横向偏差，底盘端负责把它变成差速修正量。
  if (strncmp(frame, "V,", 2) != 0) {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }

  visionError = atoi(token);
  lastVisionTime = millis();
}

void sampleSensorsIfNeeded() {
  if (millis() - lastSensorTime < SENSOR_INTERVAL_MS) {
    return;
  }
  lastSensorTime = millis();

  distanceCm = readDistanceCm();
  lineStateLeft = analogRead(PIN_IR_LEFT) < LINE_THRESHOLD ? 1 : 0;
  lineStateCenter = analogRead(PIN_IR_CENTER) < LINE_THRESHOLD ? 1 : 0;
  lineStateRight = analogRead(PIN_IR_RIGHT) < LINE_THRESHOLD ? 1 : 0;
}

void updateRpmIfNeeded() {
  if (millis() - lastRpmTime < RPM_INTERVAL_MS) {
    return;
  }

  noInterrupts();
  unsigned long pulses = encoderPulses;
  encoderPulses = 0;
  interrupts();

  // 这里假设编码器每转 20 个脉冲，仅用于原型演示的粗略速度闭环。
  currentRpm = (pulses * 60000.0f) / (20.0f * (millis() - lastRpmTime));
  lastRpmTime = millis();
}

void runCurrentMode() {
  switch (driveMode) {
    case MODE_LINE_FOLLOW:
      runLineFollowMode();
      break;
    case MODE_OBSTACLE_AVOID:
      runObstacleAvoidMode();
      break;
    case MODE_SPEED_HOLD:
      runSpeedHoldMode();
      break;
    case MODE_VISION_ASSIST:
      runVisionAssistMode();
      break;
  }
}

void runLineFollowMode() {
  int error = calculateLineError();
  if (lineStateLeft == 0 && lineStateCenter == 0 && lineStateRight == 0) {
    stopCar();
    return;
  }

  int leftPwm = constrain(BASE_SPEED - error * 55, 0, 255);
  int rightPwm = constrain(BASE_SPEED + error * 55, 0, 255);
  setMotor(true, leftPwm, true, rightPwm);
}

void runObstacleAvoidMode() {
  // 避障模式优先关心前方距离，小于阈值时直接急停并原地转向。
  if (distanceCm < SAFE_DISTANCE_CM) {
    setMotor(false, 150, true, 150);
    delay(220);
    return;
  }
  setMotor(true, BASE_SPEED, true, BASE_SPEED);
}

void runSpeedHoldMode() {
  // 速度保持模式用当前转速追踪目标转速，展示编码器速度闭环的基本效果。
  if (currentRpm < TARGET_RPM - 5) {
    speedPwm = min(speedPwm + 6, 220);
  } else if (currentRpm > TARGET_RPM + 5) {
    speedPwm = max(speedPwm - 6, 90);
  }
  setMotor(true, speedPwm, true, speedPwm);
}

void runVisionAssistMode() {
  // 若视觉侧长时间没有新偏差输入，立即停车，避免底盘使用过期视觉结果继续跑偏。
  if (millis() - lastVisionTime > VISION_TIMEOUT_MS) {
    stopCar();
    return;
  }

  int correction = visionError * 2;
  int leftPwm = constrain(BASE_SPEED + correction, 0, 255);
  int rightPwm = constrain(BASE_SPEED - correction, 0, 255);
  setMotor(true, leftPwm, true, rightPwm);
}

int calculateLineError() {
  if (lineStateCenter && !lineStateLeft && !lineStateRight) {
    return 0;
  }
  if (lineStateLeft && !lineStateCenter) {
    return -1;
  }
  if (lineStateRight && !lineStateCenter) {
    return 1;
  }
  if (lineStateLeft && lineStateCenter) {
    return -1;
  }
  if (lineStateRight && lineStateCenter) {
    return 1;
  }
  return 0;
}

int readDistanceCm() {
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

void setMotor(bool leftForward, int leftPwm, bool rightForward, int rightPwm) {
  digitalWrite(PIN_LEFT_IN1, leftForward ? HIGH : LOW);
  digitalWrite(PIN_LEFT_IN2, leftForward ? LOW : HIGH);
  digitalWrite(PIN_RIGHT_IN1, rightForward ? HIGH : LOW);
  digitalWrite(PIN_RIGHT_IN2, rightForward ? LOW : HIGH);
  analogWrite(PIN_LEFT_PWM, leftPwm);
  analogWrite(PIN_RIGHT_PWM, rightPwm);
}

void stopCar() {
  digitalWrite(PIN_LEFT_IN1, LOW);
  digitalWrite(PIN_LEFT_IN2, LOW);
  digitalWrite(PIN_RIGHT_IN1, LOW);
  digitalWrite(PIN_RIGHT_IN2, LOW);
  analogWrite(PIN_LEFT_PWM, 0);
  analogWrite(PIN_RIGHT_PWM, 0);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Mode:");
  display.print(driveMode);
  display.setCursor(0, 12);
  display.print("Dis:");
  display.print(distanceCm);
  display.print("cm");
  display.setCursor(0, 24);
  display.print("Line:");
  display.print(lineStateLeft);
  display.print(lineStateCenter);
  display.print(lineStateRight);
  display.setCursor(0, 36);
  display.print("RPM:");
  display.print(currentRpm, 1);
  display.setCursor(0, 48);
  display.print("Vision:");
  display.print(visionError);
  display.display();
}
