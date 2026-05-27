#include <Servo.h>
#include <string.h>

// 手势识别控制系统：
// PC / 树莓派端负责摄像头采集和手势识别，Arduino 端负责把识别到的手势映射为外设动作。
// 当前版本同时演示双轴舵机控制、照明控制和状态提示三类执行动作。

const int PIN_SERVO_PAN = 9;
const int PIN_SERVO_TILT = 10;
const int PIN_LED_GREEN = 5;
const int PIN_LED_RED = 6;
const int PIN_RELAY = 8;

const int MIN_CONFIDENCE = 70;
// 超过该时间没有新手势时，仅恢复状态灯，不强行改变已执行的设备状态。
const unsigned long COMMAND_HOLD_MS = 1200;

Servo panServo;
Servo tiltServo;

char serialBuffer[64];
byte serialIndex = 0;

char lastGesture[20] = "NONE";
int lastConfidence = 0;
int panAngle = 90;
int tiltAngle = 90;
bool relayOn = false;
unsigned long lastCommandTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);

  panServo.attach(PIN_SERVO_PAN);
  tiltServo.attach(PIN_SERVO_TILT);

  panServo.write(panAngle);
  tiltServo.write(tiltAngle);
  updateIndicators();
}

void loop() {
  // 主循环尽量保持轻量，视觉推理在上位机完成，Arduino 只做动作映射和状态保持。
  readGestureFrames();
  refreshIdleState();
}

void readGestureFrames() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseGestureFrame(serialBuffer);
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

void parseGestureFrame(char *frame) {
  // 串口协议：
  // G,gesture,confidence
  // 例如：G,LEFT,84
  if (strncmp(frame, "G,", 2) != 0) {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  strncpy(lastGesture, token, sizeof(lastGesture) - 1);
  lastGesture[sizeof(lastGesture) - 1] = '\0';

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  lastConfidence = atoi(token);

  // 低置信度结果直接丢弃，避免边界手势或识别抖动导致设备频繁误动作。
  if (lastConfidence < MIN_CONFIDENCE) {
    return;
  }

  executeGesture(lastGesture);
  lastCommandTime = millis();

  Serial.print("ACK,");
  Serial.print(lastGesture);
  Serial.print(",");
  Serial.println(lastConfidence);
}

void executeGesture(const char *gesture) {
  // 每个手势都对应一类明确动作，避免把视觉结果仅仅停留在串口打印层面。
  if (strcmp(gesture, "LEFT") == 0) {
    panAngle = constrain(panAngle - 12, 15, 165);
  } else if (strcmp(gesture, "RIGHT") == 0) {
    panAngle = constrain(panAngle + 12, 15, 165);
  } else if (strcmp(gesture, "UP") == 0) {
    tiltAngle = constrain(tiltAngle - 10, 30, 150);
  } else if (strcmp(gesture, "DOWN") == 0) {
    tiltAngle = constrain(tiltAngle + 10, 30, 150);
  } else if (strcmp(gesture, "OPEN") == 0) {
    relayOn = true;
  } else if (strcmp(gesture, "CLOSE") == 0) {
    relayOn = false;
  } else if (strcmp(gesture, "STOP") == 0) {
    panAngle = 90;
    tiltAngle = 90;
  }

  panServo.write(panAngle);
  tiltServo.write(tiltAngle);
  // 继电器状态与姿态控制分离，意味着用户既能控制云台，也能控制一个独立负载。
  digitalWrite(PIN_RELAY, relayOn ? HIGH : LOW);
  updateIndicators();
}

void refreshIdleState() {
  // 若长时间没有收到新手势，则恢复到“等待识别”状态指示，但不强制改变当前外设位置。
  if (millis() - lastCommandTime > COMMAND_HOLD_MS) {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
  }
}

void updateIndicators() {
  // 绿色表示最近收到过有效手势，红色由空闲超时逻辑接管。
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, LOW);
}
