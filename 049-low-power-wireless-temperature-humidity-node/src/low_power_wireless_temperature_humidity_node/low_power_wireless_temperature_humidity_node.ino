#include <DHT.h>
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

// 低功耗无线温湿度节点：
// 使用 ESP32 周期采集温湿度和电池电压，通过 ESP-NOW 广播发送到网关，
// 发送完成后进入深度睡眠，以实现电池供电下的低功耗部署。

const int PIN_DHT = 4;
const int PIN_BATTERY_ADC = 34;
const int DHT_TYPE = DHT22;
const uint64_t SLEEP_SECONDS = 300;

DHT dht(PIN_DHT, DHT_TYPE);

struct Payload {
  char nodeId[12];
  float temperatureC;
  float humidity;
  float batteryV;
  uint32_t wakeCount;
};

RTC_DATA_ATTR uint32_t wakeCount = 0;
Payload payload;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
  Serial.begin(115200);
  dht.begin();

  wakeCount++;
  preparePayload();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    goToSleep();
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  // 当前版本使用广播方式发给网关，便于先把节点和接收端链路打通。
  esp_now_send(broadcastAddress, reinterpret_cast<uint8_t *>(&payload), sizeof(payload));
  delay(200);
  goToSleep();
}

void loop() {
}

void preparePayload() {
  strncpy(payload.nodeId, "NODE-01", sizeof(payload.nodeId) - 1);
  payload.nodeId[sizeof(payload.nodeId) - 1] = '\0';
  payload.temperatureC = dht.readTemperature();
  payload.humidity = dht.readHumidity();
  payload.batteryV = readBatteryVoltage();
  payload.wakeCount = wakeCount;

  // 若传感器短时读取失败，保留一个明显异常值，便于网关侧识别节点状态。
  if (isnan(payload.temperatureC)) {
    payload.temperatureC = -100.0f;
  }
  if (isnan(payload.humidity)) {
    payload.humidity = -1.0f;
  }
}

float readBatteryVoltage() {
  int raw = analogRead(PIN_BATTERY_ADC);
  // 假设使用 1:1 分压示意，具体换算系数应按实际电阻网络重新标定。
  return raw / 4095.0f * 3.3f * 2.0f;
}

void goToSleep() {
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}
