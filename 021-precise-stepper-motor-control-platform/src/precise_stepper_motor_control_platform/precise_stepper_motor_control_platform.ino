#include <AccelStepper.h>

// 步进电机精确控制平台：
// 使用 A4988/DRV8825 类驱动器实现角度、速度和方向控制，
// 并通过限位开关完成上电回零，保证后续位置控制有确定参考点。

const int PIN_STEP = 2;
const int PIN_DIR = 3;
const int PIN_ENABLE = 4;
const int PIN_LIMIT_HOME = 5;
const int PIN_BTN_HOME = 6;
const int PIN_BTN_TARGET = 7;
const int PIN_BTN_SPEED = 8;

const float STEPS_PER_REVOLUTION = 200.0f * 16.0f;
const float DEFAULT_TARGET_ANGLE = 90.0f;
const float MAX_ANGLE = 270.0f;
const float MIN_ANGLE = 0.0f;

const unsigned long DEBOUNCE_MS = 40;
const unsigned long HOME_TIMEOUT_MS = 10000;

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

// homed 表示是否已建立零点参考，targetAngle 是当前目标角度。
bool homed = false;
float targetAngle = DEFAULT_TARGET_ANGLE;
float maxSpeedSteps = 1200.0f;
float accelerationSteps = 800.0f;

const int BUTTON_COUNT = 3;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_BTN_HOME,
  PIN_BTN_TARGET,
  PIN_BTN_SPEED
};

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];

void setup() {
  Serial.begin(115200);

  pinMode(PIN_ENABLE, OUTPUT);
  pinMode(PIN_LIMIT_HOME, INPUT_PULLUP);

  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  digitalWrite(PIN_ENABLE, LOW);
  stepper.setMaxSpeed(maxSpeedSteps);
  stepper.setAcceleration(accelerationSteps);

  homeAxis();
}

void loop() {
  // 按键触发本地操作，串口触发上位机操作，最终都汇总到同一运动控制接口。
  handleButtons();
  stepper.run();
  handleSerialCommand();
}

void handleButtons() {
  // 本地按键分别对应回零、切换目标角度和切换速度档位。
  if (buttonPressed(0)) {
    homeAxis();
  }

  if (buttonPressed(1) && homed) {
    targetAngle += 45.0f;
    if (targetAngle > MAX_ANGLE) {
      targetAngle = MIN_ANGLE;
    }
    moveToAngle(targetAngle);
  }

  if (buttonPressed(2)) {
    cycleSpeed();
  }
}

bool buttonPressed(int index) {
  bool pressed = false;
  int reading = digitalRead(BUTTON_PINS[index]);

  if (reading != lastButtonReading[index]) {
    lastDebounceTime[index] = millis();
  }

  if (millis() - lastDebounceTime[index] > DEBOUNCE_MS) {
    if (reading != stableButtonState[index]) {
      stableButtonState[index] = reading;
      if (stableButtonState[index] == LOW) {
        pressed = true;
      }
    }
  }

  lastButtonReading[index] = reading;
  return pressed;
}

void homeAxis() {
  Serial.println("Homing start");
  stepper.setMaxSpeed(600.0f);
  stepper.setAcceleration(400.0f);
  unsigned long homeStartTime = millis();

  // 先朝回零方向移动，直到碰到限位开关。
  while (digitalRead(PIN_LIMIT_HOME) == HIGH) {
    if (millis() - homeStartTime >= HOME_TIMEOUT_MS) {
      Serial.println("Homing timeout");
      homed = false;
      return;
    }
    stepper.moveTo(stepper.currentPosition() - 10);
    stepper.run();
  }

  // 触发限位后将当前位置设为零点。
  stepper.stop();
  stepper.setCurrentPosition(0);
  homed = true;

  stepper.setMaxSpeed(maxSpeedSteps);
  stepper.setAcceleration(accelerationSteps);
  moveToAngle(DEFAULT_TARGET_ANGLE);
  Serial.println("Homing done");
}

void moveToAngle(float angle) {
  // 把角度统一换算成细分步数，确保按钮控制和串口控制都共用同一标定关系。
  angle = constrain(angle, MIN_ANGLE, MAX_ANGLE);
  targetAngle = angle;
  long targetSteps = static_cast<long>(targetAngle / 360.0f * STEPS_PER_REVOLUTION);
  stepper.moveTo(targetSteps);

  Serial.print("Move to angle: ");
  Serial.println(targetAngle);
}

void cycleSpeed() {
  // 用少量固定档位方便现场演示，不需要每次都走串口调参。
  if (maxSpeedSteps < 1200.0f) {
    maxSpeedSteps = 1200.0f;
  } else if (maxSpeedSteps < 2000.0f) {
    maxSpeedSteps = 2000.0f;
  } else {
    maxSpeedSteps = 800.0f;
  }

  stepper.setMaxSpeed(maxSpeedSteps);
  Serial.print("Max speed steps/s: ");
  Serial.println(maxSpeedSteps);
}

void handleSerialCommand() {
  // 串口命令提供 HOME、ANGLE、SPEED 三类基础控制接口。
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line == "HOME") {
    homeAxis();
    return;
  }

  if (line.startsWith("ANGLE ")) {
    float angle = line.substring(6).toFloat();
    if (homed) {
      moveToAngle(angle);
    }
    return;
  }

  if (line.startsWith("SPEED ")) {
    float speed = line.substring(6).toFloat();
    if (speed > 100.0f && speed < 4000.0f) {
      maxSpeedSteps = speed;
      stepper.setMaxSpeed(maxSpeedSteps);
    }
  }
}
