#include <LiquidCrystal_I2C.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Servo.h>

// RFID 门禁与考勤系统：
// 通过 RC522 识别 RFID 卡，判断是否属于授权用户，控制舵机开门，
// 同时在 LCD 上显示刷卡结果，并在内存中统计授权卡的考勤次数。

const int PIN_SS = 10;
const int PIN_RST = 9;
const int PIN_SERVO = 6;
const int PIN_BUZZER = 8;

const int LOCK_ANGLE = 0;
const int UNLOCK_ANGLE = 90;
const unsigned long UNLOCK_DURATION_MS = 3000;
const unsigned long MESSAGE_DURATION_MS = 1500;

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_SS, PIN_RST);
Servo doorServo;

// 每张授权卡记录 UID、显示名和当前考勤计数。
struct UserCard {
  byte uid[4];
  const char* name;
  int attendanceCount;
};

UserCard authorizedCards[] = {
    {{0xDE, 0xAD, 0xBE, 0xEF}, "User A", 0},
    {{0x11, 0x22, 0x33, 0x44}, "User B", 0},
    {{0x55, 0x66, 0x77, 0x88}, "Admin", 0}};

const int AUTH_CARD_COUNT = sizeof(authorizedCards) / sizeof(authorizedCards[0]);

bool unlocked = false;
bool showingMessage = false;
unsigned long unlockStartTime = 0;
unsigned long messageStartTime = 0;
char messageLine1[17] = "";
char messageLine2[17] = "";

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();

  doorServo.attach(PIN_SERVO);
  lockDoor();
  showTemporaryMessage("RFID Access", "Scan your card");
}

void loop() {
  // 先处理定时自动上锁，再读卡，最后刷新 LCD。
  updateTimedStates();
  processRfidCard();
  refreshDisplay();
}

void updateTimedStates() {
  // 开门只维持固定时长，超时后自动恢复门锁状态。
  if (unlocked && millis() - unlockStartTime >= UNLOCK_DURATION_MS) {
    lockDoor();
    unlocked = false;
    showTemporaryMessage("Door locked", "Scan next card");
  }

  if (showingMessage && millis() - messageStartTime >= MESSAGE_DURATION_MS) {
    showingMessage = false;
  }
}

void processRfidCard() {
  // 只在检测到新卡且 UID 成功读出后继续做权限判断。
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  int userIndex = matchAuthorizedCard(rfid.uid.uidByte, rfid.uid.size);
  if (userIndex >= 0) {
    // 授权成功后既开门也累加考勤次数，便于兼顾门禁和签到两种用途。
    authorizedCards[userIndex].attendanceCount++;
    unlockDoor();
    unlocked = true;
    unlockStartTime = millis();
    successBeep();

    snprintf(messageLine1, sizeof(messageLine1), "Welcome %s", authorizedCards[userIndex].name);
    snprintf(messageLine2, sizeof(messageLine2), "Count:%d", authorizedCards[userIndex].attendanceCount);
    showTemporaryMessage(messageLine1, messageLine2);
  } else {
    // 未授权卡不做开门，只给出拒绝提示和 UID 反馈。
    unlocked = false;
    errorBeep();
    showTemporaryMessage("Access denied", uidString(rfid.uid.uidByte, rfid.uid.size).c_str());
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

int matchAuthorizedCard(const byte* uid, byte uidSize) {
  // 当前项目按 4 字节 UID 做最基础授权匹配。
  if (uidSize < 4) {
    return -1;
  }

  for (int i = 0; i < AUTH_CARD_COUNT; i++) {
    bool match = true;
    for (int j = 0; j < 4; j++) {
      if (authorizedCards[i].uid[j] != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return i;
    }
  }

  return -1;
}

void refreshDisplay() {
  // 临时消息优先显示，消息结束后再回到待刷卡主页面。
  if (showingMessage) {
    lcd.setCursor(0, 0);
    printPaddedLine(messageLine1);
    lcd.setCursor(0, 1);
    printPaddedLine(messageLine2);
    return;
  }

  lcd.setCursor(0, 0);
  printPaddedLine(unlocked ? "Door: OPEN" : "Door: LOCKED");
  lcd.setCursor(0, 1);
  printPaddedLine("Scan RFID card");
}

void showTemporaryMessage(const char* line1, const char* line2) {
  snprintf(messageLine1, sizeof(messageLine1), "%s", line1);
  snprintf(messageLine2, sizeof(messageLine2), "%s", line2);
  showingMessage = true;
  messageStartTime = millis();
}

String uidString(const byte* uid, byte uidSize) {
  // 拒绝访问时把 UID 转成字符串，方便在 LCD 上定位是哪张未知卡。
  String text = "UID:";
  for (byte i = 0; i < uidSize; i++) {
    if (uid[i] < 0x10) {
      text += '0';
    }
    text += String(uid[i], HEX);
  }
  text.toUpperCase();
  return text;
}

void unlockDoor() {
  doorServo.write(UNLOCK_ANGLE);
}

void lockDoor() {
  doorServo.write(LOCK_ANGLE);
}

void successBeep() {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(80);
  digitalWrite(PIN_BUZZER, LOW);
}

void errorBeep() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(150);
    digitalWrite(PIN_BUZZER, LOW);
    delay(80);
  }
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
