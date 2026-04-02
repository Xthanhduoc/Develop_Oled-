#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Khởi tạo OLED
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Khởi tạo Encoder & Chân linh kiện
ESP32Encoder encoder;
const int SW_PIN = 4;
const int LED_PIN = 2; // Đèn D2 trên ESP32

// Khởi tạo NTP (Đồng hồ)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);

// Quản lý State Menu
enum State { MAIN_MENU, CLOCK_WEATHER_MODE, TIMER_SETUP_MIN, TIMER_SETUP_SEC, TIMER_RUNNING, PRICE_MODE, WIFI_ANALYZER_MODE, SCAN_WIFI, SELECT_WIFI, INPUT_PASSWORD, CONNECTING };
State currentState = MAIN_MENU;

// Biến Menu chính
const int totalMenuItems = 5;
String menuItems[] = {"1. Dong ho & Thoi tiet", "2. Dem nguoc", "3. Vang & Xang", "4. Phan tich Wi-Fi", "5. Cai dat WiFi"};

// Biến WiFi cơ bản
String ssids[15];
int wifiCount = 0;
int selectedWifi = 0;
String password = "";
const char* keyboard = "<!@. abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int charIndex = 0;
const char* daysOfWeek[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

// --- BIẾN CHO WIFI ANALYZER (ĐÃ SỬA LỖI) ---
int ana_count = 0;
String ana_ssids[5];
int ana_rssi[5];
bool scanInProgress = false;      // Cờ báo hiệu đang quét
unsigned long lastScanTime = 0;   // Thời gian bắt đầu quét
bool firstEnter = true;           // Cờ cho lần đầu vào chế độ

// Biến Đếm ngược
int setMinutes = 1;
int setSeconds = 0;
unsigned long totalSeconds = 0;
unsigned long remainingSeconds = 0;
unsigned long timerMillis = 0;
bool isTimerFinished = false;

// Biến Thời tiết
float currentTemp = 0.0;
float currentWind = 0.0;
int weatherCode = 0; 
bool weatherFetched = false;
unsigned long lastWeatherFetch = 0;
const unsigned long weatherInterval = 600000; 

// ---------------- CÁC HÀM VẼ GIAO DIỆN ----------------

void drawWiFiIcon(int x, int y) {
  int cx = x + 7, cy = y + 5; 
  u8g2.drawDisc(cx, cy, 1);
  u8g2.drawCircle(cx, cy, 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  u8g2.drawCircle(cx, cy, 5, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  if (WiFi.status() != WL_CONNECTED) {
    u8g2.setFont(u8g2_font_5x8_tr); u8g2.drawStr(cx + 8, cy + 3, "!"); 
  }
}

void drawBatteryIcon(int x, int y) {
  u8g2.drawFrame(x, y, 12, 6); u8g2.drawBox(x + 12, y + 1, 1, 4); u8g2.drawBox(x + 1, y + 1, 8, 4); 
}

void drawDynamicWeatherIcon(int x, int y, int code) {
  if (code == 0 || code == 1) { 
    u8g2.drawCircle(x + 8, y + 8, 4);
    u8g2.drawLine(x + 8, y + 1, x + 8, y + 3); u8g2.drawLine(x + 8, y + 13, x + 8, y + 15);
    u8g2.drawLine(x + 1, y + 8, x + 3, y + 8); u8g2.drawLine(x + 13, y + 8, x + 15, y + 8);
    u8g2.drawLine(x + 3, y + 3, x + 4, y + 4); u8g2.drawLine(x + 13, y + 13, x + 12, y + 12);
    u8g2.drawLine(x + 13, y + 3, x + 12, y + 4); u8g2.drawLine(x + 3, y + 13, x + 4, y + 12);
  } else {
    u8g2.drawDisc(x + 5, y + 8, 3); u8g2.drawDisc(x + 9, y + 6, 4); u8g2.drawDisc(x + 13, y + 8, 3);
    u8g2.drawBox(x + 5, y + 6, 8, 6); 
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) { 
      u8g2.drawLine(x + 5, y + 13, x + 3, y + 16); u8g2.drawLine(x + 9, y + 13, x + 7, y + 16); u8g2.drawLine(x + 13, y + 13, x + 11, y + 16);
    } 
    else if (code >= 95) { 
      u8g2.drawLine(x + 9, y + 12, x + 7, y + 16); u8g2.drawLine(x + 7, y + 16, x + 11, y + 16); u8g2.drawLine(x + 11, y + 16, x + 8, y + 20);
    }
  }
}

// ---------------- CÁC HÀM XỬ LÝ DỮ LIỆU ----------------

void fetchWeatherData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://api.open-meteo.com/v1/forecast?latitude=10.0452&longitude=105.7469&current_weather=true");
    if (http.GET() > 0) {
      String payload = http.getString();
      JsonDocument doc; 
      if (!deserializeJson(doc, payload)) {
        currentTemp = doc["current_weather"]["temperature"]; currentWind = doc["current_weather"]["windspeed"]; weatherCode = doc["current_weather"]["weathercode"]; 
        weatherFetched = true; lastWeatherFetch = millis();
      }
    }
    http.end();
  }
}

// Hàm quét WiFi mới
void startWiFiScan() {
  WiFi.scanDelete();        // Xóa kết quả quét cũ
  WiFi.scanNetworks(true);  // Quét bất đồng bộ
  scanInProgress = true;
  lastScanTime = millis();
}

// Hàm xử lý kết quả quét
void processScanResult() {
  int scanResult = WiFi.scanComplete();
  
  if (scanResult == WIFI_SCAN_FAILED) {
    // Nếu quét thất bại, thử lại
    startWiFiScan();
    return;
  }
  
  if (scanResult > 0) {
    // Đã quét xong, xử lý kết quả
    int totalFound = min(scanResult, 15);
    String tempSSID[15];
    int tempRSSI[15];
    
    for (int i = 0; i < totalFound; i++) {
      tempSSID[i] = WiFi.SSID(i);
      tempRSSI[i] = WiFi.RSSI(i);
    }
    
    // Sắp xếp theo cường độ tín hiệu giảm dần
    for (int i = 0; i < totalFound - 1; i++) {
      for (int j = i + 1; j < totalFound; j++) {
        if (tempRSSI[j] > tempRSSI[i]) {
          int tr = tempRSSI[i]; tempRSSI[i] = tempRSSI[j]; tempRSSI[j] = tr;
          String ts = tempSSID[i]; tempSSID[i] = tempSSID[j]; tempSSID[j] = ts;
        }
      }
    }
    
    // Lấy 5 mạng mạnh nhất
    ana_count = min(totalFound, 5);
    for (int i = 0; i < ana_count; i++) {
      ana_ssids[i] = tempSSID[i];
      ana_rssi[i] = tempRSSI[i];
    }
    
    // Xóa kết quả quét để giải phóng bộ nhớ
    WiFi.scanDelete();
    scanInProgress = false;
    firstEnter = false;  // Đã có dữ liệu, không cần hiển thị "Đang quét sóng..."
  }
}

// ---------------- SETUP & LOOP ----------------

void setup() {
  Serial.begin(115200); Wire.begin(21, 22); u8g2.begin();
  ESP32Encoder::useInternalWeakPullResistors = (puType)1; encoder.attachHalfQuad(16, 17); encoder.setCount(0);
  pinMode(SW_PIN, INPUT_PULLUP); pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); 
  WiFi.mode(WIFI_STA);
}

void loop() {
  int currentCount = (int)encoder.getCount() / 2;

  switch (currentState) {
    case MAIN_MENU: {
      int menuIdx = abs(currentCount) % totalMenuItems;
      u8g2.clearBuffer(); drawWiFiIcon(2, 2); drawBatteryIcon(113, 2);
      u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(14, 14, "-- MENU CHINH --");

      int startIdx = menuIdx - 1;
      if (startIdx < 0) startIdx = 0;
      if (startIdx > totalMenuItems - 3) startIdx = totalMenuItems - 3;

      for (int i = 0; i < 3; i++) {
        int itemIndex = startIdx + i;
        if (itemIndex < totalMenuItems) {
          int yPos = 32 + (i * 15);
          if (itemIndex == menuIdx) u8g2.drawStr(0, yPos, ">");
          u8g2.drawStr(10, yPos, menuItems[itemIndex].c_str());
        }
      }
      u8g2.sendBuffer();

      if (digitalRead(SW_PIN) == LOW) {
        delay(250);
        if (menuIdx == 0) {
          if (!weatherFetched && WiFi.status() == WL_CONNECTED) {
            u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(10, 30, "Tai thoi tiet..."); u8g2.sendBuffer(); fetchWeatherData();
          }
          currentState = CLOCK_WEATHER_MODE;
        }
        else if (menuIdx == 1) { encoder.setCount(setMinutes * 2); currentState = TIMER_SETUP_MIN; }
        else if (menuIdx == 2) currentState = PRICE_MODE; 
        else if (menuIdx == 3) { 
          // Vào chế độ WiFi Analyzer
          ana_count = 0;
          firstEnter = true;
          scanInProgress = false;
          startWiFiScan();  // Bắt đầu quét ngay
          currentState = WIFI_ANALYZER_MODE; 
        }
        else currentState = SCAN_WIFI;
      }
      break;
    }

    // ==========================================
    // --- CHỨC NĂNG WIFI ANALYZER (ĐÃ FIX) ---
    // ==========================================
    case WIFI_ANALYZER_MODE: {
      
      // Xử lý quét WiFi
      if (scanInProgress) {
        // Kiểm tra nếu quét quá lâu (5 giây) thì quét lại
        if (millis() - lastScanTime > 5000) {
          startWiFiScan();
        } else {
          processScanResult();  // Kiểm tra kết quả quét
        }
      } else {
        // Đã quét xong, đợi 2 giây rồi quét lại
        static unsigned long lastCompleteTime = 0;
        if (lastCompleteTime == 0) {
          lastCompleteTime = millis();
        } else if (millis() - lastCompleteTime > 2000) {
          startWiFiScan();
          lastCompleteTime = 0;
        }
      }

      // --- VẼ GIAO DIỆN LÊN OLED ---
      u8g2.clearBuffer();
      
      // Header
      u8g2.setFont(u8g2_font_5x8_tr);
      u8g2.drawStr(22, 7, "- Wi-Fi Analyzer -");
      u8g2.drawHLine(0, 9, 128); 

      // Hiển thị dữ liệu
      if (ana_count == 0 && firstEnter) {
        // Chỉ hiển thị khi chưa có dữ liệu và mới vào
        u8g2.setFont(u8g2_font_6x10_tf);
        int tw = u8g2.getStrWidth("Dang quet song...");
        u8g2.drawStr(64 - tw/2, 35, "Dang quet song...");
      } else if (ana_count > 0) {
        // Hiển thị danh sách WiFi và biểu đồ
        u8g2.setFont(u8g2_font_5x8_tr);
        for (int i = 0; i < ana_count; i++) {
          int yBase = 18 + (i * 11);
          
          // Tên WiFi (cắt 9 ký tự)
          u8g2.setCursor(0, yBase);
          u8g2.print(ana_ssids[i].substring(0, 9)); 
          
          // Thanh biểu đồ cường độ tín hiệu
          int barWidth = map(ana_rssi[i], -100, -40, 0, 70); 
          barWidth = constrain(barWidth, 0, 70); 
          
          u8g2.drawFrame(55, yBase - 6, 70, 6);
          u8g2.drawBox(55, yBase - 6, barWidth, 6);
        }
      } else if (!firstEnter && ana_count == 0) {
        // Không tìm thấy WiFi nào
        u8g2.setFont(u8g2_font_6x10_tf);
        int tw = u8g2.getStrWidth("Khong tim thay WiFi");
        u8g2.drawStr(64 - tw/2, 35, "Khong tim thay WiFi");
      }
      
      u8g2.sendBuffer();

      // Thoát về menu khi nhấn nút
      if (digitalRead(SW_PIN) == LOW) {
        delay(250);
        WiFi.scanDelete();  // Dừng quét
        scanInProgress = false;
        currentState = MAIN_MENU;
        encoder.setCount(0);
      }
      break;
    }

    // ==========================================
    // --- CÁC CHỨC NĂNG CÒN LẠI (GIỮ NGUYÊN) ---
    // ==========================================
    case CLOCK_WEATHER_MODE: {
      u8g2.clearBuffer();
      if (millis() - lastWeatherFetch > weatherInterval) fetchWeatherData();
      if (WiFi.status() != WL_CONNECTED) {
        u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(5, 35, "CHUA KET NOI WIFI");
      } else {
        timeClient.update(); drawWiFiIcon(2, 2); drawBatteryIcon(113, 2);
        u8g2.setFont(u8g2_font_logisoso24_tf); 
        int timeWidth = u8g2.getStrWidth(timeClient.getFormattedTime().c_str()); u8g2.drawStr((128 - timeWidth) / 2, 30, timeClient.getFormattedTime().c_str());
        time_t rawtime = timeClient.getEpochTime(); struct tm * ti = localtime (&rawtime); char dateBuf[20];
        sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
        u8g2.setFont(u8g2_font_6x12_tr); int dateWidth = u8g2.getStrWidth(dateBuf); u8g2.drawStr((128 - dateWidth) / 2, 42, dateBuf);

        if (weatherFetched) {
          drawDynamicWeatherIcon(15, 42, weatherCode); u8g2.setFont(u8g2_font_5x8_tr); u8g2.drawStr(45, 52, "Cantho");
          char weatherText[30]; sprintf(weatherText, "%.1fC  %dkm/h", currentTemp, (int)currentWind); u8g2.drawStr(45, 62, weatherText);
        } else {
          u8g2.setFont(u8g2_font_5x8_tr); u8g2.drawStr(45, 58, "Dang tai...");
        }
      }
      u8g2.sendBuffer(); if (digitalRead(SW_PIN) == LOW) { delay(250); currentState = MAIN_MENU; encoder.setCount(0);}
      break;
    }

    case TIMER_SETUP_MIN: {
      setMinutes = constrain((int)encoder.getCount() / 2, 0, 99);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); int w1 = u8g2.getStrWidth("CHON PHUT:"); u8g2.drawStr(64 - w1/2, 20, "CHON PHUT:");
      char timeStr[10]; sprintf(timeStr, "%02d:--", setMinutes);
      u8g2.setFont(u8g2_font_logisoso20_tf); int w2 = u8g2.getStrWidth(timeStr); u8g2.drawStr(64 - w2/2, 50, timeStr); u8g2.sendBuffer();
      if (digitalRead(SW_PIN) == LOW) { delay(250); encoder.setCount(setSeconds * 2); currentState = TIMER_SETUP_SEC; }
      break;
    }

    case TIMER_SETUP_SEC: {
      setSeconds = constrain((int)encoder.getCount() / 2, 0, 59);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); int w1 = u8g2.getStrWidth("CHON GIAY:"); u8g2.drawStr(64 - w1/2, 20, "CHON GIAY:");
      char timeStr[10]; sprintf(timeStr, "%02d:%02d", setMinutes, setSeconds);
      u8g2.setFont(u8g2_font_logisoso20_tf); int w2 = u8g2.getStrWidth(timeStr); u8g2.drawStr(64 - w2/2, 50, timeStr); u8g2.sendBuffer();
      if (digitalRead(SW_PIN) == LOW) { 
        delay(250); totalSeconds = (setMinutes * 60) + setSeconds;
        if(totalSeconds > 0) { remainingSeconds = totalSeconds; timerMillis = millis(); isTimerFinished = false; digitalWrite(LED_PIN, LOW); currentState = TIMER_RUNNING; } 
        else currentState = MAIN_MENU;
      }
      break;
    }

    case TIMER_RUNNING: {
      u8g2.clearBuffer();
      if (!isTimerFinished) {
        if (millis() - timerMillis >= 1000) { timerMillis += 1000; remainingSeconds--; if (remainingSeconds <= 0) { isTimerFinished = true; digitalWrite(LED_PIN, HIGH); } }
        int centerX = 64, centerY = 32, radius = 28;
        int angle = (remainingSeconds * 360) / totalSeconds;
        for (int a = 0; a < angle; a++) {
          float rad = (a - 90) * 0.0174533; 
          u8g2.drawDisc(centerX + cos(rad) * radius, centerY + sin(rad) * radius, 1); 
        }
        char timeStr[10];
        if (remainingSeconds >= 60) {
          sprintf(timeStr, "%02d:%02d", remainingSeconds / 60, remainingSeconds % 60); u8g2.setFont(u8g2_font_logisoso16_tf);
          int textWidth = u8g2.getStrWidth(timeStr); u8g2.drawStr(centerX - (textWidth / 2), centerY + 6, timeStr); 
        } else {
          sprintf(timeStr, "%d", remainingSeconds); u8g2.setFont(u8g2_font_logisoso24_tf); 
          int textWidth = u8g2.getStrWidth(timeStr); u8g2.drawStr(centerX - (textWidth / 2), centerY + 10, timeStr); 
        }
      } else {
        u8g2.setFont(u8g2_font_logisoso20_tf); int tw = u8g2.getStrWidth("HET GIO!"); u8g2.drawStr(64 - tw/2, 35, "HET GIO!");
        u8g2.setFont(u8g2_font_6x10_tf); tw = u8g2.getStrWidth("Nhan de tat"); u8g2.drawStr(64 - tw/2, 55, "Nhan de tat");
      }
      u8g2.sendBuffer();
      if (digitalRead(SW_PIN) == LOW) { delay(250); digitalWrite(LED_PIN, LOW); currentState = MAIN_MENU; encoder.setCount(0); } break;
    }

    case PRICE_MODE: {
      u8g2.clearBuffer(); drawWiFiIcon(2, 2); drawBatteryIcon(113, 2); u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(15, 14, "- BANG GIA -"); u8g2.drawStr(0, 32, "SJC Mua : 80.50 tr/l"); u8g2.drawStr(0, 46, "SJC Ban : 82.50 tr/l");
      u8g2.drawHLine(0, 52, 128); u8g2.drawStr(0, 62, "Xang 95 : 24,800 VND"); u8g2.sendBuffer();
      if (digitalRead(SW_PIN) == LOW) { delay(250); currentState = MAIN_MENU; encoder.setCount(0);} break;
    }

    case SCAN_WIFI: {
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(20, 38, "Scanning..."); u8g2.sendBuffer();
      wifiCount = WiFi.scanNetworks(); for (int i = 0; i < min(wifiCount, 15); i++) ssids[i] = WiFi.SSID(i); currentState = SELECT_WIFI; break;
    }
    case SELECT_WIFI: {
      selectedWifi = abs(currentCount) % (wifiCount > 0 ? wifiCount : 1); u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(0, 12, "WiFi List:");
      for (int i = 0; i < 5; i++) {
        int idx = (selectedWifi / 5 * 5) + i;
        if (idx < wifiCount) { if (idx == selectedWifi) u8g2.drawStr(0, 24 + (i * 10), ">"); u8g2.drawStr(10, 24 + (i * 10), ssids[idx].substring(0, 18).c_str()); }
      }
      u8g2.sendBuffer(); if (digitalRead(SW_PIN) == LOW) { delay(250); currentState = INPUT_PASSWORD; encoder.setCount(0); } break;
    }
    case INPUT_PASSWORD: {
      charIndex = abs(currentCount) % strlen(keyboard);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(0, 12, "Pass:"); u8g2.drawFrame(0, 16, 128, 15); u8g2.drawStr(5, 27, password.c_str());
      u8g2.setFont(u8g2_font_9x15_tf); u8g2.drawStr(58, 55, String(keyboard[charIndex]).c_str()); 
      u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(40, 53, String(keyboard[(charIndex + strlen(keyboard) - 1) % strlen(keyboard)]).c_str()); u8g2.drawStr(80, 53, String(keyboard[(charIndex + 1) % strlen(keyboard)]).c_str());
      u8g2.drawHLine(55, 58, 15); u8g2.drawStr(0, 64, "<:Xoa  !:Ket noi"); u8g2.sendBuffer();
      if (digitalRead(SW_PIN) == LOW) { digitalWrite(LED_PIN, LOW); delay(200); char c = keyboard[charIndex]; if (c == '!') currentState = CONNECTING; else if (c == '<') { if (password.length() > 0) password.remove(password.length() - 1); } else password += c; } break;
    }
    case CONNECTING: {
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(0, 35, "Connecting..."); u8g2.sendBuffer();
      WiFi.begin(ssids[selectedWifi].c_str(), password.c_str()); unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(100);
      if (WiFi.status() == WL_CONNECTED) { timeClient.begin(); currentState = MAIN_MENU; } else currentState = SELECT_WIFI; break;
    }
  }
}