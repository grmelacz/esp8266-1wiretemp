# esp8266-1wiretemp
 ESP8266 ThingSpeak WiFi 1-wire thermometer app
By Jan Grmela <grmela@gmail.com> in 2018-2020
 
Tested successfully on a cheap Chinese WEMOS D1 mini copy
connected to Dallas DS18B20 this way:
- http://www.esp8266learning.com/wemos-mini-ds18b20-temperature-sensor-example.php
- https://diyprojects.io/temperature-measurement-ds18b20-arduino-code-compatible-esp8266-esp32-publication-domoticz-http/
 
- Reads the temperature from all connected 1-wire thermometers
- Sends the temperatures to a ThingSpeak channel
- Using the offset setting may also kinda join multiple D1s to one
  channel (works best when the submission frequency is low or you
  have some better than free subscription)
 
Features:
- WiFi SSID failover
- Some kind of "if the sensor is fucked, try to make it running again"
- Parasitic power compatible
- Celsius temperature output

Tested on Arduino 1.8.12 in 4/2020
