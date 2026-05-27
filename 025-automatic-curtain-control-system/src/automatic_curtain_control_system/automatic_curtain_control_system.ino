#include <RTClib.h>
#include <Stepper.h>

// 自动窗帘控制系统：
// 根据 RTC 时间和环境光照控制步进电机开合窗帘，
// 同时提供手动按键控制和限位保护，避免超过机构行程。

const int STEPS_PER_REVOLUTION = 2048;
const int PIN_STEPPER_1 = 8;
const int PIN_STEPPER_2 = 10;
const int PIN_STEPPER_3 = 9;
const int PIN_STEPPER_4 = 11;

const int PIN_LIGHT_SENSOR = A0;
const int PIN_LIMIT_OPEN = 2;
const int PIN_LIMIT_CLOSE = 3;
const int PIN_BTN_TOGGLE = 4;

const int LIGHT_OPEN_THRESHOLD = 700;
const int LIGHT_CLOSE_THRESHOLD = 450;
const int OPEN_HOUR = 7;
const int CLOSE_HOUR = 19;
const int CURTAIN_TRAVEL_STEPS = 1800;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long CHECK_INTERVAL_MS = 500;

Stepper curtainStepper(STEPS_PER_REVOLUTION, PIN_STEPPER_1, PIN_STEPPER_3, PIN_STEPPER_2, PIN_STEPPER_4);
RTC_DS3231 rtc;

// curtainOpen 是软件层对窗帘状态的估计值，真实到位由限位开关保证。
bool curtainOpen = false;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long lastCheckTime = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_CLOSE, INPUT_PULLUP);
  pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);

  curtainStepper.setSpeed(12);

  if (!rtc.begin()) {
    while (true) {
    }
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

void loop() {
  // 手动按钮和自动逻辑并行存在，手动操作优先即时生效。
  handleButton();

  if (millis() - lastCheckTime >= CHECK_INTERVAL_MS) {
    lastCheckTime = millis();
    updateCurtainLogic();
  }
}

void handleButton() {
  // 按键做消抖后在“开”和“关”之间切换窗帘状态。
  int reading = digitalRead(PIN_BTN_TOGGLE);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        if (curtainOpen) {
          closeCurtain();
        } else {
          openCurtain();
        }
      }
    }
  }

  lastButtonReading = reading;
}

void updateCurtainLogic() {
  // 开启条件同时考虑时间和光照，关闭条件则更偏向保守保护。
  DateTime now = rtc.now();
  int lightValue = analogRead(PIN_LIGHT_SENSOR);

  bool shouldOpenByTime = now.hour() >= OPEN_HOUR && now.hour() < CLOSE_HOUR;
  bool shouldOpenByLight = lightValue >= LIGHT_OPEN_THRESHOLD;
  bool shouldCloseByLight = lightValue <= LIGHT_CLOSE_THRESHOLD;

  if (!curtainOpen && shouldOpenByTime && shouldOpenByLight) {
    openCurtain();
  } else if (curtainOpen && (!shouldOpenByTime || shouldCloseByLight || now.hour() >= CLOSE_HOUR)) {
    closeCurtain();
  }
}

void openCurtain() {
  // 开窗帘时逐步走步进电机，并在到达限位时立即停止。
  if (curtainOpen) {
    return;
  }

  for (int i = 0; i < CURTAIN_TRAVEL_STEPS; i++) {
    if (digitalRead(PIN_LIMIT_OPEN) == LOW) {
      break;
    }
    curtainStepper.step(1);
  }

  curtainOpen = true;
  Serial.println("Curtain opened");
}

void closeCurtain() {
  // 关窗帘同样受限位开关保护，避免超过机构行程。
  if (!curtainOpen) {
    return;
  }

  for (int i = 0; i < CURTAIN_TRAVEL_STEPS; i++) {
    if (digitalRead(PIN_LIMIT_CLOSE) == LOW) {
      break;
    }
    curtainStepper.step(-1);
  }

  curtainOpen = false;
  Serial.println("Curtain closed");
}
