#include <Servo.h>

// 自动循迹搬运机器人：
// 通过 5 路循迹传感器沿地面黑线移动，在识别到站点标记后执行夹取或放置动作。
// 该实现重点演示“循迹 + 站点识别 + 夹爪状态机”的完整流程，而不是单独的模块演示。

const int PIN_SENSOR_0 = A0;
const int PIN_SENSOR_1 = A1;
const int PIN_SENSOR_2 = A2;
const int PIN_SENSOR_3 = A3;
const int PIN_SENSOR_4 = A4;

const int PIN_LEFT_PWM = 5;
const int PIN_LEFT_IN1 = 6;
const int PIN_LEFT_IN2 = 7;
const int PIN_RIGHT_PWM = 9;
const int PIN_RIGHT_IN1 = 10;
const int PIN_RIGHT_IN2 = 11;

const int PIN_SERVO_LIFT = 3;
const int PIN_SERVO_GRIP = 4;

const int LINE_THRESHOLD = 500;
const int BASE_SPEED = 145;
const int TURN_GAIN = 28;
const int PICKUP_STATION_INDEX = 1;
const int DROPOFF_STATION_INDEX = 3;
const unsigned long MARKER_COOLDOWN_MS = 900;

Servo liftServo;
Servo gripServo;

enum RobotState {
  FOLLOW_TO_PICKUP,
  PICKUP_OBJECT,
  FOLLOW_TO_DROPOFF,
  DROP_OBJECT,
  MISSION_COMPLETE
};

RobotState robotState = FOLLOW_TO_PICKUP;
int stationCount = 0;
bool carryingObject = false;
bool lastMarkerState = false;
unsigned long lastMarkerTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LEFT_PWM, OUTPUT);
  pinMode(PIN_LEFT_IN1, OUTPUT);
  pinMode(PIN_LEFT_IN2, OUTPUT);
  pinMode(PIN_RIGHT_PWM, OUTPUT);
  pinMode(PIN_RIGHT_IN1, OUTPUT);
  pinMode(PIN_RIGHT_IN2, OUTPUT);

  liftServo.attach(PIN_SERVO_LIFT);
  gripServo.attach(PIN_SERVO_GRIP);

  moveLiftToCarryPose();
  openGripper();
  stopCar();
}

void loop() {
  int sensors[5];
  readSensors(sensors);

  bool markerDetected = isStationMarker(sensors);
  handleMarker(markerDetected);

  switch (robotState) {
    case FOLLOW_TO_PICKUP:
      followLine(sensors);
      if (stationCount >= PICKUP_STATION_INDEX) {
        stopCar();
        robotState = PICKUP_OBJECT;
      }
      break;

    case PICKUP_OBJECT:
      performPickupSequence();
      carryingObject = true;
      robotState = FOLLOW_TO_DROPOFF;
      break;

    case FOLLOW_TO_DROPOFF:
      followLine(sensors);
      if (stationCount >= DROPOFF_STATION_INDEX) {
        stopCar();
        robotState = DROP_OBJECT;
      }
      break;

    case DROP_OBJECT:
      performDropoffSequence();
      carryingObject = false;
      robotState = MISSION_COMPLETE;
      break;

    case MISSION_COMPLETE:
      stopCar();
      break;
  }

  printStatus(sensors, markerDetected);
}

void readSensors(int sensors[5]) {
  sensors[0] = analogRead(PIN_SENSOR_0) < LINE_THRESHOLD ? 1 : 0;
  sensors[1] = analogRead(PIN_SENSOR_1) < LINE_THRESHOLD ? 1 : 0;
  sensors[2] = analogRead(PIN_SENSOR_2) < LINE_THRESHOLD ? 1 : 0;
  sensors[3] = analogRead(PIN_SENSOR_3) < LINE_THRESHOLD ? 1 : 0;
  sensors[4] = analogRead(PIN_SENSOR_4) < LINE_THRESHOLD ? 1 : 0;
}

bool isStationMarker(const int sensors[5]) {
  // 这里把“五路同时压线”视为站点标记。
  // 实物赛道上通常会在停靠区做一块较宽的黑色横条，便于与普通弯道区分。
  return sensors[0] && sensors[1] && sensors[2] && sensors[3] && sensors[4];
}

void handleMarker(bool markerDetected) {
  // 站点计数只在“无标记 -> 有标记”的上升沿加一，并加冷却时间避免停在站点时反复累加。
  if (markerDetected && !lastMarkerState && millis() - lastMarkerTime > MARKER_COOLDOWN_MS) {
    stationCount++;
    lastMarkerTime = millis();
  }
  lastMarkerState = markerDetected;
}

void followLine(const int sensors[5]) {
  const int weights[5] = {-2, -1, 0, 1, 2};
  int activeCount = 0;
  int weightedSum = 0;

  for (int i = 0; i < 5; ++i) {
    if (sensors[i]) {
      weightedSum += weights[i];
      activeCount++;
    }
  }

  // 所有传感器都离线时无法继续可靠导航，直接停车等待人工干预或重新放置。
  if (activeCount == 0) {
    stopCar();
    return;
  }

  float error = static_cast<float>(weightedSum) / activeCount;
  int leftSpeed = BASE_SPEED + static_cast<int>(error * TURN_GAIN);
  int rightSpeed = BASE_SPEED - static_cast<int>(error * TURN_GAIN);

  setMotor(true, constrain(leftSpeed, 0, 255), true, constrain(rightSpeed, 0, 255));
}

void performPickupSequence() {
  // 夹取流程拆成“张开夹爪 -> 下降 -> 闭合 -> 抬升”，便于单独调节每一步姿态。
  openGripper();
  delay(250);
  moveLiftToPickupPose();
  delay(450);
  closeGripper();
  delay(350);
  moveLiftToCarryPose();
  delay(450);
}

void performDropoffSequence() {
  // 放置流程先把载荷放低再松爪，避免物体从高处掉落导致姿态偏移。
  moveLiftToDropPose();
  delay(450);
  openGripper();
  delay(300);
  moveLiftToCarryPose();
  delay(450);
}

void moveLiftToPickupPose() {
  liftServo.write(138);
}

void moveLiftToDropPose() {
  liftServo.write(132);
}

void moveLiftToCarryPose() {
  liftServo.write(92);
}

void openGripper() {
  gripServo.write(88);
}

void closeGripper() {
  gripServo.write(42);
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

void printStatus(const int sensors[5], bool markerDetected) {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < 200) {
    return;
  }
  lastPrint = millis();

  Serial.print("State:");
  Serial.print(robotState);
  Serial.print(",Station:");
  Serial.print(stationCount);
  Serial.print(",Carry:");
  Serial.print(carryingObject ? 1 : 0);
  Serial.print(",Marker:");
  Serial.print(markerDetected ? 1 : 0);
  Serial.print(",Sensors:");
  for (int i = 0; i < 5; ++i) {
    Serial.print(sensors[i]);
  }
  Serial.println();
}
