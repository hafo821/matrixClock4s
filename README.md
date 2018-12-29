# matrixClock4s

YAEC-4 (Yet Another Esp8266 Clock with 4 "segments" of dot matrix 8x8 display)  
Dot matrix clock with 4 segments, NTP, WiFi manager and support for daylight saving and LDR backlight adjustment and MQTT messages displaying
---

Arduino code for ESP8266 with connected Matrix display(CS:D3; CLK:D5; MOSI/DIN:D7) and LDR(A0)

Just connect power, set the WifiManager (SSID: MatrixClock) and connect to your WiFi. Daylight saving mode is set to Czech republic (CET,CEST)

![alt text](https://raw.githubusercontent.com/owarek/matrixClock4s/master/img/IMG_20181207_142341.jpg)
TODO:  
*Add WifiManager settings for DLS mode  
*Add WifiManager onDemand setting  
*Add WifiManager setting for SPIFFS format  
*Upload some Wifimanager settings screenshots   
*Add support for cap. touch sensors or gesture sensor 
*Add support for buzzer (Alarms)  
*Add support for Neopixels  

McHa added: 
* directive ONE_DISPLAY for using the clock with only one matrix display (so it has to scroll to show all the numbers of the time)
* with ONE_DISPLAY set beside the scrooling of the clock the date is shown from time to time
* added font derived from classic VGA MS-DOS font CP437 by Nick Gammon (http://www.gammon.com.au/forum/?id=11516)
* added directive HAS_AMBIENT for either to use photoresistor for ambient light detection on A0 or just to use default values of display intensity
