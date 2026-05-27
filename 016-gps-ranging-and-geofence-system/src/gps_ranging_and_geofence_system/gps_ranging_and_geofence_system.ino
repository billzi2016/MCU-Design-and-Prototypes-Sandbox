#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPSPlus.h>
#include <Wire.h>

// GPS 测距与电子围栏系统：
// 获取当前位置后，计算与目标点的直线距离，并判断是否越出围栏半径。
// OLED 用于显示位置和测距结果，蜂鸣器在越界或 GPS 丢失时给出提示。

const int PIN_GPS_RX = 16;
const int PIN_GPS_TX = 17;
const int PIN_BUZZER = 18;

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_RESET = -1;

const double TARGET_LATITUDE = 39.904200;
const double TARGET_LONGITUDE = 116.407400;
const double GEOFENCE_RADIUS_METERS = 120.0;

const unsigned long DISPLAY_REFRESH_MS = 250;
const unsigned long PAGE_SWITCH_MS = 3200;
const unsigned long GPS_TIMEOUT_MS = 5000;
const unsigned long BUZZER_TOGGLE_MS = 180;

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool gpsValid = false;
bool outOfFence = false;
bool buzzerState = false;
int currentPage = 0;
double currentDistanceMeters = 0.0;
unsigned long lastGpsDataTime = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastPageSwitch = 0;
unsigned long lastBuzzerToggle = 0;

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

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
  display.println("GPS Geofence");
  display.println("Init receiver...");
  display.display();
}

void loop() {
  readGpsStream();
  updateFenceState();
  updateBuzzer();
  updatePageSwitch();

  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    drawPage();
    lastDisplayRefresh = millis();
  }
}

void readGpsStream() {
  while (gpsSerial.available()) {
    if (gps.encode(gpsSerial.read()) && gps.location.isUpdated()) {
      gpsValid = gps.location.isValid();
      if (gpsValid) {
        lastGpsDataTime = millis();
      }
    }
  }
}

void updateFenceState() {
  if (!gps.location.isValid() || millis() - lastGpsDataTime > GPS_TIMEOUT_MS) {
    gpsValid = false;
    outOfFence = true;
    return;
  }

  gpsValid = true;
  currentDistanceMeters = haversineMeters(
      gps.location.lat(), gps.location.lng(),
      TARGET_LATITUDE, TARGET_LONGITUDE);
  outOfFence = currentDistanceMeters > GEOFENCE_RADIUS_METERS;
}

void updateBuzzer() {
  if (gpsValid && !outOfFence) {
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

void updatePageSwitch() {
  if (millis() - lastPageSwitch >= PAGE_SWITCH_MS) {
    currentPage = (currentPage + 1) % 2;
    lastPageSwitch = millis();
  }
}

void drawPage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (currentPage == 0) {
    drawPositionPage();
  } else {
    drawFencePage();
  }

  display.display();
}

void drawPositionPage() {
  display.println("GPS Position");
  if (gpsValid) {
    display.print("Lat:");
    display.println(gps.location.lat(), 4);
    display.print("Lng:");
    display.println(gps.location.lng(), 4);
  } else {
    display.println("Waiting valid fix");
    display.println("Lat: ----");
    display.println("Lng: ----");
  }

  display.print("Sat:");
  display.println(gps.satellites.isValid() ? gps.satellites.value() : 0);
  display.print("State:");
  display.println(outOfFence ? "OUTSIDE" : "INSIDE");
}

void drawFencePage() {
  display.println("Range and Fence");
  display.print("TargetDist:");
  display.println(gpsValid ? currentDistanceMeters : -1.0, 1);
  display.print("FenceRadius:");
  display.println(GEOFENCE_RADIUS_METERS, 0);
  display.print("Alarm:");
  if (!gpsValid) {
    display.println("GPS LOST");
  } else {
    display.println(outOfFence ? "OUT RANGE" : "NORMAL");
  }
}

double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
  const double earthRadiusMeters = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(radians(lat1)) * cos(radians(lat2)) *
                 sin(dLon / 2.0) * sin(dLon / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return earthRadiusMeters * c;
}
