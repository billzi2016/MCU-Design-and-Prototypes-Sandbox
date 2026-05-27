#include <Servo.h>

// 多自由度机械臂控制系统：
// 通过串口命令控制底座、肩部、肘部和夹爪四路舵机，
// 支持动作录制与回放，适合作为后续上位机或摇杆控制的基础平台。

const int SERVO_COUNT = 4;
const int SERVO_PINS[SERVO_COUNT] = {3, 5, 6, 9};
const int SERVO_MIN[SERVO_COUNT] = {10, 20, 15, 20};
const int SERVO_MAX[SERVO_COUNT] = {170, 160, 165, 100};
const char* SERVO_NAMES[SERVO_COUNT] = {"BASE", "SHOULDER", "ELBOW", "GRIP"};

const int MAX_RECORDED_POSES = 12;
const unsigned long PLAYBACK_DELAY_MS = 800;

Servo servos[SERVO_COUNT];
int currentAngles[SERVO_COUNT] = {90, 90, 90, 40};
int recordedPoses[MAX_RECORDED_POSES][SERVO_COUNT];
int recordedPoseCount = 0;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < SERVO_COUNT; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].write(currentAngles[i]);
  }

  Serial.println("Robot arm ready");
  Serial.println("Commands: SET idx angle | SAVE | PLAY | RESET | STATUS");
}

void loop() {
  // 所有控制都通过串口命令完成，适合作为上位机或摇杆系统的下层执行器。
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  if (line == "SAVE") {
    saveCurrentPose();
  } else if (line == "PLAY") {
    playbackPoses();
  } else if (line == "RESET") {
    resetArm();
  } else if (line == "STATUS") {
    printStatus();
  } else if (line.startsWith("SET ")) {
    parseSetCommand(line);
  }
}

void parseSetCommand(const String& line) {
  // SET idx angle 命令直接映射到单关节角度控制。
  int firstSpace = line.indexOf(' ');
  int secondSpace = line.indexOf(' ', firstSpace + 1);
  if (secondSpace < 0) {
    return;
  }

  int servoIndex = line.substring(firstSpace + 1, secondSpace).toInt();
  int targetAngle = line.substring(secondSpace + 1).toInt();

  if (servoIndex < 0 || servoIndex >= SERVO_COUNT) {
    return;
  }

  setServoAngle(servoIndex, targetAngle);
}

void setServoAngle(int index, int angle) {
  // 每个关节都受自己的机械限位约束，避免录制动作时把舵机顶死。
  currentAngles[index] = constrain(angle, SERVO_MIN[index], SERVO_MAX[index]);
  servos[index].write(currentAngles[index]);

  Serial.print(SERVO_NAMES[index]);
  Serial.print(" -> ");
  Serial.println(currentAngles[index]);
}

void saveCurrentPose() {
  // 录制时把当前所有关节角度作为一帧动作保存。
  if (recordedPoseCount >= MAX_RECORDED_POSES) {
    Serial.println("Pose buffer full");
    return;
  }

  for (int i = 0; i < SERVO_COUNT; i++) {
    recordedPoses[recordedPoseCount][i] = currentAngles[i];
  }

  recordedPoseCount++;
  Serial.print("Saved pose count: ");
  Serial.println(recordedPoseCount);
}

void playbackPoses() {
  // 回放按保存顺序逐帧执行，是最基础的动作录制 / 回放流程。
  if (recordedPoseCount == 0) {
    Serial.println("No pose saved");
    return;
  }

  for (int poseIndex = 0; poseIndex < recordedPoseCount; poseIndex++) {
    for (int servoIndex = 0; servoIndex < SERVO_COUNT; servoIndex++) {
      setServoAngle(servoIndex, recordedPoses[poseIndex][servoIndex]);
    }
    delay(PLAYBACK_DELAY_MS);
  }
}

void resetArm() {
  int defaults[SERVO_COUNT] = {90, 90, 90, 40};
  for (int i = 0; i < SERVO_COUNT; i++) {
    setServoAngle(i, defaults[i]);
  }
}

void printStatus() {
  Serial.print("Recorded poses: ");
  Serial.println(recordedPoseCount);
  for (int i = 0; i < SERVO_COUNT; i++) {
    Serial.print(SERVO_NAMES[i]);
    Serial.print(':');
    Serial.println(currentAngles[i]);
  }
}
