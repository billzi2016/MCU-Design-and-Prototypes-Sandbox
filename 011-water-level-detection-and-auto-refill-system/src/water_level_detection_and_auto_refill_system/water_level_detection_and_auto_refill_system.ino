#include <LiquidCrystal_I2C.h>

// 水位检测与自动补水系统：
// 使用超声波模块测量液面高度，使用模拟水位探头作为高水位辅助判断，
// 通过继电器控制水泵启停，并在 LCD1602 上显示当前水位和补水状态。

const int PIN_ULTRASONIC_TRIG = 6;
const int PIN_ULTRASONIC_ECHO = 7;
const int PIN_WATER_PROBE = A0;
const int PIN_RELAY = 8;
const int PIN_BUZZER = 9;

const bool RELAY_ACTIVE_LEVEL = LOW;
const float TANK_HEIGHT_CM = 25.0f;
const float SENSOR_OFFSET_CM = 2.5f;
const float REFILL_START_PERCENT = 25.0f;
const float REFILL_STOP_PERCENT = 80.0f;
const float CRITICAL_LOW_PERCENT = 10.0f;
const int PROBE_HIGH_THRESHOLD = 520;

const unsigned long SENSOR_READ_MS = 400;
const unsigned long DISPLAY_REFRESH_MS = 200;
const unsigned long DISPLAY_PAGE_MS = 2500;
const unsigned long REFILL_MAX_RUNTIME_MS = 12000;
const unsigned long PUMP_COOLDOWN_MS = 8000;
const unsigned long BUZZER_TOGGLE_MS = 220;
const unsigned long ECHO_TIMEOUT_US = 30000;
const float LEVEL_FILTER_ALPHA = 0.18f;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// 用明确状态区分“空闲补水”“正在补水”和“传感器故障”，
// 避免直接用多个布尔变量组合出冲突场景。
enum RefillState {
  STATE_IDLE,
  STATE_REFILLING,
  STATE_SENSOR_FAULT
};

RefillState refillState = STATE_SENSOR_FAULT;

// 水位百分比由超声波距离换算得到，探头值主要作为高位辅助判断。
float levelPercent = 0.0f;
float filteredLevelPercent = 0.0f;
float distanceCm = 0.0f;
int probeRaw = 0;
bool probeHigh = false;
bool criticalLow = false;
bool buzzerState = false;
bool showDetailPage = false;
int invalidMeasureCount = 0;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long refillStartTime = 0;
unsigned long lastPumpStopTime = 0;

void setup() {
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_RELAY, !RELAY_ACTIVE_LEVEL);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  printPaddedLine("Water Refill");
  lcd.setCursor(0, 1);
  printPaddedLine("Init sensors...");
}

void loop() {
  // 主循环拆成采样、控制、报警和显示四部分，便于后续继续扩展。
  updateSensorData();
  updateRefillControl();
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
  probeRaw = analogRead(PIN_WATER_PROBE);
  probeHigh = probeRaw >= PROBE_HIGH_THRESHOLD;

  float measuredDistance = readWaterDistanceCm();
  // 超声波偶发超时比较常见，连续失败后才真正进入故障状态。
  if (measuredDistance < 0.0f) {
    invalidMeasureCount++;
    if (invalidMeasureCount >= 2) {
      refillState = STATE_SENSOR_FAULT;
    }
    return;
  }

  invalidMeasureCount = 0;
  distanceCm = measuredDistance;

  float waterHeight = TANK_HEIGHT_CM - (distanceCm - SENSOR_OFFSET_CM);
  if (waterHeight < 0.0f) {
    waterHeight = 0.0f;
  }
  if (waterHeight > TANK_HEIGHT_CM) {
    waterHeight = TANK_HEIGHT_CM;
  }

  levelPercent = waterHeight / TANK_HEIGHT_CM * 100.0f;
  // 先平滑再参与控制，避免液面轻微波动导致继电器频繁抖动。
  filteredLevelPercent = LEVEL_FILTER_ALPHA * levelPercent +
                         (1.0f - LEVEL_FILTER_ALPHA) * filteredLevelPercent;
  criticalLow = filteredLevelPercent <= CRITICAL_LOW_PERCENT;

  if (refillState == STATE_SENSOR_FAULT) {
    refillState = STATE_IDLE;
  }
}

float readWaterDistanceCm() {
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) {
    return -1.0f;
  }

  float distance = static_cast<float>(duration) * 0.0343f / 2.0f;
  if (distance < 1.5f || distance > 80.0f) {
    return -1.0f;
  }

  return distance;
}

void updateRefillControl() {
  if (refillState == STATE_SENSOR_FAULT) {
    stopPump();
    return;
  }

  if (refillState == STATE_REFILLING) {
    unsigned long runtime = millis() - refillStartTime;

    // 任一停止条件满足都立即结束补水，避免水泵空转、过充或探头长时间浸泡。
    if (filteredLevelPercent >= REFILL_STOP_PERCENT ||
        probeHigh ||
        runtime >= REFILL_MAX_RUNTIME_MS) {
      stopPump();
    }
    return;
  }

  if (filteredLevelPercent <= REFILL_START_PERCENT && canStartPump()) {
    startPump();
  }
}

bool canStartPump() {
  return lastPumpStopTime == 0 || millis() - lastPumpStopTime >= PUMP_COOLDOWN_MS;
}

void startPump() {
  // 所有启动动作统一走这里，保证状态变量和继电器输出始终一致。
  refillState = STATE_REFILLING;
  refillStartTime = millis();
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LEVEL);
}

void stopPump() {
  // 停泵时顺手记录时间，给 canStartPump 提供冷却保护依据。
  if (refillState == STATE_REFILLING) {
    lastPumpStopTime = millis();
  }

  if (refillState != STATE_SENSOR_FAULT) {
    refillState = STATE_IDLE;
  }

  digitalWrite(PIN_RELAY, !RELAY_ACTIVE_LEVEL);
}

void updateBuzzer() {
  // 蜂鸣器只在严重低水位或传感器故障时工作，避免正常补水阶段一直叫。
  bool shouldBuzz = refillState == STATE_SENSOR_FAULT || criticalLow;
  if (!shouldBuzz) {
    buzzerState = false;
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastBuzzerToggle >= BUZZER_TOGGLE_MS) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void updateDisplayPage() {
  if (millis() - lastDisplaySwitch >= DISPLAY_PAGE_MS) {
    showDetailPage = !showDetailPage;
    lastDisplaySwitch = millis();
  }
}

void refreshDisplay() {
  char line1[17];
  char line2[17];

  // 两页轮播：一页看结果，一页看采样细节，16x2 LCD 也能表达完整状态。
  if (refillState == STATE_SENSOR_FAULT) {
    lcd.setCursor(0, 0);
    printPaddedLine("Sensor fault");
    lcd.setCursor(0, 1);
    printPaddedLine("Check level unit");
    return;
  }

  if (showDetailPage) {
    snprintf(line1, sizeof(line1), "D:%4.1fcm P:%3d", distanceCm, probeRaw);
    snprintf(line2, sizeof(line2), "L:%2d%% %s", static_cast<int>(filteredLevelPercent + 0.5f), stateLabel());
  } else {
    snprintf(line1, sizeof(line1), "Level:%3d%%", static_cast<int>(filteredLevelPercent + 0.5f));
    snprintf(line2, sizeof(line2), "Pump:%s %s", refillState == STATE_REFILLING ? "ON " : "OFF", criticalLow ? "LOW" : "OK ");
  }

  lcd.setCursor(0, 0);
  printPaddedLine(line1);
  lcd.setCursor(0, 1);
  printPaddedLine(line2);
}

const char* stateLabel() {
  // 状态文案统一从这里输出，避免显示层散落硬编码字符串。
  if (refillState == STATE_REFILLING) {
    return "REFILL";
  }
  return "IDLE";
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
