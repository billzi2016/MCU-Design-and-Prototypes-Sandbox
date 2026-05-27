#include <Servo.h>

// 超声波避障小车：
// 通过舵机带动 HC-SR04 左中右扫描前方距离，根据最优方向选择前进路径。
// 当前实现是典型的“前进 - 探测 - 决策 - 转向”自主避障流程。

const int PIN_TRIG = 12;
const int PIN_ECHO = 13;
const int PIN_SERVO = 3;

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const int FORWARD_SPEED = 160;
const int TURN_SPEED = 150;
// 小于该距离就认为前方不再安全，需要停下来做避障决策。
const int SAFE_DISTANCE_CM = 25;

Servo scanServo;

void setup() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  scanServo.attach(PIN_SERVO);
  scanServo.write(90);
  Serial.begin(115200);
}

void loop() {
  // 先看正前方是否安全，不安全时再做左右扫描和转向决策。
  int centerDistance = scanDistanceAt(90);

  if (centerDistance > SAFE_DISTANCE_CM) {
    moveForward();
    return;
  }

  stopCar();
  // 停一下再扫描，避免惯性前冲时测得的仍是上一时刻的距离。
  delay(150);

  int leftDistance = scanDistanceAt(150);
  int rightDistance = scanDistanceAt(30);

  // 左右都看完后再统一决策，避免只看一侧就贸然转向。
  if (leftDistance > rightDistance && leftDistance > SAFE_DISTANCE_CM) {
    turnLeft();
  } else if (rightDistance >= leftDistance && rightDistance > SAFE_DISTANCE_CM) {
    turnRight();
  } else {
    // 两侧都不够安全时先小幅后退，为下一次转向腾出空间。
    moveBackward();
    delay(300);
    turnRight();
  }
}

int scanDistanceAt(int angle) {
  // 扫描前先把舵机转到目标角度，等待机械稳定后再测距。
  scanServo.write(angle);
  delay(250);
  return readDistanceCm();
}

int readDistanceCm() {
  // 超声波测距超时后返回一个较大值，避免因偶发失败直接把车锁死。
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

void moveForward() {
  // 前进阶段默认双轮等速，直到下一个测距周期再次判断环境。
  setMotor(true, FORWARD_SPEED, true, FORWARD_SPEED);
}

void moveBackward() {
  // 后退只在双侧都不安全时短暂触发，用于脱离死角。
  setMotor(false, FORWARD_SPEED, false, FORWARD_SPEED);
}

void turnLeft() {
  // 这里用固定时长差速转向，是基础避障平台里最简单直接的实现。
  setMotor(false, TURN_SPEED, true, TURN_SPEED);
  delay(280);
}

void turnRight() {
  setMotor(true, TURN_SPEED, false, TURN_SPEED);
  delay(280);
}

void stopCar() {
  // 停车单独封装，便于后续增加蜂鸣器、状态灯或自动/手动模式切换。
  digitalWrite(PIN_LEFT_IN1, LOW);
  digitalWrite(PIN_LEFT_IN2, LOW);
  digitalWrite(PIN_RIGHT_IN1, LOW);
  digitalWrite(PIN_RIGHT_IN2, LOW);
  analogWrite(PIN_LEFT_PWM, 0);
  analogWrite(PIN_RIGHT_PWM, 0);
}

void setMotor(bool leftForward, int leftPwm, bool rightForward, int rightPwm) {
  // 左右电机统一由同一个函数下发，便于后续换成更复杂的转向策略。
  digitalWrite(PIN_LEFT_IN1, leftForward ? HIGH : LOW);
  digitalWrite(PIN_LEFT_IN2, leftForward ? LOW : HIGH);
  digitalWrite(PIN_RIGHT_IN1, rightForward ? HIGH : LOW);
  digitalWrite(PIN_RIGHT_IN2, rightForward ? LOW : HIGH);
  analogWrite(PIN_LEFT_PWM, constrain(leftPwm, 0, 255));
  analogWrite(PIN_RIGHT_PWM, constrain(rightPwm, 0, 255));
}
