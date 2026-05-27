#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <stdio.h>
#include <string.h>
#include <Wire.h>

// 环境监测与短信报警一体化系统：
// 同时采集温湿度、烟雾和水位信息，并在达到阈值时执行本地声光报警和短信告警。
// 当前版本支持状态变化触发短信、恢复短信和短信查询状态。

const int PIN_DHT = 4;
const int PIN_MQ2 = A0;
const int PIN_WATER = A1;
const int PIN_BUZZER = 8;
const int PIN_SIM_RX = 10;
const int PIN_SIM_TX = 11;

const int DHT_TYPE = DHT22;
// 这三个阈值分别对应烟雾、水位和高温风险，需要按现场传感器标定结果调整。
const int SMOKE_THRESHOLD = 420;
const int WATER_LOW_THRESHOLD = 380;
const float TEMP_HIGH_THRESHOLD = 35.0f;
const unsigned long SAMPLE_INTERVAL_MS = 1500;
const unsigned long SMS_COOLDOWN_MS = 120000;
const unsigned long DISPLAY_ROTATE_MS = 2200;

const char PHONE_NUMBER[] = "+8613800000000";

DHT dht(PIN_DHT, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial simSerial(PIN_SIM_RX, PIN_SIM_TX);

enum AlarmState {
  ALARM_NORMAL,
  ALARM_WARNING,
  ALARM_CRITICAL
};

AlarmState alarmState = ALARM_NORMAL;
AlarmState lastNotifiedState = ALARM_NORMAL;

// lastNotifiedState 用来避免同一状态持续期间反复刷短信，
// lastSmsTime 则控制即使状态不变，多久允许重发一次提醒。
float temperatureC = 0.0f;
float humidity = 0.0f;
int smokeRaw = 0;
int waterRaw = 0;
bool dhtFault = false;
bool showMainPage = true;
unsigned long lastSampleTime = 0;
unsigned long lastSmsTime = 0;
unsigned long lastDisplaySwitch = 0;

char simBuffer[96];
byte simIndex = 0;

void setup() {
  Serial.begin(115200);
  simSerial.begin(9600);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();

  initSim800l();
  updateDisplay();
}

void loop() {
  sampleSensorsIfNeeded();
  readSimMessages();
  evaluateAlarmState();
  updateBuzzer();
  updateDisplay();
  notifyIfNeeded();
}

void initSim800l() {
  // 这里使用最基础的文本短信模式，优先保证原型可理解、可联调。
  sendAtCommand("AT");
  sendAtCommand("AT+CMGF=1");
  sendAtCommand("AT+CNMI=2,2,0,0,0");
}

void sampleSensorsIfNeeded() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  float newTemp = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  dhtFault = isnan(newTemp) || isnan(newHumidity);
  if (!dhtFault) {
    temperatureC = newTemp;
    humidity = newHumidity;
  }

  // 烟雾和水位都保留原始值显示，便于用户后期自己重新决定阈值。
  smokeRaw = analogRead(PIN_MQ2);
  waterRaw = analogRead(PIN_WATER);
}

void readSimMessages() {
  while (simSerial.available() > 0) {
    char incoming = static_cast<char>(simSerial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      simBuffer[simIndex] = '\0';
      handleSimLine(simBuffer);
      simIndex = 0;
      continue;
    }

    if (simIndex < sizeof(simBuffer) - 1) {
      simBuffer[simIndex++] = incoming;
    } else {
      simIndex = 0;
    }
  }
}

void handleSimLine(char *line) {
  // 为了保持 MCU 端逻辑简洁，这里只处理最基础的短信查询命令。
  if (strstr(line, "STATUS") != nullptr) {
    sendStatusSms("STATUS QUERY");
  }
}

void evaluateAlarmState() {
  bool smokeAlarm = smokeRaw > SMOKE_THRESHOLD;
  bool waterAlarm = waterRaw < WATER_LOW_THRESHOLD;
  bool tempAlarm = temperatureC > TEMP_HIGH_THRESHOLD;

  // 严重告警优先级最高：传感器故障或多风险叠加时，提示方式应明显升级。
  if (dhtFault || (smokeAlarm && waterAlarm)) {
    alarmState = ALARM_CRITICAL;
  } else if (smokeAlarm || waterAlarm || tempAlarm) {
    alarmState = ALARM_WARNING;
  } else {
    alarmState = ALARM_NORMAL;
  }
}

void notifyIfNeeded() {
  // 短信只在状态切换或冷却期后重发，避免同一种异常持续刷屏。
  if (alarmState == lastNotifiedState && millis() - lastSmsTime < SMS_COOLDOWN_MS) {
    return;
  }

  if (alarmState != lastNotifiedState || millis() - lastSmsTime >= SMS_COOLDOWN_MS) {
    sendStatusSms("STATE CHANGE");
    lastNotifiedState = alarmState;
    lastSmsTime = millis();
  }
}

void sendStatusSms(const char *reason) {
  // 短信内容尽量压缩在一条内，把原因、数值和等级都塞进去，方便用户直接看结果。
  char message[160];
  snprintf(
    message,
    sizeof(message),
    "[%s] T=%.1fC H=%.0f%% Smoke=%d Water=%d State=%s",
    reason,
    temperatureC,
    humidity,
    smokeRaw,
    waterRaw,
    alarmStateName()
  );

  simSerial.print("AT+CMGS=\"");
  simSerial.print(PHONE_NUMBER);
  simSerial.println("\"");
  delay(300);
  simSerial.print(message);
  delay(100);
  simSerial.write(26);

  Serial.println(message);
}

void sendAtCommand(const char *command) {
  simSerial.println(command);
  delay(300);
}

void updateBuzzer() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;
  unsigned long interval = 0;

  if (alarmState == ALARM_WARNING) {
    interval = 300;
  } else if (alarmState == ALARM_CRITICAL) {
    interval = 120;
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

void updateDisplay() {
  // 一页显示当前采样值，一页显示告警等级和模块健康状态。
  if (millis() - lastDisplaySwitch > DISPLAY_ROTATE_MS) {
    lastDisplaySwitch = millis();
    showMainPage = !showMainPage;
  }

  lcd.clear();
  if (showMainPage) {
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperatureC, 1);
    lcd.print(" H:");
    lcd.print(humidity, 0);
    lcd.setCursor(0, 1);
    lcd.print("S:");
    lcd.print(smokeRaw);
    lcd.print(" W:");
    lcd.print(waterRaw);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Alarm:");
    lcd.print(alarmStateName());
    lcd.setCursor(0, 1);
    lcd.print(dhtFault ? "DHT Fault" : "SMS Ready");
  }
}

const char *alarmStateName() {
  if (alarmState == ALARM_WARNING) {
    return "WARN";
  }
  if (alarmState == ALARM_CRITICAL) {
    return "CRIT";
  }
  return "SAFE";
}
