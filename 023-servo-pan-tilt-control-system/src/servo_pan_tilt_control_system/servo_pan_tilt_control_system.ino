#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <Wire.h>

// 舵机云台控制系统：
// 读取双轴摇杆控制水平和俯仰舵机，同时在 OLED 上显示当前角度。
// 限位逻辑保证舵机不会超出可用机械范围。

const int PIN_JOYSTICK_X = A0;
const int PIN_JOYSTICK_Y = A1;
const int PIN_SERVO_PAN = 9;
const int PIN_SERVO_TILT = 10;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const int PAN_MIN = 10;
const int PAN_MAX = 170;
const int TILT_MIN = 20;
const int TILT_MAX = 160;
const int JOYSTICK_DEADZONE = 70;
const unsigned long DISPLAY_REFRESH_MS = 150;

Servo panServo;
Servo tiltServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 当前云台角度始终保存在软件变量中，避免直接依赖舵机内部状态。
int panAngle = 90;
int tiltAngle = 90;
unsigned long lastDisplayRefresh = 0;

void setup() {
  panServo.attach(PIN_SERVO_PAN);
  tiltServo.attach(PIN_SERVO_TILT);
  panServo.write(panAngle);
  tiltServo.write(tiltAngle);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }
}

void loop() {
  // 输入采样和显示刷新分离，避免 OLED 刷新频率限制摇杆响应。
  updateServoAngles();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    drawScreen();
    lastDisplayRefresh = millis();
  }
}

void updateServoAngles() {
  int x = analogRead(PIN_JOYSTICK_X) - 512;
  int y = analogRead(PIN_JOYSTICK_Y) - 512;

  // 死区用于过滤摇杆中位抖动，避免舵机持续微抖。
  if (abs(x) > JOYSTICK_DEADZONE) {
    panAngle += x > 0 ? 1 : -1;
  }

  if (abs(y) > JOYSTICK_DEADZONE) {
    tiltAngle += y > 0 ? -1 : 1;
  }

  panAngle = constrain(panAngle, PAN_MIN, PAN_MAX);
  tiltAngle = constrain(tiltAngle, TILT_MIN, TILT_MAX);

  panServo.write(panAngle);
  tiltServo.write(tiltAngle);
}

void drawScreen() {
  // OLED 用于显示两个自由度的角度，便于调试云台机械限位。
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Servo Pan Tilt");
  display.print("Pan : ");
  display.println(panAngle);
  display.print("Tilt: ");
  display.println(tiltAngle);
  display.print("Range: ");
  display.println("PAN/TILT");
  display.display();
}
