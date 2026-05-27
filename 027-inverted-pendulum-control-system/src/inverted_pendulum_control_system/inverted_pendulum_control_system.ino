#include <PID_v1.h>

// 倒立摆控制系统：
// 使用电位器模拟摆杆角度输入，用 PID 输出电机控制量。
// 重点在于展示倒立摆角度误差、PID 计算与执行机构输出的基础框架。

const int PIN_ANGLE_SENSOR = A0;
const int PIN_CART_PWM = 5;
const int PIN_CART_IN1 = 6;
const int PIN_CART_IN2 = 7;

const unsigned long CONTROL_INTERVAL_MS = 15;
// 超过该角度认为摆杆已经失去可控性，继续输出只会造成滑台冲撞。
const double SAFE_ANGLE_DEG = 35.0;

double setpointAngle = 0.0;
double inputAngle = 0.0;
double outputPwm = 0.0;
// 这里保留为基础 PID 参数，重点是演示倒立摆控制框架而不是追求最终整定结果。
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
  // 当前版本用电位器模拟角度输入，因此能在没有真实摆杆机构时先验证控制链路。
  inputAngle = readAngleDegrees();

  // 当摆角超出安全范围时直接停机，避免演示平台机械冲撞。
  if (abs(inputAngle) > SAFE_ANGLE_DEG) {
    stopCart();
    Serial.println("Pendulum out of range");
    return;
  }

  // PID 库内部会依据设定值、当前值和参数自动更新 outputPwm。
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
  // 映射到约 -45 到 +45 度，便于模拟摆杆围绕直立位置的小范围摆动。
  return map(raw, 0, 1023, -450, 450) / 10.0;
}

void applyCartOutput(int pwm) {
  // 正负号决定小车运动方向，绝对值决定输出强度。
  bool forward = pwm >= 0;
  int output = constrain(abs(pwm), 0, 255);

  // 倒立摆本质上是“靠底座去追摆杆”，因此方向输出要和角度误差严格对应。
  digitalWrite(PIN_CART_IN1, forward ? HIGH : LOW);
  digitalWrite(PIN_CART_IN2, forward ? LOW : HIGH);
  analogWrite(PIN_CART_PWM, output);
}

void stopCart() {
  // 安全停机单独抽成函数，便于后续加入报警、刹车或状态灯等逻辑。
  digitalWrite(PIN_CART_IN1, LOW);
  digitalWrite(PIN_CART_IN2, LOW);
  analogWrite(PIN_CART_PWM, 0);
}
