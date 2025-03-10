/*
  Automatic School Bell System
  Hardware:
  - NodeMCU ESP8266
  - P10 LED Display
  - DS3231 RTC Module
  - Relay for bell
  - Buttons for control
  
  Features:
  - 12 alarms per day
  - Separate alarm set for Friday
  - Real-time display with multiple modes
  - Web configuration
  - WiFi connectivity in station and AP mode
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <DMD2.h>
#include <fonts/SystemFont5x7.h>
#include <fonts/Arial_Black_16.h>

// WiFi credentials
const char* ap_ssid = "SchoolBell";      // SoftAP SSID
const char* ap_password = "12345678";    // SoftAP password
String station_ssid = "Galaxy J6932";    // Station mode SSID (configurable via web)
String station_password = "9876789987";  // Station mode password (configurable via web)

// Pin definitions
// P10 LED Display pins
#define P10_A D0     // A
#define P10_B D8     // B
#define P10_SCLK D3  // SCLK
#define P10_DATA D4  // DATA
#define P10_NOE D5   // NOE (active low Output Enable)

// For RTC module - using default I2C pins on NodeMCU
// D1(GPIO5)=SCL, D2(GPIO4)=SDA

// Other pins
#define RELAY_PIN D6
#define BTN_MODE D7
#define BTN_SET D9
#define BTN_UP D10

// Display configuration
#define DISPLAYS_WIDE 1
#define DISPLAYS_HIGH 1


SPIDMD dmd(DISPLAYS_WIDE, DISPLAYS_HIGH, P10_NOE, P10_A, P10_B, P10_SCLK);
const uint8_t* font = Arial_Black_16;

// RTC configuration
RTC_DS3231 rtc;

// Web server
ESP8266WebServer server(80);

// Display modes
enum DisplayMode {
  TIME_ONLY,
  TIME_DATE,
  TIME_NEXT_ALARM
};
DisplayMode currentMode = TIME_ONLY;

// Alarm structure
struct Alarm {
  bool enabled;
  uint8_t hour;
  uint8_t minute;
  uint8_t duration;  // seconds
};

// Settings structure
struct Settings {
  Alarm weekdayAlarms[12];  // 12 alarms for regular weekdays
  Alarm fridayAlarms[12];   // 12 alarms for Friday
  bool isConfigured;        // Flag to check if EEPROM is configured
};

Settings settings;

// Button state tracking
bool lastModeBtn = HIGH;
bool lastSetBtn = HIGH;
bool lastUpBtn = HIGH;

// State variables
bool isSettingTime = false;
bool isSettingAlarm = false;
uint8_t settingStep = 0;
uint8_t currentAlarmEditing = 0;
bool editingFriday = false;

// Timing variables
unsigned long lastBtnCheck = 0;
const unsigned long btnDebounce = 50;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;

unsigned long bellStartTime = 0;
unsigned long bellDuration = 0;
bool bellActive = false;

// Function prototypes
void loadSettings();
void saveSettings();
void setupWiFi();
void setupWebServer();
void handleRoot();
void handleGetSettings();
void handlePostSettings();
void handleGetTime();
void handleSetTime();
void handleSetWiFi();
void checkButtons();
void updateDisplay();
Alarm* getNextAlarm(DateTime now);
void checkAlarms();
void handleBell();
void displayWelcome();
void displayError(const char* message);
uint8_t getDaysInMonth(uint8_t month, uint16_t year);

void setup() {
  Serial.begin(115200);

  // Initialize pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Bell off initially

  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_SET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);

  // Initialize EEPROM
  EEPROM.begin(sizeof(Settings) + 100);  // Extra space for future use

  // Initialize display
  dmd.begin();
  dmd.clearScreen();
  dmd.selectFont(font);

  // Initialize RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    displayError("RTC ERROR");
    delay(2000);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting default time!");
    // Set to compile time as default
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Load settings from EEPROM
  loadSettings();

  // Setup WiFi in hybrid mode
  setupWiFi();

  // Setup web server
  setupWebServer();

  // Display welcome message
  displayWelcome();

  Serial.println("System initialized");
}

void loop() {
  // Handle web server clients
  server.handleClient();

  // Check button inputs
  checkButtons();

  // Update display
  updateDisplay();

  // Check alarms
  checkAlarms();

  // Handle active bell
  handleBell();
}

// Load settings from EEPROM
void loadSettings() {
  EEPROM.get(0, settings);

  // If not configured, set defaults
  if (settings.isConfigured != true) {
    Serial.println("Setting defaults");

    // Clear all alarms
    for (int i = 0; i < 12; i++) {
      settings.weekdayAlarms[i].enabled = false;
      settings.weekdayAlarms[i].hour = 0;
      settings.weekdayAlarms[i].minute = 0;
      settings.weekdayAlarms[i].duration = 5;  // 5 seconds default

      settings.fridayAlarms[i].enabled = false;
      settings.fridayAlarms[i].hour = 0;
      settings.fridayAlarms[i].minute = 0;
      settings.fridayAlarms[i].duration = 5;  // 5 seconds default
    }

    // Set some example alarms for weekdays
    settings.weekdayAlarms[0].enabled = true;
    settings.weekdayAlarms[0].hour = 8;
    settings.weekdayAlarms[0].minute = 0;
    settings.weekdayAlarms[0].duration = 5;

    settings.weekdayAlarms[1].enabled = true;
    settings.weekdayAlarms[1].hour = 8;
    settings.weekdayAlarms[1].minute = 45;
    settings.weekdayAlarms[1].duration = 5;

    // Set some example alarms for Friday
    settings.fridayAlarms[0].enabled = true;
    settings.fridayAlarms[0].hour = 8;
    settings.fridayAlarms[0].minute = 0;
    settings.fridayAlarms[0].duration = 5;

    settings.fridayAlarms[1].enabled = true;
    settings.fridayAlarms[1].hour = 8;
    settings.fridayAlarms[1].minute = 30;
    settings.fridayAlarms[1].duration = 5;

    settings.isConfigured = true;
    saveSettings();
  }
}

// Save settings to EEPROM
void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

// Setup WiFi as SoftAP and try to connect to configured station if available
void setupWiFi() {
  // Read WiFi credentials from EEPROM if they exist
  // In this simplified example, we're using hardcoded defaults

  // Setup SoftAP
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("SoftAP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Try to connect to station if configured
  if (station_ssid.length() > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(station_ssid.c_str(), station_password.c_str());
    Serial.print("Connecting to ");
    Serial.println(station_ssid);

    // Wait for connection with timeout
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(station_ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("");
      Serial.println("Connection failed, running in AP mode only");
      WiFi.mode(WIFI_AP);
    }
  } else {
    WiFi.mode(WIFI_AP);
  }
}

// Setup web server routes
void setupWebServer() {
  // Root page
  server.on("/", HTTP_GET, handleRoot);

  // API endpoints
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/time", HTTP_GET, handleGetTime);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/wifi", HTTP_POST, handleSetWiFi);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

// Handle root page request
void handleRoot() {
  String html = "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>School Bell System</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial; margin: 0; padding: 20px; }"
    "h1 { color: #333; }"
    "table { border-collapse: collapse; width: 100%; margin-bottom: 20px; }"
    "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
    "th { background-color: #f2f2f2; }"
    "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; box-sizing: border-box; }"
    "input[type='checkbox'] { transform: scale(1.5); margin-right: 10px; }"
    "button { background-color: #4CAF50; color: white; border: none; padding: 10px 15px; cursor: pointer; margin: 5px 0; }"
    "button:hover { background-color: #45a049; }"
    ".tab { overflow: hidden; border: 1px solid #ccc; background-color: #f1f1f1; }"
    ".tab button { background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; }"
    ".tab button:hover { background-color: #ddd; }"
    ".tab button.active { background-color: #ccc; }"
    ".tabcontent { display: none; padding: 12px; border: 1px solid #ccc; border-top: none; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>School Bell System</h1>"
    
    "<div class='tab'>"
    "<button id='btn-status' class='tablinks'>Status</button>"
    "<button id='btn-weekday' class='tablinks'>Weekday Alarms</button>"
    "<button id='btn-friday' class='tablinks'>Friday Alarms</button>"
    "<button id='btn-time' class='tablinks'>Set Time</button>"
    "<button id='btn-wifi' class='tablinks'>WiFi Settings</button>"
    "</div>"
    
    "<div id='status' class='tabcontent'>"
    "<h2>Current Status</h2>"
    "<p>Time: <span id='current-time'>00:00:00</span></p>"
    "<p>Date: <span id='current-date'>01/01/2000 - Mon</span></p>"
    "<button id='refresh-btn'>Refresh Page</button>"
    "</div>"
    
    "<div id='weekday' class='tabcontent'>"
    "<h2>Weekday Alarms (Monday-Thursday)</h2>"
    "<table id='weekday-table'>"
    "<tr><th>Enable</th><th>Hour</th><th>Minute</th><th>Duration (sec)</th></tr>"
    "</table>"
    "<button id='save-weekday-btn'>Save Weekday Alarms</button>"
    "</div>"
    
    "<div id='friday' class='tabcontent'>"
    "<h2>Friday Alarms</h2>"
    "<table id='friday-table'>"
    "<tr><th>Enable</th><th>Hour</th><th>Minute</th><th>Duration (sec)</th></tr>"
    "</table>"
    "<button id='save-friday-btn'>Save Friday Alarms</button>"
    "</div>"
    
    "<div id='time' class='tabcontent'>"
    "<h2>Set Time & Date</h2>"
    "<table>"
    "<tr><th>Hour</th><th>Minute</th><th>Second</th></tr>"
    "<tr>"
    "<td><input type='number' id='time-hour' min='0' max='23' value='0'></td>"
    "<td><input type='number' id='time-minute' min='0' max='59' value='0'></td>"
    "<td><input type='number' id='time-second' min='0' max='59' value='0'></td>"
    "</tr>"
    "</table>"
    
    "<table>"
    "<tr><th>Day</th><th>Month</th><th>Year</th></tr>"
    "<tr>"
    "<td><input type='number' id='date-day' min='1' max='31' value='1'></td>"
    "<td><input type='number' id='date-month' min='1' max='12' value='1'></td>"
    "<td><input type='number' id='date-year' min='2000' max='2099' value='2023'></td>"
    "</tr>"
    "</table>"
    
    "<button id='save-time-btn'>Set Time & Date</button>"
    "</div>"
    
    "<div id='wifi' class='tabcontent'>"
    "<h2>WiFi Settings</h2>";
    
    // Display current WiFi status
    html += "<p>Current mode: ";
    html += (WiFi.status() == WL_CONNECTED) ? "Connected to " + station_ssid : "AP Mode Only";
    html += "</p>";
    html += "<p>AP SSID: " + String(ap_ssid) + " (IP: " + WiFi.softAPIP().toString() + ")</p>";
    
    if (WiFi.status() == WL_CONNECTED) {
      html += "<p>Station IP: " + WiFi.localIP().toString() + "</p>";
    }
    
    html += "<table>"
    "<tr><th>WiFi SSID</th><td><input type='text' id='wifi-ssid' value='" + station_ssid + "'></td></tr>"
    "<tr><th>WiFi Password</th><td><input type='password' id='wifi-password' value='" + station_password + "'></td></tr>"
    "</table>"
    "<button id='save-wifi-btn'>Save WiFi Settings</button>"
    "</div>"
    
    "<script>"
    "// Global variables"
    "let settings = {};"
    "let currentTime = {};"
    
    "// Tab switching function"
    "function showTab(tabId) {"
    "  var tabcontent = document.getElementsByClassName('tabcontent');"
    "  for (var i = 0; i < tabcontent.length; i++) {"
    "    tabcontent[i].style.display = 'none';"
    "  }"
    "  document.getElementById(tabId).style.display = 'block';"
    "}"
    
    "// Save time function"
    "function saveTime() {"
    "  var data = {"
    "    hour: parseInt(document.getElementById('time-hour').value),"
    "    minute: parseInt(document.getElementById('time-minute').value),"
    "    second: parseInt(document.getElementById('time-second').value),"
    "    day: parseInt(document.getElementById('date-day').value),"
    "    month: parseInt(document.getElementById('date-month').value),"
    "    year: parseInt(document.getElementById('date-year').value)"
    "  };"
    "  fetch('/api/time', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify(data)"
    "  })"
    "  .then(function(response) { return response.json(); })"
    "  .then(function(data) { alert('Time set successfully'); })"
    "  .catch(function(error) { alert('Error setting time: ' + error); });"
    "}"
    
    "// Save WiFi function"
    "function saveWiFi() {"
    "  var data = {"
    "    ssid: document.getElementById('wifi-ssid').value,"
    "    password: document.getElementById('wifi-password').value"
    "  };"
    "  fetch('/api/wifi', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify(data)"
    "  })"
    "  .then(function(response) { return response.json(); })"
    "  .then(function(data) { alert('WiFi settings saved. Reconnecting...'); })"
    "  .catch(function(error) { alert('Error saving WiFi settings: ' + error); });"
    "}"
    
    "// Settings and alarms functions"
    "function fetchSettings() {"
    "  fetch('/api/settings')"
    "  .then(function(response) { return response.json(); })"
    "  .then(function(data) {"
    "    settings = data;"
    "    // Populate weekday table"
    "    var weekdayTable = document.getElementById('weekday-table');"
    "    var fridayTable = document.getElementById('friday-table');"
    
    "    // Clear existing rows except header"
    "    while (weekdayTable.rows.length > 1) { weekdayTable.deleteRow(1); }"
    "    while (fridayTable.rows.length > 1) { fridayTable.deleteRow(1); }"
    
    "    // Add weekday alarms"
    "    for (var i = 0; i < data.weekdayAlarms.length; i++) {"
    "      var alarm = data.weekdayAlarms[i];"
    "      var row = weekdayTable.insertRow(-1);"
    
    "      var cell1 = row.insertCell(0);"
    "      var cell2 = row.insertCell(1);"
    "      var cell3 = row.insertCell(2);"
    "      var cell4 = row.insertCell(3);"
    
    "      cell1.innerHTML = '<input type=\"checkbox\" id=\"w-alarm-' + i + '-enabled\" ' + (alarm.enabled ? 'checked' : '') + '>';"
    "      cell2.innerHTML = '<input type=\"number\" id=\"w-alarm-' + i + '-hour\" min=\"0\" max=\"23\" value=\"' + alarm.hour + '\">';"
    "      cell3.innerHTML = '<input type=\"number\" id=\"w-alarm-' + i + '-minute\" min=\"0\" max=\"59\" value=\"' + alarm.minute + '\">';"
    "      cell4.innerHTML = '<input type=\"number\" id=\"w-alarm-' + i + '-duration\" min=\"1\" max=\"60\" value=\"' + alarm.duration + '\">';"
    "    }"
    
    "    // Add Friday alarms"
    "    for (var i = 0; i < data.fridayAlarms.length; i++) {"
    "      var alarm = data.fridayAlarms[i];"
    "      var row = fridayTable.insertRow(-1);"
    
    "      var cell1 = row.insertCell(0);"
    "      var cell2 = row.insertCell(1);"
    "      var cell3 = row.insertCell(2);"
    "      var cell4 = row.insertCell(3);"
    
    "      cell1.innerHTML = '<input type=\"checkbox\" id=\"f-alarm-' + i + '-enabled\" ' + (alarm.enabled ? 'checked' : '') + '>';"
    "      cell2.innerHTML = '<input type=\"number\" id=\"f-alarm-' + i + '-hour\" min=\"0\" max=\"23\" value=\"' + alarm.hour + '\">';"
    "      cell3.innerHTML = '<input type=\"number\" id=\"f-alarm-' + i + '-minute\" min=\"0\" max=\"59\" value=\"' + alarm.minute + '\">';"
    "      cell4.innerHTML = '<input type=\"number\" id=\"f-alarm-' + i + '-duration\" min=\"1\" max=\"60\" value=\"' + alarm.duration + '\">';"
    "    }"
    "  })"
    "  .catch(function(error) { console.error('Error fetching settings:', error); });"
    "}"
    
    "function saveSettings(type) {"
    "  var weekdayAlarms = [];"
    "  var fridayAlarms = [];"
    
    "  // Collect weekday alarms"
    "  for (var i = 0; i < 12; i++) {"
    "    weekdayAlarms.push({"
    "      enabled: document.getElementById('w-alarm-' + i + '-enabled').checked,"
    "      hour: parseInt(document.getElementById('w-alarm-' + i + '-hour').value),"
    "      minute: parseInt(document.getElementById('w-alarm-' + i + '-minute').value),"
    "      duration: parseInt(document.getElementById('w-alarm-' + i + '-duration').value)"
    "    });"
    "  }"
    
    "  // Collect Friday alarms"
    "  for (var i = 0; i < 12; i++) {"
    "    fridayAlarms.push({"
    "      enabled: document.getElementById('f-alarm-' + i + '-enabled').checked,"
    "      hour: parseInt(document.getElementById('f-alarm-' + i + '-hour').value),"
    "      minute: parseInt(document.getElementById('f-alarm-' + i + '-minute').value),"
    "      duration: parseInt(document.getElementById('f-alarm-' + i + '-duration').value)"
    "    });"
    "  }"
    
    "  var data = {"
    "    weekdayAlarms: weekdayAlarms,"
    "    fridayAlarms: fridayAlarms"
    "  };"
    
    "  fetch('/api/settings', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify(data)"
    "  })"
    "  .then(function(response) { return response.json(); })"
    "  .then(function(data) { alert('Settings saved successfully'); })"
    "  .catch(function(error) { alert('Error saving settings: ' + error); });"
    "}"
    
    "// Current time update function"
    "function updateTime() {"
    "  fetch('/api/time')"
    "  .then(function(response) { return response.json(); })"
    "  .then(function(data) {"
    "    currentTime = data;"
    "    var timeElem = document.getElementById('current-time');"
    "    var dateElem = document.getElementById('current-date');"
    
    "    if (timeElem && dateElem) {"
    "      // Format time as HH:MM:SS"
    "      var hour = data.hour.toString().padStart(2, '0');"
    "      var minute = data.minute.toString().padStart(2, '0');"
    "      var second = data.second.toString().padStart(2, '0');"
    "      timeElem.textContent = hour + ':' + minute + ':' + second;"
    
    "      // Format date as DD/MM/YYYY - Day"
    "      var days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];"
    "      dateElem.textContent = data.day + '/' + data.month + '/' + data.year + ' - ' + days[data.dayOfWeek];"
    
    "      // Update time setting inputs"
    "      document.getElementById('time-hour').value = data.hour;"
    "      document.getElementById('time-minute').value = data.minute;"
    "      document.getElementById('time-second').value = data.second;"
    "      document.getElementById('date-day').value = data.day;"
    "      document.getElementById('date-month').value = data.month;"
    "      document.getElementById('date-year').value = data.year;"
    "    }"
    "    setTimeout(updateTime, 1000); // Update every second"
    "  })"
    "  .catch(function(error) { console.error('Error updating time:', error); });"
    "}"
    
    "// Initialize on page load"
    "document.addEventListener('DOMContentLoaded', function() {"
    "  // Show status tab by default"
    "  showTab('status');"
    
    "  // Add tab button event listeners"
    "  document.getElementById('btn-status').addEventListener('click', function() { showTab('status'); });"
    "  document.getElementById('btn-weekday').addEventListener('click', function() { showTab('weekday'); });"
    "  document.getElementById('btn-friday').addEventListener('click', function() { showTab('friday'); });"
    "  document.getElementById('btn-time').addEventListener('click', function() { showTab('time'); });"
    "  document.getElementById('btn-wifi').addEventListener('click', function() { showTab('wifi'); });"
    
    "  // Add button event listeners"
    "  document.getElementById('refresh-btn').addEventListener('click', function() { location.reload(); });"
    "  document.getElementById('save-time-btn').addEventListener('click', saveTime);"
    "  document.getElementById('save-weekday-btn').addEventListener('click', function() { saveSettings('weekday'); });"
    "  document.getElementById('save-friday-btn').addEventListener('click', function() { saveSettings('friday'); });"
    "  document.getElementById('save-wifi-btn').addEventListener('click', saveWiFi);"
    
    "  // Initialize data"
    "  fetchSettings();"
    "  updateTime();"
    "});"
    "</script>"
    "</body>"
    "</html>";
    
  server.send(200, "text/html", html);
}

// Handle API request for current settings
void handleGetSettings() {
  DynamicJsonDocument doc(2048);

  // Add weekday alarms
  JsonArray weekdayAlarms = doc.createNestedArray("weekdayAlarms");
  for (int i = 0; i < 12; i++) {
    JsonObject alarmObj = weekdayAlarms.createNestedObject();
    alarmObj["enabled"] = settings.weekdayAlarms[i].enabled;
    alarmObj["hour"] = settings.weekdayAlarms[i].hour;
    alarmObj["minute"] = settings.weekdayAlarms[i].minute;
    alarmObj["duration"] = settings.weekdayAlarms[i].duration;
  }

  // Add Friday alarms
  JsonArray fridayAlarms = doc.createNestedArray("fridayAlarms");
  for (int i = 0; i < 12; i++) {
    JsonObject alarmObj = fridayAlarms.createNestedObject();
    alarmObj["enabled"] = settings.fridayAlarms[i].enabled;
    alarmObj["hour"] = settings.fridayAlarms[i].hour;
    alarmObj["minute"] = settings.fridayAlarms[i].minute;
    alarmObj["duration"] = settings.fridayAlarms[i].duration;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handle API request to update settings
void handlePostSettings() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(2048);

    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }

    // Update weekday alarms
    JsonArray weekdayAlarms = doc["weekdayAlarms"];
    for (int i = 0; i < 12 && i < weekdayAlarms.size(); i++) {
      settings.weekdayAlarms[i].enabled = weekdayAlarms[i]["enabled"];
      settings.weekdayAlarms[i].hour = weekdayAlarms[i]["hour"];
      settings.weekdayAlarms[i].minute = weekdayAlarms[i]["minute"];
      settings.weekdayAlarms[i].duration = weekdayAlarms[i]["duration"];
    }

    // Update Friday alarms
    JsonArray fridayAlarms = doc["fridayAlarms"];
    for (int i = 0; i < 12 && i < fridayAlarms.size(); i++) {
      settings.fridayAlarms[i].enabled = fridayAlarms[i]["enabled"];
      settings.fridayAlarms[i].hour = fridayAlarms[i]["hour"];
      settings.fridayAlarms[i].minute = fridayAlarms[i]["minute"];
      settings.fridayAlarms[i].duration = fridayAlarms[i]["duration"];
    }

    // Save to EEPROM
    saveSettings();

    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided\"}");
  }
}

// Handle API request for current time
void handleGetTime() {
  DateTime now = rtc.now();

  DynamicJsonDocument doc(256);
  doc["hour"] = now.hour();
  doc["minute"] = now.minute();
  doc["second"] = now.second();
  doc["day"] = now.day();
  doc["month"] = now.month();
  doc["year"] = now.year();
  doc["dayOfWeek"] = now.dayOfTheWeek();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handle API request to set time
void handleSetTime() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);

    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }

    int hour = doc["hour"];
    int minute = doc["minute"];
    int second = doc["second"];
    int day = doc["day"];
    int month = doc["month"];
    int year = doc["year"];

    // Validate values
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 || day < 1 || day > 31 || month < 1 || month > 12 || year < 2000 || year > 2099) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid time/date values\"}");
      return;
    }

    // Further validate day based on month and year
    if (day > getDaysInMonth(month, year)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid day for the given month and year\"}");
      return;
    }

    // Set RTC time
    rtc.adjust(DateTime(year, month, day, hour, minute, second));

    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided\"}");
  }
}

// Handle API request to set WiFi settings
void handleSetWiFi() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);

    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }

    station_ssid = doc["ssid"].as<String>();
    station_password = doc["password"].as<String>();

    // Send response before reconnecting
    server.send(200, "application/json", "{\"status\":\"success\"}");

    // Save the WiFi settings to EEPROM or other persistent storage
    // ...

    // Attempt to reconnect with new settings
    delay(500);
    setupWiFi();
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided\"}");
  }
}

// Check physical button presses
void checkButtons() {
  unsigned long currentMillis = millis();

  // Only check buttons every debounce period
  if (currentMillis - lastBtnCheck < btnDebounce) {
    return;
  }
  lastBtnCheck = currentMillis;

  // Read button states
  bool modeBtn = digitalRead(BTN_MODE);
  bool setBtn = digitalRead(BTN_SET);
  bool upBtn = digitalRead(BTN_UP);

  // Mode button pressed (falling edge)
  if (modeBtn == LOW && lastModeBtn == HIGH) {
    if (isSettingTime || isSettingAlarm) {
      // Cancel setting mode
      isSettingTime = false;
      isSettingAlarm = false;
      settingStep = 0;
    } else {
      // Cycle through display modes
      currentMode = (DisplayMode)((currentMode + 1) % 3);
    }
  }

  // Set button pressed (falling edge)
  if (setBtn == LOW && lastSetBtn == HIGH) {
    if (!isSettingTime && !isSettingAlarm) {
      // Enter time setting mode
      isSettingTime = true;
      settingStep = 0;
    } else if (isSettingTime) {
      // Move to next time setting step
      settingStep++;
      if (settingStep > 5) {  // Hour, Minute, Second, Day, Month, Year
        // Save time to RTC
        DateTime now = rtc.now();
        rtc.adjust(now);

        // Exit setting mode
        isSettingTime = false;
        settingStep = 0;
      }
    } else if (isSettingAlarm) {
      // Move to next alarm setting step
      settingStep++;
      if (settingStep > 3) {  // Hour, Minute, Duration, Enabled
        // Save settings
        saveSettings();

        // Move to next alarm or exit
        currentAlarmEditing++;
        if (currentAlarmEditing >= 12) {
          if (!editingFriday) {
            // Switch to Friday alarms
            editingFriday = true;
            currentAlarmEditing = 0;
          } else {
            // Exit setting mode
            isSettingAlarm = false;
            settingStep = 0;
          }
        } else {
          // Start setting next alarm
          settingStep = 0;
        }
      }
    }
  }

  // Up button pressed (falling edge)
  if (upBtn == LOW && lastUpBtn == HIGH) {
    if (isSettingTime) {
      // Adjust time value based on current step
      DateTime now = rtc.now();
      uint16_t year = now.year();
      uint8_t month = now.month();
      uint8_t day = now.day();
      uint8_t hour = now.hour();
      uint8_t minute = now.minute();
      uint8_t second = now.second();

      switch (settingStep) {
        case 0:  // Hour
          hour = (hour + 1) % 24;
          break;
        case 1:  // Minute
          minute = (minute + 1) % 60;
          break;
        case 2:  // Second
          second = (second + 1) % 60;
          break;
        case 3:  // Day
          day = day % getDaysInMonth(month, year) + 1;
          break;
        case 4:  // Month
          month = month % 12 + 1;
          // Adjust day if needed
          if (day > getDaysInMonth(month, year)) {
            day = getDaysInMonth(month, year);
          }
          break;
        case 5:  // Year
          year = (year + 1 > 2099) ? 2000 : year + 1;
          // Adjust day if needed (for leap years)
          if (day > getDaysInMonth(month, year)) {
            day = getDaysInMonth(month, year);
          }
          break;
      }

      rtc.adjust(DateTime(year, month, day, hour, minute, second));
    } else if (isSettingAlarm) {
      // Get correct alarm reference based on day type and index
      Alarm* currentAlarm = editingFriday ? &settings.fridayAlarms[currentAlarmEditing] : &settings.weekdayAlarms[currentAlarmEditing];

      // Adjust alarm value based on current step
      switch (settingStep) {
        case 0:  // Hour
          currentAlarm->hour = (currentAlarm->hour + 1) % 24;
          break;
        case 1:  // Minute
          currentAlarm->minute = (currentAlarm->minute + 1) % 60;
          break;
        case 2:  // Duration
          currentAlarm->duration = (currentAlarm->duration + 1 > 60) ? 1 : currentAlarm->duration + 1;
          break;
        case 3:  // Enabled
          currentAlarm->enabled = !currentAlarm->enabled;
          break;
      }
    } else {
      // Enter alarm setting mode
      isSettingAlarm = true;
      settingStep = 0;
      currentAlarmEditing = 0;
      editingFriday = false;
    }
  }

  // Update button state history
  lastModeBtn = modeBtn;
  lastSetBtn = setBtn;
  lastUpBtn = upBtn;
}

// Update the LED display
void updateDisplay() {
  unsigned long currentMillis = millis();

  // Update display at the specified interval
  if (currentMillis - lastDisplayUpdate < displayUpdateInterval) {
    return;
  }
  lastDisplayUpdate = currentMillis;

  // Get current time
  DateTime now = rtc.now();

  // Clear the display
  dmd.clearScreen();

  // Format time as HH:MM
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());

  if (isSettingTime) {
    // Display indicator for setting mode
    String indicator = "Set:";
    switch (settingStep) {
      case 0: indicator += "Hour"; break;
      case 1: indicator += "Min"; break;
      case 2: indicator += "Sec"; break;
      case 3: indicator += "Day"; break;
      case 4: indicator += "Month"; break;
      case 5: indicator += "Year"; break;
    }
    dmd.selectFont(SystemFont5x7);
    dmd.drawString(0, 0, indicator.c_str());
    dmd.selectFont(font);

    // Display blinking value
    if (millis() % 1000 < 500) {
      switch (settingStep) {
        case 0:
        case 1:
        case 2:
          dmd.drawString(0, 8, timeStr);
          break;
        case 3:
        case 4:
        case 5:
          char dateStr[11];
          sprintf(dateStr, "%02d/%02d/%d", now.day(), now.month(), now.year());
          dmd.selectFont(SystemFont5x7);
          dmd.drawString(0, 8, dateStr);
          break;
      }
    }
  } else if (isSettingAlarm) {
    // Get correct alarm reference
    Alarm* currentAlarm = editingFriday ? &settings.fridayAlarms[currentAlarmEditing] : &settings.weekdayAlarms[currentAlarmEditing];

    // Display indicator for alarm setting
    String indicator = editingFriday ? "Fri:" : "Reg:";
    indicator += String(currentAlarmEditing + 1);
    dmd.selectFont(SystemFont5x7);
    dmd.drawString(0, 0, indicator.c_str());

    // Display different value based on step
    if (settingStep < 2) {
      // Display HH:MM for alarm time
      char alarmTimeStr[6];
      sprintf(alarmTimeStr, "%02d:%02d", currentAlarm->hour, currentAlarm->minute);
      dmd.selectFont(font);

      // Blink the appropriate part
      if (millis() % 1000 < 500) {
        dmd.drawString(0, 8, alarmTimeStr);
      }
    } else if (settingStep == 2) {
      // Display duration
      String durStr = "Dur:" + String(currentAlarm->duration) + "s";
      dmd.selectFont(SystemFont5x7);

      // Blink
      if (millis() % 1000 < 500) {
        dmd.drawString(0, 8, durStr.c_str());
      }
    } else if (settingStep == 3) {
      // Display enabled status
      String enStr = "Enabled:";
      enStr += currentAlarm->enabled ? "Y" : "N";
      dmd.selectFont(SystemFont5x7);

      // Blink
      if (millis() % 1000 < 500) {
        dmd.drawString(0, 8, enStr.c_str());
      }
    }
  } else {
    // Normal display mode
    switch (currentMode) {
      case TIME_ONLY:
        // Display just the time
        dmd.selectFont(font);
        dmd.drawString(0, 0, timeStr);
        break;

      case TIME_DATE:
        // Display time and date
        dmd.selectFont(font);
        dmd.drawString(0, 0, timeStr);

        // Format date as DD/MM
        char dateStr[6];
        sprintf(dateStr, "%02d/%02d", now.day(), now.month());
        dmd.selectFont(SystemFont5x7);
        dmd.drawString(0, 16, dateStr);
        break;

      case TIME_NEXT_ALARM:
        // Display time and next alarm
        dmd.selectFont(font);
        dmd.drawString(0, 0, timeStr);

        // Find next alarm
        Alarm* nextAlarm = getNextAlarm(now);

        if (nextAlarm) {
          // Format next alarm time
          char nextStr[12];
          sprintf(nextStr, "Next:%02d:%02d", nextAlarm->hour, nextAlarm->minute);
          dmd.selectFont(SystemFont5x7);
          dmd.drawString(0, 16, nextStr);
        } else {
          dmd.selectFont(SystemFont5x7);
          dmd.drawString(0, 16, "No Alarms");
        }
        break;
    }
  }
}

// Get next scheduled alarm based on current time
Alarm* getNextAlarm(DateTime now) {
  Alarm* nextAlarm = nullptr;
  uint16_t minutesToNext = 24 * 60;  // 24 hours in minutes

  // Determine if today is Friday (day of week 5)
  bool isFriday = (now.dayOfTheWeek() == 5);

  // Current time in minutes since midnight
  uint16_t currentMinutes = now.hour() * 60 + now.minute();

  // Check appropriate alarms based on day of week
  Alarm* alarmsToCheck = isFriday ? settings.fridayAlarms : settings.weekdayAlarms;

  for (int i = 0; i < 12; i++) {
    if (!alarmsToCheck[i].enabled) continue;

    // Alarm time in minutes
    uint16_t alarmMinutes = alarmsToCheck[i].hour * 60 + alarmsToCheck[i].minute;

    // Calculate minutes until this alarm
    uint16_t minutesUntil;
    if (alarmMinutes > currentMinutes) {
      // Alarm is later today
      minutesUntil = alarmMinutes - currentMinutes;
    } else {
      // Alarm would be tomorrow, skip it
      continue;
    }

    // Check if this is sooner than previous
    if (minutesUntil < minutesToNext) {
      minutesToNext = minutesUntil;
      nextAlarm = &alarmsToCheck[i];
    }
  }

  return nextAlarm;
}

// Check for alarms that should trigger
void checkAlarms() {
  // Skip checking if already ringing
  if (bellActive) return;

  DateTime now = rtc.now();

  // Determine which set of alarms to check
  bool isFriday = (now.dayOfTheWeek() == 5);
  Alarm* alarmsToCheck = isFriday ? settings.fridayAlarms : settings.weekdayAlarms;

  // Check all alarms
  for (int i = 0; i < 12; i++) {
    if (!alarmsToCheck[i].enabled) continue;

    // Check if this alarm should trigger
    if (alarmsToCheck[i].hour == now.hour() && alarmsToCheck[i].minute == now.minute() && now.second() == 0) {  // Trigger at the start of the minute

      // Start ringing the bell
      digitalWrite(RELAY_PIN, HIGH);
      bellActive = true;
      bellStartTime = millis();
      bellDuration = alarmsToCheck[i].duration * 1000;  // Convert to ms

      Serial.print("Bell activated - duration: ");
      Serial.println(bellDuration);

      // Only trigger one alarm at a time
      break;
    }
  }
}

// Handle active bell duration
void handleBell() {
  if (bellActive) {
    // Check if bell duration has expired
    if (millis() - bellStartTime >= bellDuration) {
      // Turn off bell
      digitalWrite(RELAY_PIN, LOW);
      bellActive = false;

      Serial.println("Bell deactivated");
    }
  }
}

// Display welcome message
void displayWelcome() {
  dmd.clearScreen();
  dmd.selectFont(SystemFont5x7);

  // Show welcome message
  dmd.drawString(0, 0, "School Bell");
  dmd.drawString(0, 8, "System v1.0");

  delay(2000);
}

// Display error message
void displayError(const char* message) {
  dmd.clearScreen();
  dmd.selectFont(SystemFont5x7);

  // Show error message
  dmd.drawString(0, 0, "ERROR:");
  dmd.drawString(0, 8, message);
}

// Get days in month (accounting for leap years)
uint8_t getDaysInMonth(uint8_t month, uint16_t year) {
  if (month == 2) {
    // Check for leap year
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
      return 29;
    } else {
      return 28;
    }
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  } else {
    return 31;
  }
}
