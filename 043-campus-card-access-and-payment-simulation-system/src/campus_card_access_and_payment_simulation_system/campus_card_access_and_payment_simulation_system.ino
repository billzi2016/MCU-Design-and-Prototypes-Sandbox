#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <MFRC522.h>
#include <SPI.h>
#include <stdio.h>
#include <Wire.h>

// 校园一卡通门禁与消费模拟系统：
// 通过 RFID 识别用户身份，支持门禁放行、余额显示、消费扣费和管理员充值。
// EEPROM 用于保存余额和权限信息，模拟一卡通离线本地数据管理。

const int PIN_RFID_SS = 10;
const int PIN_RFID_RST = 9;
const int PIN_BUZZER = 8;
const int PIN_ENTRY_RELAY = 7;
const int PIN_PAY_BUTTON = 6;
const int PIN_TOPUP_BUTTON = 5;

const int EEPROM_CARD_COUNT_ADDR = 0;
const int EEPROM_CARD_BASE_ADDR = 16;
const int CARD_RECORD_SIZE = 8;
const int DEFAULT_COST = 6;

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

struct CardRecord {
  byte uid[4];
  byte balance;
  byte role;
  byte reserved[2];
};

char lastUidString[12] = "NONE";
char lastMessage[16] = "READY";
int lastBalance = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_ENTRY_RELAY, OUTPUT);
  pinMode(PIN_PAY_BUTTON, INPUT_PULLUP);
  pinMode(PIN_TOPUP_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_ENTRY_RELAY, LOW);

  lcd.init();
  lcd.backlight();

  initCardStore();
  updateDisplay();
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  CardRecord card;
  int index = findCard(card);
  formatUidString(lastUidString, sizeof(lastUidString));

  if (index < 0) {
    setMessage("UNKNOWN", 0);
    beep(3, 90);
    rfid.PICC_HaltA();
    updateDisplay();
    return;
  }

  bool payMode = digitalRead(PIN_PAY_BUTTON) == LOW;
  bool topupMode = digitalRead(PIN_TOPUP_BUTTON) == LOW;

  if (topupMode && card.role == 1) {
    // 管理员卡在充值模式下为自己加余额，演示最基本的账户维护入口。
    card.balance = min(255, card.balance + 20);
    writeCard(index, card);
    setMessage("TOPUP", card.balance);
    beep(2, 120);
  } else if (payMode) {
    handlePayment(index, card);
  } else {
    handleAccess(card);
  }

  rfid.PICC_HaltA();
  updateDisplay();
}

void initCardStore() {
  if (EEPROM.read(EEPROM_CARD_COUNT_ADDR) != 2) {
    EEPROM.write(EEPROM_CARD_COUNT_ADDR, 2);

    CardRecord student = {{0x11, 0x22, 0x33, 0x44}, 30, 0, {0, 0}};
    CardRecord admin = {{0xAA, 0xBB, 0xCC, 0xDD}, 100, 1, {0, 0}};

    writeCard(0, student);
    writeCard(1, admin);
  }
}

int findCard(CardRecord &outCard) {
  byte count = EEPROM.read(EEPROM_CARD_COUNT_ADDR);
  for (int i = 0; i < count; ++i) {
    CardRecord card = readCard(i);
    bool match = true;
    for (int j = 0; j < 4; ++j) {
      if (card.uid[j] != rfid.uid.uidByte[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      outCard = card;
      return i;
    }
  }
  return -1;
}

void handleAccess(const CardRecord &card) {
  // 门禁场景只看卡是否存在及权限角色，不消耗余额。
  digitalWrite(PIN_ENTRY_RELAY, HIGH);
  delay(1500);
  digitalWrite(PIN_ENTRY_RELAY, LOW);
  setMessage(card.role == 1 ? "ADMIN IN" : "ACCESS OK", card.balance);
  beep(1, 180);
}

void handlePayment(int index, CardRecord &card) {
  // 消费模式下优先检查余额，余额不足时拒绝扣费。
  if (card.balance < DEFAULT_COST) {
    setMessage("NO MONEY", card.balance);
    beep(3, 110);
    return;
  }

  card.balance -= DEFAULT_COST;
  writeCard(index, card);
  setMessage("PAY OK", card.balance);
  beep(2, 150);
}

CardRecord readCard(int index) {
  CardRecord card;
  int base = EEPROM_CARD_BASE_ADDR + index * CARD_RECORD_SIZE;
  for (int i = 0; i < CARD_RECORD_SIZE; ++i) {
    reinterpret_cast<byte *>(&card)[i] = EEPROM.read(base + i);
  }
  return card;
}

void writeCard(int index, const CardRecord &card) {
  int base = EEPROM_CARD_BASE_ADDR + index * CARD_RECORD_SIZE;
  for (int i = 0; i < CARD_RECORD_SIZE; ++i) {
    EEPROM.update(base + i, reinterpret_cast<const byte *>(&card)[i]);
  }
}

void formatUidString(char *buffer, size_t size) {
  snprintf(buffer, size, "%02X%02X", rfid.uid.uidByte[0], rfid.uid.uidByte[1]);
}

void setMessage(const char *message, int balance) {
  strncpy(lastMessage, message, sizeof(lastMessage) - 1);
  lastMessage[sizeof(lastMessage) - 1] = '\0';
  lastBalance = balance;
}

void beep(int count, int durationMs) {
  for (int i = 0; i < count; ++i) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durationMs);
    digitalWrite(PIN_BUZZER, LOW);
    delay(80);
  }
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(lastMessage);
  lcd.setCursor(0, 1);
  lcd.print(lastUidString);
  lcd.print(" B:");
  lcd.print(lastBalance);
}
