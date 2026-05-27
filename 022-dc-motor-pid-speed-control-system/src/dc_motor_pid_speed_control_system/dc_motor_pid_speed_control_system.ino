#include <PID_v1.h>

// 直流电机 PID 调速系统：
// 通过编码器脉冲估算当前转速，用 PID 计算 PWM 输出，
// 使直流电机尽量稳定在目标速度附近，并通过串口打印调速过程。

const int PIN_MOTOR_PWM = 5;
const int PIN_MOTOR_IN1 = 6;
const int PIN_MOTOR_IN2 = 7;
const int PIN_ENCODER = 2;
const int PIN_BTN_TARGET = 8;
const int PIN_BTN_ENABLE = 9;

const unsigned long CONTROL_INTERVAL_MS = 100;
const unsigned long DEBOUNCE_MS = 40;
const double PULSES_PER_REVOLUTION = 20.0;

volatile unsigned long encoderPulses = 0;

double targetRpm = 120.0;
double measuredRpm = 0.0;
double pwmOutput = 0.0;
double kp = 2.2;
double ki = 4.0;
double kd = 0.15;

PID speedPid(&measuredRpm, &pwmOutput, &targetRpm, kp, ki, kd, DIRECT);

bool motorEnabled = true;
int stableButtonState[2] = {HIGH, HIGH};
int lastButtonReading[2] = {HIGH, HIGH};
unsigned long lastDebounceTime[2] = {0, 0};
unsigned long lastControlTime = 0;
unsigned long lastPulseSnapshot = 0;

void IRAM_ATTR onEncoderPulse() {
  encoderPulses++;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_MOTOR_PWM, OUTPUT);
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  pinMode(PIN_BTN_TARGET, INPUT_PULLUP);
  pinMode(PIN_BTN_ENABLE, INPUT_PULLUP);
  pinMode(PIN_ENCODER, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER), onEncoderPulse, RISING);

  digitalWrite(PIN_MOTOR_IN1, HIGH);
  digitalWrite(PIN_MOTOR_IN2, LOW);

  speedPid.SetOutputLimits(0, 255);
  speedPid.SetSampleTime(CONTROL_INTERVAL_MS);
  speedPid.SetMode(AUTOMATIC);
  lastControlTime = millis();
}

void loop() {
  // 这里用固定控制周期更新转速和 PID，避免频率漂移影响调参。
  handleButtons();
  updateSpeedControl();
}

void handleButtons() {
  // 一个按键调目标速度，一个按键控制使能，便于现场做阶跃响应测试。
  if (buttonPressed(0)) {
    targetRpm += 40.0;
    if (targetRpm > 280.0) {
      targetRpm = 80.0;
    }
  }

  if (buttonPressed(1)) {
    motorEnabled = !motorEnabled;
    if (!motorEnabled) {
      analogWrite(PIN_MOTOR_PWM, 0);
    }
  }
}

bool buttonPressed(int index) {
  int pin = index == 0 ? PIN_BTN_TARGET : PIN_BTN_ENABLE;
  bool pressed = false;
  int reading = digitalRead(pin);

  if (reading != lastButtonReading[index]) {
    lastDebounceTime[index] = millis();
  }

  if (millis() - lastDebounceTime[index] > DEBOUNCE_MS) {
    if (reading != stableButtonState[index]) {
      stableButtonState[index] = reading;
      if (stableButtonState[index] == LOW) {
        pressed = true;
      }
    }
  }

  lastButtonReading[index] = reading;
  return pressed;
}

void updateSpeedControl() {
  if (millis() - lastControlTime < CONTROL_INTERVAL_MS) {
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - lastControlTime;
  lastControlTime = now;

  // 用控制周期内的编码器脉冲增量估算当前转速。
  noInterrupts();
  unsigned long pulseSnapshot = encoderPulses;
  interrupts();

  unsigned long pulseDelta = pulseSnapshot - lastPulseSnapshot;
  lastPulseSnapshot = pulseSnapshot;

  double revolutions = static_cast<double>(pulseDelta) / PULSES_PER_REVOLUTION;
  measuredRpm = revolutions * 60000.0 / static_cast<double>(elapsed);

  if (motorEnabled) {
    // 电机使能时才运行 PID，否则强制输出 0。
    speedPid.Compute();
    analogWrite(PIN_MOTOR_PWM, static_cast<int>(pwmOutput));
  } else {
    pwmOutput = 0.0;
    analogWrite(PIN_MOTOR_PWM, 0);
  }

  Serial.print("TargetRPM:");
  Serial.print(targetRpm);
  Serial.print(",MeasuredRPM:");
  Serial.print(measuredRpm);
  Serial.print(",PWM:");
  Serial.println(pwmOutput);
}
