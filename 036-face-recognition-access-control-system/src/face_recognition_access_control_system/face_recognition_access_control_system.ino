#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <string.h>

// 人脸识别门禁系统：
// 树莓派端负责摄像头采集、人脸检测和身份识别，
// Arduino 端负责接收识别结果，执行门锁控制、蜂鸣提示、显示和访问状态记录。

const int PIN_RELAY_LOCK = 8;
const int PIN_BUZZER = 9;
const int PIN_PI_RX = 10;
const int PIN_PI_TX = 11;
const int PIN_STATUS_LED = 12;

const unsigned long DOOR_UNLOCK_MS = 5000;
const unsigned long SCREEN_ROTATE_MS = 2000;
const unsigned long UNKNOWN_COOLDOWN_MS = 3000;
const int UNKNOWN_ALARM_THRESHOLD = 3;
// 识别分值低于该阈值时，即使标记为已知用户也不直接放行。
const int MIN_SCORE = 70;

LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial piSerial(PIN_PI_RX, PIN_PI_TX);

enum AccessState {
  ACCESS_IDLE,
  ACCESS_GRANTED,
  ACCESS_DENIED,
  ACCESS_LOCKDOWN
};

// accessState 描述当前门禁是待机、已开门、普通拒绝还是高风险锁定状态。
AccessState accessState = ACCESS_IDLE;

char serialBuffer[80];
byte serialIndex = 0;

char lastName[20] = "SYSTEM";
char lastEvent[20] = "WAITING";
int lastScore = 0;
int unknownAttempts = 0;
// 这些时间戳分别用于开门超时、陌生人累计冷却和屏幕轮播切换。
unsigned long lastUnlockTime = 0;
unsigned long lastUnknownTime = 0;
unsigned long lastScreenSwitch = 0;
bool showSummaryPage = true;

void setup() {
  Serial.begin(115200);
  piSerial.begin(115200);

  pinMode(PIN_RELAY_LOCK, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  digitalWrite(PIN_RELAY_LOCK, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_STATUS_LED, LOW);

  lcd.init();
  lcd.backlight();
  updateDisplay();
}

void loop() {
  // 门禁逻辑拆成“收识别结果 -> 更新门锁状态 -> 刷屏 -> 声音提示”四步，
  // 这样后续接入更多事件源时不需要重写整条主循环。
  readPiFrames();
  updateDoorState();
  updateDisplay();
  updateBuzzer();
}

void readPiFrames() {
  while (piSerial.available() > 0) {
    char incoming = static_cast<char>(piSerial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialIndex] = '\0';
      parseFaceFrame(serialBuffer);
      serialIndex = 0;
      continue;
    }

    if (serialIndex < sizeof(serialBuffer) - 1) {
      serialBuffer[serialIndex++] = incoming;
    } else {
      serialIndex = 0;
    }
  }
}

void parseFaceFrame(char *frame) {
  // 串口协议：
  // FACE,KNOWN,name,score
  // FACE,UNKNOWN,UNKNOWN,score
  // FACE,CLEAR,SYSTEM,0
  if (strncmp(frame, "FACE,", 5) != 0) {
    return;
  }

  char *token = strtok(frame, ",");
  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }

  char result[12];
  strncpy(result, token, sizeof(result) - 1);
  result[sizeof(result) - 1] = '\0';

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  strncpy(lastName, token, sizeof(lastName) - 1);
  lastName[sizeof(lastName) - 1] = '\0';

  token = strtok(nullptr, ",");
  if (token == nullptr) {
    return;
  }
  lastScore = atoi(token);

  // 识别端只负责告诉 Arduino “识别到了谁”和“可信度如何”，
  // 真正是否放行由 MCU 端基于阈值和门禁策略决定。
  if (strcmp(result, "KNOWN") == 0 && lastScore >= MIN_SCORE) {
    grantAccess();
  } else if (strcmp(result, "UNKNOWN") == 0) {
    denyAccess();
  } else if (strcmp(result, "CLEAR") == 0) {
    resetToIdle();
  }
}

void grantAccess() {
  // 一旦放行，之前累计的陌生人计数应清零，避免下一位访客被历史事件影响。
  accessState = ACCESS_GRANTED;
  unknownAttempts = 0;
  strncpy(lastEvent, "ACCESS OK", sizeof(lastEvent) - 1);
  lastEvent[sizeof(lastEvent) - 1] = '\0';

  digitalWrite(PIN_RELAY_LOCK, HIGH);
  digitalWrite(PIN_STATUS_LED, HIGH);
  lastUnlockTime = millis();

  Serial.print("LOG,GRANTED,");
  Serial.print(lastName);
  Serial.print(",");
  Serial.println(lastScore);
}

void denyAccess() {
  // 陌生人识别只在冷却时间后累计一次，避免摄像头连续多帧导致误判次数暴涨。
  if (millis() - lastUnknownTime > UNKNOWN_COOLDOWN_MS) {
    unknownAttempts++;
    lastUnknownTime = millis();
  }

  accessState = unknownAttempts >= UNKNOWN_ALARM_THRESHOLD ? ACCESS_LOCKDOWN : ACCESS_DENIED;
  strncpy(lastEvent, accessState == ACCESS_LOCKDOWN ? "LOCKDOWN" : "DENIED", sizeof(lastEvent) - 1);
  lastEvent[sizeof(lastEvent) - 1] = '\0';

  digitalWrite(PIN_RELAY_LOCK, LOW);
  digitalWrite(PIN_STATUS_LED, accessState == ACCESS_LOCKDOWN ? HIGH : LOW);

  Serial.print("LOG,DENIED,");
  Serial.print(lastName);
  Serial.print(",");
  Serial.print(lastScore);
  Serial.print(",Count:");
  Serial.println(unknownAttempts);
}

void resetToIdle() {
  // CLEAR 常用于树莓派识别画面里“当前无人站在门口”的状态同步。
  if (accessState == ACCESS_GRANTED) {
    digitalWrite(PIN_RELAY_LOCK, LOW);
  }
  accessState = ACCESS_IDLE;
  strncpy(lastName, "SYSTEM", sizeof(lastName) - 1);
  strncpy(lastEvent, "WAITING", sizeof(lastEvent) - 1);
  lastScore = 0;
  digitalWrite(PIN_STATUS_LED, LOW);
}

void updateDoorState() {
  // 门禁只在授权状态下保持一段有限开门时间，超时后自动恢复锁定。
  if (accessState == ACCESS_GRANTED && millis() - lastUnlockTime > DOOR_UNLOCK_MS) {
    digitalWrite(PIN_RELAY_LOCK, LOW);
    digitalWrite(PIN_STATUS_LED, LOW);
    accessState = ACCESS_IDLE;
    strncpy(lastEvent, "LOCKED", sizeof(lastEvent) - 1);
    lastEvent[sizeof(lastEvent) - 1] = '\0';
  }
}

void updateDisplay() {
  // 两页轮播的目的不是炫技，而是在 16x2 屏幕上同时兼顾状态、姓名和异常累计信息。
  if (millis() - lastScreenSwitch > SCREEN_ROTATE_MS) {
    lastScreenSwitch = millis();
    showSummaryPage = !showSummaryPage;
  }

  lcd.clear();
  if (showSummaryPage) {
    lcd.setCursor(0, 0);
    lcd.print("State:");
    lcd.print(accessStateName());
    lcd.setCursor(0, 1);
    lcd.print("Name:");
    lcd.print(lastName);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Score:");
    lcd.print(lastScore);
    lcd.setCursor(0, 1);
    lcd.print("Unknown:");
    lcd.print(unknownAttempts);
  }
}

const char *accessStateName() {
  if (accessState == ACCESS_GRANTED) {
    return "OPEN";
  }
  if (accessState == ACCESS_DENIED) {
    return "DENY";
  }
  if (accessState == ACCESS_LOCKDOWN) {
    return "ALARM";
  }
  return "IDLE";
}

void updateBuzzer() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;
  unsigned long interval = 0;

  // 普通拒绝和锁定告警采用不同节奏，用户不看屏幕也能分辨风险等级。
  if (accessState == ACCESS_DENIED) {
    interval = 220;
  } else if (accessState == ACCESS_LOCKDOWN) {
    interval = 90;
  } else if (accessState == ACCESS_GRANTED) {
    interval = 0;
    digitalWrite(PIN_BUZZER, LOW);
    return;
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
