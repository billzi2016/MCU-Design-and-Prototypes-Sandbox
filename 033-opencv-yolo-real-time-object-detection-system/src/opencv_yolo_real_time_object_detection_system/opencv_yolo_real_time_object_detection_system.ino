#include <Servo.h>
#include <string.h>

// OpenCV + YOLO 实时目标检测系统：
// 上位机端负责视频采集和 YOLO 检测，Arduino 端负责接收检测结果，
// 并根据类别执行蜂鸣提示、继电器动作和指向舵机联动。

const int PIN_BUZZER = 3;
const int PIN_STATUS_LED = 4;
const int PIN_RELAY = 5;
const int PIN_POINTER_SERVO = 9;

const int FRAME_WIDTH = 640;
const int CONFIDENCE_THRESHOLD = 60;
const unsigned long DETECTION_HOLD_MS = 1200;

Servo pointerServo;

char serialBuffer[96];
byte serialIndex = 0;

char currentLabel[24] = "";
int currentConfidence = 0;
int currentCenterX = FRAME_WIDTH / 2;
bool hasActiveDetection = false;
unsigned long lastDetectionTime = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_STATUS_LED, LOW);
  digitalWrite(PIN_RELAY, LOW);

  pointerServo.attach(PIN_POINTER_SERVO);
  pointerServo.write(90);
}

void loop() {
  readSerialFrame();
  updateOutputs();
}

void readSerialFrame() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseDetectionFrame(serialBuffer);
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

void parseDetectionFrame(char *frame) {
  // 串口协议：
  // D,label,confidence,centerX
  // 例如：D,person,86,420
  if (strcmp(frame, "CLEAR") == 0) {
    clearDetection();
    return;
  }

  if (frame[0] != 'D' || frame[1] != ',') {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  strncpy(currentLabel, token, sizeof(currentLabel) - 1);
  currentLabel[sizeof(currentLabel) - 1] = '\0';

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  currentConfidence = atoi(token);

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  currentCenterX = atoi(token);

  if (currentConfidence < CONFIDENCE_THRESHOLD) {
    return;
  }

  hasActiveDetection = true;
  lastDetectionTime = millis();

  Serial.print("DETECTED,");
  Serial.print(currentLabel);
  Serial.print(",");
  Serial.print(currentConfidence);
  Serial.print(",");
  Serial.println(currentCenterX);
}

void updateOutputs() {
  if (hasActiveDetection && millis() - lastDetectionTime > DETECTION_HOLD_MS) {
    clearDetection();
  }

  if (!hasActiveDetection) {
    digitalWrite(PIN_STATUS_LED, LOW);
    digitalWrite(PIN_RELAY, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    pointerServo.write(90);
    return;
  }

  digitalWrite(PIN_STATUS_LED, HIGH);

  // 舵机根据检测框中心位置转向，方便做“指向式”联动演示。
  int pointerAngle = map(constrain(currentCenterX, 0, FRAME_WIDTH), 0, FRAME_WIDTH, 20, 160);
  pointerServo.write(pointerAngle);

  // 不同类别用不同联动策略，避免所有目标都使用同一套动作。
  if (strcmp(currentLabel, "fire") == 0 || strcmp(currentLabel, "smoke") == 0) {
    digitalWrite(PIN_RELAY, HIGH);
    beepPattern(120);
  } else if (strcmp(currentLabel, "person") == 0) {
    digitalWrite(PIN_RELAY, LOW);
    beepPattern(350);
  } else {
    digitalWrite(PIN_RELAY, LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void beepPattern(unsigned long intervalMs) {
  if (millis() - lastBuzzerToggle < intervalMs) {
    return;
  }
  lastBuzzerToggle = millis();
  buzzerState = !buzzerState;
  digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
}

void clearDetection() {
  hasActiveDetection = false;
  currentLabel[0] = '\0';
  currentConfidence = 0;
  buzzerState = false;
}
