#include <SoftwareSerial.h>

// 蓝牙遥控小车：
// 通过 HC-05 接收手机发送的简单字符命令，控制双电机前进、后退、转向和停止。
// 数字字符 0-9 用于设置速度档位，便于演示蓝牙遥控和基础驱动控制。

const int PIN_BT_RX = 10;
const int PIN_BT_TX = 11;
const int PIN_ENA = 5;
const int PIN_IN1 = 2;
const int PIN_IN2 = 3;
const int PIN_ENB = 6;
const int PIN_IN3 = 4;
const int PIN_IN4 = 7;
const int PIN_STATUS_LED = 13;

SoftwareSerial bluetooth(PIN_BT_RX, PIN_BT_TX);

// 将运动状态显式枚举出来，便于串口命令和电机输出统一映射。
enum CarState {
  CAR_STOP,
  CAR_FORWARD,
  CAR_BACKWARD,
  CAR_LEFT,
  CAR_RIGHT
};

CarState carState = CAR_STOP;
int speedPwm = 180;
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_TIMEOUT_MS = 1500;

void setup() {
  pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_ENB, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  Serial.begin(9600);
  bluetooth.begin(9600);

  stopCar();
}

void loop() {
  // 蓝牙读取、失联保护和电机输出分开处理，逻辑更稳定。
  readBluetoothCommand();
  updateFailsafe();
  applyMotorState();
}

void readBluetoothCommand() {
  while (bluetooth.available()) {
    char command = static_cast<char>(bluetooth.read());
    lastCommandTime = millis();
    handleCommand(command);
  }
}

void handleCommand(char command) {
  // 方向命令和速度命令都走同一入口，便于后续扩展手机控制协议。
  if (command >= '0' && command <= '9') {
    speedPwm = map(command - '0', 0, 9, 0, 255);
    return;
  }

  if (command == 'F') {
    carState = CAR_FORWARD;
  } else if (command == 'B') {
    carState = CAR_BACKWARD;
  } else if (command == 'L') {
    carState = CAR_LEFT;
  } else if (command == 'R') {
    carState = CAR_RIGHT;
  } else if (command == 'S') {
    carState = CAR_STOP;
  }
}

void updateFailsafe() {
  // 蓝牙断连或长时间未收到命令时自动停车，避免小车继续失控运行。
  if (carState != CAR_STOP && millis() - lastCommandTime >= COMMAND_TIMEOUT_MS) {
    carState = CAR_STOP;
  }
}

void applyMotorState() {
  // 状态灯用于直观区分“静止”和“正在运动”。
  digitalWrite(PIN_STATUS_LED, carState == CAR_STOP ? LOW : HIGH);

  if (carState == CAR_FORWARD) {
    setLeftMotor(true, speedPwm);
    setRightMotor(true, speedPwm);
  } else if (carState == CAR_BACKWARD) {
    setLeftMotor(false, speedPwm);
    setRightMotor(false, speedPwm);
  } else if (carState == CAR_LEFT) {
    setLeftMotor(false, speedPwm);
    setRightMotor(true, speedPwm);
  } else if (carState == CAR_RIGHT) {
    setLeftMotor(true, speedPwm);
    setRightMotor(false, speedPwm);
  } else {
    stopCar();
  }
}

void setLeftMotor(bool forward, int pwm) {
  digitalWrite(PIN_IN1, forward ? HIGH : LOW);
  digitalWrite(PIN_IN2, forward ? LOW : HIGH);
  analogWrite(PIN_ENA, constrain(pwm, 0, 255));
}

void setRightMotor(bool forward, int pwm) {
  digitalWrite(PIN_IN3, forward ? HIGH : LOW);
  digitalWrite(PIN_IN4, forward ? LOW : HIGH);
  analogWrite(PIN_ENB, constrain(pwm, 0, 255));
}

void stopCar() {
  // 停车时同时拉低方向和 PWM，避免驱动板残留输出。
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
  analogWrite(PIN_ENA, 0);
  analogWrite(PIN_ENB, 0);
}
