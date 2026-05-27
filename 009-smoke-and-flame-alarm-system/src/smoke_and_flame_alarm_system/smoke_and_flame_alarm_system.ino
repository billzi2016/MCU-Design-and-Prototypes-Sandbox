#include <LiquidCrystal_I2C.h>

// 烟雾与火焰报警系统：
// 综合 MQ-2 模拟量和火焰传感器数字量，输出“正常 / 预警 / 报警”三级状态。
// 通过确认时间、恢复保持和回差，尽量避免偶发噪声导致误联动。

const int PIN_FLAME_SENSOR = 2;
const int PIN_MQ2 = A0;
const int PIN_BUZZER = 8;
const int PIN_RELAY = 9;
const int PIN_LED_RED = 10;
const int PIN_LED_YELLOW = 11;
const int PIN_LED_GREEN = 12;

const bool RELAY_ACTIVE_LEVEL = LOW;
const unsigned long SENSOR_READ_MS = 250;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned long DISPLAY_PAGE_MS = 2500;
const unsigned long WARNING_BUZZER_MS = 500;
const unsigned long ALERT_BUZZER_MS = 150;
const unsigned long CONFIRM_DURATION_MS = 1200;
const unsigned long RECOVERY_HOLD_MS = 5000;
const float MQ2_FILTER_ALPHA = 0.20f;

const int MQ2_WARNING_THRESHOLD = 350;
const int MQ2_ALERT_THRESHOLD = 550;
const int MQ2_HYSTERESIS = 35;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 报警级别越高，蜂鸣频率和联动动作越强。
enum AlarmMode {
  MODE_NORMAL,
  MODE_SMOKE_WARNING,
  MODE_FIRE_ALERT
};

AlarmMode alarmMode = MODE_NORMAL;
float mq2Filtered = 0.0f;
bool flameDetected = false;
bool warningCondition = false;
bool fireCondition = false;
bool showActionPage = false;
bool buzzerState = false;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long warningConditionStart = 0;
unsigned long fireConditionStart = 0;
unsigned long recoveryStart = 0;

void setup() {
  pinMode(PIN_FLAME_SENSOR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);

  lcd.init();
  lcd.backlight();

  mq2Filtered = static_cast<float>(analogRead(PIN_MQ2));
  applyOutputs();
}

void loop() {
  // 先更新传感器和状态，再决定蜂鸣与显示，保证输出始终跟随最新判定。
  updateSensorData();
  updateAlarmMode();
  updateBuzzer();
  updateDisplayPage();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    refreshDisplay();
    lastDisplayRefresh = millis();
  }
}

void updateSensorData() {
  if (millis() - lastSensorRead < SENSOR_READ_MS) {
    return;
  }

  lastSensorRead = millis();

  int mq2Raw = analogRead(PIN_MQ2);
  mq2Filtered = MQ2_FILTER_ALPHA * static_cast<float>(mq2Raw) +
                (1.0f - MQ2_FILTER_ALPHA) * mq2Filtered;

  // 常见火焰传感器数字输出在检测到火焰时为 LOW。
  flameDetected = digitalRead(PIN_FLAME_SENSOR) == LOW;

  // MQ-2 预警和报警使用两档阈值，并配合回差减少边界抖动。
  if (static_cast<int>(mq2Filtered) >= MQ2_WARNING_THRESHOLD) {
    warningCondition = true;
  } else if (static_cast<int>(mq2Filtered) <= MQ2_WARNING_THRESHOLD - MQ2_HYSTERESIS) {
    warningCondition = false;
  }

  if (flameDetected || static_cast<int>(mq2Filtered) >= MQ2_ALERT_THRESHOLD) {
    fireCondition = true;
  } else if (static_cast<int>(mq2Filtered) <= MQ2_ALERT_THRESHOLD - MQ2_HYSTERESIS) {
    fireCondition = false;
  }
}

void updateAlarmMode() {
  unsigned long now = millis();

  // 只有条件持续满足确认时间后才真正升级状态，防止瞬时尖峰误触发。
  if (fireCondition) {
    if (fireConditionStart == 0) {
      fireConditionStart = now;
    }
  } else {
    fireConditionStart = 0;
  }

  if (warningCondition) {
    if (warningConditionStart == 0) {
      warningConditionStart = now;
    }
  } else {
    warningConditionStart = 0;
  }

  if (fireConditionStart != 0 && now - fireConditionStart >= CONFIRM_DURATION_MS) {
    setAlarmMode(MODE_FIRE_ALERT);
    recoveryStart = 0;
    return;
  }

  if (alarmMode != MODE_FIRE_ALERT &&
      warningConditionStart != 0 &&
      now - warningConditionStart >= CONFIRM_DURATION_MS) {
    setAlarmMode(MODE_SMOKE_WARNING);
    recoveryStart = 0;
    return;
  }

  // 所有风险都消失后也不会立刻恢复正常，给系统一个稳定观察窗口。
  if (!fireCondition && !warningCondition) {
    if (recoveryStart == 0) {
      recoveryStart = now;
    }

    if (now - recoveryStart >= RECOVERY_HOLD_MS) {
      setAlarmMode(MODE_NORMAL);
    }
  } else {
    recoveryStart = 0;
  }
}

void setAlarmMode(AlarmMode nextMode) {
  // 状态改变时统一清理蜂鸣器瞬态，避免不同报警节奏衔接时出现毛刺。
  if (alarmMode == nextMode) {
    return;
  }

  alarmMode = nextMode;
  buzzerState = false;
  digitalWrite(PIN_BUZZER, LOW);
  applyOutputs();
}

void applyOutputs() {
  // 继电器只在最高级报警时联动，预警阶段先保守提示，不直接动作外设。
  bool relayOn = alarmMode == MODE_FIRE_ALERT;

  digitalWrite(PIN_LED_GREEN, alarmMode == MODE_NORMAL ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, alarmMode == MODE_SMOKE_WARNING ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, alarmMode == MODE_FIRE_ALERT ? HIGH : LOW);
  digitalWrite(PIN_RELAY, relayOn ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
}

void updateBuzzer() {
  // 预警慢鸣、报警快鸣，听觉上就能区分风险等级。
  if (alarmMode == MODE_NORMAL) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerState = false;
    return;
  }

  unsigned long interval = alarmMode == MODE_FIRE_ALERT ? ALERT_BUZZER_MS : WARNING_BUZZER_MS;

  if (millis() - lastBuzzerToggle >= interval) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void updateDisplayPage() {
  // 两页轮播：一页看输入状态，一页看联动动作，16x2 LCD 也能表达完整信息。
  if (millis() - lastDisplaySwitch >= DISPLAY_PAGE_MS) {
    showActionPage = !showActionPage;
    lastDisplaySwitch = millis();
  }
}

void refreshDisplay() {
  char line1[17];
  char line2[17];

  // 第一页看“为什么报警”，第二页看“报警后做了什么”。
  if (!showActionPage) {
    snprintf(line1, sizeof(line1), "S:%4d F:%s", static_cast<int>(mq2Filtered), flameDetected ? "YES" : "NO");
    snprintf(line2, sizeof(line2), "Mode:%s", modeLabel());
  } else {
    snprintf(line1, sizeof(line1), "Relay:%s Bz:%s", alarmMode == MODE_FIRE_ALERT ? "ON" : "OFF", buzzerState ? "ON" : "OFF");
    snprintf(line2, sizeof(line2), "%s", actionLabel());
  }

  lcd.setCursor(0, 0);
  printPaddedLine(line1);
  lcd.setCursor(0, 1);
  printPaddedLine(line2);
}

const char* modeLabel() {
  if (alarmMode == MODE_NORMAL) {
    return "NORMAL";
  }
  if (alarmMode == MODE_SMOKE_WARNING) {
    return "WARNING";
  }
  return "ALERT";
}

const char* actionLabel() {
  if (alarmMode == MODE_NORMAL) {
    return "Monitoring...";
  }
  if (alarmMode == MODE_SMOKE_WARNING) {
    return "Smoke warning";
  }
  return "Fire action ON";
}

void printPaddedLine(const char* text) {
  int length = 0;

  while (text[length] != '\0' && length < 16) {
    lcd.print(text[length]);
    length++;
  }

  while (length < 16) {
    lcd.print(' ');
    length++;
  }
}
