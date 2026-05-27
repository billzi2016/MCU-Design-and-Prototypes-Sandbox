#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// DHT22 温湿度监测系统：
// 采集温度和湿度后进行上下限判断，用 LCD1602 显示实时值和阈值页，
// 并用回差和读数异常判断减少误报警和显示抖动。

const int PIN_DHT = 2;
const int PIN_BUZZER = 8;
const int DHT_TYPE = DHT22;

const float TEMP_HIGH_THRESHOLD = 30.0f;
const float TEMP_LOW_THRESHOLD = 10.0f;
const float HUMIDITY_HIGH_THRESHOLD = 80.0f;
const float HUMIDITY_LOW_THRESHOLD = 30.0f;

const float TEMP_HYSTERESIS = 0.8f;
const float HUMIDITY_HYSTERESIS = 3.0f;

const unsigned long SENSOR_READ_MS = 2000;
const unsigned long DISPLAY_PAGE_MS = 3000;
const unsigned long BUZZER_TOGGLE_MS = 220;
const unsigned long DISPLAY_REFRESH_MS = 250;

DHT dht(PIN_DHT, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 报警状态按“正常 / 单项异常 / 多项异常 / 传感器故障”组织，便于显示和蜂鸣控制。
enum AlarmState {
  ALARM_NORMAL,
  ALARM_TEMP_HIGH,
  ALARM_TEMP_LOW,
  ALARM_HUMIDITY_HIGH,
  ALARM_HUMIDITY_LOW,
  ALARM_MULTI,
  ALARM_SENSOR_FAULT
};

float temperatureC = 0.0f;
float humidity = 0.0f;
bool tempHighActive = false;
bool tempLowActive = false;
bool humidityHighActive = false;
bool humidityLowActive = false;
bool sensorFault = false;
AlarmState alarmState = ALARM_SENSOR_FAULT;

unsigned long lastSensorRead = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long lastDisplayRefresh = 0;
bool buzzerState = false;
bool showThresholdPage = false;
int invalidReadCount = 0;

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  lcd.init();
  lcd.backlight();

  dht.begin();

  lcd.setCursor(0, 0);
  printPaddedLine("DHT22 Monitor");
  lcd.setCursor(0, 1);
  printPaddedLine("Init sensors...");
}

void loop() {
  // 采样、告警、提示音和页面切换各自独立，避免互相耦合。
  updateSensorData();
  updateAlarmState();
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

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  // DHT22 偶发读数失败并不罕见，因此连续失败两次后才判定为故障。
  if (isnan(newHumidity) || isnan(newTemperature)) {
    invalidReadCount++;
    if (invalidReadCount >= 2) {
      sensorFault = true;
    }
    return;
  }

  invalidReadCount = 0;
  sensorFault = false;
  humidity = newHumidity;
  temperatureC = newTemperature;

  updateThresholdLatch();
}

void updateThresholdLatch() {
  // 每个阈值都单独加入回差，避免在临界点附近来回进出报警。
  if (!tempHighActive && temperatureC >= TEMP_HIGH_THRESHOLD) {
    tempHighActive = true;
  } else if (tempHighActive && temperatureC <= TEMP_HIGH_THRESHOLD - TEMP_HYSTERESIS) {
    tempHighActive = false;
  }

  if (!tempLowActive && temperatureC <= TEMP_LOW_THRESHOLD) {
    tempLowActive = true;
  } else if (tempLowActive && temperatureC >= TEMP_LOW_THRESHOLD + TEMP_HYSTERESIS) {
    tempLowActive = false;
  }

  if (!humidityHighActive && humidity >= HUMIDITY_HIGH_THRESHOLD) {
    humidityHighActive = true;
  } else if (humidityHighActive && humidity <= HUMIDITY_HIGH_THRESHOLD - HUMIDITY_HYSTERESIS) {
    humidityHighActive = false;
  }

  if (!humidityLowActive && humidity <= HUMIDITY_LOW_THRESHOLD) {
    humidityLowActive = true;
  } else if (humidityLowActive && humidity >= HUMIDITY_LOW_THRESHOLD + HUMIDITY_HYSTERESIS) {
    humidityLowActive = false;
  }
}

void updateAlarmState() {
  // 先统计当前一共有多少项超限，再决定是单项报警还是多项报警。
  if (sensorFault) {
    alarmState = ALARM_SENSOR_FAULT;
    return;
  }

  int activeCount = 0;

  if (tempHighActive) {
    activeCount++;
  }
  if (tempLowActive) {
    activeCount++;
  }
  if (humidityHighActive) {
    activeCount++;
  }
  if (humidityLowActive) {
    activeCount++;
  }

  if (activeCount == 0) {
    alarmState = ALARM_NORMAL;
  } else if (activeCount >= 2) {
    alarmState = ALARM_MULTI;
  } else if (tempHighActive) {
    alarmState = ALARM_TEMP_HIGH;
  } else if (tempLowActive) {
    alarmState = ALARM_TEMP_LOW;
  } else if (humidityHighActive) {
    alarmState = ALARM_HUMIDITY_HIGH;
  } else {
    alarmState = ALARM_HUMIDITY_LOW;
  }
}

void updateBuzzer() {
  // 传感器故障只显示提示，不蜂鸣；真正越限时才间歇鸣叫。
  if (alarmState == ALARM_NORMAL || alarmState == ALARM_SENSOR_FAULT) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerState = false;
    return;
  }

  if (millis() - lastBuzzerToggle >= BUZZER_TOGGLE_MS) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void updateDisplayPage() {
  // 轮播显示实时值页和阈值页，16x2 LCD 也能承载更多信息。
  if (millis() - lastDisplaySwitch >= DISPLAY_PAGE_MS) {
    showThresholdPage = !showThresholdPage;
    lastDisplaySwitch = millis();
  }
}

void refreshDisplay() {
  char line1[17];
  char line2[17];
  char tempBuffer[8];

  // AVR 平台上浮点格式化支持有限，因此温度用 dtostrf 单独转成字符串。
  if (sensorFault) {
    lcd.setCursor(0, 0);
    printPaddedLine("DHT22 read error");
    lcd.setCursor(0, 1);
    printPaddedLine("Check wiring...");
    return;
  }

  if (showThresholdPage) {
    snprintf(line1, sizeof(line1), "T %d-%dC", static_cast<int>(TEMP_LOW_THRESHOLD), static_cast<int>(TEMP_HIGH_THRESHOLD));
    snprintf(line2, sizeof(line2), "H %d-%d%% %s", static_cast<int>(HUMIDITY_LOW_THRESHOLD), static_cast<int>(HUMIDITY_HIGH_THRESHOLD), alarmLabel());
  } else {
    dtostrf(temperatureC, 4, 1, tempBuffer);
    snprintf(line1, sizeof(line1), "T:%sC H:%2d%%", tempBuffer, static_cast<int>(humidity + 0.5f));
    snprintf(line2, sizeof(line2), "Status:%s", alarmLabel());
  }

  lcd.setCursor(0, 0);
  printPaddedLine(line1);
  lcd.setCursor(0, 1);
  printPaddedLine(line2);
}

const char* alarmLabel() {
  // 所有状态文案统一从这里生成，避免显示层散落硬编码字符串。
  if (alarmState == ALARM_NORMAL) {
    return "OK";
  }
  if (alarmState == ALARM_TEMP_HIGH) {
    return "T HIGH";
  }
  if (alarmState == ALARM_TEMP_LOW) {
    return "T LOW";
  }
  if (alarmState == ALARM_HUMIDITY_HIGH) {
    return "H HIGH";
  }
  if (alarmState == ALARM_HUMIDITY_LOW) {
    return "H LOW";
  }
  if (alarmState == ALARM_MULTI) {
    return "MULTI";
  }
  return "FAULT";
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
