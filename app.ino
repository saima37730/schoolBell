/*

Version 2.0
fully functional project with thease featurs
previous features.
wifi setting 
sta mode
ap mode

in this update. v 2.0
max7219 display.

*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// Pin definitions
#define RELAY_PIN D3  // Changed from D1 to avoid conflict with I2C SCL pin
// Define the hardware type, size and output pins for MAX7219
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4  // Number of modules connected (adjust based on your setup)
#define CS_PIN D8      // CS pin for MAX7219
#define DATA_PIN D7    // DATA pin (MOSI)
#define CLK_PIN D5     // CLK pin (SCK)

// RTC setup
RTC_DS3231 rtc;

//Bell Duration
int Duration = 2;
int bellDuration = Duration * 1000;

// Add global variables for duration settings
int shortDuration = 1; // Default 1 second
int longDuration = 3;  // Default 3 seconds

// Create a new instance of the MD_Parola class for scrolling text
MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Variables for time display
char timeBuffer[10];  // Buffer to hold the time string (HH:MM:SS)
char dateBuffer[12];  // Buffer to hold the date string (YYYY-MM-DD)
unsigned long lastDisplayUpdate = 0;
const int displayUpdateInterval = 1000;      // Update display every second
int displayMode = 0;                         // 0 = time, 1 = date, 2 = day of week
const int displayModeChangeInterval = 5000;  // Change display mode every 5 seconds
unsigned long lastModeChange = 0;


// WiFi and server setup
const char* ap_ssid = "SchoolBell_AP";
const char* ap_password = "school1234";
char sta_ssid[32] = "";
char sta_password[32] = "";

ESP8266WebServer server(80);

// First, modify the Alarm structure to include duration type
struct Alarm {
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  uint8_t durationType; // 0 for short, 1 for long
};

// Alarm storage (12 alarms per day, plus separate Friday alarms)
Alarm regularAlarms[12];
Alarm fridayAlarms[12];

// EEPROM addresses
#define EEPROM_SIZE 512
#define EEPROM_INITIALIZED_ADDR 0  // Add this line - dedicated address for initialization flag
#define WIFI_SSID_ADDR 32
#define WIFI_PASS_ADDR 64        // Adjusted to avoid potential overlap
#define REGULAR_ALARMS_ADDR 128  // Adjusted to avoid potential overlap
#define FRIDAY_ALARMS_ADDR 320   // Adjusted to avoid potential overlap
// Add EEPROM addresses for storing duration settings
#define SHORT_DURATION_ADDR 492  // Near the end of EEPROM
#define LONG_DURATION_ADDR 496   // Near the end of EEPROM

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
  // Initialize the MAX7219 LED Matrix
  setupMatrix();

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

  // Update the matrix display
  matrix.displayAnimate();
  updateMatrixDisplay();

  delay(100);
}

void setupWiFi() {
  // First try to connect to stored WiFi network
  if (strlen(sta_ssid) > 0) {
    Serial.print("Found stored WiFi credentials for: ");
    Serial.println(sta_ssid);

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
      // Add a small delay before disconnecting and switching mode
      delay(1000);
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
    }
  } else {
    Serial.println("No stored WiFi credentials found, starting in AP mode only");
    WiFi.mode(WIFI_AP);
  }

  // Always start the AP for configuration
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setupMatrix() {
  // Initialize the MAX7219 display
  matrix.begin();

  // Set the intensity (brightness) of the display (0-15)
  matrix.setIntensity(8);

  // Clear the display
  matrix.displayClear();

  // Set text alignment (center, left, right)
  matrix.setTextAlignment(PA_CENTER);

  // Set scroll speed
  matrix.setSpeed(100);

  // No scrolling effect for time display
  matrix.setPause(1000);
  matrix.setTextEffect(PA_NO_EFFECT, PA_NO_EFFECT);

  Serial.println("MAX7219 Matrix initialized");
}
void updateMatrixDisplay() {
  DateTime now = rtc.now();
  
  if (millis() - lastModeChange > displayModeChangeInterval) {
    displayMode = (displayMode + 1) % 3;  // Cycle through display modes
    lastModeChange = millis();
    matrix.displayClear();
  }
  
  // Only update if it's time to
  if (millis() - lastDisplayUpdate > displayUpdateInterval) {
    lastDisplayUpdate = millis();
    
    switch (displayMode) {
      case 0:  // Time display (HH:MM)
        sprintf(timeBuffer, "%02d:%02d", now.hour(), now.minute());
        matrix.setTextEffect(PA_NO_EFFECT, PA_NO_EFFECT);
        matrix.setPause(1000);
        matrix.displayText(timeBuffer, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
        break;
        
      case 1:  // Date display (MM-DD)
        sprintf(dateBuffer, "%02d-%02d", now.month(), now.day());
        matrix.setTextEffect(PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        matrix.setPause(1000);
        matrix.displayText(dateBuffer, PA_LEFT, 100, 1000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        break;
        
      case 2:  // Day of week display
        const char* dayOfWeek;
        switch (now.dayOfTheWeek()) {
          case 0: dayOfWeek = "SUN"; break;
          case 1: dayOfWeek = "MON"; break;
          case 2: dayOfWeek = "TUE"; break;
          case 3: dayOfWeek = "WED"; break;
          case 4: dayOfWeek = "THU"; break;
          case 5: dayOfWeek = "FRI"; break;
          case 6: dayOfWeek = "SAT"; break;
        }
        matrix.setTextEffect(PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        matrix.setPause(1000);
        matrix.displayText(dayOfWeek, PA_LEFT, 100, 1000, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        break;
    }
  }
}
void handleSetBrightness() {
  // Check if the request contains a body
  if (server.hasArg("plain")) {
    // Get the JSON data from the request body
    String json = server.arg("plain");
    
    // Create a JSON document with size 64 bytes (enough for a small JSON object)
    DynamicJsonDocument doc(64);
    
    // Try to parse the JSON string into the doc object
    DeserializationError error = deserializeJson(doc, json);

    // If there was an error parsing the JSON
    if (error) {
      // Send an error response back to the client
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    // Check if the JSON contains the expected "brightness" key
    if (doc.containsKey("brightness")) {
      // Extract the brightness value from the JSON
      int brightness = doc["brightness"].as<int>();
      
      // Validate that the brightness value is within the allowed range (0-15)
      if (brightness >= 0 && brightness <= 15) {
        // Set the LED matrix brightness
        matrix.setIntensity(brightness);
        
        // Send a success response
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        // Send an error if brightness is out of range
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Brightness must be between 0 and 15\"}");
      }
    } else {
      // Send an error if the brightness key is missing
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Brightness value is required\"}");
    }
  } else {
    // Send an error if no data was received
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
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

  // Add new endpoint for display brightness
  server.on("/api/display/brightness", HTTP_POST, handleSetBrightness);
  // Add new endpoint for duration settings
  server.on("/api/bell/durations", HTTP_POST, handleSetDurations);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

// Add a handler for duration settings
void handleSetDurations() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    DynamicJsonDocument doc(64);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("shortDuration") && doc.containsKey("longDuration")) {
      int shortValue = doc["shortDuration"].as<int>();
      int longValue = doc["longDuration"].as<int>();
      
      // Validate values
      if (shortValue >= 1 && shortValue <= 5 && longValue >= 1 && longValue <= 5) {
        shortDuration = shortValue;
        longDuration = longValue;
        saveDurationSettings();
        server.send(200, "application/json", "{\"success\":true}");
      } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Duration must be between 1 and 5 seconds\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Both shortDuration and longDuration are required\"}");
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

// Modify initializeDefaultAlarms to set default duration type
void initializeDefaultAlarms() {
  // Check if alarms have been initialized before
  bool initialized = EEPROM.read(EEPROM_INITIALIZED_ADDR) == 0xAA;

  if (!initialized) {
    Serial.println("Initializing default alarms...");

    // Set all alarms to defaults (disabled, 8:00 AM, short duration)
    for (int i = 0; i < 12; i++) {
      regularAlarms[i].hour = 8;
      regularAlarms[i].minute = 0;
      regularAlarms[i].enabled = false;
      regularAlarms[i].durationType = 0; // Default to short duration

      fridayAlarms[i].hour = 8;
      fridayAlarms[i].minute = 0;
      fridayAlarms[i].enabled = false;
      fridayAlarms[i].durationType = 0; // Default to short duration
    }

    // Initialize duration settings
    shortDuration = 1; // 1 second default
    longDuration = 3;  // 3 seconds default
    saveDurationSettings();

    // Save to EEPROM
    saveAlarmSettings();

    // Mark as initialized - use the dedicated address
    EEPROM.write(EEPROM_INITIALIZED_ADDR, 0xAA);
    EEPROM.commit();

    Serial.println("Default alarms initialized.");
  }
}

void loadSettings() {
  // Load WiFi settings
  loadWiFiSettings();

  // Load regular alarms
  for (int i = 0; i < 12; i++) {
    regularAlarms[i].hour = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 4));     // Changed from 3 to 4 bytes per alarm
    regularAlarms[i].minute = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 4) + 1);
    regularAlarms[i].enabled = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 4) + 2);
    regularAlarms[i].durationType = EEPROM.read(REGULAR_ALARMS_ADDR + (i * 4) + 3);
  }

  // Load Friday alarms
  for (int i = 0; i < 12; i++) {
    fridayAlarms[i].hour = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 4));      // Changed from 3 to 4 bytes per alarm
    fridayAlarms[i].minute = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 4) + 1);
    fridayAlarms[i].enabled = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 4) + 2);
    fridayAlarms[i].durationType = EEPROM.read(FRIDAY_ALARMS_ADDR + (i * 4) + 3);
  }

  // Load duration settings
  shortDuration = EEPROM.read(SHORT_DURATION_ADDR);
  if (shortDuration < 1 || shortDuration > 5) shortDuration = 1; // Validate and set default if invalid
  
  longDuration = EEPROM.read(LONG_DURATION_ADDR);
  if (longDuration < 1 || longDuration > 5) longDuration = 3; // Validate and set default if invalid
}

void saveWiFiSettings() {
  Serial.println("Saving WiFi settings to EEPROM...");

  // Save WiFi settings byte by byte
  for (int i = 0; i < sizeof(sta_ssid); i++) {
    EEPROM.write(WIFI_SSID_ADDR + i, sta_ssid[i]);
  }

  for (int i = 0; i < sizeof(sta_password); i++) {
    EEPROM.write(WIFI_PASS_ADDR + i, sta_password[i]);
  }

  // Commit changes to EEPROM
  bool success = EEPROM.commit();
  if (success) {
    Serial.println("WiFi settings saved successfully");
  } else {
    Serial.println("Error saving WiFi settings to EEPROM");
  }
}
void loadWiFiSettings() {
  // Clear buffers first
  memset(sta_ssid, 0, sizeof(sta_ssid));
  memset(sta_password, 0, sizeof(sta_password));

  // Load WiFi settings byte by byte
  for (int i = 0; i < sizeof(sta_ssid) - 1; i++) {
    char c = EEPROM.read(WIFI_SSID_ADDR + i);
    // Break at null terminator or if we find a non-printable character (possible corruption)
    if (c == 0 || (c < 32 && c != 0)) break;
    sta_ssid[i] = c;
  }

  for (int i = 0; i < sizeof(sta_password) - 1; i++) {
    char c = EEPROM.read(WIFI_PASS_ADDR + i);
    // Break at null terminator or if we find a non-printable character (possible corruption)
    if (c == 0 || (c < 32 && c != 0)) break;
    sta_password[i] = c;
  }

  // Ensure null termination
  sta_ssid[sizeof(sta_ssid) - 1] = 0;
  sta_password[sizeof(sta_password) - 1] = 0;

  // Print loaded WiFi settings for debugging
  Serial.print("Loaded WiFi SSID from EEPROM: ");
  Serial.println(sta_ssid);
  Serial.println("Password loaded (not displayed for security)");
}
// Modify the saveAlarmSettings function to save duration settings
void saveAlarmSettings() {
  // Save regular alarms
  for (int i = 0; i < 12; i++) {
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 4), regularAlarms[i].hour);
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 4) + 1, regularAlarms[i].minute);
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 4) + 2, regularAlarms[i].enabled);
    EEPROM.write(REGULAR_ALARMS_ADDR + (i * 4) + 3, regularAlarms[i].durationType);
  }

  // Save Friday alarms
  for (int i = 0; i < 12; i++) {
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 4), fridayAlarms[i].hour);
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 4) + 1, fridayAlarms[i].minute);
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 4) + 2, fridayAlarms[i].enabled);
    EEPROM.write(FRIDAY_ALARMS_ADDR + (i * 4) + 3, fridayAlarms[i].durationType);
  }

  EEPROM.commit();
}

// Add a function to save duration settings
void saveDurationSettings() {
  EEPROM.write(SHORT_DURATION_ADDR, shortDuration);
  EEPROM.write(LONG_DURATION_ADDR, longDuration);
  EEPROM.commit();
}

// Modify the checkAlarms function to pass duration type to ringBell
void checkAlarms() {
  DateTime now = rtc.now();

  // Get the current alarm set based on the day of the week
  Alarm* currentAlarms = (now.dayOfTheWeek() == 5) ? fridayAlarms : regularAlarms;

  // Check each alarm
  for (int i = 0; i < 12; i++) {
    if (currentAlarms[i].enabled && currentAlarms[i].hour == now.hour() && currentAlarms[i].minute == now.minute() && now.second() == 0) {
      // Alarm matched! Ring the bell with the specified duration type
      ringBell(currentAlarms[i].durationType);
      break;
    }
  }
}

// Modify the ringBell function to handle different durations
void ringBell(int alarmDurationType) {
  int ringDuration;
  
  // Get the appropriate duration based on type
  if (alarmDurationType == 0) {
    ringDuration = shortDuration * 1000; // Convert to milliseconds
  } else {
    ringDuration = longDuration * 1000;  // Convert to milliseconds
  }
  
  Serial.print("RING BELL for ");
  Serial.print(ringDuration / 1000);
  Serial.println(" seconds!");
  
  digitalWrite(RELAY_PIN, HIGH);
  
  // Display "BELL!" message on the matrix
  matrix.displayClear();
  matrix.setTextEffect(PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  matrix.displayText("BELL!", PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  
  delay(ringDuration);  // Ring for the specified duration
  digitalWrite(RELAY_PIN, LOW);
  
  // Reset the display mode timer to ensure clean transition back to time display
  lastModeChange = millis();
  displayMode = 0;
}
// Update the handleRoot function to change how we display the current SSID

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
    ".alarm-row select{width:100px}"
    ".tabs{display:flex;margin-bottom:20px}"
    ".tab{padding:10px 20px;cursor:pointer;background-color:#ddd;margin-right:5px}"
    ".tab.active{background-color:#4CAF50;color:white}"
    ".tab-content{display:none}"
    ".tab-content.active{display:block}"
    "#status{font-weight:bold;margin-bottom:10px}"
    ".current-setting{font-style:italic;color:#555;margin-bottom:5px}"
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
    "<tr><th>#</th><th>Time</th><th>Duration</th><th>Enabled</th></tr>"
    "</table>"
    "<button onclick=\"saveAlarms('regular')\">Save Regular Alarms</button>"
    "</div>"
    "</div>"
    "<div id=\"friday\" class=\"tab-content\">"
    "<div class=\"container\">"
    "<h2>Friday Alarms</h2>"
    "<table id=\"fridayAlarms\">"
    "<tr><th>#</th><th>Time</th><th>Duration</th><th>Enabled</th></tr>"
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
    "<div class=\"form-group\"><label>Current WiFi Network:</label><div id=\"currentSSID\" class=\"current-setting\">None</div></div>"
    "<div class=\"form-group\"><label for=\"ssid\">New SSID:</label><input type=\"text\" id=\"ssid\" placeholder=\"Enter new network name\"></div>"
    "<div class=\"form-group\"><label for=\"password\">New Password:</label><input type=\"password\" id=\"password\" placeholder=\"Enter new WiFi password\"></div>"
    "<button onclick=\"setWiFi()\">Save WiFi Settings</button>"
    "</div>"
    "<div class=\"container\">"
    "<h2>Bell Duration Settings</h2>"
    "<div class=\"form-group\"><label for=\"shortDuration\">Short Duration (seconds):</label>"
    "<select id=\"shortDuration\">"
    "<option value=\"1\">1</option><option value=\"2\">2</option><option value=\"3\">3</option>"
    "<option value=\"4\">4</option><option value=\"5\">5</option>"
    "</select></div>"
    "<div class=\"form-group\"><label for=\"longDuration\">Long Duration (seconds):</label>"
    "<select id=\"longDuration\">"
    "<option value=\"1\">1</option><option value=\"2\">2</option><option value=\"3\">3</option>"
    "<option value=\"4\">4</option><option value=\"5\">5</option>"
    "</select></div>"
    "<button onclick=\"setDurations()\">Save Duration Settings</button>"
    "</div>"
    "<div class=\"container\">"
    "<h2>Test Bell</h2>"
    "<button onclick=\"testBell()\">Ring Bell (Short Duration)</button>"
    "</div>"
    "<div class=\"container\">"
    "<h2>Display Settings</h2>"
    "<div class=\"form-group\"><label for=\"brightness\">Display Brightness (0-15):</label><input type=\"range\" id=\"brightness\" min=\"0\" max=\"15\" value=\"8\"></div>"
    "<button onclick=\"setBrightness()\">Set Brightness</button>"
    "</div>"
    "</div>";

  String script =
    "<script>"
    "document.addEventListener('DOMContentLoaded',function(){refreshStatus();loadAlarms('regular');loadAlarms('friday');setInterval(refreshStatus,10000)});"
    "function openTab(n){var t=document.getElementsByClassName('tab-content');for(var i=0;i<t.length;i++)t[i].classList.remove('active');var a=document.getElementsByClassName('tab');for(var i=0;i<a.length;i++)a[i].classList.remove('active');document.getElementById(n).classList.add('active');for(var i=0;i<a.length;i++)if(a[i].textContent.toLowerCase().includes(n)){a[i].classList.add('active');break}}"
    "function refreshStatus(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){document.getElementById('status').textContent='Current Time: '+d.date+' '+d.time+' ('+d.day+') - IP: '+d.ip;document.getElementById('currentTime').textContent=d.date+' '+d.time;if(d.ssid){document.getElementById('currentSSID').textContent=d.ssid;}else{document.getElementById('currentSSID').textContent='Not connected';}document.getElementById('shortDuration').value=d.shortDuration;document.getElementById('longDuration').value=d.longDuration;}).catch(function(e){console.error('Error:',e)})}"
    "function loadAlarms(t){fetch('/api/alarms/'+t).then(function(r){return r.json()}).then(function(d){var e=document.getElementById(t+'Alarms');while(e.rows.length>1)e.deleteRow(1);for(var i=0;i<d.length;i++){var r=e.insertRow(-1);r.className='alarm-row';var c1=r.insertCell(0);c1.textContent=i+1;var c2=r.insertCell(1);var h=document.createElement('input');h.type='time';var hr=d[i].hour.toString();if(hr.length<2)hr='0'+hr;var mn=d[i].minute.toString();if(mn.length<2)mn='0'+mn;h.value=hr+':'+mn;h.className='time-input';h.style.width='120px';c2.appendChild(h);var c3=r.insertCell(2);var s=document.createElement('select');s.innerHTML='<option value=\"0\">Short</option><option value=\"1\">Long</option>';s.value=d[i].durationType;c3.appendChild(s);var c4=r.insertCell(3);var b=document.createElement('input');b.type='checkbox';b.checked=d[i].enabled;c4.appendChild(b)}}).catch(function(e){console.error('Error:',e)})}"
    "function saveAlarms(t){var e=document.getElementById(t+'Alarms');var a=[];for(var i=1;i<e.rows.length;i++){var r=e.rows[i];var h=r.cells[1].querySelector('input').value;var d=parseInt(r.cells[2].querySelector('select').value);var c=r.cells[3].querySelector('input').checked;a.push({hour:parseInt(h.split(':')[0]),minute:parseInt(h.split(':')[1]),durationType:d,enabled:c})}fetch('/api/alarms/'+t,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(a)}).then(function(r){return r.json()}).then(function(d){if(d.success)alert((t.charAt(0).toUpperCase()+t.slice(1))+' alarms saved');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setDateTime(){var d=document.getElementById('date').value;var t=document.getElementById('time').value;if(!d||!t){alert('Please enter both date and time');return}fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({date:d,time:t})}).then(function(r){return r.json()}).then(function(d){if(d.success){alert('Date and time set successfully');refreshStatus()}else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setWiFi(){var s=document.getElementById('ssid').value;var p=document.getElementById('password').value;if(!s){alert('Please enter SSID');return}fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,password:p})}).then(function(r){return r.json()}).then(function(d){if(d.success){alert('WiFi settings saved successfully!');document.getElementById('ssid').value='';document.getElementById('password').value='';refreshStatus();}else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setDurations(){var s=parseInt(document.getElementById('shortDuration').value);var l=parseInt(document.getElementById('longDuration').value);fetch('/api/bell/durations',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({shortDuration:s,longDuration:l})}).then(function(r){return r.json()}).then(function(d){if(d.success)alert('Duration settings saved successfully!');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function testBell(){fetch('/api/bell/test',{method:'POST'}).then(function(r){return r.json()}).then(function(d){if(d.success)alert('Bell test initiated!');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "function setBrightness(){var b=parseInt(document.getElementById('brightness').value);fetch('/api/display/brightness',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:b})}).then(function(r){return r.json()}).then(function(d){if(d.success)alert('Display brightness updated');else alert('Error: '+d.message)}).catch(function(e){console.error('Error:',e)})}"
    "</script>"
    "</body>"
    "</html>";

  server.send(200, "text/html", html + script);
}

// Update the handleStatus function to include duration settings
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
  doc["shortDuration"] = shortDuration;
  doc["longDuration"] = longDuration;

  // Encode the SSID for proper display in the browser
  char encodedSsid[64];
  memset(encodedSsid, 0, sizeof(encodedSsid));
  strncpy(encodedSsid, sta_ssid, sizeof(encodedSsid) - 1);
  doc["ssid"] = encodedSsid;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Modify the JSON response for alarms to include duration type
void handleGetRegularAlarms() {
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < 12; i++) {
    JsonObject alarmObj = array.createNestedObject();
    alarmObj["hour"] = regularAlarms[i].hour;
    alarmObj["minute"] = regularAlarms[i].minute;
    alarmObj["enabled"] = regularAlarms[i].enabled;
    alarmObj["durationType"] = regularAlarms[i].durationType;
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
    alarmObj["durationType"] = fridayAlarms[i].durationType;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}
// Update the handleSetRegularAlarms and handleSetFridayAlarms functions
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
      
      // Add durationType (with default if not provided)
      if (alarmObj.containsKey("durationType")) {
        regularAlarms[i].durationType = alarmObj["durationType"];
      } else {
        regularAlarms[i].durationType = 0; // Default to short
      }
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
      
      // Add durationType (with default if not provided)
      if (alarmObj.containsKey("durationType")) {
        fridayAlarms[i].durationType = alarmObj["durationType"];
      } else {
        fridayAlarms[i].durationType = 0; // Default to short
      }
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

// Update handleTestBell to use the short duration by default
void handleTestBell() {
  ringBell(0); // Use short duration for test
  server.send(200, "application/json", "{\"success\":true}");
}
