#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Servo.h>
#include <Wire.h>

// 智能药盒提醒系统：
// 通过 RTC 定时触发服药提醒，驱动舵机打开对应药仓，
// 并要求用户在限定时间内确认服药，否则记录为 missed dose。

const int PIN_BUZZER = 3;
const int PIN_SERVO_SLOT1 = 5;
const int PIN_SERVO_SLOT2 = 6;
const int PIN_CONFIRM_BUTTON = 8;
const int PIN_NEXT_BUTTON = 9;

const unsigned long SCREEN_ROTATE_MS = 2500;
const unsigned long DOSE_WINDOW_MS = 15000;

RTC_DS3231 rtc;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo servoSlot1;
Servo servoSlot2;

struct ReminderSchedule {
  uint8_t hour;
  uint8_t minute;
  uint8_t slot;
  bool doneToday;
};

ReminderSchedule schedules[2] = {
  {8, 0, 1, false},
  {20, 0, 2, false}
};

enum ReminderState {
  STATE_IDLE,
  STATE_REMINDING,
  STATE_MISSED
};

ReminderState reminderState = STATE_IDLE;

int activeScheduleIndex = -1;
int missedDoseCount = 0;
int takenDoseCount = 0;
unsigned long reminderStartTime = 0;
unsigned long lastScreenSwitch = 0;
bool showSchedulePage = true;
uint8_t lastDay = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_CONFIRM_BUTTON, INPUT_PULLUP);
  pinMode(PIN_NEXT_BUTTON, INPUT_PULLUP);

  servoSlot1.attach(PIN_SERVO_SLOT1);
  servoSlot2.attach(PIN_SERVO_SLOT2);
  closeAllSlots();

  rtc.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  DateTime now = rtc.now();
  lastDay = now.day();
}

void loop() {
  DateTime now = rtc.now();
  resetDailyFlagsIfNeeded(now);
  checkForReminder(now);
  handleButtons();
  updateReminderState();
  updateDisplay(now);
  updateBuzzer();
}

void resetDailyFlagsIfNeeded(const DateTime &now) {
  // 每天零点后要允许当日提醒重新触发，因此 doneToday 需要按日期复位。
  if (now.day() == lastDay) {
    return;
  }
  lastDay = now.day();
  for (int i = 0; i < 2; ++i) {
    schedules[i].doneToday = false;
  }
}

void checkForReminder(const DateTime &now) {
  if (reminderState == STATE_REMINDING) {
    return;
  }

  for (int i = 0; i < 2; ++i) {
    if (schedules[i].doneToday) {
      continue;
    }
    if (now.hour() == schedules[i].hour && now.minute() == schedules[i].minute) {
      triggerReminder(i);
      return;
    }
  }
}

void triggerReminder(int scheduleIndex) {
  activeScheduleIndex = scheduleIndex;
  reminderState = STATE_REMINDING;
  reminderStartTime = millis();
  openSlot(schedules[scheduleIndex].slot);
}

void handleButtons() {
  static bool lastConfirm = HIGH;
  static bool lastNext = HIGH;

  bool confirm = digitalRead(PIN_CONFIRM_BUTTON);
  bool next = digitalRead(PIN_NEXT_BUTTON);

  if (lastConfirm == HIGH && confirm == LOW && reminderState == STATE_REMINDING) {
    // 只有在提醒窗口内按确认键，才视为真正服药完成。
    schedules[activeScheduleIndex].doneToday = true;
    takenDoseCount++;
    reminderState = STATE_IDLE;
    activeScheduleIndex = -1;
    closeAllSlots();
  }

  if (lastNext == HIGH && next == LOW) {
    showSchedulePage = !showSchedulePage;
  }

  lastConfirm = confirm;
  lastNext = next;
}

void updateReminderState() {
  if (reminderState != STATE_REMINDING) {
    return;
  }

  // 超过服药确认窗口仍未确认，则记录 missed dose，并关闭药仓避免长时间暴露。
  if (millis() - reminderStartTime > DOSE_WINDOW_MS) {
    reminderState = STATE_MISSED;
    missedDoseCount++;
    schedules[activeScheduleIndex].doneToday = true;
    activeScheduleIndex = -1;
    closeAllSlots();
  }
}

void openSlot(uint8_t slot) {
  if (slot == 1) {
    servoSlot1.write(120);
  } else if (slot == 2) {
    servoSlot2.write(120);
  }
}

void closeAllSlots() {
  servoSlot1.write(40);
  servoSlot2.write(40);
}

void updateBuzzer() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;
  unsigned long interval = 0;

  if (reminderState == STATE_REMINDING) {
    interval = 220;
  } else if (reminderState == STATE_MISSED) {
    interval = 650;
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastToggle < interval) {
    return;
  }
  lastToggle = millis();
  buzzerOn = !buzzerOn;
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
}

void updateDisplay(const DateTime &now) {
  if (millis() - lastScreenSwitch > SCREEN_ROTATE_MS) {
    lastScreenSwitch = millis();
    showSchedulePage = !showSchedulePage;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (showSchedulePage) {
    display.setCursor(0, 0);
    display.print("Time:");
    printTwoDigit(now.hour());
    display.print(":");
    printTwoDigit(now.minute());
    display.setCursor(0, 14);
    display.print("S1 08:00 ");
    display.print(schedules[0].doneToday ? "Done" : "Wait");
    display.setCursor(0, 28);
    display.print("S2 20:00 ");
    display.print(schedules[1].doneToday ? "Done" : "Wait");
  } else {
    display.setCursor(0, 0);
    display.print("State:");
    display.print(reminderState);
    display.setCursor(0, 14);
    display.print("Taken:");
    display.print(takenDoseCount);
    display.setCursor(0, 28);
    display.print("Missed:");
    display.print(missedDoseCount);
    display.setCursor(0, 42);
    display.print("Active:");
    display.print(activeScheduleIndex);
  }

  display.display();
}

void printTwoDigit(uint8_t value) {
  if (value < 10) {
    display.print('0');
  }
  display.print(value);
}
