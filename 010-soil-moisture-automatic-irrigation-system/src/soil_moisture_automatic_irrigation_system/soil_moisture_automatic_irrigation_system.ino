#include <LiquidCrystal_I2C.h>

// 土壤湿度自动灌溉系统：
// 用土壤湿度估算值驱动继电器和水泵，并保留手动浇水、阈值设置和自动模式开关。
// 重点是避免“湿度一抖就启停水泵”，因此加入平滑、启停边界和冷却时间。

const int PIN_SOIL_SENSOR = A0;
const int PIN_RELAY = 8;
const int PIN_BUTTON_SET = 2;
const int PIN_BUTTON_UP = 3;
const int PIN_BUTTON_DOWN = 4;

const bool RELAY_ACTIVE_LEVEL = LOW;
const int BUTTON_COUNT = 3;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_BUTTON_SET,
  PIN_BUTTON_UP,
  PIN_BUTTON_DOWN
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long SENSOR_READ_MS = 600;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned long MESSAGE_DURATION_MS = 1500;
const unsigned long AUTO_MAX_PUMP_MS = 8000;
const unsigned long MANUAL_PUMP_MS = 5000;
const unsigned long PUMP_COOLDOWN_MS = 10000;
const float SOIL_FILTER_ALPHA = 0.18f;

const int SOIL_RAW_DRY = 860;
const int SOIL_RAW_WET = 380;
const int MIN_THRESHOLD = 20;
const int MAX_THRESHOLD = 80;
const int START_MARGIN = 3;
const int STOP_MARGIN = 5;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 记录这次启动水泵是自动触发还是手动触发，便于应用不同停机策略。
enum PumpSource {
  PUMP_NONE,
  PUMP_AUTO,
  PUMP_MANUAL
};

bool autoEnabled = true;
bool editMode = false;
bool pumpRunning = false;
PumpSource pumpSource = PUMP_NONE;

int irrigationThreshold = 35;
int soilRaw = 0;
int soilPercent = 0;
float soilFiltered = 0.0f;

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];

unsigned long lastSensorRead = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long pumpStartTime = 0;
unsigned long lastPumpStopTime = 0;
unsigned long messageStartTime = 0;
bool showingMessage = false;
char messageLine1[17] = "";
char messageLine2[17] = "";

void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, !RELAY_ACTIVE_LEVEL);

  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  lcd.init();
  lcd.backlight();

  soilRaw = analogRead(PIN_SOIL_SENSOR);
  soilFiltered = static_cast<float>(soilRaw);
  soilPercent = rawToPercent(soilRaw);

  showTemporaryMessage("Irrigation Init", "Auto mode ready");
}

void loop() {
  // 先处理用户输入，再更新采样和控制，保证手动操作优先级更高。
  handleButtons();
  updateSoilReading();
  updatePumpControl();
  updateMessageState();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    refreshDisplay();
    lastDisplayRefresh = millis();
  }
}

void handleButtons() {
  if (buttonPressed(0)) {
    // SET 键在“正常显示”和“阈值编辑”之间切换。
    editMode = !editMode;
    if (editMode) {
      showTemporaryMessage("Threshold edit", "Use UP / DOWN");
    } else {
      showTemporaryMessage("Edit finished", "Saved in RAM");
    }
  }

  if (buttonPressed(1)) {
    if (editMode) {
      irrigationThreshold++;
      if (irrigationThreshold > MAX_THRESHOLD) {
        irrigationThreshold = MAX_THRESHOLD;
      }
    } else {
      // 非编辑模式下，UP 键作为自动模式总开关。
      autoEnabled = !autoEnabled;
      showTemporaryMessage(autoEnabled ? "Auto mode ON" : "Auto mode OFF", autoEnabled ? "Pump by sensor" : "Manual only");

      if (!autoEnabled && pumpRunning && pumpSource == PUMP_AUTO) {
        stopPump();
      }
    }
  }

  if (buttonPressed(2)) {
    if (editMode) {
      irrigationThreshold--;
      if (irrigationThreshold < MIN_THRESHOLD) {
        irrigationThreshold = MIN_THRESHOLD;
      }
    } else if (pumpRunning) {
      // 手动模式下再次按下可主动停止，避免必须等超时结束。
      stopPump();
      showTemporaryMessage("Pump stopped", "Manual cancel");
    } else if (canStartPump()) {
      startPump(PUMP_MANUAL);
      showTemporaryMessage("Manual watering", "Pump started");
    } else {
      showTemporaryMessage("Pump cooling", "Wait a moment");
    }
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

void updateSoilReading() {
  if (millis() - lastSensorRead < SENSOR_READ_MS) {
    return;
  }

  lastSensorRead = millis();
  soilRaw = analogRead(PIN_SOIL_SENSOR);
  // 先平滑再映射成百分比，减少土壤传感器原始值波动带来的误判。
  soilFiltered = SOIL_FILTER_ALPHA * static_cast<float>(soilRaw) +
                 (1.0f - SOIL_FILTER_ALPHA) * soilFiltered;
  soilPercent = rawToPercent(static_cast<int>(soilFiltered));
}

void updatePumpControl() {
  if (pumpRunning) {
    unsigned long runtime = millis() - pumpStartTime;

    if (pumpSource == PUMP_AUTO) {
      // 自动浇水满足“湿度恢复 / 超时 / 用户关闭自动模式”任一条件就停止。
      if (soilPercent >= irrigationThreshold + STOP_MARGIN || runtime >= AUTO_MAX_PUMP_MS || !autoEnabled) {
        stopPump();
        showTemporaryMessage("Auto watering", "Pump stopped");
      }
    } else if (pumpSource == PUMP_MANUAL) {
      if (runtime >= MANUAL_PUMP_MS) {
        stopPump();
        showTemporaryMessage("Manual done", "Pump stopped");
      }
    }

    return;
  }

  if (editMode || !autoEnabled) {
    return;
  }

  // 自动启动阈值和自动停止阈值分开设置，避免水泵频繁抖动启停。
  if (soilPercent <= irrigationThreshold - START_MARGIN && canStartPump()) {
    startPump(PUMP_AUTO);
    showTemporaryMessage("Soil too dry", "Auto pump start");
  }
}

bool canStartPump() {
  // 首次启动允许立即运行；之后必须等冷却时间结束，保护继电器和水泵。
  return lastPumpStopTime == 0 || millis() - lastPumpStopTime >= PUMP_COOLDOWN_MS;
}

void startPump(PumpSource source) {
  // 所有启动动作统一走这里，保证状态变量和继电器输出一致。
  pumpRunning = true;
  pumpSource = source;
  pumpStartTime = millis();
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LEVEL);
}

void stopPump() {
  // 停机时记录停止时间，后续 canStartPump 依赖它实现冷却保护。
  pumpRunning = false;
  pumpSource = PUMP_NONE;
  lastPumpStopTime = millis();
  digitalWrite(PIN_RELAY, !RELAY_ACTIVE_LEVEL);
}

void refreshDisplay() {
  char line1[17];
  char line2[17];

  // 临时消息优先级最高，用来明确反馈当前用户操作或自动控制结果。
  if (showingMessage) {
    lcd.setCursor(0, 0);
    printPaddedLine(messageLine1);
    lcd.setCursor(0, 1);
    printPaddedLine(messageLine2);
    return;
  }

  if (editMode) {
    snprintf(line1, sizeof(line1), "Set threshold");
    snprintf(line2, sizeof(line2), "<   %2d%%   >", irrigationThreshold);
  } else {
    snprintf(line1, sizeof(line1), "M:%2d%% T:%2d%%", soilPercent, irrigationThreshold);
    snprintf(line2, sizeof(line2), "%s %s", autoEnabled ? "AUTO" : "MANL", pumpRunning ? "PUMP ON" : "PUMP OFF");
  }

  lcd.setCursor(0, 0);
  printPaddedLine(line1);
  lcd.setCursor(0, 1);
  printPaddedLine(line2);
}

void showTemporaryMessage(const char* line1, const char* line2) {
  // 所有短提示都复用这两个缓冲区，避免在多个地方直接操作 LCD。
  snprintf(messageLine1, sizeof(messageLine1), "%s", line1);
  snprintf(messageLine2, sizeof(messageLine2), "%s", line2);
  showingMessage = true;
  messageStartTime = millis();
}

void updateMessageState() {
  if (showingMessage && millis() - messageStartTime >= MESSAGE_DURATION_MS) {
    showingMessage = false;
  }
}

int rawToPercent(int rawValue) {
  // 这里的干湿标定值需要按实际传感器重新校准，代码先给出一个典型范围。
  int percent = map(rawValue, SOIL_RAW_DRY, SOIL_RAW_WET, 0, 100);
  return constrain(percent, 0, 100);
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
