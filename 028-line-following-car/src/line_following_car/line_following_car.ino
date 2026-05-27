// 巡线小车：
// 使用 3 路红外循迹模块检测黑线位置，并按偏差控制左右电机速度。
// 通过简单比例调节实现直线跟随和轻微弯道修正。

const int PIN_SENSOR_LEFT = A0;
const int PIN_SENSOR_CENTER = A1;
const int PIN_SENSOR_RIGHT = A2;

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const int BASE_SPEED = 150;
const int TURN_GAIN = 70;
const int LINE_THRESHOLD = 500;

void setup() {
  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  Serial.begin(115200);
}

void loop() {
  // 三路状态先读入，再统一计算误差并输出差速控制。
  int left = readLineState(PIN_SENSOR_LEFT);
  int center = readLineState(PIN_SENSOR_CENTER);
  int right = readLineState(PIN_SENSOR_RIGHT);

  int error = calculateError(left, center, right);

  if (left == 0 && center == 0 && right == 0) {
    stopCar();
    Serial.println("Line lost");
    return;
  }

  // 偏差越大，左右轮速度差越大，小车转向越明显。
  int leftSpeed = BASE_SPEED - error * TURN_GAIN;
  int rightSpeed = BASE_SPEED + error * TURN_GAIN;

  setMotor(true, constrain(leftSpeed, 0, 255), true, constrain(rightSpeed, 0, 255));

  Serial.print("L:");
  Serial.print(left);
  Serial.print(" C:");
  Serial.print(center);
  Serial.print(" R:");
  Serial.print(right);
  Serial.print(" Err:");
  Serial.println(error);
}

int readLineState(int pin) {
  // 这里假设黑线反射更弱，因此模拟值低于阈值时认为压在线上。
  return analogRead(pin) < LINE_THRESHOLD ? 1 : 0;
}

int calculateError(int left, int center, int right) {
  if (center && !left && !right) {
    return 0;
  }
  if (left && !center) {
    return -1;
  }
  if (right && !center) {
    return 1;
  }
  if (left && center) {
    return -1;
  }
  if (right && center) {
    return 1;
  }
  return 0;
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
  // 丢线或异常时统一通过这里停车。
  digitalWrite(PIN_LEFT_IN1, LOW);
  digitalWrite(PIN_LEFT_IN2, LOW);
  digitalWrite(PIN_RIGHT_IN1, LOW);
  digitalWrite(PIN_RIGHT_IN2, LOW);
  analogWrite(PIN_LEFT_PWM, 0);
  analogWrite(PIN_RIGHT_PWM, 0);
}
