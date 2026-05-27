#include <PID_v1.h>

// 倒立摆控制系统：
// 使用电位器模拟摆杆角度输入，用 PID 输出电机控制量。
// 重点在于展示倒立摆角度误差、PID 计算与执行机构输出的基础框架。

const int PIN_ANGLE_SENSOR = A0;
const int PIN_CART_PWM = 5;
const int PIN_CART_IN1 = 6;
const int PIN_CART_IN2 = 7;

const unsigned long CONTROL_INTERVAL_MS = 15;
const double SAFE_ANGLE_DEG = 35.0;

double setpointAngle = 0.0;
double inputAngle = 0.0;
double outputPwm = 0.0;
double kp = 9.0;
double ki = 0.5;
double kd = 0.3;

PID pendulumPid(&inputAngle, &outputPwm, &setpointAngle, kp, ki, kd, DIRECT);
unsigned long lastControlTime = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_CART_PWM, OUTPUT);
  pinMode(PIN_CART_IN1, OUTPUT);
  pinMode(PIN_CART_IN2, OUTPUT);

  pendulumPid.SetOutputLimits(-255, 255);
  pendulumPid.SetSampleTime(CONTROL_INTERVAL_MS);
  pendulumPid.SetMode(AUTOMATIC);
  lastControlTime = millis();
}

void loop() {
  // 倒立摆控制采用固定控制周期，便于做 PID 参数整定。
  if (millis() - lastControlTime < CONTROL_INTERVAL_MS) {
    return;
  }

  lastControlTime = millis();
  inputAngle = readAngleDegrees();

  // 当摆角超出安全范围时直接停机，避免演示平台机械冲撞。
  if (abs(inputAngle) > SAFE_ANGLE_DEG) {
    stopCart();
    Serial.println("Pendulum out of range");
    return;
  }

  pendulumPid.Compute();
  applyCartOutput(static_cast<int>(outputPwm));

  Serial.print("Angle:");
  Serial.print(inputAngle);
  Serial.print(",PWM:");
  Serial.println(outputPwm);
}

double readAngleDegrees() {
  // 这里用电位器输入模拟摆角，便于在没有真实摆杆机构时验证控制框架。
  int raw = analogRead(PIN_ANGLE_SENSOR);
  return map(raw, 0, 1023, -450, 450) / 10.0;
}

void applyCartOutput(int pwm) {
  // 正负号决定小车运动方向，绝对值决定输出强度。
  bool forward = pwm >= 0;
  int output = constrain(abs(pwm), 0, 255);

  digitalWrite(PIN_CART_IN1, forward ? HIGH : LOW);
  digitalWrite(PIN_CART_IN2, forward ? LOW : HIGH);
  analogWrite(PIN_CART_PWM, output);
}

void stopCart() {
  digitalWrite(PIN_CART_IN1, LOW);
  digitalWrite(PIN_CART_IN2, LOW);
  analogWrite(PIN_CART_PWM, 0);
}
