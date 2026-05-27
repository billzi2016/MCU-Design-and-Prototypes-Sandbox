#include <MPU6050_tockn.h>
#include <Wire.h>

// 两轮自平衡小车：
// 通过 MPU6050 获取车体俯仰角，使用 PID 计算电机输出。
// 这里重点演示姿态采集、目标角度设定和双电机平衡控制框架。

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const float TARGET_ANGLE = 0.0f;
// 超过该倾角说明车体已经明显失稳，继续加力只会把小车甩翻。
const float SAFE_ANGLE_LIMIT = 25.0f;
// 平衡控制需要高频稳定执行，因此这里固定为 10ms 控制周期。
const unsigned long CONTROL_INTERVAL_MS = 10;

MPU6050 mpu6050(Wire);

// 三个 PID 参数作用于俯仰角误差，构成最基础的平衡控制器。
// 真机调试时通常先调 kp 让车体“愿意扶正”，再逐步加入 kd 抑制振荡，最后少量加入 ki 修正静差。
float kp = 26.0f;
float ki = 0.9f;
float kd = 0.8f;

// 这些变量分别保存当前误差、积分项、上次误差和最终电机指令，
// 便于在固定控制周期内完成完整的一次 PID 运算。
float angleError = 0.0f;
float angleIntegral = 0.0f;
float lastAngleError = 0.0f;
float motorCommand = 0.0f;
unsigned long lastControlTime = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  mpu6050.begin();
  // 上电先做陀螺仪零偏校准，否则角度积分会很快漂移。
  mpu6050.calcGyroOffsets(true);

  stopMotors();
  lastControlTime = millis();
}

void loop() {
  if (millis() - lastControlTime < CONTROL_INTERVAL_MS) {
    return;
  }

  unsigned long now = millis();
  // dt 由真实经过时间换算得到，避免主循环偶发抖动时把微分项放大失真。
  float dt = static_cast<float>(now - lastControlTime) / 1000.0f;
  lastControlTime = now;

  mpu6050.update();
  float angle = mpu6050.getAngleX();

  // 倾角过大时直接停机，防止继续大力驱动把车体甩翻。
  if (abs(angle) > SAFE_ANGLE_LIMIT) {
    stopMotors();
    angleIntegral = 0.0f;
    Serial.println("Safety stop");
    return;
  }

  // 目标角与当前俯仰角的差值就是“车体还差多少需要扶正”的核心误差。
  angleError = TARGET_ANGLE - angle;
  // 积分项持续累加小偏差，用来抵消轻微重心偏移或电机左右不完全一致的情况。
  angleIntegral += angleError * dt;
  float derivative = (angleError - lastAngleError) / dt;

  // PID 输出统一映射为双电机同向补偿，是最基础的平衡控制框架。
  motorCommand = kp * angleError + ki * angleIntegral + kd * derivative;
  // 平衡车驱动能力有限，输出必须约束在 PWM 有效范围内。
  motorCommand = constrain(motorCommand, -255.0f, 255.0f);
  lastAngleError = angleError;

  setMotorOutput(static_cast<int>(motorCommand));

  Serial.print("Angle:");
  Serial.print(angle);
  Serial.print(",Cmd:");
  Serial.println(motorCommand);
}

void setMotorOutput(int pwm) {
  // 正负号决定前后方向，绝对值决定输出强度。
  bool forward = pwm >= 0;
  int output = abs(pwm);

  // 左右轮保持同方向同幅值，是最基础的俯仰平衡控制形式。
  // 真正可骑行的平衡车还会叠加速度环和转向差速环。
  digitalWrite(PIN_LEFT_IN1, forward ? HIGH : LOW);
  digitalWrite(PIN_LEFT_IN2, forward ? LOW : HIGH);
  digitalWrite(PIN_RIGHT_IN1, forward ? HIGH : LOW);
  digitalWrite(PIN_RIGHT_IN2, forward ? LOW : HIGH);

  analogWrite(PIN_LEFT_PWM, output);
  analogWrite(PIN_RIGHT_PWM, output);
}

void stopMotors() {
  // 停机时同时清零方向和 PWM，避免驱动板残留方向状态导致重新启动时猛冲。
  digitalWrite(PIN_LEFT_IN1, LOW);
  digitalWrite(PIN_LEFT_IN2, LOW);
  digitalWrite(PIN_RIGHT_IN1, LOW);
  digitalWrite(PIN_RIGHT_IN2, LOW);
  analogWrite(PIN_LEFT_PWM, 0);
  analogWrite(PIN_RIGHT_PWM, 0);
}
