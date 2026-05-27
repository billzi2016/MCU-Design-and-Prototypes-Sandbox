#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MPU6050_tockn.h>
#include <Wire.h>

// 简易无人机姿态采集与显示系统：
// 使用 MPU6050 采集姿态角，进行基础滤波后在 OLED 上显示，
// 同时通过串口持续输出姿态数据，作为后续无线传输或飞控联调的基础。

const unsigned long SAMPLE_INTERVAL_MS = 20;
const float ALPHA = 0.85f;

MPU6050 mpu6050(Wire);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

unsigned long lastSampleTime = 0;
float rollDeg = 0.0f;
float pitchDeg = 0.0f;
float yawDeg = 0.0f;
float filteredRoll = 0.0f;
float filteredPitch = 0.0f;
float filteredYaw = 0.0f;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  mpu6050.begin();
  // 启动时先做一次陀螺零偏校准，减少静止状态下姿态漂移。
  mpu6050.calcGyroOffsets(true);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void loop() {
  if (millis() - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = millis();

  updateAttitude();
  updateDisplay();
  outputTelemetry();
}

void updateAttitude() {
  mpu6050.update();

  rollDeg = mpu6050.getAngleX();
  pitchDeg = mpu6050.getAngleY();
  yawDeg = mpu6050.getAngleZ();

  // 这里用一阶低通思想平滑姿态读数，目的是让显示和无线输出更稳定。
  filteredRoll = ALPHA * filteredRoll + (1.0f - ALPHA) * rollDeg;
  filteredPitch = ALPHA * filteredPitch + (1.0f - ALPHA) * pitchDeg;
  filteredYaw = ALPHA * filteredYaw + (1.0f - ALPHA) * yawDeg;
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Roll :");
  display.print(filteredRoll, 1);
  display.setCursor(0, 16);
  display.print("Pitch:");
  display.print(filteredPitch, 1);
  display.setCursor(0, 32);
  display.print("Yaw  :");
  display.print(filteredYaw, 1);
  display.setCursor(0, 48);
  display.print("Raw:");
  display.print(rollDeg, 0);
  display.print("/");
  display.print(pitchDeg, 0);
  display.display();
}

void outputTelemetry() {
  // 串口输出统一保持 CSV 风格，方便后续上位机、蓝牙或无线模块直接转发。
  Serial.print("ATT,");
  Serial.print(filteredRoll, 2);
  Serial.print(",");
  Serial.print(filteredPitch, 2);
  Serial.print(",");
  Serial.println(filteredYaw, 2);
}
