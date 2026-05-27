#include <RTClib.h>
#include <TM1637Display.h>

// 多功能电子钟：
// 使用 DS3231 负责实时时间保存，TM1637 数码管负责显示。
// 模式切换把“看时间”“校时”“设置闹钟”放在同一套按键流程中。

const int PIN_TM1637_CLK = 2;
const int PIN_TM1637_DIO = 3;
const int PIN_MODE = 4;
const int PIN_PLUS = 5;
const int PIN_MINUS = 6;
const int PIN_CONFIRM = 7;
const int PIN_BUZZER = 8;

const int BUTTON_COUNT = 4;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_MODE,
  PIN_PLUS,
  PIN_MINUS,
  PIN_CONFIRM
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned long BLINK_MS = 500;

RTC_DS3231 rtc;
TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);

// 模式状态机：
// MODE_CLOCK 为正常显示，其余模式用于修改时间或闹钟参数。
enum Mode {
  MODE_CLOCK,
  MODE_SET_HOUR,
  MODE_SET_MINUTE,
  MODE_SET_ALARM_HOUR,
  MODE_SET_ALARM_MINUTE,
  MODE_SET_ALARM_ENABLE
};

Mode mode = MODE_CLOCK;

// settingHour / settingMinute 是编辑缓冲区，只有确认保存后才真正写回 RTC。
int settingHour = 0;
int settingMinute = 0;
int alarmHour = 7;
int alarmMinute = 30;
bool alarmEnabled = false;
int lastAlarmMinute = -1;
int lastHourlyBeepHour = -1;

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];
unsigned long lastDisplayRefresh = 0;
unsigned long lastBlinkTime = 0;
bool colonVisible = true;

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // 使用内部上拉电阻，按键按下时读数为 LOW。
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  display.setBrightness(5);

  if (!rtc.begin()) {
    // RTC 初始化失败时显示错误码 EEEE。
    display.showNumberDecEx(1111, 0, true);
    while (true) {
      delay(1000);
    }
  }

  // 如果 RTC 断电过，使用编译时间初始化一次。
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();
  settingHour = now.hour();
  settingMinute = now.minute();
}

void loop() {
  // 显示、按键和提示音都围绕 mode 状态机运转。
  handleButtons();
  updateBlinkState();
  checkAlarmAndHourlyBeep();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    refreshDisplay();
    lastDisplayRefresh = millis();
  }
}

void handleButtons() {
  if (buttonPressed(0)) {
    switchMode();
  }
  if (buttonPressed(1)) {
    increaseCurrentValue();
  }
  if (buttonPressed(2)) {
    decreaseCurrentValue();
  }
  if (buttonPressed(3)) {
    saveSettings();
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

void switchMode() {
  if (mode == MODE_CLOCK) {
    // 进入设置前先读取一次当前 RTC 时间，避免用户在旧值上继续编辑。
    DateTime now = rtc.now();
    settingHour = now.hour();
    settingMinute = now.minute();
    mode = MODE_SET_HOUR;
    return;
  }

  mode = static_cast<Mode>((mode + 1) % 6);
}

void increaseCurrentValue() {
  if (mode == MODE_SET_HOUR) {
    settingHour = (settingHour + 1) % 24;
  } else if (mode == MODE_SET_MINUTE) {
    settingMinute = (settingMinute + 1) % 60;
  } else if (mode == MODE_SET_ALARM_HOUR) {
    alarmHour = (alarmHour + 1) % 24;
  } else if (mode == MODE_SET_ALARM_MINUTE) {
    alarmMinute = (alarmMinute + 1) % 60;
  } else if (mode == MODE_SET_ALARM_ENABLE) {
    alarmEnabled = !alarmEnabled;
  }
}

void decreaseCurrentValue() {
  if (mode == MODE_SET_HOUR) {
    settingHour = (settingHour + 23) % 24;
  } else if (mode == MODE_SET_MINUTE) {
    settingMinute = (settingMinute + 59) % 60;
  } else if (mode == MODE_SET_ALARM_HOUR) {
    alarmHour = (alarmHour + 23) % 24;
  } else if (mode == MODE_SET_ALARM_MINUTE) {
    alarmMinute = (alarmMinute + 59) % 60;
  } else if (mode == MODE_SET_ALARM_ENABLE) {
    alarmEnabled = !alarmEnabled;
  }
}

void saveSettings() {
  DateTime now = rtc.now();

  // 保存当前设置的小时和分钟，秒数从 0 开始。
  rtc.adjust(DateTime(now.year(), now.month(), now.day(), settingHour, settingMinute, 0));
  mode = MODE_CLOCK;
}

void updateBlinkState() {
  // 数码管冒号闪烁用于提供“正在运行”的时间感。
  if (millis() - lastBlinkTime >= BLINK_MS) {
    colonVisible = !colonVisible;
    lastBlinkTime = millis();
  }
}

void checkAlarmAndHourlyBeep() {
  // 只在正常显示模式下检查闹钟，避免设置过程中被闹钟或整点提示打断。
  if (mode != MODE_CLOCK) {
    return;
  }

  DateTime now = rtc.now();

  // 闹钟在同一分钟内只触发一次。
  if (alarmEnabled &&
      now.hour() == alarmHour &&
      now.minute() == alarmMinute &&
      lastAlarmMinute != now.minute()) {
    beep(800);
    lastAlarmMinute = now.minute();
  }

  if (now.minute() != alarmMinute) {
    lastAlarmMinute = -1;
  }

  // 整点短提示在同一小时内只触发一次。
  if (now.minute() == 0 && now.second() == 0 && lastHourlyBeepHour != now.hour()) {
    beep(150);
    lastHourlyBeepHour = now.hour();
  }
}

void beep(unsigned int durationMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durationMs);
  digitalWrite(PIN_BUZZER, LOW);
}

void refreshDisplay() {
  int hourValue = 0;
  int minuteValue = 0;

  // 不同模式复用同一块数码管显示不同来源的数据。
  if (mode == MODE_CLOCK) {
    DateTime now = rtc.now();
    hourValue = now.hour();
    minuteValue = now.minute();
  } else if (mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE) {
    hourValue = settingHour;
    minuteValue = settingMinute;
  } else if (mode == MODE_SET_ALARM_HOUR || mode == MODE_SET_ALARM_MINUTE) {
    hourValue = alarmHour;
    minuteValue = alarmMinute;
  } else {
    // 闹钟开关模式下，0001 表示开启，0000 表示关闭。
    display.showNumberDec(alarmEnabled ? 1 : 0, true);
    return;
  }

  // 数码管本身不理解“时间”，所以统一转换为 HHMM 整数后输出。
  int displayValue = hourValue * 100 + minuteValue;
  uint8_t colonFlag = colonVisible ? 0b01000000 : 0;

  display.showNumberDecEx(displayValue, colonFlag, true);
}
