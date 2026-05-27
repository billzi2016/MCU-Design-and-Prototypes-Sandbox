#include <Servo.h>

// 小型机械臂分拣系统：
// 根据颜色传感器读数识别红/绿/蓝三类物体，
// 再用四舵机机械臂执行抓取、移动和放置动作。

const int PIN_S0 = 2;
const int PIN_S1 = 3;
const int PIN_S2 = 4;
const int PIN_S3 = 5;
const int PIN_OUT = 6;

const int PIN_SERVO_BASE = 8;
const int PIN_SERVO_ARM = 9;
const int PIN_SERVO_FOREARM = 10;
const int PIN_SERVO_GRIP = 11;

Servo servoBase;
Servo servoArm;
Servo servoForearm;
Servo servoGrip;

enum ColorType {
  COLOR_UNKNOWN,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_BLUE
};

void setup() {
  Serial.begin(115200);

  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_OUT, INPUT);

  digitalWrite(PIN_S0, HIGH);
  digitalWrite(PIN_S1, LOW);

  servoBase.attach(PIN_SERVO_BASE);
  servoArm.attach(PIN_SERVO_ARM);
  servoForearm.attach(PIN_SERVO_FOREARM);
  servoGrip.attach(PIN_SERVO_GRIP);

  // 上电先回到统一待机姿态，避免机械臂在未知角度下直接执行抓取发生干涉。
  moveToHomePose();
}

void loop() {
  // 先识别颜色，再执行抓取和分拣动作，形成一个完整循环。
  ColorType detectedColor = detectColor();
  if (detectedColor == COLOR_UNKNOWN) {
    delay(400);
    return;
  }

  pickObject();
  placeObjectByColor(detectedColor);
  // 每次分拣结束都回到统一原点，便于下一轮识别时目标位置和姿态一致。
  moveToHomePose();
  delay(500);
}

ColorType detectColor() {
  // TCS3200 类传感器通常通过不同滤光配置分别读取 RGB 响应。
  int red = readColorChannel(LOW, LOW);
  int green = readColorChannel(HIGH, HIGH);
  int blue = readColorChannel(LOW, HIGH);

  Serial.print("R:");
  Serial.print(red);
  Serial.print(" G:");
  Serial.print(green);
  Serial.print(" B:");
  Serial.println(blue);

  int minValue = red;
  ColorType color = COLOR_RED;

  if (green < minValue) {
    minValue = green;
    color = COLOR_GREEN;
  }

  if (blue < minValue) {
    minValue = blue;
    color = COLOR_BLUE;
  }

  // 当三个颜色通道都不够明显时，认为目标不存在或颜色不在预设范围内。
  if (minValue > 180) {
    return COLOR_UNKNOWN;
  }

  return color;
}

int readColorChannel(int s2, int s3) {
  // pulseIn 返回的是频率对应的脉宽，值越小通常表示该颜色响应越强。
  digitalWrite(PIN_S2, s2);
  digitalWrite(PIN_S3, s3);
  delay(20);
  return pulseIn(PIN_OUT, LOW, 30000);
}

void pickObject() {
  // 抓取动作拆成“移动到抓取位 - 下探 - 闭合夹爪”三步，便于后续单独调节。
  servoBase.write(90);
  servoArm.write(110);
  servoForearm.write(120);
  servoGrip.write(90);
  delay(400);

  servoArm.write(140);
  servoForearm.write(150);
  delay(400);
  servoGrip.write(40);
  delay(400);
}

void placeObjectByColor(ColorType color) {
  // 不同颜色只改变底座目标角度，其余释放动作保持一致。
  if (color == COLOR_RED) {
    moveToDropPose(40);
  } else if (color == COLOR_GREEN) {
    moveToDropPose(90);
  } else if (color == COLOR_BLUE) {
    moveToDropPose(140);
  }
}

void moveToDropPose(int baseAngle) {
  // 放置区只通过底座转角区分，机械臂和前臂维持相对稳定的释放姿态。
  servoBase.write(baseAngle);
  servoArm.write(95);
  servoForearm.write(100);
  delay(500);
  servoGrip.write(90);
  delay(400);
}

void moveToHomePose() {
  // 每次分拣结束都回到统一初始姿态，便于下一次识别与抓取。
  servoBase.write(90);
  servoArm.write(80);
  servoForearm.write(80);
  servoGrip.write(90);
  delay(400);
}
