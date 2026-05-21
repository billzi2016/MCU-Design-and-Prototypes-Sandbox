#include <TM1637Display.h>

const int PIN_CAR_RED = 2;
const int PIN_CAR_YELLOW = 3;
const int PIN_CAR_GREEN = 4;
const int PIN_PED_RED = 5;
const int PIN_PED_GREEN = 6;
const int PIN_PED_BUTTON = 7;
const int PIN_BUZZER = 8;
const int PIN_TM1637_CLK = 9;
const int PIN_TM1637_DIO = 10;

const unsigned long DEBOUNCE_MS = 40;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned long BUZZER_INTERVAL_MS = 500;

const unsigned int CAR_GREEN_SECONDS = 15;
const unsigned int CAR_YELLOW_SECONDS = 3;
const unsigned int PEDESTRIAN_GREEN_SECONDS = 10;
const unsigned int ALL_RED_SECONDS = 2;

TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);

enum TrafficState {
  STATE_CAR_GREEN,
  STATE_CAR_YELLOW,
  STATE_PEDESTRIAN_GREEN,
  STATE_ALL_RED
};

TrafficState currentState = STATE_CAR_GREEN;
unsigned long stateStartTime = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastBuzzerToggle = 0;

bool pedestrianRequest = false;
bool buzzerState = false;
int stableButtonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;

void setup() {
  pinMode(PIN_CAR_RED, OUTPUT);
  pinMode(PIN_CAR_YELLOW, OUTPUT);
  pinMode(PIN_CAR_GREEN, OUTPUT);
  pinMode(PIN_PED_RED, OUTPUT);
  pinMode(PIN_PED_GREEN, OUTPUT);
  pinMode(PIN_PED_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  display.setBrightness(5);

  enterState(STATE_CAR_GREEN);
}

void loop() {
  handlePedestrianButton();
  updateTrafficState();
  updateBuzzer();

  // 定期刷新倒计时显示，避免频繁写入数码管。
  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    refreshDisplay();
    lastDisplayRefresh = millis();
  }
}

void handlePedestrianButton() {
  int reading = digitalRead(PIN_PED_BUTTON);

  // 如果读数发生变化，重新开始消抖计时。
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  // 读数保持稳定超过消抖时间后，才认为状态有效。
  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      // INPUT_PULLUP 下，LOW 表示按键被按下。
      if (stableButtonState == LOW) {
        pedestrianRequest = true;
      }
    }
  }

  lastButtonReading = reading;
}

void updateTrafficState() {
  unsigned int elapsedSeconds = elapsedStateSeconds();
  unsigned int duration = currentStateDuration();

  if (elapsedSeconds < duration) {
    return;
  }

  if (currentState == STATE_CAR_GREEN) {
    enterState(STATE_CAR_YELLOW);
  } else if (currentState == STATE_CAR_YELLOW) {
    if (pedestrianRequest) {
      pedestrianRequest = false;
      enterState(STATE_PEDESTRIAN_GREEN);
    } else {
      enterState(STATE_ALL_RED);
    }
  } else if (currentState == STATE_PEDESTRIAN_GREEN) {
    enterState(STATE_ALL_RED);
  } else if (currentState == STATE_ALL_RED) {
    enterState(STATE_CAR_GREEN);
  }
}

void enterState(TrafficState nextState) {
  currentState = nextState;
  stateStartTime = millis();
  buzzerState = false;
  digitalWrite(PIN_BUZZER, LOW);

  // 进入新状态时统一更新灯光，避免出现相位冲突。
  if (currentState == STATE_CAR_GREEN) {
    setLights(false, false, true, true, false);
  } else if (currentState == STATE_CAR_YELLOW) {
    setLights(false, true, false, true, false);
  } else if (currentState == STATE_PEDESTRIAN_GREEN) {
    setLights(true, false, false, false, true);
  } else if (currentState == STATE_ALL_RED) {
    setLights(true, false, false, true, false);
  }
}

void setLights(bool carRed, bool carYellow, bool carGreen, bool pedRed, bool pedGreen) {
  digitalWrite(PIN_CAR_RED, carRed ? HIGH : LOW);
  digitalWrite(PIN_CAR_YELLOW, carYellow ? HIGH : LOW);
  digitalWrite(PIN_CAR_GREEN, carGreen ? HIGH : LOW);
  digitalWrite(PIN_PED_RED, pedRed ? HIGH : LOW);
  digitalWrite(PIN_PED_GREEN, pedGreen ? HIGH : LOW);
}

void updateBuzzer() {
  if (currentState != STATE_PEDESTRIAN_GREEN) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerState = false;
    return;
  }

  // 人行绿灯期间蜂鸣器间歇响，提示正在通行。
  if (millis() - lastBuzzerToggle >= BUZZER_INTERVAL_MS) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void refreshDisplay() {
  unsigned int remaining = remainingStateSeconds();
  display.showNumberDec(remaining, true);
}

unsigned int elapsedStateSeconds() {
  return (millis() - stateStartTime) / 1000;
}

unsigned int remainingStateSeconds() {
  unsigned int elapsed = elapsedStateSeconds();
  unsigned int duration = currentStateDuration();

  if (elapsed >= duration) {
    return 0;
  }

  return duration - elapsed;
}

unsigned int currentStateDuration() {
  if (currentState == STATE_CAR_GREEN) {
    return CAR_GREEN_SECONDS;
  }
  if (currentState == STATE_CAR_YELLOW) {
    return CAR_YELLOW_SECONDS;
  }
  if (currentState == STATE_PEDESTRIAN_GREEN) {
    return PEDESTRIAN_GREEN_SECONDS;
  }
  return ALL_RED_SECONDS;
}

