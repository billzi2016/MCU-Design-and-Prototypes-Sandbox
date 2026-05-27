#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Servo.h>
#include <stdio.h>
#include <string.h>
#include <Wire.h>

// 智能仓储分拣原型系统：
// 通过入料检测、RFID 识别和颜色识别确定货物类型，
// 再控制传送带与机械臂完成拣取、放置和状态显示。

const int PIN_ITEM_SENSOR = 2;
const int PIN_CONVEYOR_RELAY = 3;
const int PIN_ARM_BASE = 5;
const int PIN_ARM_LIFT = 6;
const int PIN_ARM_GRIP = 9;

const int PIN_TCS_S0 = A0;
const int PIN_TCS_S1 = A1;
const int PIN_TCS_S2 = A2;
const int PIN_TCS_S3 = A3;
const int PIN_TCS_OUT = A4;

const int PIN_RFID_SS = 10;
const int PIN_RFID_RST = 8;

const unsigned long RFID_TIMEOUT_MS = 3000;
// 停带后先等物体稳定，减少读卡距离变化和机械臂抓取偏位。
const unsigned long ITEM_SETTLE_MS = 450;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoBase;
Servo servoLift;
Servo servoGrip;

enum SortState {
  WAIT_ITEM,
  IDENTIFY_ITEM,
  PICK_ITEM,
  PLACE_ITEM,
  SORT_COMPLETE
};

SortState sortState = WAIT_ITEM;

// itemTag 表示业务属性，itemColor 表示颜色分类，targetZone 才是最终投放决策结果。
char itemUid[24] = "NONE";
char itemTag[16] = "UNKNOWN";
char itemColor[16] = "UNKNOWN";
char targetZone[16] = "ZONE-2";
unsigned long identifyStart = 0;
int sortedCount = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(PIN_ITEM_SENSOR, INPUT_PULLUP);
  pinMode(PIN_CONVEYOR_RELAY, OUTPUT);
  pinMode(PIN_TCS_S0, OUTPUT);
  pinMode(PIN_TCS_S1, OUTPUT);
  pinMode(PIN_TCS_S2, OUTPUT);
  pinMode(PIN_TCS_S3, OUTPUT);
  pinMode(PIN_TCS_OUT, INPUT);

  digitalWrite(PIN_TCS_S0, HIGH);
  digitalWrite(PIN_TCS_S1, LOW);

  servoBase.attach(PIN_ARM_BASE);
  servoLift.attach(PIN_ARM_LIFT);
  servoGrip.attach(PIN_ARM_GRIP);
  moveArmHome();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  startConveyor();
}

void loop() {
  // 状态机负责分拣节拍，显示更新单独拆开，避免机械动作期间屏幕完全不刷新。
  updateStateMachine();
  updateDisplay();
}

void updateStateMachine() {
  switch (sortState) {
    case WAIT_ITEM:
      if (itemDetected()) {
        // 货物一到位就立即停带，防止继续滑动导致识别窗口错过目标。
        stopConveyor();
        identifyStart = millis();
        sortState = IDENTIFY_ITEM;
      }
      break;

    case IDENTIFY_ITEM:
      if (millis() - identifyStart < ITEM_SETTLE_MS) {
        return;
      }
      // 先拿到 RFID 和颜色，再统一决策目标区，避免识别与放置规则分散在多个阶段。
      readRfidTag();
      readColorTag();
      decideTargetZone();
      sortState = PICK_ITEM;
      break;

    case PICK_ITEM:
      performPickSequence();
      sortState = PLACE_ITEM;
      break;

    case PLACE_ITEM:
      performPlaceSequence();
      sortedCount++;
      sortState = SORT_COMPLETE;
      break;

    case SORT_COMPLETE:
      resetItemState();
      startConveyor();
      sortState = WAIT_ITEM;
      break;
  }
}

bool itemDetected() {
  return digitalRead(PIN_ITEM_SENSOR) == LOW;
}

void readRfidTag() {
  unsigned long start = millis();
  strncpy(itemUid, "NO-RFID", sizeof(itemUid) - 1);
  strncpy(itemTag, "UNKNOWN", sizeof(itemTag) - 1);

  // 读取 RFID 时给一个有限超时，避免标签缺失时整条分拣线永久卡死。
  while (millis() - start < RFID_TIMEOUT_MS) {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
      continue;
    }

    formatUid(itemUid, sizeof(itemUid));
    if (rfid.uid.uidByte[0] % 3 == 0) {
      strncpy(itemTag, "VIP", sizeof(itemTag) - 1);
    } else if (rfid.uid.uidByte[0] % 3 == 1) {
      strncpy(itemTag, "NORMAL", sizeof(itemTag) - 1);
    } else {
      strncpy(itemTag, "RETURN", sizeof(itemTag) - 1);
    }
    rfid.PICC_HaltA();
    return;
  }
}

void readColorTag() {
  int red = readColorChannel(LOW, LOW);
  int green = readColorChannel(HIGH, HIGH);
  int blue = readColorChannel(LOW, HIGH);

  // TCS3200 返回的是脉宽量，通常值越小说明该颜色响应越强。
  if (red < green && red < blue) {
    strncpy(itemColor, "RED", sizeof(itemColor) - 1);
  } else if (green < red && green < blue) {
    strncpy(itemColor, "GREEN", sizeof(itemColor) - 1);
  } else if (blue < red && blue < green) {
    strncpy(itemColor, "BLUE", sizeof(itemColor) - 1);
  } else {
    strncpy(itemColor, "MIXED", sizeof(itemColor) - 1);
  }
}

void decideTargetZone() {
  // RFID 用来区分业务优先级，颜色用于决定基础分类，二者组合后得到最终投放区。
  if (strcmp(itemTag, "VIP") == 0) {
    strncpy(targetZone, "ZONE-1", sizeof(targetZone) - 1);
  } else if (strcmp(itemColor, "BLUE") == 0) {
    strncpy(targetZone, "ZONE-3", sizeof(targetZone) - 1);
  } else {
    strncpy(targetZone, "ZONE-2", sizeof(targetZone) - 1);
  }
}

void performPickSequence() {
  // 抓取动作保持固定顺序，先张爪再下探，能降低夹爪直接撞货物的概率。
  servoGrip.write(88);
  delay(250);
  servoBase.write(90);
  servoLift.write(132);
  delay(500);
  servoGrip.write(42);
  delay(300);
  servoLift.write(92);
  delay(400);
}

void performPlaceSequence() {
  // 放置区通过底座角区分，便于在简单机构上快速实现多区域投放。
  if (strcmp(targetZone, "ZONE-1") == 0) {
    servoBase.write(45);
  } else if (strcmp(targetZone, "ZONE-3") == 0) {
    servoBase.write(135);
  } else {
    servoBase.write(90);
  }

  servoLift.write(118);
  delay(450);
  servoGrip.write(88);
  delay(320);
  moveArmHome();
}

void moveArmHome() {
  // 每轮都回原点，下一件货物的抓取起始条件才一致。
  servoBase.write(90);
  servoLift.write(90);
  servoGrip.write(88);
  delay(420);
}

int readColorChannel(int s2, int s3) {
  digitalWrite(PIN_TCS_S2, s2);
  digitalWrite(PIN_TCS_S3, s3);
  delay(20);
  return pulseIn(PIN_TCS_OUT, LOW, 30000);
}

void formatUid(char *buffer, size_t size) {
  buffer[0] = '\0';
  for (byte i = 0; i < rfid.uid.size; ++i) {
    char part[4];
    snprintf(part, sizeof(part), "%02X", rfid.uid.uidByte[i]);
    strncat(buffer, part, size - strlen(buffer) - 1);
  }
}

void startConveyor() {
  // 这里假定继电器高电平有效，如硬件相反只需在这一层反转逻辑。
  digitalWrite(PIN_CONVEYOR_RELAY, HIGH);
}

void stopConveyor() {
  digitalWrite(PIN_CONVEYOR_RELAY, LOW);
}

void resetItemState() {
  strncpy(itemUid, "NONE", sizeof(itemUid) - 1);
  strncpy(itemTag, "UNKNOWN", sizeof(itemTag) - 1);
  strncpy(itemColor, "UNKNOWN", sizeof(itemColor) - 1);
  strncpy(targetZone, "ZONE-2", sizeof(targetZone) - 1);
}

void updateDisplay() {
  // OLED 直接显示关键中间结果，方便调试“识别错了”还是“投放规则错了”。
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("State:");
  display.print(sortState);
  display.setCursor(0, 12);
  display.print("RFID:");
  display.print(itemTag);
  display.setCursor(0, 24);
  display.print("Color:");
  display.print(itemColor);
  display.setCursor(0, 36);
  display.print("Target:");
  display.print(targetZone);
  display.setCursor(0, 48);
  display.print("Count:");
  display.print(sortedCount);
  display.display();
}
