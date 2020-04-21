/*
 * ESP8266 ThingSpeak WiFi 1-wire thermometer app
 * By Jan Grmela <grmela@gmail.com> in 2018-2020
 * 
 * Tested successfully on a cheap Chinese WEMOS D1 mini copy
 * connected to Dallas DS18B20 this way:
 * - http://www.esp8266learning.com/wemos-mini-ds18b20-temperature-sensor-example.php
 * - https://diyprojects.io/temperature-measurement-ds18b20-arduino-code-compatible-esp8266-esp32-publication-domoticz-http/
 * 
 * - Reads the temperature from all connected 1-wire thermometers
 * - Sends the temperatures to a ThingSpeak channel
 * - Using the offset setting may also kinda join multiple D1s to one
 *   channel (works best when the submission frequency is low or you
 *   have some better than free subscription)
 * 
 * Features:
 * - WiFi SSID failover
 * - Some kind of "if the sensor is fucked, try to make it running again"
 * - Parasitic power compatible
 * - Celsius temperature output
 * 
 * Tested on Arduion 1.8.12 on 4/2020
 */

/*
 *  DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004
 
Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>

Everyone is permitted to copy and distribute verbatim or modified
copies of this license document, and changing it is allowed as long
as the name is changed.
 
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ThingSpeak.h>

/* SETTINGS GO BELOW */

// ThingSpeak settings
#define CHANNEL_NUMBER 123456
const char * myWriteAPIKey = "YOUR_TS_API_KEY";
#define CHANNEL_OFFSET 0 // wanna put more D1s in one channel? Increase this by connect sensors count

// primary & secondary WiFi credentials
const char* ssid = "first_wifi";
const char* password = "first_password";
const char* ssid_backup = "backup_wifi";
const char* password_backup = "backup_password";
#define FAILOVER_TRIES 30 // how many times try to connect to primary WiFi SSID before switching to backup

/* SETTINGS END */


/* LED blink status - blink counts */
#define STATUS_OK 1
#define STATUS_THERMO_DISCONNECTED 2
#define STATUS_WIFI_CONNECT_FAILED 3
#define STATUS_WIFI_LOST 4
#define STATUS_WIFI_OTHER_ERROR 5
#define STATUS_TS_ERROR 6

#define ONE_WIRE_BUS D3 // temperature sensor pin
#define ONE_WIRE_MAX_DEV 4 // the maximum number of devices
#define INTERVAL 5000L // LED indicator blink interval
#define STATUS_LED D4 // LED pin
#define DURATION_TEMP 120 // temperature measurement frequency
#define TEMPERATURE_PRECISION_PARASITE 9 // set lower measurement precision for parasitic-powered devices

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int numberOfDevices; // number of temperature devices found
DeviceAddress devAddr[ONE_WIRE_MAX_DEV];  // an array device temperature sensors
float tempDev[ONE_WIRE_MAX_DEV]; // saving the last measurement of temperature
float tempDevLast[ONE_WIRE_MAX_DEV]; // previous temperature measurement
long lastTemp; // the last measurement
WiFiClient client;

String GetAddressToString(DeviceAddress deviceAddress){
  String str = "";
  for (uint8_t i = 0; i < 8; i++){
    if(deviceAddress[i] < 16) str += String(0, HEX);
    str += String(deviceAddress[i], HEX);
  }
  return str;
}

void SetupDS18B20(){
  DS18B20.begin();

  Serial.print("Parasite power is: "); 
  if( DS18B20.isParasitePowerMode() ){ 
    Serial.println("ON");
  }else{
    Serial.println("OFF");
  }
  
  numberOfDevices = DS18B20.getDeviceCount();
  Serial.print("Device count: ");
  Serial.println(numberOfDevices);

  lastTemp = millis();
  DS18B20.requestTemperatures();

  // Loop through each device, print out address
  for(int i=0; i<numberOfDevices; i++){
    // Search the wire for address
    if(DS18B20.getAddress(devAddr[i], i)){
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: " + GetAddressToString(devAddr[i]));
      Serial.println();
    }else{
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }

    //Get resolution of DS18b20
    Serial.print("Resolution: ");
    Serial.print(DS18B20.getResolution(devAddr[i]));
    Serial.println();

    //Read temperature from DS18b20
    DS18B20.requestTemperatures();
    
    float tempC = DS18B20.getTempC(devAddr[i]);
    Serial.print("Temp C: ");
    Serial.println(tempC);
  }
}

void TempLoop(long now){
  if(now - lastTemp > DURATION_TEMP){ // take a measurement at a fixed time
    DS18B20.requestTemperatures();
    
    for(int i=0; i<numberOfDevices; i++){
      if(DS18B20.isParasitePowerMode()) {
        DS18B20.setWaitForConversion(true);

        DS18B20.setResolution(devAddr[i], TEMPERATURE_PRECISION_PARASITE);

        int numTries = 0;
        while(!DS18B20.isConversionComplete() && numTries++ < 10) {
          delay(100);
        }
      }
      else {
        DS18B20.setWaitForConversion(false); // no waiting for measurement
      }
      
      
      float tempC = DS18B20.getTempC(devAddr[i]); // measuring temperature in Celsius

      if(tempC == DEVICE_DISCONNECTED_C) {
         Serial.println("device disconnected");
      }
      
      tempDev[i] = tempC; // save the measured value to the array

      ThingSpeak.setField(i+1+CHANNEL_OFFSET, tempC);
    }
    ThingSpeak.writeFields(CHANNEL_NUMBER, myWriteAPIKey);
    
    DS18B20.requestTemperatures(); // initiate the temperature measurement
    lastTemp = millis();  // remember the last time measurement
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");

  // wait for WiFi connection
  int triesDone = 0;
  bool triedFailover = false;
  Serial.print("Connecting to primary ssid ");
  Serial.print(ssid);
  Serial.print("...");
  while (WiFi.status() != WL_CONNECTED && triesDone <= FAILOVER_TRIES) {
    delay(500);
    Serial.print(".");
    triesDone++;
  }
  
  if(triesDone >= FAILOVER_TRIES) {
    Serial.println("");
    Serial.print("Primary ssid connection failed, switching to backup ssid ");
    Serial.print(ssid_backup);
    Serial.print("...");
    WiFi.disconnect();
    WiFi.begin(ssid_backup, password_backup);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  SetupDS18B20();

  ThingSpeak.begin(client);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH); // preset the status LED to OFF
}

int blink() {
  static unsigned long lastFlip = 0;
  static int count = 0;
  bool last = false;

  unsigned long now = millis();

  if(now - lastFlip >= 200) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); // internal ESP LED
    lastFlip = now;
    count++;
    if (count == 2) {
      count = 0;
      last = true;
      }
   }

   return (last) ? 1 : 0 ;
}

void loop() {
  unsigned long t = millis();
  static unsigned long lastBlink = 0;
  static int count = 0;
  int blinkTimes = 0;

  TempLoop(t);

  // diagnostic blinking statuses
  if(DS18B20.getDeviceCount() == 0) {
    blinkTimes = STATUS_THERMO_DISCONNECTED;
  }
  else if(WiFi.status() == WL_CONNECT_FAILED) {
    blinkTimes = STATUS_WIFI_CONNECT_FAILED;
  }
  else if(WiFi.status() == WL_CONNECTION_LOST || WiFi.status() == WL_DISCONNECTED) {
    blinkTimes = STATUS_WIFI_LOST;
  }
  else if(WiFi.status() != WL_CONNECTED) {
    blinkTimes = STATUS_WIFI_OTHER_ERROR;
  }
  else if(ThingSpeak.getLastReadStatus() != 200) {
    blinkTimes = STATUS_TS_ERROR;
  }
  else {
    blinkTimes = STATUS_OK;
  }

  if(count > 0) {
    count -= blink();
    lastBlink = t;
  }
  else {
    if(t - lastBlink >= INTERVAL) {
      count = blinkTimes;
    }
  }
}
