#include <Servo.h>
#include <string.h>

// 视觉引导机械臂抓取系统：
// 上位机端负责目标识别、像素坐标到抓取位姿的转换，
// Arduino 端负责接收抓取指令并执行“预抓取 - 下探 - 夹取 - 放置 - 复位”动作序列。

const int PIN_SERVO_BASE = 3;
const int PIN_SERVO_SHOULDER = 5;
const int PIN_SERVO_ELBOW = 6;
const int PIN_SERVO_GRIP = 9;

const int HOME_BASE = 90;
const int HOME_SHOULDER = 82;
const int HOME_ELBOW = 78;
const int OPEN_GRIP_ANGLE = 88;
const int CLOSE_GRIP_ANGLE = 42;

Servo servoBase;
Servo servoShoulder;
Servo servoElbow;
Servo servoGrip;

char serialBuffer[80];
byte serialIndex = 0;
bool busyExecuting = false;

void setup() {
  Serial.begin(115200);

  servoBase.attach(PIN_SERVO_BASE);
  servoShoulder.attach(PIN_SERVO_SHOULDER);
  servoElbow.attach(PIN_SERVO_ELBOW);
  servoGrip.attach(PIN_SERVO_GRIP);

  moveToHome();
}

void loop() {
  readSerialFrame();
}

void readSerialFrame() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseCommand(serialBuffer);
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

void parseCommand(char *command) {
  if (busyExecuting) {
    Serial.println("BUSY");
    return;
  }

  if (strcmp(command, "HOME") == 0) {
    moveToHome();
    Serial.println("ACK,HOME");
    return;
  }

  // 串口协议：
  // G,base,shoulder,elbow,slot
  // 例如：G,102,118,134,2
  if (command[0] != 'G' || command[1] != ',') {
    return;
  }

  char *token = strtok(command, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int baseAngle = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int shoulderAngle = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int elbowAngle = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int dropSlot = atoi(token);

  executeGrasp(baseAngle, shoulderAngle, elbowAngle, dropSlot);
}

void executeGrasp(int baseAngle, int shoulderAngle, int elbowAngle, int dropSlot) {
  busyExecuting = true;

  // 上位机已完成坐标换算，这里只负责按安全范围执行对应姿态。
  baseAngle = constrain(baseAngle, 20, 160);
  shoulderAngle = constrain(shoulderAngle, 65, 150);
  elbowAngle = constrain(elbowAngle, 70, 160);

  Serial.println("ACK,START");

  servoGrip.write(OPEN_GRIP_ANGLE);
  delay(250);

  // 先到预抓取位，避免底座和前臂直接高速切入目标导致碰撞。
  servoBase.write(baseAngle);
  servoShoulder.write(max(65, shoulderAngle - 18));
  servoElbow.write(max(75, elbowAngle - 20));
  delay(500);

  servoShoulder.write(shoulderAngle);
  servoElbow.write(elbowAngle);
  delay(500);

  servoGrip.write(CLOSE_GRIP_ANGLE);
  delay(350);

  // 夹紧后先抬起再旋转去投放区，减少拖拽物体的风险。
  servoShoulder.write(max(65, shoulderAngle - 25));
  servoElbow.write(max(75, elbowAngle - 30));
  delay(450);

  moveToDropSlot(dropSlot);
  servoGrip.write(OPEN_GRIP_ANGLE);
  delay(350);

  moveToHome();
  Serial.println("ACK,DONE");
  busyExecuting = false;
}

void moveToDropSlot(int dropSlot) {
  int dropBase = 90;

  if (dropSlot == 1) {
    dropBase = 45;
  } else if (dropSlot == 2) {
    dropBase = 90;
  } else if (dropSlot == 3) {
    dropBase = 135;
  }

  servoBase.write(dropBase);
  servoShoulder.write(92);
  servoElbow.write(104);
  delay(550);
}

void moveToHome() {
  // 机械臂复位单独封装，便于每次动作结束和异常恢复时统一回到安全姿态。
  servoBase.write(HOME_BASE);
  servoShoulder.write(HOME_SHOULDER);
  servoElbow.write(HOME_ELBOW);
  servoGrip.write(OPEN_GRIP_ANGLE);
  delay(500);
}
