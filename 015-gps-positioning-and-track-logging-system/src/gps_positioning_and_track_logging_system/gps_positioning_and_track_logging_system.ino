#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SD.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <Wire.h>

// GPS 定位与轨迹记录系统：
// 使用 GPS 获取经纬度、速度和时间，使用 SD 卡持续记录轨迹，
// 并在 OLED 上显示当前位置、速度、卫星数和日志状态。

const int PIN_GPS_RX = 16;
const int PIN_GPS_TX = 17;
const int PIN_SD_CS = 5;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const unsigned long DISPLAY_REFRESH_MS = 300;
const unsigned long LOG_INTERVAL_MS = 5000;
const unsigned long PAGE_SWITCH_MS = 3500;

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// SD 状态和页码状态分开管理，显示和记录逻辑更直观。
bool sdReady = false;
bool logHeaderReady = false;
int currentPage = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastLogTime = 0;
unsigned long lastPageSwitch = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GPS Track Logger");
  display.println("Init modules...");
  display.display();

  sdReady = SD.begin(PIN_SD_CS);
  if (sdReady) {
    ensureLogHeader();
  }
}

void loop() {
  // 同时处理 GPS 串口解析、周期记录和 OLED 刷新。
  readGpsStream();
  updatePageSwitch();

  if (millis() - lastLogTime >= LOG_INTERVAL_MS) {
    logTrackPoint();
    lastLogTime = millis();
  }

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    drawPage();
    lastDisplayRefresh = millis();
  }
}

void readGpsStream() {
  // TinyGPSPlus 需要持续喂入串口数据，才能逐步解析出有效定位信息。
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
}

void updatePageSwitch() {
  if (millis() - lastPageSwitch >= PAGE_SWITCH_MS) {
    currentPage = (currentPage + 1) % 2;
    lastPageSwitch = millis();
  }
}

void ensureLogHeader() {
  // 第一次写入时补 CSV 表头，后续可以直接导入表格软件分析。
  if (!sdReady || logHeaderReady) {
    return;
  }

  if (!SD.exists("/track.csv")) {
    File file = SD.open("/track.csv", FILE_WRITE);
    if (file) {
      file.println("date,time,latitude,longitude,speed_kmh,satellites");
      file.close();
    }
  }

  logHeaderReady = true;
}

void logTrackPoint() {
  // 只有 SD 正常且定位有效时才记录轨迹，避免写入无意义占位数据。
  if (!sdReady || !gps.location.isValid()) {
    return;
  }

  ensureLogHeader();

  File file = SD.open("/track.csv", FILE_WRITE);
  if (!file) {
    sdReady = false;
    return;
  }

  file.print(dateString());
  file.print(',');
  file.print(timeString());
  file.print(',');
  file.print(gps.location.lat(), 6);
  file.print(',');
  file.print(gps.location.lng(), 6);
  file.print(',');
  file.print(gps.speed.kmph(), 2);
  file.print(',');
  file.println(gps.satellites.isValid() ? gps.satellites.value() : 0);
  file.close();
}

void drawPage() {
  // 两页轮播：位置页看实时定位，状态页看日志和时间同步。
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (currentPage == 0) {
    drawLocationPage();
  } else {
    drawStatusPage();
  }

  display.display();
}

void drawLocationPage() {
  // 位置页突出经纬度、速度和卫星数这几个核心指标。
  display.println("GPS Position");
  if (gps.location.isValid()) {
    display.print("Lat:");
    display.println(gps.location.lat(), 4);
    display.print("Lng:");
    display.println(gps.location.lng(), 4);
  } else {
    display.println("Waiting fix...");
    display.println("Lat: ----");
    display.println("Lng: ----");
  }

  display.print("Spd:");
  display.print(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1);
  display.println("km/h");
  display.print("Sat:");
  display.println(gps.satellites.isValid() ? gps.satellites.value() : 0);
}

void drawStatusPage() {
  // 状态页更适合确认 SD 是否正常、时间是否有效、定位是否已锁定。
  display.println("Track Logger");
  display.print("Date: ");
  display.println(dateString());
  display.print("Time: ");
  display.println(timeString());
  display.print("SD  : ");
  display.println(sdReady ? "READY" : "FAULT");
  display.print("Fix : ");
  display.println(gps.location.isValid() ? "VALID" : "SEARCH");
}

String dateString() {
  // GPS 时间无效时返回占位值，避免显示垃圾时间。
  if (!gps.date.isValid()) {
    return "0000-00-00";
  }

  char buffer[11];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
  return String(buffer);
}

String timeString() {
  if (!gps.time.isValid()) {
    return "00:00:00";
  }

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
  return String(buffer);
}
