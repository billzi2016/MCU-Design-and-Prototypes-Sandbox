#include <Servo.h>
#include <string.h>

// OpenCV 实时颜色识别与目标跟踪：
// PC / 树莓派端负责摄像头采集、HSV 颜色分割和目标中心计算，
// Arduino 端负责接收串口目标坐标并驱动双轴舵机云台跟踪目标。

const int PIN_SERVO_PAN = 9;
const int PIN_SERVO_TILT = 10;
const int PIN_STATUS_LED = 13;

const int FRAME_CENTER_X = 160;
const int FRAME_CENTER_Y = 120;
const int DEADZONE_X = 12;
const int DEADZONE_Y = 10;
const int SEARCH_STEP = 2;
const unsigned long TARGET_TIMEOUT_MS = 600;

Servo panServo;
Servo tiltServo;

int panAngle = 90;
int tiltAngle = 90;
int searchDirection = 1;
unsigned long lastTargetTime = 0;

char serialBuffer[48];
byte serialIndex = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_STATUS_LED, OUTPUT);

  panServo.attach(PIN_SERVO_PAN);
  tiltServo.attach(PIN_SERVO_TILT);
  panServo.write(panAngle);
  tiltServo.write(tiltAngle);
}

void loop() {
  readSerialFrame();

  // 若长时间收不到有效目标，云台进入水平扫描模式，便于重新把目标找回来。
  if (millis() - lastTargetTime > TARGET_TIMEOUT_MS) {
    digitalWrite(PIN_STATUS_LED, LOW);
    runSearchPattern();
  }
}

void readSerialFrame() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseTrackingFrame(serialBuffer);
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

void parseTrackingFrame(char *frame) {
  // 串口协议：
  // T,found,cx,cy
  // 例如：T,1,145,118 表示找到了目标，中心点位于 (145,118)
  if (frame[0] != 'T' || frame[1] != ',') {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int found = atoi(token);

  if (!found) {
    return;
  }

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int centerX = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  int centerY = atoi(token);

  updateTracking(centerX, centerY);
}

void updateTracking(int centerX, int centerY) {
  int errorX = centerX - FRAME_CENTER_X;
  int errorY = centerY - FRAME_CENTER_Y;

  // x 轴误差控制水平旋转，y 轴误差控制俯仰角。
  // 只在误差超过死区时调整，避免目标在中心附近时云台抖动。
  if (abs(errorX) > DEADZONE_X) {
    panAngle -= errorX / 20;
  }
  if (abs(errorY) > DEADZONE_Y) {
    tiltAngle += errorY / 20;
  }

  panAngle = constrain(panAngle, 10, 170);
  tiltAngle = constrain(tiltAngle, 30, 150);

  panServo.write(panAngle);
  tiltServo.write(tiltAngle);

  lastTargetTime = millis();
  digitalWrite(PIN_STATUS_LED, HIGH);

  Serial.print("TRACK,");
  Serial.print(centerX);
  Serial.print(",");
  Serial.print(centerY);
  Serial.print(",");
  Serial.print(panAngle);
  Serial.print(",");
  Serial.println(tiltAngle);
}

void runSearchPattern() {
  static unsigned long lastSearchMove = 0;
  if (millis() - lastSearchMove < 35) {
    return;
  }
  lastSearchMove = millis();

  panAngle += SEARCH_STEP * searchDirection;
  if (panAngle >= 165 || panAngle <= 15) {
    searchDirection *= -1;
    panAngle = constrain(panAngle, 15, 165);
  }

  panServo.write(panAngle);
  tiltServo.write(90);
}
