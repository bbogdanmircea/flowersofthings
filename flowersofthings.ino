
/*__________________________________________________________Libraries__________________________________________________________*/
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "FS.h"
#include "SPIFFS.h"
#include <pins_arduino.h>
#include <ArduinoJson.h>

/*__________________________________________________________General_things__________________________________________________________*/
#define ONE_HOUR 3600000UL
#include "DHT.h"
#define DHTPIN 13     // what digital pin the DHT22 is connected to
#define D1 4 // pin for Pump1 on GPIO4
#define DHTTYPE DHT22   // there are multiple kinds of DHT sensors

const int led = 2;  // LED0 on board is connected to pin D2

// Wifi login
const char *ssid = "TP-LINK_8EFC";
const char *password = "ABCDEFGHIJ";

DHT dht(DHTPIN, DHTTYPE); // DHT sensor declaration
float h = 0;  // variable for air humidity
float t = 0; // variable for air temperature

bool humidity_sensor_fail = false;
int humiditylimit = 750;  // soil humidity threshold - when to water
int waterduration = 10; // how long should one watering period last
int waitingtime = 30; // how long should be waited after one watering
uint32_t lastwater = 0; // saves the timestamp of the last watering

WebServer server(80); // create a web server on port 80

File fsUploadFile; // a File variable to temporarily store the received file

// A name and a password for the OTA service
const char *OTAName = "ESP32";         
const char *OTAPassword = "ESP32";
// Domain name for the mDNS responder
const char* mdnsName = "esp32";

WiFiUDP UDP;// Create an instance of the WiFiUDP class to send and receive UDP messages

// NTP related variables
IPAddress timeServerIP;// The time.nist.gov NTP server's IP address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48;// NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE];// A buffer to hold incoming and outgoing packets

/*__________________________________________________________SPPIF_______________________________________________________*/
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while(file.available()){
        Serial.write(file.read());
    }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("- file written");
    } else {
        Serial.println("- frite failed");
    }
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("- file renamed");
    } else {
        Serial.println("- rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\r\n", path);
    if(fs.remove(path)){
        Serial.println("- file deleted");
    } else {
        Serial.println("- delete failed");
    }
}
/*__________________________________________________________Config_file__________________________________________________________*/
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  } else {
    Serial.printf("Config file size is %d \n", size);
  }

  // Allocate a buffer to store contents of the file.
  uint8_t buf[100];

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.

  //Reading doesn't work
  configFile.read(buf, size);
  for (uint8_t i = 0; i < size; i++) {
    Serial.write(configFile.read());
  }
  Serial.printf("config.json = %s \n", buf);
  StaticJsonDocument<300> doc;
  // use this variable just to check the JSon is working ok
  char json[] =
      /*"{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.756080,2.302038]}";*/
      "{\"humiditylimit\":\"751\",\"waterduration\":\"11\",\"waitingtime\":\"31\",\"lastwater\":\"1\"}";
  // Deserialize the JSON document
  // evetually json needs to be replace with buf
  DeserializationError error = deserializeJson(doc, json);

  // Fetch values.
  //
  // Most of the time, you can rely on the implicit casts.
  // In other case, you can do doc["time"].as<long>();
  const char* sensor = doc["sensor"];
  long time = doc["time"];
  double latitude = doc["data"][0];
  double longitude = doc["data"][1];

  const String hl = doc["humiditylimit"];
  int humiditylimit_tmp = hl.toInt(); // read humididty limit from json
  if (humiditylimit_tmp == 0) {
    Serial.printf("Humidity limit read = %d not valid \n", humiditylimit_tmp);
    Serial.printf("Humidity limit default set to = %d \n", humiditylimit);
  } else {
    Serial.printf("Humidity limit read = %d \n", humiditylimit_tmp);
    humiditylimit = humiditylimit_tmp;
    Serial.printf("Humidity limit default set to = %d \n", humiditylimit);    
  }

 const String wd = doc["waterduration"]; // read waterduration from json
  int waterduration_tmp = wd.toInt();
  if (waterduration_tmp == 0) {
    Serial.printf("Water duration limit read = %d not valid \n", waterduration_tmp);
    Serial.printf("Water duration default set to = %d \n", waterduration);
  } else {
    Serial.printf("Water duration limit read = %d not valid \n", waterduration_tmp);
    waterduration = waterduration_tmp;
    Serial.printf("Water duration default set to = %d \n", waterduration);    
  }

const String wt = doc["waitingtime"]; // read waitingtime from json
  int waitingtime_tmp = wt.toInt();
  if (waitingtime_tmp == 0) {
    Serial.printf("Waiting time limit read = %d not valid \n", waitingtime_tmp);
    Serial.printf("Waiting time default set to = %d \n", waitingtime);
  } else {
    Serial.printf("Waiting time limit read = %d not valid \n", waitingtime_tmp);
    waitingtime = waitingtime_tmp;
    Serial.printf("Waiting time default set to = %d \n", waitingtime);    
  }

const String lw = doc["lastwater"];
  int lastwater_tmp = lw.toInt();
  if (lastwater_tmp == 0) {
    Serial.printf("Last water limit read = %d not valid \n", lastwater_tmp);
    Serial.printf("Last water default set to = %d \n", lastwater);
  } else {
    Serial.printf("Last water limit read = %d not valid \n", lastwater_tmp);
    lastwater = lastwater_tmp;
    Serial.printf("Last water default set to = %d \n", lastwater);    
  }

  // Real world application would store these values in some variables for
  // later use.
Serial.println("Values read from config.json: \n");
  Serial.printf("Humidity limit: %d \n",humiditylimit);
  Serial.printf("Water duration: %d \n",waterduration);
  Serial.printf("Waiting time: %d \n",waitingtime);
  Serial.printf("Last water: %d \n",lastwater);
  return true;
}

/*__________________________________________________________SETUP__________________________________________________________*/

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/

void startWiFi() { // Try to connect to some given access points. Then wait for a connection
/*  wifiMulti.addAP("TP-LINK_8EFC","ABCDEFGHIJ");

  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
    delay(250);
    Serial.print('.');
  }
  Serial.println("\r\n");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());             // Tell us what network we're connected to
  Serial.print("IP address:\t");
  Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
  Serial.println("\r\n");
*/
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void startMDNS() { // Start the mDNS responder
    if (!MDNS.begin("esp32")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");
}

void handleRoot() {
  digitalWrite(led, 1);
  char temp[400];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  snprintf(temp, 400,

           "<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>ESP32 Demo</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP32!</h1>\
    <p>Uptime: %02d:%02d:%02d</p>\
    <img src=\"/test.svg\" />\
  </body>\
</html>",

           hr, min % 60, sec % 60
          );
  server.send(200, "text/html", temp);
  delay(500);
  digitalWrite(led, 0);
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  /*
    server.on("/edit.html",  HTTP_POST, []() {  // If a POST request is sent to the /edit.html address,
      server.send(200, "text/plain", "");
  }, handleFileUpload);                       // go to 'handleFileUpload'
  */
    server.on("/", handleRoot);

    server.on("/water", handleWater);
    server.on("/soil", handleSoil);
    server.on("/hum", handleHum);
    server.on("/temp", handleTemp);
    server.on("/setwaterduration", handleSetWaterDuration);
    server.on("/setwaiting", handleSetWaiting);
    server.on("/sethumidity", handleSetHumidity);

  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
  server.begin();                             // start the HTTP server
  Serial.println("HTTP server started.");
}

void startUDP() {
  Serial.println("Starting UDP");
UDP.beginPacket("192.168.0.255",123);                          // Start listening for UDP 
  Serial.print("Local port:\t");
//  Serial.println(UDP.localPort());
}

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
   listDir(SPIFFS, "/", 0);
    Serial.printf("\n");
  }


//  if (!saveConfig()) {
//    Serial.println("Failed to save config");
//  } else {
//    Serial.println("Config saved");
//  }
  // for some reason /config.json can't be read ????
  readFile(SPIFFS, "/config.json");

  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }

}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {                         // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz";                  // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))                     // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");               // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

unsigned long getTime() { // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (packetBuffer[40] << 24) | (packetBuffer[41] << 16) | (packetBuffer[42] << 8) | packetBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}


void sendNTPpacket(IPAddress& address) {
  Serial.println("Sending NTP request");
  memset(packetBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(packetBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

/*__________________________________________________________flowersofthings_functions__________________________________________________________*/

void water_plants(String plant, int duration) {
  if (plant == "1") {
         //digitalWrite(D2, 1); // switch all valves
         //digitalWrite(D3, 1); // switch all valves
         //delay(250); //wait
         digitalWrite(D1, 1); // turn on pump
         delay(duration*250);
         digitalWrite(D1, 0);
         Serial.println();
         Serial.println("*********************************************************");
         Serial.printf("Plant %s watered for %d seconds \n", plant, duration);
         Serial.println("*********************************************************");
         Serial.println();
      }

if (plant == "2") {
// add valves that have to be switched
// add pump
      }
  }


void handleWater() {

String plant = "";

int duration = 0;

if (server.arg("plant")== ""){     //Parameter not found

Serial.println("Argument not found");

}else{     //Parameter found

plant = server.arg("plant");     //Gets the value of the query parameter

}

if (server.arg("duration")== ""){     //Parameter not found

Serial.println("Argument not found");

}else{     //Parameter found

duration = server.arg("duration").toInt();     //Gets the value of the query parameter

}

water_plants(plant,duration);

Serial.println("manually water plant "+String(plant)+" for "+String(duration*250)+" seconds");

server.send(200, "text/plain", "OK_"+String(plant)+"_"+String(duration));          //Returns the HTTP response

}

void handleSoil() {
Serial.println("Soil");

int humidity = analogRead(A0);

Serial.println("soil humidity requested manually "+String(humidity));

server.send(200, "text/plain", String(humidity));          //Returns the HTTP response

}

void handleTemp() {
Serial.println("Temp");

float t = dht.readTemperature();

Serial.println("temperature requested manually "+String(t));

server.send(200, "text/plain", String(t));          //Returns the HTTP response

}


void handleHum() {

float h = dht.readHumidity();

Serial.println("humidity requested manually "+String(h));

server.send(200, "text/plain", String(h));          //Returns the HTTP response

}

void changeConfig(String key,int var){

   File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
  }


  Serial.println(key+String(var));


  SPIFFS.remove("/config.json");
  File newconfigFile = SPIFFS.open("/config.json", "w");


  }


void handleSetHumidity() {
  if (server.arg("humidity")== ""){     //Parameter not found
    server.send(200, "text/plain", "Argument not found");          //Returns the HTTP response
  } else {     //Parameter found
    humiditylimit = server.arg("humidity").toInt();     //Gets the value of the query parameter
    changeConfig("Humidity limit changed in config to: ",humiditylimit);
    server.send(200, "text/plain", "OK_"+String(humiditylimit));          //Returns the HTTP response
  }
}

void handleSetWaterDuration() {
  if (server.arg("duration")== ""){     //Parameter not found
    server.send(200, "text/plain", "Argument not found");          //Returns the HTTP response
  }else{     //Parameter found
    waterduration = server.arg("duration").toInt();     //Gets the value of the query parameter
    changeConfig("Water duration changed in config to",waterduration);
    server.send(200, "text/plain", "OK_"+String(waterduration));          //Returns the HTTP response
  }
}

void handleSetWaiting() {

if (server.arg("time")== ""){     //Parameter not found

server.send(200, "text/plain", "Argument not found");          //Returns the HTTP response

}else{     //Parameter found

waitingtime = server.arg("time").toInt();     //Gets the value of the query parameter

changeConfig("waitingtime",waitingtime);

server.send(200, "text/plain", "OK_"+String(waitingtime));          //Returns the HTTP response
}

}

void setup() {
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);        // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println("\r\n");

  dht.begin(); // initalize DHT
  pinMode(D1, OUTPUT); // initialize pin for pump

  startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection

//  startOTA();                  // Start the OTA service

  startSPIFFS();               // Start the SPIFFS and list all contents

  startMDNS();                 // Start the mDNS responder

  startServer();               // Start a HTTP server with a file read handler and an upload handler
    
  // Add service to MDNS-SD
  //MDNS.addService("http", "tcp", 80);

//  startUDP();                  // Start listening for UDP messages to port 123

  WiFi.hostByName(ntpServerName, timeServerIP); // Get the IP address of the NTP server
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);
  sendNTPpacket(timeServerIP);
  delay(500);
}


/*__________________________________________________________LOOP__________________________________________________________*/

const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

const unsigned long intervalTemp = 6000;   // Do a temperature measurement every minute
unsigned long prevTemp = 0;
bool tmpRequested = false;
const unsigned long DS_delay = 750;         // Reading the temperature from the DS18x20 can take up to 750ms

uint32_t timeUNIX = 0;                      // The most recent timestamp received from the time server

void loop() {


  unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // Request the time from the time server every hour
    prevNTP = currentMillis;
    sendNTPpacket(timeServerIP);
  }

  uint32_t time = getTime();                   // Check if the time server has responded, if so, get the UNIX time
  if (time) {
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = millis();
  } else if ((millis() - lastNTPResponse) > 24UL * ONE_HOUR) {
    Serial.println("More than 24 hours since last NTP response. Rebooting.");
    Serial.flush();
    ESP.restart();
  }

  if (timeUNIX != 0) {
    if (currentMillis - prevTemp > intervalTemp) {  // Every minute, request the temperature

      //tempSensors.requestTemperatures(); // Request the temperature from the sensor (it takes some time to read it)
      tmpRequested = true;
      prevTemp = currentMillis;
      Serial.println("Temperature requested");
    }
    
    if (currentMillis - prevTemp > DS_delay && tmpRequested) { // 750 ms after requesting the temperature
      uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
      // The actual time is the last NTP time plus the time that has elapsed since the last NTP response
      tmpRequested = false;
      //float temp = tempSensors.getTempCByIndex(0); // Get the temperature from the sensor
      if (humidity_sensor_fail == false) {
        if (currentMillis%60 == 0) {
          Serial.printf("Remaining time until next watering = %d minutes \n", (waitingtime-(actualTime-lastwater))/60);
        }
      } else {
        Serial.printf("Elapsed time= %d\n", (1800-(actualTime-lastwater)));
        if (currentMillis%60 == 0) {
          Serial.printf("Remaining time until next watering = %d minutes \n", (1800-(actualTime-lastwater))/60);
        }        
      }

      h = dht.readHumidity();
      t = dht.readTemperature();
      delay(3000);
      h = dht.readHumidity(); // read twice to avoid nans
      t = dht.readTemperature();

      int humidity =  analogRead(0);
      if (humidity >= 4000) {
        Serial.printf("Humidity read from sensor = %d is not OK \n", humidity);
        Serial.println("Watering will be done every 30 minutes");
        humidity_sensor_fail = true;
      }

      if (humidity_sensor_fail == false && humidity>humiditylimit && (actualTime-lastwater)>waitingtime){
        // change here when using multiple soil humidity sensors with multiple if cases testing each sensor
         water_plants("1",waterduration);
         lastwater=actualTime;
         changeConfig("lastwater",lastwater);
      } else if (humidity_sensor_fail == true && (actualTime-lastwater)> 1800) {
         water_plants("1",waterduration);
         lastwater=actualTime;
         changeConfig("lastwater",lastwater);
         Serial.printf("Remaining time until next watering = %d minutes \n", (1800-(actualTime-lastwater))/60);
      }

      Serial.printf("Appending parameters to file at time: %lu, \n", actualTime);
      Serial.printf("Soil Humidity: %s \n",String(humidity));
      Serial.printf("Air Temperature: %s \n",String(t));
      Serial.printf("Air Humidity: %s \n",String(h));

      File tempLog = SPIFFS.open("/data.csv", "a"); // Write the time and the temperature to the csv file
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.print(t);
      tempLog.print(',');
      tempLog.print(h);
      tempLog.print(',');
      tempLog.println(humidity);
      int filesize = tempLog.size();
      Serial.printf("Size of /data.csv: %s \n",String(filesize));
      tempLog.close();

      if(filesize>500000){
        //remove data if file gets to big, can be done better by deleting just the oldest data
        Serial.println("Deleted data file because it was too big \n");
        SPIFFS.remove("/data.csv");
        }
      if(filesize>480000){ //toggle status LED when it is time to save the data.csv
        digitalWrite(led, !digitalRead(led));
        }      
    } // end reading data from DHT
  } else {                                    // If we didn't receive an NTP response yet, send another request
    sendNTPpacket(timeServerIP);
    delay(500);
  }

  // WiFiClient client = server.available();     // listen for incoming clients
  server.handleClient();
  // ArduinoOTA.handle();                        // listen for OTA events
} // end loop
