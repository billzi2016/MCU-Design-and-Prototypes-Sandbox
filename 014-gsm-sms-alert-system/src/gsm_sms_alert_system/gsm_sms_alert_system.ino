#include <SoftwareSerial.h>

// GSM/SMS 短信报警系统：
// 使用门磁或开关量传感器检测异常，通过 SIM800L 发送短信，并接受短信命令。
// 当前示例实现 STATUS、ARM、DISARM、SIREN ON、SIREN OFF 五类短信指令。

const int PIN_SENSOR = 2;
const int PIN_BUZZER = 8;
const int PIN_SIM800_RX = 10;
const int PIN_SIM800_TX = 11;

const unsigned long BUZZER_TOGGLE_MS = 250;
const unsigned long SENSOR_DEBOUNCE_MS = 80;
const unsigned long SMS_RETRY_GAP_MS = 15000;

const char AUTHORIZED_PHONE[] = "+8613800000000";

SoftwareSerial sim800(PIN_SIM800_RX, PIN_SIM800_TX);

// 布防、警号开关和报警状态拆开保存，便于短信指令独立控制。
bool armed = true;
bool sirenEnabled = true;
bool alertActive = false;
bool buzzerState = false;
bool lastSensorStableState = HIGH;
bool lastSensorReading = HIGH;
bool alertSmsSent = false;
bool recoverSmsSent = false;
unsigned long lastDebounceTime = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long lastSmsAttempt = 0;
String serialLine = "";

void setup() {
  pinMode(PIN_SENSOR, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.begin(9600);
  sim800.begin(9600);

  delay(1000);
  sendCommand("AT");
  sendCommand("AT+CMGF=1");
  sendCommand("AT+CNMI=1,2,0,0,0");
}

void loop() {
  // 本地异常检测、蜂鸣器控制和串口短信处理并行进行。
  updateSensorState();
  updateAlarmLogic();
  updateBuzzer();
  readSim800Serial();
}

void updateSensorState() {
  int reading = digitalRead(PIN_SENSOR);

  if (reading != lastSensorReading) {
    lastDebounceTime = millis();
  }

  // 这里对门磁或开关量输入做基础消抖，避免抖动引发误短信。
  if (millis() - lastDebounceTime > SENSOR_DEBOUNCE_MS) {
    lastSensorStableState = reading;
  }

  lastSensorReading = reading;
}

void updateAlarmLogic() {
  bool sensorTriggered = lastSensorStableState == LOW;

  if (armed && sensorTriggered) {
    alertActive = true;
    recoverSmsSent = false;

    // 异常开始时只发一次报警短信，失败后按时间间隔重试。
    if (!alertSmsSent &&
        (lastSmsAttempt == 0 || millis() - lastSmsAttempt >= SMS_RETRY_GAP_MS)) {
      sendSms(AUTHORIZED_PHONE, "ALERT: Sensor triggered.");
      alertSmsSent = true;
      lastSmsAttempt = millis();
    }
  } else {
    // 从异常恢复到正常后，补发一次恢复短信，便于远程确认现场状态。
    if (alertActive &&
        !recoverSmsSent &&
        (lastSmsAttempt == 0 || millis() - lastSmsAttempt >= SMS_RETRY_GAP_MS)) {
      sendSms(AUTHORIZED_PHONE, "RECOVER: Sensor back to normal.");
      recoverSmsSent = true;
      lastSmsAttempt = millis();
    }

    alertActive = false;
    alertSmsSent = false;
  }
}

void updateBuzzer() {
  // 本地蜂鸣器是短信报警的补充，可通过短信远程单独开关。
  if (!alertActive || !sirenEnabled) {
    buzzerState = false;
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  if (millis() - lastBuzzerToggle >= BUZZER_TOGGLE_MS) {
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
    lastBuzzerToggle = millis();
  }
}

void readSim800Serial() {
  // 按行读取串口文本，兼容模块直接推送短信内容的工作方式。
  while (sim800.available()) {
    char ch = static_cast<char>(sim800.read());
    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      processIncomingLine(serialLine);
      serialLine = "";
    } else {
      serialLine += ch;
    }
  }
}

void processIncomingLine(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  Serial.println(line);

  // 使用 CNMI 直推短信内容时，短信正文会作为独立行出现。
  if (line == "STATUS") {
    sendStatusSms();
  } else if (line == "ARM") {
    armed = true;
    sendSms(AUTHORIZED_PHONE, "System armed.");
  } else if (line == "DISARM") {
    armed = false;
    alertActive = false;
    sendSms(AUTHORIZED_PHONE, "System disarmed.");
  } else if (line == "SIREN ON") {
    sirenEnabled = true;
    sendSms(AUTHORIZED_PHONE, "Siren enabled.");
  } else if (line == "SIREN OFF") {
    sirenEnabled = false;
    digitalWrite(PIN_BUZZER, LOW);
    sendSms(AUTHORIZED_PHONE, "Siren disabled.");
  }
}

void sendStatusSms() {
  // 状态短信统一从这里构造，减少各个指令分散拼装字符串。
  String message = "STATUS:";
  message += armed ? "ARMED," : "DISARMED,";
  message += alertActive ? "ALERT," : "NORMAL,";
  message += sirenEnabled ? "SIREN ON" : "SIREN OFF";
  sendSms(AUTHORIZED_PHONE, message.c_str());
}

void sendCommand(const char* command) {
  sim800.println(command);
  delay(300);
}

void sendSms(const char* phone, const char* message) {
  // 发送流程保持最基础的 AT 顺序，便于和常见 SIM800L 模块兼容。
  sim800.print("AT+CMGS=\"");
  sim800.print(phone);
  sim800.println("\"");
  delay(500);
  sim800.print(message);
  delay(300);
  sim800.write(26);
  delay(3000);
}
