#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// 电子密码锁：
// 通过矩阵键盘输入密码，LCD 给出交互反馈，舵机模拟门锁动作。
// 核心设计是状态机：正常输入、开锁、锁定、验证旧密码、输入新密码。

const int PIN_SERVO = 10;
const int PIN_BUZZER = 11;

const int LOCKED_ANGLE = 0;
const int UNLOCKED_ANGLE = 90;
const int PASSWORD_LENGTH = 4;
const int MAX_FAILED_ATTEMPTS = 3;

const unsigned long UNLOCK_DURATION_MS = 3000;
const unsigned long LOCKOUT_DURATION_MS = 10000;
const unsigned long MESSAGE_DURATION_MS = 1200;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lockServo;

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8, 9};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 这些状态决定键盘输入应该被解释成什么操作。
enum SystemState {
  STATE_INPUT,
  STATE_UNLOCKED,
  STATE_LOCKOUT,
  STATE_CHANGE_VERIFY,
  STATE_CHANGE_NEW
};

SystemState state = STATE_INPUT;

// password 是当前有效密码，inputBuffer 是本次输入的临时缓冲区。
char password[PASSWORD_LENGTH + 1] = "1234";
char inputBuffer[PASSWORD_LENGTH + 1] = "";
int inputLength = 0;
int failedAttempts = 0;

unsigned long stateStartTime = 0;
unsigned long messageStartTime = 0;
bool showingMessage = false;

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  lcd.init();
  lcd.backlight();

  lockServo.attach(PIN_SERVO);
  lockDoor();

  showInputScreen();
}

void loop() {
  // 先更新超时状态，再读取键盘，保证自动上锁和临时消息按时结束。
  updateTimedStates();

  char key = keypad.getKey();
  if (key) {
    beep(50);
    handleKey(key);
  }
}

void handleKey(char key) {
  // 锁定和已开锁阶段不接受新的输入，避免状态互相打断。
  if (state == STATE_LOCKOUT || state == STATE_UNLOCKED) {
    return;
  }

  if (key >= '0' && key <= '9') {
    appendDigit(key);
    return;
  }

  if (key == '*') {
    clearInput();
    redrawCurrentScreen();
    return;
  }

  if (key == '#') {
    confirmInput();
    return;
  }

  if (key == 'A' && state == STATE_INPUT) {
    enterChangeVerify();
  }
}

void appendDigit(char key) {
  // 密码固定为 4 位，超过长度后不再继续写入。
  if (inputLength >= PASSWORD_LENGTH) {
    return;
  }

  inputBuffer[inputLength] = key;
  inputLength++;
  inputBuffer[inputLength] = '\0';
  redrawCurrentScreen();
}

void confirmInput() {
  // 所有需要“确认”的流程都复用 # 键，但具体行为由当前状态决定。
  if (inputLength != PASSWORD_LENGTH) {
    showTemporaryMessage("Need 4 digits", "Press * clear");
    return;
  }

  if (state == STATE_INPUT) {
    verifyPasswordForUnlock();
  } else if (state == STATE_CHANGE_VERIFY) {
    verifyPasswordForChange();
  } else if (state == STATE_CHANGE_NEW) {
    saveNewPassword();
  }
}

void verifyPasswordForUnlock() {
  if (passwordMatches()) {
    failedAttempts = 0;
    clearInput();
    unlockDoor();
    state = STATE_UNLOCKED;
    stateStartTime = millis();
    showMessage("Unlocked", "Auto lock soon");
  } else {
    // 开锁失败和改密验证失败都统一走错误处理逻辑。
    handleWrongPassword();
  }
}

void verifyPasswordForChange() {
  if (passwordMatches()) {
    clearInput();
    state = STATE_CHANGE_NEW;
    showChangeNewScreen();
  } else {
    handleWrongPassword();
    state = STATE_INPUT;
  }
}

void saveNewPassword() {
  // 将输入缓冲区中的 4 位数字保存为新密码。
  for (int i = 0; i < PASSWORD_LENGTH; i++) {
    password[i] = inputBuffer[i];
  }
  password[PASSWORD_LENGTH] = '\0';

  clearInput();
  failedAttempts = 0;
  state = STATE_INPUT;
  showTemporaryMessage("Password saved", "Use new code");
}

bool passwordMatches() {
  for (int i = 0; i < PASSWORD_LENGTH; i++) {
    if (inputBuffer[i] != password[i]) {
      return false;
    }
  }
  return true;
}

void handleWrongPassword() {
  // 错误次数累计到上限后进入临时锁定，而不是无限次尝试。
  failedAttempts++;
  clearInput();
  errorBeep();

  if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
    state = STATE_LOCKOUT;
    stateStartTime = millis();
    failedAttempts = 0;
    showMessage("Locked 10 sec", "Too many errors");
  } else {
    showTemporaryMessage("Wrong password", "Try again");
  }
}

void enterChangeVerify() {
  clearInput();
  state = STATE_CHANGE_VERIFY;
  showChangeVerifyScreen();
}

void updateTimedStates() {
  // 这里统一处理所有“过一段时间后自动切回”的状态。
  if (state == STATE_UNLOCKED && millis() - stateStartTime >= UNLOCK_DURATION_MS) {
    lockDoor();
    state = STATE_INPUT;
    showInputScreen();
  }

  if (state == STATE_LOCKOUT && millis() - stateStartTime >= LOCKOUT_DURATION_MS) {
    state = STATE_INPUT;
    showInputScreen();
  }

  if (showingMessage && millis() - messageStartTime >= MESSAGE_DURATION_MS) {
    showingMessage = false;
    redrawCurrentScreen();
  }
}

void unlockDoor() {
  lockServo.write(UNLOCKED_ANGLE);
}

void lockDoor() {
  lockServo.write(LOCKED_ANGLE);
}

void clearInput() {
  for (int i = 0; i <= PASSWORD_LENGTH; i++) {
    inputBuffer[i] = '\0';
  }
  inputLength = 0;
}

void redrawCurrentScreen() {
  // 临时提示显示期间不立刻重绘原页面，避免提示一闪而过。
  if (showingMessage) {
    return;
  }

  if (state == STATE_INPUT) {
    showInputScreen();
  } else if (state == STATE_CHANGE_VERIFY) {
    showChangeVerifyScreen();
  } else if (state == STATE_CHANGE_NEW) {
    showChangeNewScreen();
  }
}

void showInputScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter password");
  drawMaskedInput();
}

void showChangeVerifyScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Old password");
  drawMaskedInput();
}

void showChangeNewScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("New password");
  drawMaskedInput();
}

void drawMaskedInput() {
  lcd.setCursor(0, 1);
  lcd.print("Code: ");

  // 输入内容只显示星号，不直接显示数字。
  for (int i = 0; i < inputLength; i++) {
    lcd.print('*');
  }

  for (int i = inputLength; i < PASSWORD_LENGTH; i++) {
    lcd.print('_');
  }
}

void showMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void showTemporaryMessage(const char* line1, const char* line2) {
  showingMessage = true;
  messageStartTime = millis();
  showMessage(line1, line2);
}

void beep(unsigned int durationMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durationMs);
  digitalWrite(PIN_BUZZER, LOW);
}

void errorBeep() {
  // 用三连短鸣把普通按键反馈和错误报警区分开。
  for (int i = 0; i < 3; i++) {
    beep(120);
    delay(80);
  }
}
