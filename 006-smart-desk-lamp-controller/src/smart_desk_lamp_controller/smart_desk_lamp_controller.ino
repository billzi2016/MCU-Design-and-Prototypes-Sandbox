#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

const int PIN_LIGHT_SENSOR = A0;
const int PIN_MODE = 2;
const int PIN_BRIGHTNESS_UP = 3;
const int PIN_BRIGHTNESS_DOWN = 4;
const int PIN_LED_PWM = 5;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const int BUTTON_COUNT = 3;
const int BUTTON_PINS[BUTTON_COUNT] = {
  PIN_MODE,
  PIN_BRIGHTNESS_UP,
  PIN_BRIGHTNESS_DOWN
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long SCREEN_REFRESH_MS = 150;
const float FILTER_ALPHA = 0.12f;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool autoMode = true;
int lightRaw = 0;
float lightFiltered = 0.0f;
int targetBrightness = 0;
int manualBrightness = 180;

int stableButtonState[BUTTON_COUNT];
int lastButtonReading[BUTTON_COUNT];
unsigned long lastDebounceTime[BUTTON_COUNT];
unsigned long lastScreenRefresh = 0;

void setup() {
  pinMode(PIN_LED_PWM, OUTPUT);

  // 使用内部上拉电阻，按键按下时读数为 LOW。
  for (int i = 0; i < BUTTON_COUNT; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    stableButtonState[i] = HIGH;
    lastButtonReading[i] = HIGH;
    lastDebounceTime[i] = 0;
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  lightRaw = analogRead(PIN_LIGHT_SENSOR);
  lightFiltered = static_cast<float>(lightRaw);
  targetBrightness = manualBrightness;
  analogWrite(PIN_LED_PWM, targetBrightness);
}

void loop() {
  handleButtons();
  updateLightReading();
  updateBrightness();

  // 定期刷新 OLED，避免过度频繁重绘。
  if (millis() - lastScreenRefresh >= SCREEN_REFRESH_MS) {
    drawScreen();
    lastScreenRefresh = millis();
  }
}

void handleButtons() {
  if (buttonPressed(0)) {
    autoMode = !autoMode;
  }

  if (!autoMode && buttonPressed(1)) {
    manualBrightness += 10;
    if (manualBrightness > 255) {
      manualBrightness = 255;
    }
  }

  if (!autoMode && buttonPressed(2)) {
    manualBrightness -= 10;
    if (manualBrightness < 0) {
      manualBrightness = 0;
    }
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

void updateLightReading() {
  lightRaw = analogRead(PIN_LIGHT_SENSOR);

  // 对采样值做一阶低通平滑，减少亮度抖动。
  lightFiltered = FILTER_ALPHA * static_cast<float>(lightRaw) +
                  (1.0f - FILTER_ALPHA) * lightFiltered;
}

void updateBrightness() {
  if (autoMode) {
    // 光照越暗，输出亮度越高。
    targetBrightness = map(static_cast<int>(lightFiltered), 0, 1023, 255, 20);
  } else {
    targetBrightness = manualBrightness;
  }

  if (targetBrightness < 0) {
    targetBrightness = 0;
  }
  if (targetBrightness > 255) {
    targetBrightness = 255;
  }

  analogWrite(PIN_LED_PWM, targetBrightness);
}

void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Smart Desk Lamp");

  display.setCursor(0, 16);
  display.print("Mode: ");
  display.print(autoMode ? "AUTO" : "MANUAL");

  display.setCursor(0, 30);
  display.print("Light: ");
  display.print(static_cast<int>(lightFiltered));

  display.setCursor(0, 44);
  display.print("Output: ");
  display.print(targetBrightness);

  if (!autoMode) {
    display.setCursor(0, 56);
    display.print("Set: ");
    display.print(manualBrightness);
  } else {
    display.setCursor(0, 56);
    display.print("Auto adjust active");
  }

  display.display();
}

