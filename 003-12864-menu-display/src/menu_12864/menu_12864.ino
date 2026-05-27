#include <U8g2lib.h>

// 12864 菜单交互系统：
// 用最简单的多级状态思想，演示“选中项”和“编辑项”的区别。
// 这个项目本质上是后续其他项目的人机交互模板。

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 13, 11, 10, U8X8_PIN_NONE);

const int PIN_UP = 2;
const int PIN_DOWN = 3;
const int PIN_OK = 4;
const int PIN_BACK = 5;

const int BUTTON_COUNT = 4;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_UP,
  PIN_DOWN,
  PIN_OK,
  PIN_BACK
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long SCREEN_REFRESH_MS = 150;

const int MENU_COUNT = 3;
const char* MENU_NAMES[MENU_COUNT] = {
  "Brightness",
  "Alarm",
  "Interval"
};

// selectedIndex 表示当前光标落在哪个菜单项；
// editMode 表示当前是在“移动光标”还是在“修改值”。
int selectedIndex = 0;
bool editMode = false;

int brightnessThreshold = 60;
int alarmThreshold = 80;
int sampleInterval = 5;

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];
unsigned long lastScreenRefresh = 0;

void setup() {
  // 使用内部上拉电阻，按键按下时读数为 LOW。
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  u8g2.begin();
  u8g2.enableUTF8Print();
}

void loop() {
  handleButtons();

  // 定期刷新屏幕，避免过度重绘。
  if (millis() - lastScreenRefresh >= SCREEN_REFRESH_MS) {
    drawScreen();
    lastScreenRefresh = millis();
  }
}

void handleButtons() {
  if (buttonPressed(0)) {
    handleUp();
  }
  if (buttonPressed(1)) {
    handleDown();
  }
  if (buttonPressed(2)) {
    handleOk();
  }
  if (buttonPressed(3)) {
    handleBack();
  }
}

bool buttonPressed(int index) {
  bool pressed = false;
  int reading = digitalRead(BUTTON_PINS[index]);

  // 如果读数发生变化，重新开始消抖计时。
  if (reading != lastButtonReading[index]) {
    lastDebounceTime[index] = millis();
  }

  // 读数保持稳定超过消抖时间后，才认为状态有效。
  if (millis() - lastDebounceTime[index] > DEBOUNCE_MS) {
    if (reading != stableButtonState[index]) {
      stableButtonState[index] = reading;

      // INPUT_PULLUP 下，LOW 表示按键被按下。
      if (stableButtonState[index] == LOW) {
        pressed = true;
      }
    }
  }

  lastButtonReading[index] = reading;
  return pressed;
}

void handleUp() {
  if (editMode) {
    // 编辑模式下，上键不再移动菜单，而是直接修改当前参数。
    changeCurrentValue(1);
  } else {
    selectedIndex--;
    if (selectedIndex < 0) {
      selectedIndex = MENU_COUNT - 1;
    }
  }
}

void handleDown() {
  if (editMode) {
    changeCurrentValue(-1);
  } else {
    selectedIndex++;
    if (selectedIndex >= MENU_COUNT) {
      selectedIndex = 0;
    }
  }
}

void handleOk() {
  // OK 键只负责进入编辑，不负责提交，退出由 BACK 键完成。
  editMode = true;
}

void handleBack() {
  // 当前项目不做多级页面跳转，因此 BACK 的含义就是退出编辑。
  editMode = false;
}

void changeCurrentValue(int step) {
  if (selectedIndex == 0) {
    brightnessThreshold = constrain(brightnessThreshold + step, 0, 100);
  } else if (selectedIndex == 1) {
    alarmThreshold = constrain(alarmThreshold + step, 0, 100);
  } else if (selectedIndex == 2) {
    sampleInterval = constrain(sampleInterval + step, 1, 60);
  }
}

int currentValue(int index) {
  if (index == 0) {
    return brightnessThreshold;
  }
  if (index == 1) {
    return alarmThreshold;
  }
  return sampleInterval;
}

void drawScreen() {
  // 所有显示内容每帧重新绘制，确保参数变化后界面始终一致。
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(0, 10, "12864 Menu System");
  u8g2.drawHLine(0, 13, 128);

  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 28 + i * 12;

    // 当前选中项用箭头标识。
    if (i == selectedIndex) {
      u8g2.drawStr(0, y, editMode ? "*" : ">");
    }

    u8g2.drawStr(10, y, MENU_NAMES[i]);
    u8g2.setCursor(88, y);
    u8g2.print(currentValue(i));
  }

  u8g2.drawHLine(0, 55, 128);
  u8g2.setCursor(0, 64);

  if (editMode) {
    u8g2.print("UP/DOWN edit BACK exit");
  } else {
    u8g2.print("UP/DOWN select OK edit");
  }

  u8g2.sendBuffer();
}
