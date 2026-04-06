#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
#include <Preferences.h>

// ==========================================
// ĐIỀN SCRIPT ID CỦA GOOGLE APPS SCRIPT VÀO ĐÂY
// ==========================================
String GOOGLE_SCRIPT_ID = "THAY_BANG_ID_CUA_BAN_O_DAY"; 

// Khởi tạo OLED
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Khởi tạo Encoder & Chân linh kiện
ESP32Encoder encoder;
const int SW_PIN = 4;
const int LED_PIN = 2; 
const int BUZZER_PIN = 23; // --- MỚI THÊM: Chân còi buzzer
int lastEncoderCount = 0;  // --- MỚI THÊM: Biến lưu trạng thái núm vặn để check xoay

// Khởi tạo NTP (Đồng hồ)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);

// Khởi tạo Preferences (Bộ nhớ Flash)
Preferences preferences;

// Quản lý State Menu
enum State { MAIN_MENU, CLOCK_WEATHER_MODE, RELAY_MENU, TIMER_SETUP_MIN, TIMER_SETUP_SEC, TIMER_RUNNING, ALARM_SETUP_ON_HOUR, ALARM_SETUP_ON_MIN, ALARM_SETUP_OFF_HOUR, ALARM_SETUP_OFF_MIN, ALARM_WAITING, WIFI_ANALYZER_MODE, WIFI_GRAPH_MODE, SCAN_WIFI, SELECT_WIFI, INPUT_PASSWORD, CONNECTING };
State currentState = MAIN_MENU;

// Biến Menu chính 
const int totalMenuItems = 4;
String menuItems[] = {"1. Dong ho & Thoi tiet", "2. Control Relay", "3. Phan tich Wi-Fi", "4. Cai dat WiFi"};

// Biến WiFi cơ bản
String ssids[15];
int wifiCount = 0;
int selectedWifi = 0;
String password = "";
const char* keyboard = "<!@. abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int charIndex = 0;
const char* daysOfWeek[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

// --- BIẾN CHO WIFI ANALYZER ---
int ana_count = 0;
String ana_ssids[15]; 
int ana_rssi[15];
bool scanInProgress = false;      
unsigned long lastScanTime = 0;   
bool firstEnter = true;           

// --- BIẾN CHO WIFI GRAPH ---
String selectedGraphSSID = "";
int rssiHistory[100]; 
int currentTargetRSSI = -100; 

// --- BIẾN CHO ĐẾM NGƯỢC ---
int setMinutes = 1;
int setSeconds = 0;
unsigned long totalSeconds = 0;
unsigned long remainingSeconds = 0;
unsigned long timerMillis = 0;
bool isTimerFinished = false;

// --- BIẾN CHO HẸN GIỜ THỰC ---
int alarmOnHour = 0;
int alarmOnMinute = 0;
int alarmOffHour = 0;
int alarmOffMinute = 0;
bool isAlarmSet = false;

// Biến Thời tiết
float currentTemp = 0.0;
float currentWind = 0.0;
int weatherCode = 0; 
bool weatherFetched = false;
unsigned long lastWeatherFetch = 0;
const unsigned long weatherInterval = 600000; 

// --- BIẾN CHO NÚT NHẤN ---
unsigned long btnLastDebounceTime = 0;
int btnLastState = HIGH;
int btnState = HIGH;
unsigned long btnFirstClickTime = 0;
int btnClickCount = 0;
bool singleClick = false;
bool doubleClick = false;
const int DOUBLE_CLICK_TIME = 400; 

// ---------------- CÁC HÀM XỬ LÝ DỮ LIỆU VÀ GIAO DIỆN ----------------

// --- MỚI THÊM: Hàm phát tiếng tít
void playTick() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(15);
  digitalWrite(BUZZER_PIN, LOW);
}

// Hàm gửi dữ liệu lên Google Sheets
void sendDataToGoogleSheets(String date, String onTime, String offTime, String status) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 

    String url = "https://script.google.com/macros/s/AKfycby5WW3ZQA1dxt9TevjKN7u2j6L75gnNSSjt_bMVjMpxFC6UdyFS8GhlcSSxeSbVCL6f/exec?";
    url += "date=" + date;
    url += "&onTime=" + onTime;
    url += "&offTime=" + offTime;
    url += "&status=" + status;

    Serial.println("Dang gui len Google Sheets: " + status);
    http.begin(client, url);
    int httpCode = http.GET();
    
    if (httpCode > 0) Serial.println("Thanh cong! Ma HTTP: " + String(httpCode));
    else Serial.println("That bai, loi: " + http.errorToString(httpCode));
    http.end();
  }
}

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

void startWiFiScan() {
  WiFi.scanDelete();        
  WiFi.scanNetworks(true);  
  scanInProgress = true;
  lastScanTime = millis();
}

void processScanResult() {
  int scanResult = WiFi.scanComplete();
  if (scanResult == WIFI_SCAN_FAILED) {
    startWiFiScan();
    return;
  }
  if (scanResult > 0) {
    int totalFound = min(scanResult, 15);
    String tempSSID[15]; int tempRSSI[15];
    for (int i = 0; i < totalFound; i++) { tempSSID[i] = WiFi.SSID(i); tempRSSI[i] = WiFi.RSSI(i); }
    for (int i = 0; i < totalFound - 1; i++) {
      for (int j = i + 1; j < totalFound; j++) {
        if (tempRSSI[j] > tempRSSI[i]) {
          int tr = tempRSSI[i]; tempRSSI[i] = tempRSSI[j]; tempRSSI[j] = tr;
          String ts = tempSSID[i]; tempSSID[i] = tempSSID[j]; tempSSID[j] = ts;
        }
      }
    }
    ana_count = min(totalFound, 15);
    for (int i = 0; i < ana_count; i++) { ana_ssids[i] = tempSSID[i]; ana_rssi[i] = tempRSSI[i]; }
    WiFi.scanDelete(); scanInProgress = false; firstEnter = false; 
  }
}

// ---------------- SETUP & LOOP ----------------

void setup() {
  Serial.begin(115200); Wire.begin(21, 22); u8g2.begin();
  ESP32Encoder::useInternalWeakPullResistors = (puType)1; encoder.attachHalfQuad(16, 17); encoder.setCount(0);
  pinMode(SW_PIN, INPUT_PULLUP); pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); 
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); // --- MỚI THÊM: Setup Buzzer
  
  WiFi.mode(WIFI_STA);

  preferences.begin("wifi", false); 
  String saved_ssid = preferences.getString("ssid", "");
  String saved_pass = preferences.getString("pass", "");
  preferences.end(); 

  if (saved_ssid != "") {
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
  }
}

void loop() {
  int currentCount = (int)encoder.getCount() / 2;

  // --- MỚI THÊM: Phát tiếng "tít" khi xoay núm vặn ---
  if (currentCount != lastEncoderCount) {
    playTick();
    lastEncoderCount = currentCount;
  }
  // ----------------------------------------------------

  singleClick = false; doubleClick = false;
  int reading = digitalRead(SW_PIN);
  if (reading != btnLastState) btnLastDebounceTime = millis();

  if ((millis() - btnLastDebounceTime) > 50) { 
    if (reading != btnState) {
      btnState = reading;
      if (btnState == LOW) { 
        btnClickCount++;
        if (btnClickCount == 1) btnFirstClickTime = millis();
        else if (btnClickCount == 2) {
          if (millis() - btnFirstClickTime < DOUBLE_CLICK_TIME) { doubleClick = true; playTick(); btnClickCount = 0; } // --- MỚI THÊM: playTick()
        }
      }
    }
  }

  if (btnClickCount == 1 && (millis() - btnFirstClickTime > DOUBLE_CLICK_TIME)) { singleClick = true; playTick(); btnClickCount = 0; } // --- MỚI THÊM: playTick()
  btnLastState = reading;

  if (doubleClick && currentState != MAIN_MENU) {
    WiFi.scanDelete(); scanInProgress = false; digitalWrite(LED_PIN, LOW); isTimerFinished = false; isAlarmSet = false;
    currentState = MAIN_MENU; encoder.setCount(0);
  }

  switch (currentState) {
    case MAIN_MENU: {
      int menuIdx = abs(currentCount) % totalMenuItems;
      u8g2.clearBuffer(); drawWiFiIcon(2, 2); drawBatteryIcon(113, 2);
      u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(14, 14, "   MENU CHINH   ");

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

      if (singleClick) {
        if (menuIdx == 0) {
          if (!weatherFetched && WiFi.status() == WL_CONNECTED) {
            u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(10, 30, "Tai thoi tiet..."); u8g2.sendBuffer(); fetchWeatherData();
          }
          currentState = CLOCK_WEATHER_MODE;
        }
        else if (menuIdx == 1) { encoder.setCount(0); currentState = RELAY_MENU; } 
        else if (menuIdx == 2) { 
          ana_count = 0; firstEnter = true; scanInProgress = false; encoder.setCount(0); startWiFiScan(); currentState = WIFI_ANALYZER_MODE; 
        }
        else currentState = SCAN_WIFI;
      }
      break;
    }

    case RELAY_MENU: {
      int opt = abs(currentCount) % 2; 
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(15, 14, "- CONTROL RELAY -"); u8g2.drawHLine(0, 18, 128);

      if (opt == 0) u8g2.drawStr(0, 35, "> 1. Dem nguoc"); else u8g2.drawStr(10, 35, "1. Dem nguoc");
      if (opt == 1) u8g2.drawStr(0, 50, "> 2. Hen gio thuc"); else u8g2.drawStr(10, 50, "2. Hen gio thuc");
      u8g2.sendBuffer();

      if (singleClick) {
        if (opt == 0) { encoder.setCount(setMinutes * 2); currentState = TIMER_SETUP_MIN; } 
        else {
          if (WiFi.status() != WL_CONNECTED) {
            u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(12, 35, "LOI: CHUA KET NOI"); u8g2.drawStr(38, 50, "WIFI!"); u8g2.sendBuffer(); delay(1500); singleClick = false; 
          } else { timeClient.update(); alarmOnHour = timeClient.getHours(); encoder.setCount(alarmOnHour * 2); currentState = ALARM_SETUP_ON_HOUR; }
        }
      }
      break;
    }

    case TIMER_SETUP_MIN: {
      setMinutes = constrain((int)encoder.getCount() / 2, 0, 99);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); int w1 = u8g2.getStrWidth("CHON PHUT:"); u8g2.drawStr(64 - w1/2, 20, "CHON PHUT:");
      char timeStr[10]; sprintf(timeStr, "%02d:--", setMinutes);
      u8g2.setFont(u8g2_font_logisoso20_tf); int w2 = u8g2.getStrWidth(timeStr); u8g2.drawStr(64 - w2/2, 50, timeStr); u8g2.sendBuffer();
      if (singleClick) { encoder.setCount(setSeconds * 2); currentState = TIMER_SETUP_SEC; } break;
    }
    case TIMER_SETUP_SEC: {
      setSeconds = constrain((int)encoder.getCount() / 2, 0, 59);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); int w1 = u8g2.getStrWidth("CHON GIAY:"); u8g2.drawStr(64 - w1/2, 20, "CHON GIAY:");
      char timeStr[10]; sprintf(timeStr, "%02d:%02d", setMinutes, setSeconds);
      u8g2.setFont(u8g2_font_logisoso20_tf); int w2 = u8g2.getStrWidth(timeStr); u8g2.drawStr(64 - w2/2, 50, timeStr); u8g2.sendBuffer();
      if (singleClick) { 
        totalSeconds = (setMinutes * 60) + setSeconds;
        if(totalSeconds > 0) { remainingSeconds = totalSeconds; timerMillis = millis(); isTimerFinished = false; digitalWrite(LED_PIN, LOW); currentState = TIMER_RUNNING; } 
        else { currentState = RELAY_MENU; encoder.setCount(0); }
      } break;
    }
    case TIMER_RUNNING: {
      u8g2.clearBuffer();
      if (!isTimerFinished) {
        if (millis() - timerMillis >= 1000) { timerMillis += 1000; remainingSeconds--; if (remainingSeconds <= 0) { isTimerFinished = true; digitalWrite(LED_PIN, HIGH); } }
        int centerX = 64, centerY = 32, radius = 28; int angle = (remainingSeconds * 360) / totalSeconds;
        for (int a = 0; a < angle; a++) { float rad = (a - 90) * 0.0174533; u8g2.drawDisc(centerX + cos(rad) * radius, centerY + sin(rad) * radius, 1); }
        char timeStr[10];
        if (remainingSeconds >= 60) {
          sprintf(timeStr, "%02d:%02d", remainingSeconds / 60, remainingSeconds % 60); u8g2.setFont(u8g2_font_logisoso16_tf);
          int textWidth = u8g2.getStrWidth(timeStr); u8g2.drawStr(centerX - (textWidth / 2), centerY + 6, timeStr); 
        } else {
          sprintf(timeStr, "%d", remainingSeconds); u8g2.setFont(u8g2_font_logisoso24_tf); 
          int textWidth = u8g2.getStrWidth(timeStr); u8g2.drawStr(centerX - (textWidth / 2), centerY + 10, timeStr); 
        }
      } else {
        u8g2.setFont(u8g2_font_logisoso20_tf); int tw = u8g2.getStrWidth("DA BAT!"); u8g2.drawStr(64 - tw/2, 35, "DA BAT!");
        u8g2.setFont(u8g2_font_6x10_tf); tw = u8g2.getStrWidth("Nhan de tat"); u8g2.drawStr(64 - tw/2, 55, "Nhan de tat");
      }
      u8g2.sendBuffer(); if (singleClick) { digitalWrite(LED_PIN, LOW); currentState = MAIN_MENU; encoder.setCount(0); } break;
    }

    case ALARM_SETUP_ON_HOUR: {
      int rawHour = (int)encoder.getCount() / 2; if (rawHour < 0) { rawHour = 23; encoder.setCount(46); } else if (rawHour > 23) { rawHour = 0; encoder.setCount(0); } alarmOnHour = rawHour;
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("GIO BAT:")/2, 20, "GIO BAT:");
      char timeStr[10]; sprintf(timeStr, "%02d:--", alarmOnHour); u8g2.setFont(u8g2_font_logisoso20_tf); u8g2.drawStr(64 - u8g2.getStrWidth(timeStr)/2, 50, timeStr); u8g2.sendBuffer();
      if (singleClick) { encoder.setCount(alarmOnMinute * 2); currentState = ALARM_SETUP_ON_MIN; } break;
    }
    case ALARM_SETUP_ON_MIN: {
      int rawMin = (int)encoder.getCount() / 2; if (rawMin < 0) { rawMin = 59; encoder.setCount(118); } else if (rawMin > 59) { rawMin = 0; encoder.setCount(0); } alarmOnMinute = rawMin;
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("PHUT BAT:")/2, 20, "PHUT BAT:");
      char timeStr[10]; sprintf(timeStr, "%02d:%02d", alarmOnHour, alarmOnMinute); u8g2.setFont(u8g2_font_logisoso20_tf); u8g2.drawStr(64 - u8g2.getStrWidth(timeStr)/2, 50, timeStr); u8g2.sendBuffer();
      if (singleClick) { alarmOffHour = alarmOnHour; encoder.setCount(alarmOffHour * 2); currentState = ALARM_SETUP_OFF_HOUR; } break;
    }
    case ALARM_SETUP_OFF_HOUR: {
      int rawHour = (int)encoder.getCount() / 2; if (rawHour < 0) { rawHour = 23; encoder.setCount(46); } else if (rawHour > 23) { rawHour = 0; encoder.setCount(0); } alarmOffHour = rawHour;
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("GIO TAT:")/2, 20, "GIO TAT:");
      char timeStr[10]; sprintf(timeStr, "%02d:--", alarmOffHour); u8g2.setFont(u8g2_font_logisoso20_tf); u8g2.drawStr(64 - u8g2.getStrWidth(timeStr)/2, 50, timeStr); u8g2.sendBuffer();
      if (singleClick) { encoder.setCount(alarmOffMinute * 2); currentState = ALARM_SETUP_OFF_MIN; } break;
    }
    
    case ALARM_SETUP_OFF_MIN: {
      int rawMin = (int)encoder.getCount() / 2; if (rawMin < 0) { rawMin = 59; encoder.setCount(118); } else if (rawMin > 59) { rawMin = 0; encoder.setCount(0); } alarmOffMinute = rawMin;
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("PHUT TAT:")/2, 20, "PHUT TAT:");
      char timeStr[10]; sprintf(timeStr, "%02d:%02d", alarmOffHour, alarmOffMinute); u8g2.setFont(u8g2_font_logisoso20_tf); u8g2.drawStr(64 - u8g2.getStrWidth(timeStr)/2, 50, timeStr); u8g2.sendBuffer();
      
      if (singleClick) { 
        isAlarmSet = true; 
        
        // Khởi tạo các biến chuỗi ngày giờ để gửi
        timeClient.update();
        time_t rawtime = timeClient.getEpochTime(); 
        struct tm * ti = localtime(&rawtime); 
        char dateBuf[20];
        sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);

        char onStr[10]; sprintf(onStr, "%02d:%02d", alarmOnHour, alarmOnMinute);
        char offStr[10]; sprintf(offStr, "%02d:%02d", alarmOffHour, alarmOffMinute);

        // Hiển thị màn hình chờ mượt mà
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(64 - u8g2.getStrWidth("Dang luu lich...")/2, 35, "Dang luu lich...");
        u8g2.sendBuffer();

        // Bắn tín hiệu "Đã lên lịch" lên Google Sheets
        sendDataToGoogleSheets(String(dateBuf), String(onStr), String(offStr), "Da_len_lich");

        currentState = ALARM_WAITING; 
      } 
      break;
    }
    
    case ALARM_WAITING: {
      u8g2.clearBuffer(); timeClient.update(); int curH = timeClient.getHours(); int curM = timeClient.getMinutes(); int curS = timeClient.getSeconds();
      static int lastTriggerMin = -1;

      // KIỂM TRA & KÍCH HOẠT RELAY + GỬI CẬP NHẬT LÊN GOOGLE SHEETS
      if (isAlarmSet && curM != lastTriggerMin) {
        
        // --- 1. SỰ KIỆN ĐẾN GIỜ BẬT ---
        if (curH == alarmOnHour && curM == alarmOnMinute) { 
          digitalWrite(LED_PIN, HIGH); 
          lastTriggerMin = curM; 

          time_t rawtime = timeClient.getEpochTime(); 
          struct tm * ti = localtime(&rawtime); 
          char dateBuf[20]; sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
          char onStr[10]; sprintf(onStr, "%02d:%02d", alarmOnHour, alarmOnMinute);
          char offStr[10]; sprintf(offStr, "%02d:%02d", alarmOffHour, alarmOffMinute);

          // Vẽ thông báo để tránh đơ OLED lúc gửi
          u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("Dang cap nhat...")/2, 35, "Dang cap nhat..."); u8g2.sendBuffer();
          
          sendDataToGoogleSheets(String(dateBuf), String(onStr), String(offStr), "Da_bat");
        } 
        
        // --- 2. SỰ KIỆN ĐẾN GIỜ TẮT ---
        else if (curH == alarmOffHour && curM == alarmOffMinute) { 
          digitalWrite(LED_PIN, LOW); 
          lastTriggerMin = curM; 
          
          time_t rawtime = timeClient.getEpochTime(); 
          struct tm * ti = localtime(&rawtime); 
          char dateBuf[20]; sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
          char onStr[10]; sprintf(onStr, "%02d:%02d", alarmOnHour, alarmOnMinute);
          char offStr[10]; sprintf(offStr, "%02d:%02d", alarmOffHour, alarmOffMinute);

          u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(64 - u8g2.getStrWidth("Dang cap nhat...")/2, 35, "Dang cap nhat..."); u8g2.sendBuffer();

          sendDataToGoogleSheets(String(dateBuf), String(onStr), String(offStr), "Hoan_tat");
          
          // Hoàn thành nhiệm vụ thì tắt cờ báo thức
          isAlarmSet = false; 
        }
      }

      // --- VẼ BẢNG ĐIỀU KHIỂN CHỜ ---
      u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(30, 10, "- HEN GIO -");
      char onStrDisp[15]; sprintf(onStrDisp, "ON : %02d:%02d", alarmOnHour, alarmOnMinute); 
      char offStrDisp[15]; sprintf(offStrDisp, "OFF: %02d:%02d", alarmOffHour, alarmOffMinute);
      u8g2.drawStr(5, 25, onStrDisp); u8g2.drawStr(5, 38, offStrDisp);
      if (digitalRead(LED_PIN) == HIGH) u8g2.drawStr(85, 32, "[BAT]"); else u8g2.drawStr(85, 32, "[TAT]");
      u8g2.drawHLine(0, 45, 128); char curStr[20]; sprintf(curStr, "Now: %02d:%02d:%02d", curH, curM, curS);
      u8g2.drawStr(64 - u8g2.getStrWidth(curStr)/2, 58, curStr); u8g2.sendBuffer();
      
      if (singleClick) { isAlarmSet = false; currentState = MAIN_MENU; encoder.setCount(0); } 
      break;
    }

    // ==========================================
    // --- CHỨC NĂNG 3: WIFI ANALYZER LIST ---
    // ==========================================
    case WIFI_ANALYZER_MODE: {
      if (scanInProgress) {
        if (millis() - lastScanTime > 5000) startWiFiScan();
        else processScanResult();
      } else {
        static unsigned long lastCompleteTime = 0;
        if (lastCompleteTime == 0) lastCompleteTime = millis();
        else if (millis() - lastCompleteTime > 2000) { startWiFiScan(); lastCompleteTime = 0; }
      }

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x8_tr); u8g2.drawStr(22, 7, "- Wi-Fi Analyzer -"); u8g2.drawHLine(0, 9, 128);

      if (ana_count == 0 && firstEnter) {
        u8g2.setFont(u8g2_font_6x10_tf); int tw = u8g2.getStrWidth("Dang quet song..."); u8g2.drawStr(64 - tw/2, 35, "Dang quet song...");
      } else if (ana_count > 0) {
        int itemsPerPage = 3; 
        
        int selectedAnaIdx = abs(currentCount) % ana_count;
        int scrollIdx = selectedAnaIdx - 1; 
        if (scrollIdx < 0) scrollIdx = 0;
        if (scrollIdx > ana_count - 3 && ana_count >= 3) scrollIdx = ana_count - 3;
        
        u8g2.setFont(u8g2_font_5x8_tr);
        
        for (int i = 0; i < itemsPerPage; i++) {
          int idx = scrollIdx + i;
          if (idx >= ana_count) break;
          int yBlock = 15 + (i * 17); 
          
          String dbmText = String(ana_rssi[idx]) + "dBm";
          u8g2.setCursor(45, yBlock + 6); u8g2.print(dbmText);

          String quality = "";
          if (ana_rssi[idx] >= -60) quality = "Tot"; else if (ana_rssi[idx] >= -75) quality = "Kha"; else if (ana_rssi[idx] >= -85) quality = "Yeu"; else quality = "Kem";
          u8g2.setCursor(95, yBlock + 6); u8g2.print(quality);

          int barWidth = map(ana_rssi[idx], -100, -40, 0, 83); barWidth = constrain(barWidth, 0, 83); 
          u8g2.drawFrame(45, yBlock + 8, 83, 4); u8g2.drawBox(45, yBlock + 8, barWidth, 4); 

          String ssid = ana_ssids[idx];
          if (ssid.length() > 7) ssid = ssid.substring(0, 7);
          
          if (idx == selectedAnaIdx) {
            u8g2.setDrawColor(1);
            u8g2.drawBox(0, yBlock + 2, 42, 9); 
            u8g2.setDrawColor(0); 
            u8g2.setCursor(1, yBlock + 10);
            u8g2.print(ssid);
            u8g2.setDrawColor(1); 
          } else {
            u8g2.setCursor(0, yBlock + 10);
            u8g2.print(ssid);
          }
        }

        if (singleClick) {
          selectedGraphSSID = ana_ssids[selectedAnaIdx];
          currentTargetRSSI = ana_rssi[selectedAnaIdx]; 
          
          for (int i = 0; i < 100; i++) rssiHistory[i] = currentTargetRSSI; 
          
          WiFi.scanDelete(); 
          scanInProgress = false;
          currentState = WIFI_GRAPH_MODE;
        }
      } else if (!firstEnter && ana_count == 0) {
        u8g2.setFont(u8g2_font_6x10_tf); int tw = u8g2.getStrWidth("Khong tim thay WiFi"); u8g2.drawStr(64 - tw/2, 35, "Khong tim thay WiFi");
        if (singleClick) { WiFi.scanDelete(); scanInProgress = false; currentState = MAIN_MENU; encoder.setCount(0); }
      }
      u8g2.sendBuffer();
      break;
    }

    // ==========================================
    // --- CHỨC NĂNG 3.1: WIFI REALTIME GRAPH ---
    // ==========================================
    case WIFI_GRAPH_MODE: {
      
      if (scanInProgress) {
        int scanResult = WiFi.scanComplete();
        if (scanResult == WIFI_SCAN_FAILED) {
          startWiFiScan();
        } else if (scanResult > 0) {
          bool found = false;
          for (int i = 0; i < scanResult; i++) {
            if (WiFi.SSID(i) == selectedGraphSSID) {
              currentTargetRSSI = WiFi.RSSI(i);
              found = true;
              break;
            }
          }
          if (!found) {
            currentTargetRSSI -= 5;
            if (currentTargetRSSI < -100) currentTargetRSSI = -100;
          }
          WiFi.scanDelete();
          scanInProgress = false;
        }
      } else {
        startWiFiScan();
      }

      static unsigned long lastShiftTime = 0;
      if (millis() - lastShiftTime > 150) {
        for (int i = 0; i < 99; i++) {
          rssiHistory[i] = rssiHistory[i + 1];
        }
        
        int rawNoise = currentTargetRSSI + random(-2, 3); 
        int smoothedRSSI = (rssiHistory[98] * 2 + rawNoise) / 3; 
        rssiHistory[99] = constrain(smoothedRSSI, -100, -30);
        
        lastShiftTime = millis();
      }

      int maxRSSI = -100;
      int minRSSI = -30;
      for(int i = 100 - 94; i < 100; i++) { 
          if(rssiHistory[i] > maxRSSI) maxRSSI = rssiHistory[i];
          if(rssiHistory[i] < minRSSI) minRSSI = rssiHistory[i];
      }
      
      int yTop = maxRSSI + 4;
      if(yTop > -30) yTop = -30;
      int yBot = minRSSI - 4;
      if(yBot < -100) yBot = -100;
      
      if(yTop - yBot < 16) {
          yTop = currentTargetRSSI + 8;
          yBot = currentTargetRSSI - 8;
      }

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_5x8_tr);
      
      String headerStr = selectedGraphSSID;
      if (headerStr.length() > 10) headerStr = headerStr.substring(0, 10);
      headerStr += " [" + String(rssiHistory[99]) + "dBm]";
      u8g2.drawStr(0, 7, headerStr.c_str());
      
      u8g2.drawLine(25, 61, 25, 12);     
      u8g2.drawLine(23, 15, 25, 12);     
      u8g2.drawLine(27, 15, 25, 12);     
      u8g2.drawLine(25, 61, 125, 61);    
      u8g2.drawLine(122, 59, 125, 61);   
      u8g2.drawLine(122, 63, 125, 61);   
      
      u8g2.setCursor(0, 18); u8g2.print(yTop);
      u8g2.setCursor(0, 38); u8g2.print((yTop + yBot) / 2);
      u8g2.setCursor(0, 61); u8g2.print(yBot);

      for (int i = 0; i < 93; i++) {
        int dataIdx = i + (100 - 94); 
        
        int x1 = 27 + i;
        int y1 = map(rssiHistory[dataIdx], yBot, yTop, 60, 14);
        int x2 = 27 + i + 1;
        int y2 = map(rssiHistory[dataIdx + 1], yBot, yTop, 60, 14);
        
        y1 = constrain(y1, 14, 60);
        y2 = constrain(y2, 14, 60);
        
        if (rssiHistory[dataIdx] == -100 && rssiHistory[dataIdx + 1] == -100) continue;
        
        u8g2.drawLine(x1, y1, x2, y2);
        u8g2.drawLine(x1, y1 + 1, x2, y2 + 1); 
        
        if (i % 2 == 0) { 
            u8g2.drawLine(x1, y1 + 3, x1, 60);
        }
      }

      u8g2.sendBuffer();

      if (singleClick) {
        WiFi.scanDelete();
        scanInProgress = false;
        currentState = WIFI_ANALYZER_MODE;
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
      u8g2.sendBuffer(); 
      if (singleClick) { currentState = MAIN_MENU; encoder.setCount(0);}
      break;
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
      u8g2.sendBuffer(); 
      if (singleClick) { currentState = INPUT_PASSWORD; encoder.setCount(0); } break;
    }

    case INPUT_PASSWORD: {
      charIndex = abs(currentCount) % strlen(keyboard);
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(0, 12, "Pass:"); u8g2.drawFrame(0, 16, 128, 15); u8g2.drawStr(5, 27, password.c_str());
      u8g2.setFont(u8g2_font_9x15_tf); u8g2.drawStr(58, 55, String(keyboard[charIndex]).c_str()); 
      u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(40, 53, String(keyboard[(charIndex + strlen(keyboard) - 1) % strlen(keyboard)]).c_str()); u8g2.drawStr(80, 53, String(keyboard[(charIndex + 1) % strlen(keyboard)]).c_str());
      u8g2.drawHLine(55, 58, 15); u8g2.drawStr(0, 64, "<:Xoa  !:Ket noi"); u8g2.sendBuffer();
      
      if (singleClick) { 
        digitalWrite(LED_PIN, LOW); char c = keyboard[charIndex]; 
        if (c == '!') currentState = CONNECTING; 
        else if (c == '<') { if (password.length() > 0) password.remove(password.length() - 1); } 
        else password += c; 
      } break;
    }

    case CONNECTING: {
      u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf); u8g2.drawStr(0, 35, "Connecting..."); u8g2.sendBuffer();
      WiFi.begin(ssids[selectedWifi].c_str(), password.c_str()); unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(100);
      
      if (WiFi.status() == WL_CONNECTED) { 
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssids[selectedWifi]);
        preferences.putString("pass", password);
        preferences.end();
        
        timeClient.begin(); 
        currentState = MAIN_MENU; 
      } else {
        currentState = SELECT_WIFI; 
      }
      break;
    }
  }
}