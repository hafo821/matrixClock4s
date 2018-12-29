/*
  YAEC-4 (Yet Another Esp8266 Clock with 4 "segments" of dot matrix 8x8 display)

  NTP Matrix clock with WiFiManager support, daylightsaving support, LDR backlight adjustment and MQTT marquee message

  https://github.com/owarek/matrixClock4s
*/
#include <FS.h>                  //https://github.com/esp8266/Arduino/tree/master/cores/esp8266 this needs to be first, or it all crashes and burns...
#include <TimeLib.h>			 //https://github.com/PaulStoffregen/Time
#include <ESP8266WiFi.h>		 //https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <WiFiUdp.h>
#include <Timezone.h>            //https://github.com/JChristensen/Timezone
#include <DNSServer.h>			 //https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer
#include <ESP8266WebServer.h>	 //https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>         //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>        //https://github.com/knolleary/pubsubclient
#include "TimerObject.h"         //https://playground.arduino.cc/Code/ArduinoTimerObject
#include <MD_Parola.h>           //https://github.com/MajicDesigns/MD_Parola
#include <MD_MAX72xx.h>          //https://github.com/MajicDesigns/MD_MAX72XX
#include <Time.h>			     //https://github.com/ebrevdo/arduino/tree/master/libraries/Time
#include "Parola_Fonts_data.h"
//----------------------------------------------------------------------------------------------------------
#define DEBUG_LEVEL 2 // 0, 1 or 2

#define PRINT_TIME_TRUE		true
#define PRINT_TIME_FALSE	false

#define NEWLINE_TRUE		true
#define NEWLINE_FALSE		false

// values for WiFiManager setup
// define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server_adress[40] = "iot.owar.cz";
char mqtt_port[6] = "1883";
char mqtt_user[40] = "owar";
char mqtt_password[40] = "owarek23*";
char mqtt_message_topic[40] = "test/matrixclock/message";
char ntp_server_adress[40] = "pool.ntp.org";
char offline_mode[40] = "ONLINE";
char mqtt_only_mode[40] = "FALSE";
char number_of_display_segments[3] = "1";
char number_of_marquee_repetitions[10] = "2";
char marquee_speed[10] = "25";

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
//#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
unsigned int MAX_DEVICES = 1;
#define CLK_PIN   D5
#define DATA_PIN  D7
#define CS_PIN    D3
// using only one display has specific issues, e.g. clock are also scrolling
#define ONE_DISPLAY
#ifdef ONE_DISPLAY
#define MAX_FLIPCOUNT 11
#endif

// TIMEZONE AND DLS SETUP
TimeChangeRule CEST = { "CEST", Last, Sun, Mar, 2, 120 };     // Central European Summer Time
TimeChangeRule CET = { "CET", Last, Sun, Oct, 3, 60 };        // Central European Standard Time
Timezone CE(CEST, CET);                                     // Central European Time (PRAGUE)

// DISPLAY SPEED
unsigned long ANIM_DELAY = 25;               // marquee animation speed
unsigned long REPETITIONS = 3;               // number of marquee repetition
const int DOT_DELAY = 500;                   // dot blink speed

// https://pjrp.github.io/MDParolaFontEditor
// LED CLOCK MOVING SEMICOLON SYMBOLS
uint8_t semi1[] = { 2, 24, 24 }; // semicolon
uint8_t semi2[] = { 2, 36, 36 }; // longer semicolon
uint8_t narrowSemicolon[] = { 2, 0x66, 0x66};  // narrow ':'
uint8_t narrowDot[] = { 2, 0x60, 0x60};  // narrow '.'
// CHRISTMASS EDITION
uint8_t tree[] = { 8, 64, 112, 124, 255, 255, 124, 112, 64 };
uint8_t ball[] = { 8, 60, 82, 137, 145, 137, 145, 74, 60 };

// OBJECT INITIALISATIONS
MD_Parola* ptrToParola = NULL;
WiFiUDP Udp;
TimeChangeRule *tcr;
WiFiClient espClient;
PubSubClient mqtt(espClient);
// Create timer object, init with time, callback and singleshot=false
TimerObject *timerDot = new TimerObject(DOT_DELAY, &updateDot, false);
//TimerObject *timerSwitch = new TimerObject(SWITCH_DELAY, &updateSwitch, false);
TimerObject *timerMarquee = new TimerObject(ANIM_DELAY, &updateMarquee, false);

// variables
char message[100] = "";                             // recieved marquee message
int messageSize = 0;                                // size of recieved message
unsigned int localPort = 2390;                      // local port to listen for UDP packets
bool shouldSaveConfig = false;                      // flag for saving data
int x = 0, y = 0;                                   // marquee coordinates start top left
bool dot = 0;                                       // state of the dot
unsigned long repetitionsCounter = 0;               // counter for marquee repetition
bool boolDot = false;                               // dot switch flag
bool boolSwitch = false;                            // time/marquee switch flag
bool boolMarquee = false;                           // marquee animation flag
unsigned int h = 00;                                // actual hour
unsigned int m = 00;                                // actual minute
unsigned int s = 00;                                // actual second
char text[100] = "";                                // display text holder
time_t prevDisplay = 0;                             // when the digital clock was displayed
const int NTP_PACKET_SIZE = 48;                     // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];                 // buffer to hold incoming & outgoing packets
unsigned long WiFireconnectCounter = 0;             // counter for WiFi reconnection
unsigned long MQTTreconnectCounter = 0;             // counter for MQTT reconnection
unsigned long intensity = 0;                        // intensity of display backlight
unsigned long PREVIOUS_REPETITIONS = REPETITIONS;   // store actual count of repetitions to revert back later
time_t myTimeStatus = 0;
//-------------------------------------------------------------------------------------------
void setup()
{
  //  Serial.begin(115200);
  //  resetHandler("WIFI");
  //SPIFFS.format();

  ptrToParola = new MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
  ptrToParola->begin();
  ptrToParola->addChar(':', narrowSemicolon);
  ptrToParola->addChar('.', narrowDot);
  ptrToParola->addChar('^', semi1);
  ptrToParola->addChar('$', semi2);
  ptrToParola->addChar('&', tree);
  ptrToParola->addChar('%', ball);
  ptrToParola->setFont(CP437);
  ptrToParola->setIntensity(9);   // 0 = low, 15 = high

  pinMode(A0, INPUT);

  messageSize = 100;
  clearMessage();
  pinMode(A0, INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial); // Needed for Leonardo only
  delay(250);
  debugPrint(PRINT_TIME_TRUE, "MATRIX CLOCK", "SETUP START", NEWLINE_TRUE, 0);

  ptrToParola->displayText("FS", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  ptrToParola->displayAnimate();

  //read configuration from FS json
  while (reconnectWiFi() == false) {
    //loop until we are connected to WiFi
  }

  delete(ptrToParola);
  ptrToParola = new MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
  ptrToParola->begin();
  ptrToParola->addChar(':', narrowSemicolon);
  ptrToParola->addChar('.', narrowDot);
  ptrToParola->addChar('^', semi1);
  ptrToParola->addChar('$', semi2);
  ptrToParola->addChar('&', tree);
  ptrToParola->addChar('%', ball);
  ptrToParola->setFont(CP437);
  ptrToParola->setIntensity(15);

  //debugPrint(PRINT_TIME_TRUE, "UDP PORT", String(Udp.localPort()), NEWLINE_FALSE, 0);
  debugPrint(PRINT_TIME_FALSE, "NTP SERVER ADDRESS", String(ntp_server_adress), NEWLINE_FALSE, 0);
  debugPrint(PRINT_TIME_FALSE, "NTP", "WAITING FOR SYNC", NEWLINE_TRUE, 0);

  ptrToParola->displayText("NTP", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  ptrToParola->displayAnimate();

  setSyncProvider(getNtpTime);
  myTimeStatus = getNtpTime();

  while (myTimeStatus == timeNotSet) {
    myTimeStatus = getNtpTime();
    ptrToParola->displayText("NONTP", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    ptrToParola->displayAnimate();
    delay(5000);
  }

  int port = atoi(mqtt_port);

  ptrToParola->displayText("MQTT", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  ptrToParola->displayAnimate();

  mqtt.setServer(mqtt_server_adress, port);
  mqtt.setCallback(mqttCallback);

  reconnectMQTT();

  debugPrint(PRINT_TIME_TRUE, "MATRIX CLOCK", "PROGRAM START", NEWLINE_TRUE, 0);

  ptrToParola->displayText("GO", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  ptrToParola->displayAnimate();
  delay(1000);

  timerDot->Start();
  timerMarquee->Start();
}


void loop() {
  int i = 0; // iteration variable
#ifdef ONE_DISPLAY
  static int flipCount = 0;
#endif
  mqtt.loop();

  if (!mqtt.connected() && (strcmp("ONLINE", offline_mode) == 0)) {
    debugPrint(PRINT_TIME_TRUE, "MQTT", "DISCONNECTED", NEWLINE_TRUE, 0);
    ptrToParola->displayText("OFFLINE", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    ptrToParola->displayAnimate();
    reconnectMQTT();
  }

  String textStr = "";

  if (boolSwitch == true || strcmp("TRUE", mqtt_only_mode) == 0) {
    // DISPLAY MARQUEE
    if (ptrToParola->displayAnimate()) {
      repetitionsCounter++;
      debugPrint(PRINT_TIME_TRUE, "REPETITIONS", String(repetitionsCounter) + "/" + String(REPETITIONS), NEWLINE_TRUE, 0);
#ifdef ONE_DISPLAY
      ANIM_DELAY = 75;
#endif
      ptrToParola->displayText(message, PA_LEFT, ANIM_DELAY, 1000, PA_SCROLL_LEFT, PA_SCROLL_DOWN);
    }
    if (repetitionsCounter >= REPETITIONS && strcmp("FALSE", mqtt_only_mode) == 0) {
      boolSwitch = false;
      clearMessage();
    }
  }
  else {
    // DISPLAY TIME
    if (timeStatus() != timeNotSet) {

      h = hour();
      m = minute();
      s = second();
#ifdef ONE_DISPLAY
      if (flipCount >= MAX_FLIPCOUNT) {
        textStr = dayShortStr(weekday()) + String(" ");
        textStr += convertDigitsNoColon(day());
        textStr += ".";
        textStr += convertDigitsNoColon(month());
        textStr += ".";
        textStr += convertDigitsNoColon(year());
      } else {
        textStr = convertDigitsNoColon(h);
        textStr += ":";
        textStr += convertDigitsNoColon(m);
      }
      intensity = 1;
      ptrToParola->setIntensity(intensity);
#else
      if (boolDot == true) {
        if (MAX_DEVICES <= 4) {
          textStr += convertDigitsNoColon(h);
          textStr += "$";
          textStr += convertDigitsNoColon(m);
        }
        else {
          textStr += convertDigitsNoColon(h);
          textStr += "$";
          textStr += convertDigitsNoColon(m);
          textStr += "^";
          textStr += convertDigitsNoColon(s);
        }
      }
      else
      {
        if (MAX_DEVICES <= 4) {
          textStr += convertDigitsNoColon(h);
          textStr += "^";
          textStr += convertDigitsNoColon(m);
        }
        else {
          textStr += convertDigitsNoColon(h);
          textStr += "^";
          textStr += convertDigitsNoColon(m);
          textStr += "$";
          textStr += convertDigitsNoColon(s);
        }
        intensity = map(analogRead(A0), 0, 1023, 0, 15);
        intensity = constrain(intensity, 0, 15);
        ptrToParola->setIntensity(intensity);
      }
#endif
      for (i = 0; i < textStr.length(); i++) {
        text[i] = textStr[i];
      }
      text[i] = '\0';
#ifndef ONE_DISPLAY
      ptrToParola->displayText(text, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
#else
      if (ptrToParola->displayAnimate()) {
        ptrToParola->displayClear();
        if (flipCount < MAX_FLIPCOUNT) {
          if (((flipCount + 1) % 2) == 0)
            ptrToParola->displayText(text, PA_LEFT, 75, 5, PA_SCROLL_RIGHT, PA_SCROLL_RIGHT);
          else
            ptrToParola->displayText(text, PA_LEFT, 75, 5, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        } else {
          ptrToParola->displayText(text, PA_LEFT, 75, 5, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
          if ( flipCount > MAX_FLIPCOUNT) {
            flipCount = 0;
          }
        }
        debugPrint(PRINT_TIME_TRUE, "TIME+", text, NEWLINE_TRUE, 0);
        debugPrint(PRINT_TIME_TRUE, "TIME+", String(flipCount), NEWLINE_TRUE, 0);
        flipCount++;
      }
#endif
      ptrToParola->displayAnimate();
      if (now() >= prevDisplay + 60) {
        prevDisplay = now();
        debugPrint(PRINT_TIME_TRUE, "TIME", "ONE MINUTE MARK", NEWLINE_TRUE, 0);
        debugPrint(PRINT_TIME_TRUE, "TIME", text, NEWLINE_TRUE, 0);
      }
    }
  }
  TimerUpdate();
}
//-------------------------------------------------------------------------------------------
//dot timer callback
void updateDot(void) {
  if (boolDot == true) {
    boolDot = false;
  }
  else {
    boolDot = true;
  }
}

//switch timer callback
void updateSwitch(void) {
  boolSwitch = true;
}

//marquee timer callback
void updateMarquee(void) {
  boolMarquee = true;
}

//callback notifying us of the need to save config
void saveConfigCallback() {
  debugPrint(PRINT_TIME_TRUE, "JSON CONFIG", "SHOULD BE SAVED", NEWLINE_TRUE, 0);
  shouldSaveConfig = true;
}

// callback for incomming MQTT messages
void mqttCallback(char* topic, byte * payload, unsigned int length) {
  String top = topic;
  String incom = String((char*)payload);
  String incomMarquee;
  String outputMessage;

  if (top == mqtt_message_topic) {
    repetitionsCounter = 0;

    /*for (int i = 0; i < length; i++) {
    	incom += (char)payload[i];
      }*/

    if (getValue(incom, '*', 1) == "MQTTONLY") {
      getValue(incom, '*', 2).toCharArray(mqtt_only_mode, getValue(incom, '*', 2).length() + 1);

      for (int i = 0; i < MAX_DEVICES - 1; i++) {
        incomMarquee += ' ';
      }
      incomMarquee += getValue(incom, '*', 3);
      incomMarquee.toCharArray(message, incomMarquee.length() + 1);
      messageSize = incomMarquee.length();

      REPETITIONS = 0;
    }
    else if (getValue(incom, '*', 1) == "REPETITIONS") {

      PREVIOUS_REPETITIONS = REPETITIONS;
      REPETITIONS = getValue(incom, '*', 2).toInt();
      memcpy(mqtt_only_mode, "FALSE", 5);

      for (int i = 0; i < MAX_DEVICES - 1; i++) {
        incomMarquee += ' ';
      }
      incomMarquee += getValue(incom, '*', 3);
      incomMarquee.toCharArray(message, incomMarquee.length() + 1);
      messageSize = incomMarquee.length();
    }
    else if (getValue(incom, '*', 1) == "RESET") {
      resetHandler(getValue(incom, '*', 2));
    }
    else {
      REPETITIONS = 2;
      PREVIOUS_REPETITIONS = REPETITIONS;

      for (int i = 0; i < MAX_DEVICES - 1; i++) {
        incomMarquee += ' ';
      }

      for (int i = 0; i < length; i++) {
        incomMarquee += (char)payload[i];
      }

      incomMarquee.toCharArray(message, incomMarquee.length() + 1);
      messageSize = incomMarquee.length();

      memcpy(mqtt_only_mode, "FALSE", 5);
    }
    updateSwitch();

    debugPrint(PRINT_TIME_TRUE, "MESSAGE TOPIC", topic, NEWLINE_FALSE, 0);
    debugPrint(PRINT_TIME_FALSE, "MESSAGE SIZE", String(messageSize), NEWLINE_FALSE, 0);
    debugPrint(PRINT_TIME_FALSE, "MESSAGE PAYLOAD", String(incom), NEWLINE_TRUE, 0);

    debugPrint(PRINT_TIME_TRUE, "PARSED MESSAGE", getValue(incom, '*', 1) + "--" + getValue(incom, '*', 2) + "--" + getValue(incom, '*', 3) + "--", NEWLINE_FALSE, 1);
    debugPrint(PRINT_TIME_FALSE, "FINAL MESSAGE", message, NEWLINE_TRUE, 1);

    debugPrint(PRINT_TIME_TRUE, "MARQUEE DEBUG", "--MARQUEE--", NEWLINE_TRUE, 2);

    ptrToParola->setIntensity(15); // 0 = low, 15 = high
    ptrToParola->displayClear();
#ifdef ONE_DISPLAY
    ANIM_DELAY = 75;
#endif
    ptrToParola->displayText(message, PA_LEFT, ANIM_DELAY, 1000, PA_SCROLL_LEFT, PA_SCROLL_DOWN);
  }
}

// clearing of mqtt message
void clearMessage(void) {
  for (int i = 0; i < messageSize; i++) {
    message[i] = ' ';
  }
  messageSize = 0;
}

// checking if there is some mqtt message to marquee
bool checkMessage(void) {
  if (messageSize > 0) {
    return true;
  }
  return false;
}

// formating of time
String convertDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  String output = ":";
  if (digits < 10)
    output += "0";
  output += String(digits);
  return output;
}

String convertDigitsNoColon(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  String output = "";
  if (digits < 10)
    output += "0";
  output += String(digits);
  return output;
}
// NTP time getter
time_t getNtpTime(void) {
  while (Udp.parsePacket() > 0); // discard any previously received packets
  debugPrint(PRINT_TIME_FALSE, "NTP", "TRANSMIT REQUEST", NEWLINE_FALSE, 0);
  sendNTPpacket();
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      debugPrint(PRINT_TIME_FALSE, "NTP", "RECEIVED RESPONSE", NEWLINE_FALSE, 0);
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      time_t utc = secsSince1900 - 2208988800UL;
      debugPrint(PRINT_TIME_FALSE, "UTC TIME", printDateTime(utc), NEWLINE_FALSE, 0);
      //debugPrint(PRINT_TIME_FALSE, "NOTE", " ---day light saving---> ", NEWLINE_FALSE, 0);
      debugPrint(PRINT_TIME_FALSE, "LOCAL TIME", printDateTime(CE.toLocal(utc, &tcr)), NEWLINE_FALSE, 0);
      debugPrint(PRINT_TIME_FALSE, "", "", true, 0);
      return CE.toLocal(utc, &tcr);
    }
  }
  debugPrint(PRINT_TIME_FALSE, "NTP", "NO RESPONSE", NEWLINE_TRUE, 0);
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(void) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(ntp_server_adress, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

// update all timer objects
void TimerUpdate(void) {
  timerDot->Update();
  timerMarquee->Update();
}

// reconnect to WiFi
bool reconnectWiFi(void) {
  debugPrint(PRINT_TIME_TRUE, "SPIFFS", "MOUNTING FS", NEWLINE_TRUE, 1);
  if (SPIFFS.begin()) {
    debugPrint(PRINT_TIME_TRUE, "SPIFFS", "MOUNTED FS", NEWLINE_TRUE, 1);
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      debugPrint(PRINT_TIME_TRUE, "SPIFFS", "READING CONFIG FILE", NEWLINE_TRUE, 1);
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        if (DEBUG_LEVEL > 0)
          debugPrint(PRINT_TIME_TRUE, "SPIFFS", "OPENED CONFIG FILE", NEWLINE_TRUE, 1);
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (DEBUG_LEVEL > 1) {
          json.printTo(Serial);
          debugPrint(PRINT_TIME_FALSE, "", "", NEWLINE_TRUE, 0);
        }
        if (json.success()) {
          debugPrint(PRINT_TIME_TRUE, "SPIFFS", "PARSED JSON", NEWLINE_TRUE, 1);
          ptrToParola->displayText("FSOK", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
          ptrToParola->displayAnimate();

          strcpy(mqtt_server_adress, json["mqtt_server_adress"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_message_topic, json["mqtt_message_topic"]);
          strcpy(ntp_server_adress, json["ntp_server_adress"]);
          strcpy(offline_mode, json["offline_mode"]);
          strcpy(mqtt_only_mode, json["mqtt_only_mode"]);
          strcpy(number_of_display_segments, json["number_of_display_segments"]);
          strcpy(number_of_marquee_repetitions, json["number_of_marquee_repetitions"]);
          strcpy(marquee_speed, json["marquee_speed"]);
        }
        else {
          debugPrint(PRINT_TIME_TRUE, "SPIFFS", "FAILED TO LOAD JSON CONFIG", NEWLINE_TRUE, 0);
        }
      }
    }
  }
  else {
    debugPrint(PRINT_TIME_TRUE, "SPIFFS", "FAILED TO MOUNT FS", NEWLINE_TRUE, 0);
  }
  //end read
  ptrToParola->displayText("WIFI", PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  ptrToParola->displayAnimate();

  WiFiManagerParameter custom_text_mqtt_server_address("<p>mqtt server address</p>");
  WiFiManagerParameter custom_text_mqtt_server_port("<p>mqtt server port</p>");
  WiFiManagerParameter custom_text_mqtt_server_user("<p>mqtt server user</p>");
  WiFiManagerParameter custom_text_mqtt_server_password("<p>mqtt server password</p>");
  WiFiManagerParameter custom_text_mqtt_message_topic("<p>mqtt message topic</p>");
  WiFiManagerParameter custom_text_ntp_server_address("<p>ntp server address</p>");
  WiFiManagerParameter custom_text_offline_mode("<p>offline mode</p>");
  WiFiManagerParameter custom_text_mqtt_only_mode("<p>mqtt only mode</p>");
  WiFiManagerParameter custom_text_number_of_display_segmetns("<p>number of display segments</p>");
  WiFiManagerParameter custom_text_marquee_repetition_count("<p>marquee repetition count</p>");
  WiFiManagerParameter custom_text_marquee_animation_speed("<p>marquee animation speed</p>");

  WiFiManagerParameter custom_mqtt_server_adress("mqtt_server_adress", "example.com", mqtt_server_adress, 40);
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "1883", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("mqtt_user", "username", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "password", mqtt_password, 40);
  WiFiManagerParameter custom_mqtt_message_topic("mqtt_message_topic", "example/topic", mqtt_message_topic, 40);
  WiFiManagerParameter custom_ntp_server_adress("ntp_server_adress", "pool.ntp.org", ntp_server_adress, 40);
  WiFiManagerParameter custom_offline_mode("offline_mode", "OFFLINE or ONLINE", offline_mode, 40);
  WiFiManagerParameter custom_mqtt_only_mode("mqtt_only_mode", "TRUE or FALSE", mqtt_only_mode, 40);
  WiFiManagerParameter custom_number_of_display_segments("number_of_display_segments", "4", number_of_display_segments, 3);
  WiFiManagerParameter custom_number_of_marquee_repetitions("number_of_marquee_repetitions", "2", number_of_marquee_repetitions, 10);
  WiFiManagerParameter custom_marquee_speed("marquee_speed", "40", marquee_speed, 10);

  WiFiManager wifiManager;
  //wifiManager.resetSettings();

  if (DEBUG_LEVEL < 1) {
    wifiManager.setDebugOutput(false);
  }

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_text_mqtt_server_address);
  wifiManager.addParameter(&custom_mqtt_server_adress);
  wifiManager.addParameter(&custom_text_mqtt_server_port);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_text_mqtt_server_user);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_text_mqtt_server_password);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_text_mqtt_message_topic);
  wifiManager.addParameter(&custom_mqtt_message_topic);
  wifiManager.addParameter(&custom_text_ntp_server_address);
  wifiManager.addParameter(&custom_ntp_server_adress);
  wifiManager.addParameter(&custom_text_offline_mode);
  wifiManager.addParameter(&custom_offline_mode);
  wifiManager.addParameter(&custom_text_mqtt_only_mode);
  wifiManager.addParameter(&custom_mqtt_only_mode);
  wifiManager.addParameter(&custom_text_number_of_display_segmetns);
  wifiManager.addParameter(&custom_number_of_display_segments);
  wifiManager.addParameter(&custom_text_marquee_repetition_count);
  wifiManager.addParameter(&custom_number_of_marquee_repetitions);
  wifiManager.addParameter(&custom_text_marquee_animation_speed);
  wifiManager.addParameter(&custom_marquee_speed);

  //wifiManager.autoConnect("MatrixClock" + long(ESP.getChipId()));
  wifiManager.autoConnect("MatrixClock");


  //read updated parameters
  strcpy(mqtt_server_adress, custom_mqtt_server_adress.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_message_topic, custom_mqtt_message_topic.getValue());
  strcpy(ntp_server_adress, custom_ntp_server_adress.getValue());
  strcpy(offline_mode, custom_offline_mode.getValue());
  strcpy(mqtt_only_mode, custom_mqtt_only_mode.getValue());
  strcpy(number_of_display_segments, custom_number_of_display_segments.getValue());
  strcpy(number_of_marquee_repetitions, custom_number_of_marquee_repetitions.getValue());
  strcpy(marquee_speed, custom_marquee_speed.getValue());

  if (atoi(number_of_display_segments) > 0) {
    MAX_DEVICES = atoi(number_of_display_segments);
  }

  if (atoi(number_of_marquee_repetitions) > 0) {
    REPETITIONS = atoi(number_of_marquee_repetitions);
    PREVIOUS_REPETITIONS = REPETITIONS;
  }

  if (atoi(marquee_speed) > 0) {
    ANIM_DELAY = atoi(marquee_speed);
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    debugPrint(PRINT_TIME_TRUE, "SPIFFS", "SAVING CONFIG", NEWLINE_TRUE, 0);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server_adress"] = mqtt_server_adress;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_message_topic"] = mqtt_message_topic;
    json["ntp_server_adress"] = ntp_server_adress;
    json["offline_mode"] = offline_mode;
    json["mqtt_only_mode"] = mqtt_only_mode;
    json["number_of_display_segments"] = number_of_display_segments;
    json["number_of_marquee_repetitions"] = number_of_marquee_repetitions;
    json["marquee_speed"] = marquee_speed;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      debugPrint(PRINT_TIME_TRUE, "SPIFFS", "FAILED TO OPEN CONFIG FILE FOR WRITE", NEWLINE_TRUE, 0);
    }

    json.printTo(Serial);
    debugPrint(PRINT_TIME_FALSE, "", "", NEWLINE_TRUE, 0);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  debugPrint(PRINT_TIME_TRUE, "WIFI IP ASSIGNED", WiFi.localIP().toString(), NEWLINE_TRUE, 0);
  debugPrint(PRINT_TIME_TRUE, "STARTING UDP PORT", String(localPort), NEWLINE_TRUE, 0);
  Udp.begin(localPort);

  WiFireconnectCounter++;

  if (WiFi.status() == WL_CONNECTED) {
    debugPrint(PRINT_TIME_TRUE, "WIFI", "CONNECTED", NEWLINE_TRUE, 0);
    return true;
  }
  else {
    debugPrint(PRINT_TIME_TRUE, "WIFI", "NOT CONNECTED", NEWLINE_TRUE, 0);
    debugPrint(PRINT_TIME_TRUE, "WIFI STATUS", String(WiFi.status()), NEWLINE_TRUE, 0);
    return false;
  }
}

// reconnect to MQTT
void reconnectMQTT(void) {
  // Loop until we're reconnected
  if (strcmp("ONLINE", offline_mode) == 0) {
    debugPrint(PRINT_TIME_TRUE, "MQTT SERVER CONNECTING", String(mqtt_server_adress) + ":" + String(mqtt_port), NEWLINE_FALSE, 0);

    int port = atoi(mqtt_port);

    while (!mqtt.connected()) {
      //debugPrint(PRINT_TIME_FALSE, "", "_*_", NEWLINE_FALSE, 0);
      String client_ID = "ESP-";
      client_ID += long(ESP.getChipId());
      char msg[20];
      client_ID.toCharArray(msg, client_ID.length() + 1);
      if (mqtt.connect(msg, mqtt_user, mqtt_password)) {
        mqtt.subscribe(mqtt_message_topic);
      }
      debugPrint(PRINT_TIME_FALSE, "", "", NEWLINE_TRUE, 0);
      delay(500);
      MQTTreconnectCounter++;
    }
    debugPrint(PRINT_TIME_TRUE, "MQTT", "CONNECTED", NEWLINE_TRUE, 0);
    if (MQTTreconnectCounter > (10000 / 500)) {
      reconnectWiFi();
    }
    if (WiFireconnectCounter > (10000 / 500)) {
      resetHandler("RESTART");
    }
  }
  else {
    debugPrint(PRINT_TIME_TRUE, "MQTT", "DISABLED", NEWLINE_FALSE, 0);
  }
}

// date time formating
String printDateTime(time_t t)
{
  String output = "";
  output += String(hour(t));
  output += convertDigits(minute(t));
  output += convertDigits(second(t));

  output += " ";
  output += String(dayShortStr(weekday(t)));
  output += "/";
  output += String(day(t));
  output += "/";
  output += String(monthShortStr(month(t)));
  output += "/";
  output += String(year(t));

  return output;
}

// message parser
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// handler for reset/restart/format requests
void resetHandler(String type) {
  if (type == "FACTORY") {
    debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------PERFORMING FACTORY RESET!!!-------------------------", NEWLINE_TRUE, 0);
    SPIFFS.format();
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    WiFi.disconnect();
  }
  if (type == "SPIFFS") {
    debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------FORMATTING SPIFFS!!!--------------------------------", NEWLINE_TRUE, 0);
    SPIFFS.format();
  }
  if (type == "WIFI") {
    debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------RESETING WIFI!!!------------------------------------", NEWLINE_TRUE, 0);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
  }
  if (type == "RESTART") {
    debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------RESTARTING!---------------------------------------", NEWLINE_TRUE, 0);
  }
  debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------PERFORMING RESTART!!!-------------------------------", NEWLINE_TRUE, 0);
  debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------PLEASE DO MANUAL RESTART IF THIS HANGS--------------", NEWLINE_TRUE, 0);
  debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------THIS IS KNOWN PROBLEM ON ESP8266--------------------", NEWLINE_TRUE, 0);
  debugPrint(PRINT_TIME_TRUE, "RESET HANDLER", "---------AND SHOULD APPEAR ONLY ONCE AFTER FW UPLOAD---------", NEWLINE_TRUE, 0);

  //ESP.restart();

  pinMode(D0, OUTPUT);
  digitalWrite(D0, LOW);
}

// formated debug serial output
bool debugPrint(bool t, String ID, String message, bool newline, int level) {
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 1
#endif

  if (DEBUG_LEVEL >= level) {
    if (t) {
      if (myTimeStatus != timeNotSet) {
        Serial.print("[");
        Serial.print(hour());
        Serial.print(convertDigits(minute()));
        Serial.print(convertDigits(second()));
        Serial.print(" ");
        Serial.print(day());
        Serial.print(".");
        Serial.print(month());
        Serial.print(".");
        Serial.print(year());
        Serial.print("] ");
      }
      else {
        Serial.print("[");
        Serial.print("TIME NOT SET");
        Serial.print("] ");
      }
    }
    if (ID != "") {
      Serial.print("[");
      Serial.print(ID);
      Serial.print("][");
      Serial.print(message);
      Serial.print("] ");
    }
    else {
      Serial.print(message);
    }
    if (newline)
      Serial.println();
    else
      Serial.print(" ");
    return true;
  }
  return false;
}

uint8_t utf8Ascii(uint8_t ascii)
// Convert a single Character from UTF8 to Extended ASCII according to ISO 8859-1,
// also called ISO Latin-1. Codes 128-159 contain the Microsoft Windows Latin-1
// extended characters:
// - codes 0..127 are identical in ASCII and UTF-8
// - codes 160..191 in ISO-8859-1 and Windows-1252 are two-byte characters in UTF-8
//                 + 0xC2 then second byte identical to the extended ASCII code.
// - codes 192..255 in ISO-8859-1 and Windows-1252 are two-byte characters in UTF-8
//                 + 0xC3 then second byte differs only in the first two bits to extended ASCII code.
// - codes 128..159 in Windows-1252 are different, but usually only the �-symbol will be needed from this range.
//                 + The euro symbol is 0x80 in Windows-1252, 0xa4 in ISO-8859-15, and 0xe2 0x82 0xac in UTF-8.
//
// Modified from original code at http://playground.arduino.cc/Main/Utf8ascii
// Extended ASCII encoding should match the characters at http://www.ascii-code.com/
//
// Return "0" if a byte has to be ignored.
{
  static uint8_t cPrev;
  uint8_t c = '\0';

  if (ascii < 0x7f)   // Standard ASCII-set 0..0x7F, no conversion
  {
    cPrev = '\0';
    c = ascii;
  }
  else
  {
    switch (cPrev)  // Conversion depending on preceding UTF8-character
    {
      case 0xC2: c = ascii;  break;
      case 0xC3: c = ascii | 0xC0;  break;
      case 0x82: if (ascii == 0xAC) c = 0x80; // Euro symbol special case
    }
    cPrev = ascii;   // save last char
  }

  //PRINTX("\nConverted 0x", ascii);
  //PRINTX(" to 0x", c);

  return (c);
}

void utf8Ascii(char* s)
// In place conversion UTF-8 string to Extended ASCII
// The extended ASCII string is always shorter.
{
  uint8_t c, k = 0;
  char *cp = s;

  //PRINT("\nConverting: ", s);

  while (*s != '\0')
  {
    c = utf8Ascii(*s++);
    if (c != '\0')
      *cp++ = c;
  }
  *cp = '\0';   // terminate the new string
}
