#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// Pin definitions
#define RELAY_PIN D3  // Changed from D1 to avoid conflict with I2C SCL pin

// RTC setup
RTC_DS3231 rtc;

//Bell Duration
int Duration = 2;
int bellDuration = Duration * 1000;

// WiFi and server setup
const char* ap_ssid = "SchoolBell_AP";
const char* ap_password = "school1234";
char sta_ssid[32] = "";
char sta_password[32] = "";

ESP8266WebServer server(80);

// Alarms structure
struct Alarm {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
};

// Alarm storage (12 alarms per day, plus separate Friday alarms)
Alarm regularAlarms[12];
Alarm fridayAlarms[12];

// EEPROM addresses
#define EEPROM_SIZE 512
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 32
#define REGULAR_ALARMS_ADDR 64
#define FRIDAY_ALARMS_ADDR 256

void setup() {
  Serial.begin(115200);

  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Initialize RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1)
      ;
  }

  // Set RTC to system time if RTC lost power
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting default time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize default alarms if needed
  initializeDefaultAlarms();

  // Load settings from EEPROM
  loadSettings();

  // Setup WiFi
  setupWiFi();

  // Setup web server
  setupWebServer();

  Serial.println("School Bell System is ready!");
}
void loop() {
  server.handleClient();
  checkAlarms();
  delay(100);
}

void setupWiFi() {
  // First try to connect to stored WiFi network
  if (strlen(sta_ssid) > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(sta_ssid, sta_password);

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("Connected to: ");
      Serial.println(sta_ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("");
      Serial.println("Failed to connect to stored network, falling back to AP mode only");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
    }
  } else {
    WiFi.mode(WIFI_AP);
  }

  // Always start the AP for configuration
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}


void setupWebServer() {
  // Main page
  server.on("/", HTTP_GET, handleRoot);

  // API endpoints
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/alarms/regular", HTTP_GET, handleGetRegularAlarms);
  server.on("/api/alarms/friday", HTTP_GET, handleGetFridayAlarms);
  server.on("/api/alarms/regular", HTTP_POST, handleSetRegularAlarms);
  server.on("/api/alarms/friday", HTTP_POST, handleSetFridayAlarms);
  server.on("/api/wifi", HTTP_POST, handleSetWiFi);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/bell/test", HTTP_POST, handleTestBell);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}
// Initialize alarm data with defaults if not already initialized
void initializeDefaultAlarms() {
  // Check if alarms have been initialized before
  bool initialized = EEPROM.read(0) == 0xAA;

  if (!initialized) {
    Serial.println("Initializing default alarms...");

    // Set all alarms to defaults (disabled, 8:00 AM)
    for (int i = 0; i < 12; i++) {
      regularAlarms[i].hour = 8;
      regularAlarms[i].minute = 0;
      regularAlarms[i].enabled = false;

      fridayAlarms[i].hour = 8;
      fridayAlarms[i].minute = 0;
      fridayAlarms[i].enabled = false;
    }

    // Save to EEPROM
    saveAlarmSettings();

    // Mark as initialized
    EEPROM.write(0, 0xAA);
    EEPROM.commit();

    Serial.println("Default alarms initialized.");
  }
}
void loadSettings() {
  // Load WiFi settings
  loadWiFiSettings();

  // Load regular alarms
  for (int i = 0; i < 12; i++) {
    regularAlarms[i].hour = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 3));
    regularAlarms[i].minute = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 3) + 1);
    regularAlarms[i].enabled = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 3) + 2);
  }

  // Load Friday alarms
  for (int i = 0; i < 12; i++) {
    fridayAlarms[i].hour = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 3));
    fridayAlarms[i].minute = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 3) + 1);
    fridayAlarms[i].enabled = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 3) + 2);
  }
}

void saveWiFiSettings() {
  // Save WiFi settings byte by byte
  for (int i = 0; i < sizeof(sta_ssid); i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, sta_ssid[i]);
  }

  for (int i = 0; i < sizeof(sta_password); i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, sta_password[i]);
  }

  EEPROM.commit();
}
void loadWiFiSettings() {
  // Clear buffers first
  memset(sta_ssid, 0, sizeof(sta_ssid));
  memset(sta_password, 0, sizeof(sta_password));

  // Load WiFi settings byte by byte
  for (int i = 0; i < sizeof(sta_ssid) - 1; i++) {
    sta_ssid[i] = EEPROM.read(WIFI_SSID_ADDR + i);
    // Break at null terminator
    if (sta_ssid[i] == 0) break;
  }

  for (int i = 0; i < sizeof(sta_password) - 1; i++) {
    sta_password[i] = EEPROM.read(WIFI_PASS_ADDR + i);
    // Break at null terminator
    if (sta_password[i] == 0) break;
  }

  // Ensure null termination
  sta_ssid[sizeof(sta_ssid) - 1] = 0;
  sta_password[sizeof(sta_password) - 1] = 0;
}

void saveAlarmSettings() {
  // Save regular alarms
  for (int i = 0; i < 12; i++) {
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 3), regularAlarms[i].hour);
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 3) + 1, regularAlarms[i].minute);
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 3) + 2, regularAlarms[i].enabled);
  }

  // Save Friday alarms
  for (int i = 0; i < 12; i++) {
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 3), fridayAlarms[i].hour);
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 3) + 1, fridayAlarms[i].minute);
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 3) + 2, fridayAlarms[i].enabled);
  }

  EEPROM.commit();
}

void checkAlarms() {
  DateTime now = rtc.now();

  // Get the current alarm set based on the day of the week
  Alarm* currentAlarms = (now.dayOfTheWeek() == 5) ? fridayAlarms : regularAlarms;

  // Check each alarm
  for (int i = 0; i < 12; i++) {
    if (currentAlarms[i].enabled && currentAlarms[i].hour == now.hour() && currentAlarms[i].minute == now.minute() && now.second() == 0) {
      // Alarm matched! Ring the bell
      ringBell();
      break;
    }
  }
}

void ringBell() {
  Serial.println("RING BELL!");
  digitalWrite(RELAY_PIN, HIGH);
  delay(bellDuration);  // Ring for 3 seconds
  digitalWrite(RELAY_PIN, LOW);
}

void handleRoot() {
  String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>School Bell System</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>"
    "body{font-family:Arial,sans-serif;max-width:800px;margin:0 auto;padding:20px}"
    ".container{background-color:#f9f9f9;padding:20px;border-radius:5px;margin-bottom:20px}"
    ".form-group{margin-bottom:15px}"
    "label{display:block;margin-bottom:5px;font-weight:bold}"
    "input,select{padding:8px;width:100%;box-sizing:border-box}"
    "button{padding:10px 15px;background-color:#4CAF50;color:white;border:none;cursor:pointer}"
    "button:hover{background-color:#45a049}"
    "table{width:100%;border-collapse:collapse}"
    "th,td{border:1px solid #ddd;padding:8px;text-align:left}"
    "th{background-color:#f2f2f2}"
    ".alarm-row input[type=\"checkbox\"]{width:auto}"
    ".alarm-row input[type=\"time\"]{width:120px}"
    ".tabs{display:flex;margin-bottom:20px}"
    ".tab{padding:10px 20px;cursor:pointer;background-color:#ddd;margin-right:5px}"
    ".tab.active{background-color:#4CAF50;color:white}"
    ".tab-content{display:none}"
    ".tab-content.active{display:block}"
    "#status{font-weight:bold;margin-bottom:10px}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>School Bell System</h1>"
    "<div id=\"status\"></div>"
    "<div class=\"tabs\">"
    "<div class=\"tab active\" onclick=\"openTab('regular')\">Regular Alarms</div>"
    "<div class=\"tab\" onclick=\"openTab('friday')\">Friday Alarms</div>"
    "<div class=\"tab\" onclick=\"openTab('settings')\">Settings</div>"
    "</div>"
    "<div id=\"regular\" class=\"tab-content active\">"
    "<div class=\"container\">"
    "<h2>Regular Alarms (Monday-Thursday)</h2>"
    "<table id=\"regularAlarms\">"
    "<tr><th>#</th><th>Time</th><th style=\"display:none\"></th><th></th><th>Enabled</th></tr>"
    "</table>"
    "<button onclick=\"saveAlarms('regular')\">Save Regular Alarms</button>"
    "</div>"
    "</div>"
    "<div id=\"friday\" class=\"tab-content\">"
    "<div class=\"container\">"
    "<h2>Friday Alarms</h2>"
    "<table id=\"fridayAlarms\">"
    "<tr><th>#</th><th>Time</th><th style=\"display:none\"></th><th></th><th>Enabled</th></tr>"
    "</table>"
    "<button onclick=\"saveAlarms('friday')\">Save Friday Alarms</button>"
    "</div>"
    "</div>"
    "<div id=\"settings\" class=\"tab-content\">"
    "<div class=\"container\">"
    "<h2>Date and Time</h2>"
    "<div class=\"form-group\"><label>Current Date/Time:</label><div id=\"currentTime\"></div></div>"
    "<div class=\"form-group\"><label for=\"date\">Set Date:</label><input type=\"date\" id=\"date\"></div>"
    "<div class=\"form-group\"><label for=\"time\">Set Time:</label><input type=\"time\" id=\"time\"></div>"
    "<button onclick=\"setDateTime()\">Set Date/Time</button>"
    "</div>"
    "<div class=\"container\">"
    "<h2>WiFi Settings</h2>"
    "<div class=\"form-group\"><label for=\"ssid\">SSID:</label><input type=\"text\" id=\"ssid\" placeholder=\"Network name\"></div>"
    "<div class=\"form-group\"><label for=\"password\">Password:</label><input type=\"password\" id=\"password\" placeholder=\"WiFi password\"></div>"
    "<button onclick=\"setWiFi()\">Save WiFi Settings</button>"
    "</div>"
    "<div class=\"container\">"
    "<h2>Test Bell</h2>"
    "<button onclick=\"testBell()\">Ring Bell (3 seconds)</button>"
    "</div>"
    "</div>";

  String script =
    "<script>"
    "document.addEventListener('DOMContentLoaded',function(){refreshStatus();loadAlarms('regular');loadAlarms('friday');setInterval(refreshStatus,10000)});"
    "function openTab(n){var t=document.getElementsByClassName('tab-content');for(var i=0;i<t.length;i++)t[i].classList.remove('active');var a=document.getElementsByClassName('tab');for(var i=0;i<a.length;i++)a[i].classList.remove('active');document.getElementById(n).classList.add('active');for(var i=0;i<a.length;i++)if(a[i].textContent.toLowerCase().includes(n)){a[i].classList.add('active');break}}"
    "function refreshStatus(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){document.getElementById('status').textContent='Current Time: '+d.date+' '+d.time+' ('+d.day+') - IP: '+d.ip;document.getElementById('currentTime').textContent=d.date+' '+d.time;if(d.ssid)document.getElementById('ssid').value=d.ssid}).catch(function(e){console.error('Error:',e)})}"
    "function loadAlarms(t){fetch('/api/alarms/'+t).then(function(r){return r.json()}).then(function(d){var e=document.getElementById(t+'Alarms');while(e.rows.length>1)e.deleteRow(1);for(var i=0;i<d.length;i++){var r=e.insertRow(-1);r.className='alarm-row';var c1=r.insertCell(0);c1.textContent=i+1;var c2=r.insertCell(1);var c3=r.insertCell(2);var h=document.createElement('input');h.type='time';var hr=d[i].hour.toString();if(hr.length<2)hr='0'+hr;var mn=d[i].minute.toString();if(mn.length<2)mn='0'+mn;h.value=hr+':'+mn;h.className='time-input';h.style.width='120px';c2.appendChild(h);c2.colSpan=2;c3.style.display='none';var c4=r.insertCell(3);var b=document.createElement('input');b.type='checkbox';b.checked=d[i].enabled;c4.appendChild(b)}}).catch(function(e){console.error('Error:',e)})}"
    "function saveAlarms(t){var e=document.getElementById(t+'Alarms');var a=[];for(var i=1;i<e.rows.length;i++){var r=e.rows[i];var h=r.cells[1].querySelector('input').value;var c=r.cells[3].querySelector('input').checked;a.push({hour:parseInt(h.split(':')[0]),minute:parseInt(h.split(':')[1]),enabled:c})}fetch('/api/alarms/'+t,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(a)}).then(function(r){return r.json()}).then(function(d){if(d.success)alert((t.charAt(0).toUpperCase()+t.slice(1))+' alarms saved');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setDateTime(){var d=document.getElementById('date').value;var t=document.getElementById('time').value;if(!d||!t){alert('Please enter both date and time');return}fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({date:d,time:t})}).then(function(r){return r.json()}).then(function(d){if(d.success){alert('Date and time set successfully');refreshStatus()}else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setWiFi(){var s=document.getElementById('ssid').value;var p=document.getElementById('password').value;if(!s){alert('Please enter SSID');return}fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,password:p})}).then(function(r){return r.json()}).then(function(d){if(d.success)alert('WiFi settings saved successfully!');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function testBell(){fetch('/api/bell/test',{method:'POST'}).then(function(r){return r.json()}).then(function(d){if(d.success)alert('Bell test initiated!');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "</script>"
    "</body>"
    "</html>";

  server.send(200, "text/html", html + script);
}
void handleStatus() {
  DateTime now = rtc.now();

  String day;
  switch (now.dayOfTheWeek()) {
    case 0: day = "Sunday"; break;
    case 1: day = "Monday"; break;
    case 2: day = "Tuesday"; break;
    case 3: day = "Wednesday"; break;
    case 4: day = "Thursday"; break;
    case 5: day = "Friday"; break;
    case 6: day = "Saturday"; break;
  }

  String dateStr = String(now.year()) + "-" + String(now.month() < 10 ? "0" : "") + String(now.month()) + "-" + String(now.day() < 10 ? "0" : "") + String(now.day());

  String timeStr = String(now.hour() < 10 ? "0" : "") + String(now.hour()) + ":" + String(now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" + String(now.second() < 10 ? "0" : "") + String(now.second());

  String ipAddress = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  DynamicJsonDocument doc(256);
  doc["date"] = dateStr;
  doc["time"] = timeStr;
  doc["day"] = day;
  doc["ip"] = ipAddress;

  // Encode the SSID for proper display in the browser
  char encodedSsid[64];
  memset(encodedSsid, 0, sizeof(encodedSsid));
  strncpy(encodedSsid, sta_ssid, sizeof(encodedSsid) - 1);
  doc["ssid"] = encodedSsid;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetRegularAlarms() {
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < 12; i++) {
    JsonObject alarmObj = array.createNestedObject();
    alarmObj["hour"] = regularAlarms[i].hour;
    alarmObj["minute"] = regularAlarms[i].minute;
    alarmObj["enabled"] = regularAlarms[i].enabled;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetFridayAlarms() {
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < 12; i++) {
    JsonObject alarmObj = array.createNestedObject();
    alarmObj["hour"] = fridayAlarms[i].hour;
    alarmObj["minute"] = fridayAlarms[i].minute;
    alarmObj["enabled"] = fridayAlarms[i].enabled;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSetRegularAlarms() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    JsonArray array = doc.as<JsonArray>();
    if (array.size() != 12) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Exactly 12 alarms required\"}");
      return;
    }

    for (int i = 0; i < 12; i++) {
      JsonObject alarmObj = array[i];
      regularAlarms[i].hour = alarmObj["hour"];
      regularAlarms[i].minute = alarmObj["minute"];
      regularAlarms[i].enabled = alarmObj["enabled"];
    }

    saveAlarmSettings();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void handleSetFridayAlarms() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    JsonArray array = doc.as<JsonArray>();
    if (array.size() != 12) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Exactly 12 alarms required\"}");
      return;
    }

    for (int i = 0; i < 12; i++) {
      JsonObject alarmObj = array[i];
      fridayAlarms[i].hour = alarmObj["hour"];
      fridayAlarms[i].minute = alarmObj["minute"];
      fridayAlarms[i].enabled = alarmObj["enabled"];
    }

    saveAlarmSettings();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void handleSetWiFi() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("ssid")) {
      String ssid = doc["ssid"].as<String>();
      String password = doc.containsKey("password") ? doc["password"].as<String>() : "";

      // Clear previous SSID and password
      memset(sta_ssid, 0, sizeof(sta_ssid));
      memset(sta_password, 0, sizeof(sta_password));

      // Copy new values with explicit length limitation
      ssid.toCharArray(sta_ssid, sizeof(sta_ssid) - 1);
      password.toCharArray(sta_password, sizeof(sta_password) - 1);

      saveWiFiSettings();

      // Try to connect to the new network
      WiFi.disconnect();
      WiFi.begin(sta_ssid, sta_password);

      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID is required\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void handleSetTime() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("date") && doc.containsKey("time")) {
      String dateStr = doc["date"];
      String timeStr = doc["time"];

      // Parse date (format: YYYY-MM-DD)
      int year = dateStr.substring(0, 4).toInt();
      int month = dateStr.substring(5, 7).toInt();
      int day = dateStr.substring(8, 10).toInt();

      // Parse time (format: HH:MM)
      int hour = timeStr.substring(0, 2).toInt();
      int minute = timeStr.substring(3, 5).toInt();

      if (year >= 2020 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
        // Set RTC time
        rtc.adjust(DateTime(year, month, day, hour, minute, 0));
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid date or time values\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Date and time are required\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void handleTestBell() {
  ringBell();
  server.send(200, "application/json", "{\"success\":true}");
}
