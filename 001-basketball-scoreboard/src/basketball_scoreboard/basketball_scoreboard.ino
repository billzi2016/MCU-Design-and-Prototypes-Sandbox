#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int PIN_A_PLUS_1 = 2;
const int PIN_A_PLUS_2 = 3;
const int PIN_A_PLUS_3 = 4;
const int PIN_B_PLUS_1 = 5;
const int PIN_B_PLUS_2 = 6;
const int PIN_B_PLUS_3 = 7;
const int PIN_PERIOD = 8;
const int PIN_TIMER = 9;
const int PIN_RESET = 10;
const int PIN_BUZZER = 11;

const int BUTTON_COUNT = 9;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_A_PLUS_1,
  PIN_A_PLUS_2,
  PIN_A_PLUS_3,
  PIN_B_PLUS_1,
  PIN_B_PLUS_2,
  PIN_B_PLUS_3,
  PIN_PERIOD,
  PIN_TIMER,
  PIN_RESET
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned int INITIAL_SECONDS = 10 * 60;

int scoreA = 0;
int scoreB = 0;
int period = 1;
unsigned int remainingSeconds = INITIAL_SECONDS;
bool timerRunning = false;
bool buzzerPlayed = false;

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];
unsigned long lastTimerTick = 0;
unsigned long lastDisplayRefresh = 0;

void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // 使用内部上拉电阻，按键按下时读数为 LOW。
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  refreshDisplay();
}

void loop() {
  handleButtons();
  updateTimer();

  // 定期刷新显示，避免 LCD 频繁写入。
  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    refreshDisplay();
    lastDisplayRefresh = millis();
  }
}

void handleButtons() {
  if (buttonPressed(0)) {
    addScoreA(1);
  }
  if (buttonPressed(1)) {
    addScoreA(2);
  }
  if (buttonPressed(2)) {
    addScoreA(3);
  }
  if (buttonPressed(3)) {
    addScoreB(1);
  }
  if (buttonPressed(4)) {
    addScoreB(2);
  }
  if (buttonPressed(5)) {
    addScoreB(3);
  }
  if (buttonPressed(6)) {
    nextPeriod();
  }
  if (buttonPressed(7)) {
    toggleTimer();
  }
  if (buttonPressed(8)) {
    resetGame();
  }
}

bool buttonPressed(int index) {
  bool pressed = false;
  int reading = digitalRead(BUTTON_PINS[index]);

  // 如果读数发生变化，重新开始消抖计时。
  if (reading != lastButtonReading[index]) {
    lastDebounceTime[index] = millis();
  }

  // 读数保持稳定超过消抖时间后，才认为状态有效。
  if (millis() - lastDebounceTime[index] > DEBOUNCE_MS) {
    if (reading != stableButtonState[index]) {
      stableButtonState[index] = reading;

      // INPUT_PULLUP 下，LOW 表示按键被按下。
      if (stableButtonState[index] == LOW) {
        pressed = true;
      }
    }
  }

  lastButtonReading[index] = reading;
  return pressed;
}

void addScoreA(int points) {
  scoreA += points;
  refreshDisplay();
}

void addScoreB(int points) {
  scoreB += points;
  refreshDisplay();
}

void nextPeriod() {
  period++;
  if (period > 4) {
    period = 1;
  }
  refreshDisplay();
}

void toggleTimer() {
  timerRunning = !timerRunning;
  lastTimerTick = millis();
  refreshDisplay();
}

void resetGame() {
  scoreA = 0;
  scoreB = 0;
  period = 1;
  remainingSeconds = INITIAL_SECONDS;
  timerRunning = false;
  buzzerPlayed = false;
  digitalWrite(PIN_BUZZER, LOW);
  refreshDisplay();
}

void updateTimer() {
  if (!timerRunning) {
    return;
  }

  // 使用 millis 计算秒级倒计时，避免 delay 阻塞按键读取。
  if (millis() - lastTimerTick >= 1000) {
    lastTimerTick += 1000;

    if (remainingSeconds > 0) {
      remainingSeconds--;
    }

    if (remainingSeconds == 0) {
      timerRunning = false;
      playBuzzerOnce();
    }
  }
}

void playBuzzerOnce() {
  if (buzzerPlayed) {
    return;
  }

  buzzerPlayed = true;
  digitalWrite(PIN_BUZZER, HIGH);
  delay(500);
  digitalWrite(PIN_BUZZER, LOW);
}

void refreshDisplay() {
  char line1[17];
  char line2[17];

  unsigned int minutes = remainingSeconds / 60;
  unsigned int seconds = remainingSeconds % 60;

  snprintf(line1, sizeof(line1), "A:%03d B:%03d P%d", scoreA, scoreB, period);
  snprintf(line2, sizeof(line2), "%02u:%02u %s", minutes, seconds, timerRunning ? "RUN " : "STOP");

  lcd.setCursor(0, 0);
  printPaddedLine(line1);
  lcd.setCursor(0, 1);
  printPaddedLine(line2);
}

void printPaddedLine(const char* text) {
  int length = 0;

  while (text[length] != '\0' && length < 16) {
    lcd.print(text[length]);
    length++;
  }

  // 清掉上一帧残留字符。
  while (length < 16) {
    lcd.print(' ');
    length++;
  }
}

