#include <LiquidCrystal_I2C.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Servo.h>
#include <string.h>
#include <Wire.h>

// 迷你自动售货机原型：
// 通过按钮选择商品，使用 RFID 卡模拟支付余额，
// 支付成功后驱动舵机出货，并在 LCD 上显示库存和交易结果。

const int PIN_RFID_SS = 10;
const int PIN_RFID_RST = 9;
const int PIN_SELECT_A = 2;
const int PIN_SELECT_B = 3;
const int PIN_BUY_BUTTON = 4;
const int PIN_SERVO_A = 5;
const int PIN_SERVO_B = 6;
const int PIN_BUZZER = 8;

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoA;
Servo servoB;

struct Product {
  const char *name;
  int price;
  int stock;
};

Product products[2] = {
  {"Snack", 5, 5},
  {"Drink", 7, 4}
};

int selectedIndex = 0;
int cardBalance = 20;
char lastMessage[16] = "READY";

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(PIN_SELECT_A, INPUT_PULLUP);
  pinMode(PIN_SELECT_B, INPUT_PULLUP);
  pinMode(PIN_BUY_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  servoA.attach(PIN_SERVO_A);
  servoB.attach(PIN_SERVO_B);
  resetServos();

  lcd.init();
  lcd.backlight();
  updateDisplay();
}

void loop() {
  handleSelectionButtons();
  handlePurchaseFlow();
  updateDisplay();
}

void handleSelectionButtons() {
  static bool lastA = HIGH;
  static bool lastB = HIGH;
  bool buttonA = digitalRead(PIN_SELECT_A);
  bool buttonB = digitalRead(PIN_SELECT_B);

  if (lastA == HIGH && buttonA == LOW) {
    selectedIndex = 0;
    setMessage("SELECT A");
  }

  if (lastB == HIGH && buttonB == LOW) {
    selectedIndex = 1;
    setMessage("SELECT B");
  }

  lastA = buttonA;
  lastB = buttonB;
}

void handlePurchaseFlow() {
  static bool lastBuy = HIGH;
  bool buyButton = digitalRead(PIN_BUY_BUTTON);

  if (!(lastBuy == HIGH && buyButton == LOW)) {
    lastBuy = buyButton;
    return;
  }
  lastBuy = buyButton;

  Product &product = products[selectedIndex];

  if (product.stock <= 0) {
    setMessage("OUT OF STOCK");
    beep(3, 90);
    return;
  }

  // 购买时要求先刷卡，避免在未支付情况下直接驱动出货机构。
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    setMessage("TAP CARD");
    beep(2, 120);
    return;
  }

  if (cardBalance < product.price) {
    setMessage("NO BALANCE");
    beep(3, 90);
    rfid.PICC_HaltA();
    return;
  }

  cardBalance -= product.price;
  product.stock--;
  dispenseProduct(selectedIndex);
  setMessage("BUY OK");
  beep(1, 180);
  rfid.PICC_HaltA();
}

void dispenseProduct(int index) {
  // 两个商品各自绑定一个舵机，转动一次模拟单件出货。
  if (index == 0) {
    servoA.write(110);
    delay(350);
  } else {
    servoB.write(110);
    delay(350);
  }
  resetServos();
}

void resetServos() {
  servoA.write(20);
  servoB.write(20);
}

void setMessage(const char *message) {
  strncpy(lastMessage, message, sizeof(lastMessage) - 1);
  lastMessage[sizeof(lastMessage) - 1] = '\0';
}

void beep(int count, int durationMs) {
  for (int i = 0; i < count; ++i) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durationMs);
    digitalWrite(PIN_BUZZER, LOW);
    delay(70);
  }
}

void updateDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(products[selectedIndex].name);
  lcd.print(" $");
  lcd.print(products[selectedIndex].price);
  lcd.setCursor(0, 1);
  lcd.print("S:");
  lcd.print(products[selectedIndex].stock);
  lcd.print(" B:");
  lcd.print(cardBalance);
}
