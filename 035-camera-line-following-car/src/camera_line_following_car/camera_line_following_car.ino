#include <string.h>

// 摄像头巡线小车：
// OpenMV / 树莓派摄像头端负责图像采集、中心线提取和偏差计算，
// Arduino 端负责接收偏差值并完成双电机差速控制。

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const int BASE_SPEED = 150;
const int KP = 2;
const int KD = 3;
const unsigned long FRAME_TIMEOUT_MS = 450;
const int TARGET_INTERSECTION_COUNT = 3;

char serialBuffer[48];
byte serialIndex = 0;

int lastError = 0;
int currentError = 0;
bool stopFlag = false;
bool crossFlag = false;
bool lastCrossFlag = false;
int intersectionCount = 0;
unsigned long lastFrameTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  stopCar();
}

void loop() {
  readSerialFrame();

  // 摄像头端如果掉帧或串口中断，车辆必须尽快停车，避免带着旧误差继续跑偏。
  if (millis() - lastFrameTime > FRAME_TIMEOUT_MS) {
    stopCar();
    return;
  }

  if (stopFlag || intersectionCount >= TARGET_INTERSECTION_COUNT) {
    stopCar();
    return;
  }

  applySteering();
}

void readSerialFrame() {
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
  // 串口协议：
  // L,error,cross,stop
  // 例如：L,-18,0,0
  if (frame[0] != 'L' || frame[1] != ',') {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  currentError = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  crossFlag = atoi(token) == 1;

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  stopFlag = atoi(token) == 1;

  // 路口计数只在上升沿累加，防止摄像头在同一个路口连续多帧重复触发。
  if (crossFlag && !lastCrossFlag) {
    intersectionCount++;
  }
  lastCrossFlag = crossFlag;

  lastFrameTime = millis();
}

void applySteering() {
  int derivative = currentError - lastError;
  int correction = KP * currentError + KD * derivative;

  int leftSpeed = BASE_SPEED + correction;
  int rightSpeed = BASE_SPEED - correction;

  leftSpeed = constrain(leftSpeed, 0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);

  // 当前版本默认小车保持前进，通过差速调整朝向。
  setMotor(true, leftSpeed, true, rightSpeed);
  lastError = currentError;
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
