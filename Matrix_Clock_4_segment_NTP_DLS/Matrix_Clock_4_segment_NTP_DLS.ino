/*
  YAEC-4 (Yet Another Esp8266 Clock with 4 "segments" of dot matrix 8x8 display)

  NTP Matrix clock with WiFiManager support, daylightsaving support, LDR backlight adjustment and MQTT marquee message
*/
#include <FS.h>                  //this needs to be first, or it all crashes and burns...
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Timezone.h>            //https://github.com/JChristensen/Timezone
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>         //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>        //https://github.com/knolleary/pubsubclient
#include "TimerObject.h"         //https://playground.arduino.cc/Code/ArduinoTimerObject
#include <LEDMatrixDriver.hpp>   //https://github.com/bartoszbielawski/LEDMatrixDriver/blob/master/src/LEDMatrixDriver.hpp
#include <Time.h>

#define DEBUG_LEVEL 1 // 0, 1 or 2

// values for WiFiManager setup
// define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server_adress[40] = "";
char mqtt_port[6] = "";
char mqtt_user[40] = "";
char mqtt_password[40] = "";
char mqtt_message_topic[40] = "";
char ntp_server_adress[40] = "";
char offline_mode[40] = "";
char mqtt_only_mode[40] = "";
char number_of_display_segments[3] = "";
char number_of_marquee_repetitions[10] = "";
char marquee_speed[10] = "";

//LED matrix definition
// Define the ChipSelect pin for the led matrix (Dont use the SS or MISO pin of your Arduino!)
// Other pins are arduino specific SPI pins (MOSI=DIN of the LEDMatrix and CLK) see https://www.arduino.cc/en/Reference/SPI
const uint8_t LEDMATRIX_CS_PIN = D3;
// Define LED Matrix dimensions (0-n) - eg: 32x8 = 31x7
int LEDMATRIX_SEGMENTS = 4;
const int LEDMATRIX_HEIGHT = 7;
const int LEDMATRIX_WIDTH = (LEDMATRIX_SEGMENTS * 8) - 1;

// TIMEZONE AND DLS SETUP
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, 60};        // Central European Standard Time
Timezone CE(CEST, CET);                                     // Central European Time (PRAGUE)

// DISPLAY SPEED
unsigned long ANIM_DELAY = 40;               // marquee animation speed
unsigned long REPETITIONS = 2;               // number of marquee repetition
const int DOT_DELAY = 500;         // dot blink speed

// This is the font definition. You can use http://gurgleapps.com/tools/matrix to create your own font or sprites.
// If you like the font feel free to use it. I created it myself and donate it to the public domain.
byte font[95][8] = { {0, 0, 0, 0, 0, 0, 0, 0}, // SPACE
  {0x10, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18}, // !
  {0x28, 0x28, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, // :
  {0x00, 0x0a, 0x7f, 0x14, 0x28, 0xfe, 0x50, 0x00}, // #
  {0x10, 0x38, 0x54, 0x70, 0x1c, 0x54, 0x38, 0x10}, // $
  {0x00, 0x60, 0x66, 0x08, 0x10, 0x66, 0x06, 0x00}, // %
  {0, 0, 0, 0, 0, 0, 0, 0}, // &
  {0x00, 0x10, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00}, // '
  {0x02, 0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04}, // (
  {0x40, 0x20, 0x10, 0x10, 0x10, 0x10, 0x10, 0x20}, // )
  {0x00, 0x10, 0x54, 0x38, 0x10, 0x38, 0x54, 0x10}, // *
  {0x00, 0x08, 0x08, 0x08, 0x7f, 0x08, 0x08, 0x08}, // +
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x08}, // ,
  {0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00}, // -
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}, // .
  {0x00, 0x04, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40}, // /
  {0x38, 0x44, 0x4c, 0x5c, 0x74, 0x64, 0x44, 0x38}, // 0
  {0x04, 0x0c, 0x14, 0x24, 0x04, 0x04, 0x04, 0x04}, // 1
  {0x78, 0x44, 0x04, 0x04, 0x3c, 0x40, 0x40, 0x7c}, // 2
  {0x7c, 0x44, 0x04, 0x3c, 0x04, 0x04, 0x44, 0x7c}, // 3
  {0x40, 0x40, 0x40, 0x48, 0x48, 0x7c, 0x08, 0x08}, // 4
  {0x7c, 0x40, 0x40, 0x78, 0x04, 0x04, 0x04, 0x78}, // 5
  {0x38, 0x44, 0x40, 0x78, 0x44, 0x44, 0x44, 0x38}, // 6
  {0x7c, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x10}, // 7
  {0x38, 0x44, 0x44, 0x38, 0x38, 0x44, 0x44, 0x38}, // 8
  {0x38, 0x44, 0x44, 0x44, 0x3c, 0x04, 0x04, 0x38}, // 9
  {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00}, // :
  {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x08}, // ;
  {0x00, 0x10, 0x20, 0x40, 0x80, 0x40, 0x20, 0x10}, // <
  {0x00, 0x00, 0x7e, 0x00, 0x00, 0xfc, 0x00, 0x00}, // =
  {0x00, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, // >
  {0x00, 0x38, 0x44, 0x04, 0x08, 0x10, 0x00, 0x10}, // ?
  {0x00, 0x30, 0x48, 0xba, 0xba, 0x84, 0x78, 0x00}, // @
  {0x00, 0x1c, 0x22, 0x42, 0x42, 0x7e, 0x42, 0x42}, // A
  {0x00, 0x78, 0x44, 0x44, 0x78, 0x44, 0x44, 0x7c}, // B
  {0x00, 0x3c, 0x44, 0x40, 0x40, 0x40, 0x44, 0x7c}, // C
  {0x00, 0x7c, 0x42, 0x42, 0x42, 0x42, 0x44, 0x78}, // D
  {0x00, 0x78, 0x40, 0x40, 0x70, 0x40, 0x40, 0x7c}, // E
  {0x00, 0x7c, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40}, // F
  {0x00, 0x3c, 0x40, 0x40, 0x5c, 0x44, 0x44, 0x78}, // G
  {0x00, 0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42}, // H
  {0x00, 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7e}, // I
  {0x00, 0x7e, 0x02, 0x02, 0x02, 0x02, 0x04, 0x38}, // J
  {0x00, 0x44, 0x48, 0x50, 0x60, 0x50, 0x48, 0x44}, // K
  {0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7c}, // L
  {0x00, 0x82, 0xc6, 0xaa, 0x92, 0x82, 0x82, 0x82}, // M
  {0x00, 0x42, 0x42, 0x62, 0x52, 0x4a, 0x46, 0x42}, // N
  {0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x44, 0x38}, // O
  {0x00, 0x78, 0x44, 0x44, 0x48, 0x70, 0x40, 0x40}, // P
  {0x00, 0x3c, 0x42, 0x42, 0x52, 0x4a, 0x44, 0x3a}, // Q
  {0x00, 0x78, 0x44, 0x44, 0x78, 0x50, 0x48, 0x44}, // R
  {0x00, 0x38, 0x40, 0x40, 0x38, 0x04, 0x04, 0x78}, // S
  {0x00, 0x7e, 0x90, 0x10, 0x10, 0x10, 0x10, 0x10}, // T
  {0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3e}, // U
  {0x00, 0x42, 0x42, 0x42, 0x42, 0x44, 0x28, 0x10}, // V
  {0x80, 0x82, 0x82, 0x92, 0x92, 0x92, 0x94, 0x78}, // W
  {0x00, 0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42}, // X
  {0x00, 0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x10}, // Y
  {0x00, 0x7c, 0x04, 0x08, 0x7c, 0x20, 0x40, 0xfe}, // Z
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, //
  {0x00, 0x1c, 0x22, 0x02, 0x02, 0x3e, 0x22, 0x3e}, // a
  {0x00, 0x20, 0x20, 0x20, 0x20, 0x3e, 0x22, 0x3e}, // b
  {0x00, 0x1e, 0x20, 0x20, 0x20, 0x20, 0x20, 0x1e}, // c
  {0x00, 0x02, 0x02, 0x02, 0x1e, 0x22, 0x22, 0x1e}, // d
  {0x00, 0x1c, 0x22, 0x22, 0x3c, 0x20, 0x20, 0x1e}, // e
  {0x00, 0x3c, 0x24, 0x20, 0x70, 0x20, 0x20, 0x20}, // f
  {0x00, 0x1c, 0x22, 0x22, 0x22, 0x1e, 0x02, 0x1c}, // g
  {0x00, 0x20, 0x20, 0x20, 0x3c, 0x22, 0x22, 0x22}, // h
  {0x00, 0x20, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20}, // i
  {0x00, 0x04, 0x00, 0x04, 0x04, 0x04, 0x24, 0x18}, // j
  {0x00, 0x44, 0x48, 0x50, 0x60, 0x50, 0x48, 0x44}, // k
  {0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}, // l
  {0x00, 0x00, 0x28, 0x54, 0x54, 0x54, 0x54, 0x54}, // m
  {0x00, 0x00, 0x20, 0x50, 0x50, 0x50, 0x50, 0x50}, // n
  {0x00, 0x30, 0x48, 0x48, 0x48, 0x48, 0x48, 0x30}, // o
  {0x00, 0x30, 0x48, 0x48, 0x70, 0x40, 0x40, 0x40}, // p
  {0x00, 0x00, 0x30, 0x48, 0x48, 0x38, 0x08, 0x08}, // q
  {0x00, 0x00, 0x30, 0x48, 0x40, 0x40, 0x40, 0x40}, // r
  {0x00, 0x18, 0x24, 0x20, 0x18, 0x04, 0x24, 0x18}, // s
  {0x00, 0x00, 0x20, 0x70, 0x20, 0x20, 0x24, 0x18}, // t
  {0x00, 0x00, 0x00, 0x24, 0x24, 0x24, 0x24, 0x18}, // u
  {0x00, 0x00, 0x00, 0x28, 0x28, 0x28, 0x28, 0x10}, // v
  {0x00, 0x00, 0x00, 0x2a, 0x2a, 0x2a, 0x2a, 0x14}, // w
  {0x00, 0x00, 0x00, 0x28, 0x28, 0x10, 0x28, 0x28}, // x
  {0x00, 0x00, 0x00, 0x28, 0x28, 0x10, 0x10, 0x10}, // y
  {0x00, 0x00, 0x00, 0x78, 0x48, 0x10, 0x28, 0x78}, // z
  // (the font does not contain any lower case letters. you can add your own.)
};    // {}, //
// OBJECT INITIALISATIONS
// The LEDMatrixDriver class instance
LEDMatrixDriver lmd(LEDMATRIX_SEGMENTS, LEDMATRIX_CS_PIN);
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
bool boolDot  = false;                              // dot switch flag
bool boolSwitch  = false;                           // time/marquee switch flag
bool boolMarquee  = false;                          // marquee animation flag
unsigned int h = 00;                                // actual hour
unsigned int m = 00;                                // actual minute
unsigned int s = 00;                                // actual second
char text[] = "000000";                             // display text holder
time_t prevDisplay = 0;                             // when the digital clock was displayed
const int NTP_PACKET_SIZE = 48;                     // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];                 // buffer to hold incoming & outgoing packets
unsigned long WiFireconnectCounter = 0;             // counter for WiFi reconnection
unsigned long MQTTreconnectCounter = 0;             // counter for MQTT reconnection
unsigned long intensity = 0;                        // intensity of display backlight
unsigned long PREVIOUS_REPETITIONS = REPETITIONS;   // store actual count of repetitions to revert back later
//-------------------------------------------------------------------------------------------
void setup()
{
  time_t myTimeStatus;
  messageSize = 100;
  clearMessage();
  pinMode(A0, INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial) ; // Needed for Leonardo only
  delay(250);
  debugPrint(true, "MATRIX CLOCK", "PROGRAM START", true, 0);

  lmd.setEnabled(true);
  lmd.setIntensity(10);   // 0 = low, 10 = high

  drawString("FS    ", LEDMATRIX_SEGMENTS, 0, 0);
  lmd.display();

  //clean FS, for testing
  //SPIFFS.format();
  //read configuration from FS json
  while (reconnectWiFi() == false) {
    //loop until we are connected to WiFi
  }

  debugPrint(true, "UDP PORT", String(Udp.localPort()), false, 0);
  debugPrint(false, "NTP SERVER ADDRESS", String(ntp_server_adress), false, 0);
  debugPrint(false, "NTP", "WAITING FOR SYNC", true, 0);

  drawString("NTP   ", LEDMATRIX_SEGMENTS, 0, 0);
  lmd.display();

  delay(1000);

  setSyncProvider(getNtpTime);

  while (myTimeStatus == timeNotSet) {
    drawString("DEAD  ", LEDMATRIX_SEGMENTS, 0, 0);
    myTimeStatus = getNtpTime();
    lmd.display();
    delay(5000);
  }

  int port = atoi(mqtt_port);

  drawString("MQTT  ", LEDMATRIX_SEGMENTS, 0, 0);
  lmd.display();

  mqtt.setServer(mqtt_server_adress, port);
  mqtt.setCallback(mqttCallback);

  reconnectMQTT();

  debugPrint(true, "MATRIX CLOCK", "1...2...3...GO", true, 0);

  drawString(" GO   ", LEDMATRIX_SEGMENTS, 0, 0);
  lmd.display();
  delay(1000);

  timerDot->Start();
  timerMarquee->Start();
  pinMode(A0, INPUT);
}

void loop() {
  mqtt.loop();
  if  (!mqtt.connected() && (strcmp ("ONLINE", offline_mode) == 0)) {
    debugPrint(true, "MQTT", "DISCONNECTED", true, 0);
    drawString("OFFLINE", LEDMATRIX_SEGMENTS, 0, 0);
    lmd.display();
    reconnectMQTT();
  }
  String textStr = "";

  if (boolSwitch == true) {
    // display marquee

    intensity += 2;
    intensity = constrain(intensity, 0, 10);
    lmd.setIntensity(intensity); // 0 = low, 10 = high

    drawString("    ", LEDMATRIX_SEGMENTS, 0, 0);

    if (checkMessage()) {
      if ((repetitionsCounter < REPETITIONS) || strcmp ("TRUE", mqtt_only_mode) == 0) {
        if (boolMarquee == true) {
          boolMarquee = false;
          debugPrint(false, "", ".", false, 2);
          drawString(message, messageSize, x, 0);
          lmd.display();
          // Advance to next coordinate
          if ( --x < messageSize * -8 ) {
            x = LEDMATRIX_WIDTH;
            repetitionsCounter++;
            debugPrint(false, "", "", true, 2);
            debugPrint(true, "REPETITIONS", String(repetitionsCounter) + "/" + String(repetitionsCounter), true, 0);
          }
        }
      }
      else {
        clearMessage();
        repetitionsCounter = 0;
        boolSwitch = false;
      }
    }
    else {
      boolSwitch = false;
    }
  }
  else {
    // display time
    if (timeStatus() != timeNotSet) {
      if (now() != prevDisplay) { //update the display only if time has changed
        prevDisplay = now();
        debugPrint(true, "", "", true, 0);

        h = hour();
        m = minute();
        s = second();

        if (h < 10) {
          textStr += String(0);
        }
        textStr += String(h);
        if (m < 10) {
          textStr += String(0);
        }
        textStr += String(m);
        if (s < 10) {
          textStr += String(0);
        }
        textStr += String(s);

        for (int i = 0; i < LEDMATRIX_SEGMENTS; i++) {
          text[i] = textStr[i];
        }
        drawString(text, LEDMATRIX_SEGMENTS, 0, 0);
      }
    }
    if (boolDot == true) {
      boolDot = false;
      debugPrint(true, "DISPLAY DEBUG", "--DOT--", true, 2);
      if (dot == 0) {
        lmd.setPixel(15, 1, true);
        lmd.setPixel(15, 2, true);
        lmd.setPixel(15, 5, true);
        lmd.setPixel(15, 6, true);
        if (LEDMATRIX_SEGMENTS > 4) {
          lmd.setPixel(31, 1, false);
          lmd.setPixel(31, 2, false);
          lmd.setPixel(31, 5, false);
          lmd.setPixel(31, 6, false);
        }
        dot = 1;
      }
      else {
        lmd.setPixel(15, 1, false);
        lmd.setPixel(15, 2, false);
        lmd.setPixel(15, 5, false);
        lmd.setPixel(15, 6, false);
        if (LEDMATRIX_SEGMENTS > 4) {
          lmd.setPixel(31, 1, true);
          lmd.setPixel(31, 2, true);
          lmd.setPixel(31, 5, true);
          lmd.setPixel(31, 6, true);
        }
        dot = 0;
      }
      intensity = map(analogRead(A0), 0, 1023, 0, 10);
      intensity = constrain(intensity, 0, 10);
      lmd.setIntensity(intensity);
      lmd.display();
    }
  }
  TimerUpdate();
}

//-------------------------------------------------------------------------------------------
//dot timer callback
void updateDot(void) {
  boolDot = true;
}

//switch timer callback
void updateSwitch(void) {
  boolSwitch = true;
  debugPrint(true, "MARQUEE DEBUG", "--MARQUEE--", true, 2);
}

//marquee timer callback
void updateMarquee(void) {
  boolMarquee = true;
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  debugPrint(true, "JSON CONFIG", "SHOULD BE SAVED", true, 0);
  shouldSaveConfig = true;
}

// callback for incomming MQTT messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String top = topic;
  String incom;
  String incomMarquee;

  if (top == mqtt_message_topic) {
    repetitionsCounter = 0;

    for (int i = 0; i < length; i++) {
      incom += (char)payload[i];
    }

    if (getValue(incom, '*', 1) == "1") {
      getValue(incom, '*', 2).toCharArray(mqtt_only_mode, getValue(incom, '*', 2).length() + 1);

      for (int i = 0; i < LEDMATRIX_SEGMENTS - 1; i++) {
        incomMarquee += ' ';
      }
      incomMarquee += getValue(incom, '*', 3);
      incomMarquee.toCharArray(message, incomMarquee.length() + 1);
      messageSize = incomMarquee.length();

      REPETITIONS = 0;
    }
    else if ( getValue(incom, '*', 1) == "0") {

      PREVIOUS_REPETITIONS = REPETITIONS;
      REPETITIONS = getValue(incom, '*', 2).toInt();
      memcpy(mqtt_only_mode, "FALSE", 5);

      for (int i = 0; i < LEDMATRIX_SEGMENTS - 1; i++) {
        incomMarquee += ' ';
      }
      incomMarquee += getValue(incom, '*', 3);
      incomMarquee.toCharArray(message, incomMarquee.length() + 1);
      messageSize = incomMarquee.length();
    }
    else if (getValue(incom, '*', 1) == "2") {
      switch (REPETITIONS = getValue(incom, '*', 2).toInt()) {
        case 0:
          resetHandler("FACTORY");
          break;
        case 1:
          resetHandler("SPIFFS");
          break;
        case 2:
          resetHandler("WIFI");
          break;
      }

    }
    else {
      REPETITIONS = PREVIOUS_REPETITIONS;
      memcpy(mqtt_only_mode, "FALSE", 5);

      for (int i = 0; i < length; i++) {
        message[i + LEDMATRIX_SEGMENTS - 1] = (char)payload[i];
        messageSize = length + LEDMATRIX_SEGMENTS - 1;
      }
    }
    updateSwitch();
  }

  debugPrint(true, "MESSAGE TOPIC", topic, false, 0);
  debugPrint(false, "MESSAGE SIZE", String(messageSize), false, 0);
  debugPrint(false, "MESSAGE PAYLOAD", String(incom), true, 0);

  debugPrint(true, "PARSED MESSAGE", getValue(incom, '*', 1) + "--" + getValue(incom, '*', 2) + "--" + getValue(incom, '*', 3) + "--", false, 1);
  debugPrint(false, "FINAL MESSAGE", message, true, 1);
}

// clearing of mqtt message
void clearMessage(void) {
  for (int i = 0; i < messageSize - 1; i++) {
    message[i] = ' ';
  }
  messageSize = 0;
}

// checking if there is some mqtt message to marquee
bool checkMessage(void) {
  //  for (int i = 0; i < 19; i++) {
  //    if (message[i] != ' ')
  //      return true;
  //  }
  //  return false;
  if (messageSize > 0) {
    return true;
  }
  return false;
}

// formating and printing actual time to Serial
//void digitalClockDisplay() {
//  // digital clock display of the time
//  Serial.print(hour());
//  printDigits(minute());
//  printDigits(second());
//  Serial.print(" ");
//  Serial.print(day());
//  Serial.print(".");
//  Serial.print(month());
//  Serial.print(".");
//  Serial.print(year());
//  Serial.println();
//}

// formating of time
void printDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

// NTP time getter
time_t getNtpTime() {
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  debugPrint(false, "NTP", "TRANSMIT REQUEST", false, 0);
  sendNTPpacket();
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      debugPrint(false, "NTP", "RECEIVED RESPONSE", false, 0);
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      time_t utc = secsSince1900 - 2208988800UL;
      printDateTime(utc);
      debugPrint(false, "", " ---day light saving---> ", false, 0);
      printDateTime(CE.toLocal(utc, &tcr));
      debugPrint(false, "", "", true, 0);
      return CE.toLocal(utc, &tcr);
    }
  }
  debugPrint(false, "NTP", "NO RESPONSE", true, 0);
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
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(ntp_server_adress, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

// draw string to matrix display buffer
void drawString(char* text, int len, int x, int y ) {
  for ( int idx = 0; idx < len; idx ++ )
  {
    int c = text[idx] - 32;

    // stop if char is outside visible area
    if ( x + idx * 8  > LEDMATRIX_WIDTH )
      return;

    // only draw if char is visible
    if ( 8 + x + idx * 8 > 0 )
      drawSprite( font[c], x + idx * 8, y, 8, 8 );
  }
}

// draw sprite to matrix display buffer
void drawSprite( byte* sprite, int x, int y, int width, int height ) {
  // The mask is used to get the column bit from the sprite row
  byte mask = B10000000;

  for ( int iy = 0; iy < height; iy++ )
  {
    for ( int ix = 0; ix < width; ix++ )
    {
      lmd.setPixel(x + ix, y + iy, (bool)(sprite[iy] & mask ));

      // shift the mask by one pixel to the right
      mask = mask >> 1;
    }

    // reset column mask
    mask = B10000000;
  }
}

// update all timer objects
void TimerUpdate(void) {
  timerDot->Update();
  timerMarquee->Update();
}

// reconnect to WiFi
bool reconnectWiFi(void) {
  debugPrint(true, "SPIFFS", "MOUNTING FS", true, 1);
  if (SPIFFS.begin()) {
    debugPrint(true, "SPIFFS", "MOUNTED FS", true, 1);
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      debugPrint(true, "SPIFFS", "READING CONFIG FILE", true, 1);
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        if (DEBUG_LEVEL > 0)
          debugPrint(true, "SPIFFS", "OPENED CONFIG FILE", true, 1);
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (DEBUG_LEVEL > 1) {
          json.printTo(Serial);
          Serial.println();
        }
        if (json.success()) {
          debugPrint(true, "SPIFFS", "PARSED JSON", true, 1);
          drawString("FSOK  ", LEDMATRIX_SEGMENTS, 0, 0);
          lmd.display();

          strcpy(mqtt_server_adress, json["mqtt_server_adress"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_message_topic, json["mqtt_message_topic"]);
          strcpy(ntp_server_adress, json["ntp_server_adress"]);
          strcpy(offline_mode, json["offline_mode"]);

        } else {
          debugPrint(true, "SPIFFS", "FAILED TO LOAD JSON CONFIG", true, 0);
        }
      }
    }
  } else {
    debugPrint(true, "SPIFFS", "FAILED TO MOUNT FS", true, 0);
  }
  //end read
  drawString("WIFI  ", LEDMATRIX_SEGMENTS, 0, 0);
  lmd.display();

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

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
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

    if (atoi(number_of_display_segments) > 0) {
      LEDMATRIX_SEGMENTS = atoi(number_of_display_segments);
    }

    if (atoi(number_of_marquee_repetitions) > 0) {
      REPETITIONS = atoi(number_of_marquee_repetitions);
      PREVIOUS_REPETITIONS = REPETITIONS;
    }

    if (atoi(marquee_speed) > 0) {
      ANIM_DELAY = atoi(marquee_speed);
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    Serial.println();
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  debugPrint(true, "WIFI IP", WiFi.localIP().toString(), true, 0);
  debugPrint(true, "STARTING UDP PORT", String(localPort), true, 0);
  Udp.begin(localPort);

  WiFireconnectCounter++;

  if (WiFi.status() == WL_CONNECTED) {
    debugPrint(true, "WIFI", "CONNECTED", true, 0);
    return true;
  }
  else {
    debugPrint(true, "WIFI", "NOT CONNECTED", true, 0);
    debugPrint(true, "WIFI STATUS", String(WiFi.status()), true, 0);
    return false;
  }
}

// reconnect to MQTT
void reconnectMQTT(void) {
  // Loop until we're reconnected
  if (strcmp ("ONLINE", offline_mode) == 0) {
    debugPrint(true, "MQTT SERVER", String(mqtt_server_adress) + ":" + String(mqtt_port), false, 0);

    int port = atoi(mqtt_port);

    while (!mqtt.connected()) {
      debugPrint(false, "", "_*_", false, 0);
      String client_ID = "ESP-";
      client_ID += long(ESP.getChipId());
      char msg[20];
      client_ID.toCharArray(msg, client_ID.length() + 1);
      if (mqtt.connect(msg, mqtt_user, mqtt_password)) {
        mqtt.subscribe(mqtt_message_topic);
      }
      debugPrint(false, "", "", true, 0);
      delay(500);
      MQTTreconnectCounter ++;
    }
    debugPrint(true, "MQTT", "CONNECTED" , true, 0);
    if (MQTTreconnectCounter > (10000 / 500)) {
      reconnectWiFi();
    }
    if (WiFireconnectCounter > (10000 / 500)) {
      ESP.restart();
    }
  }
  else {
    debugPrint(true, "MQTT", "DISABLED", false, 0);
  }
}

void printDateTime(time_t t)
{
  Serial.print(hour(t));
  printDigits(minute(t));
  printDigits(second(t));

  Serial.print(" ");
  Serial.print(dayShortStr(weekday(t)));
  Serial.print("/");
  Serial.print(day(t));
  Serial.print("/");
  Serial.print(monthShortStr(month(t)));
  Serial.print("/");
  Serial.print(year(t));
}

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

void resetHandler(String type) {
  if (type == "FACTORY") {
    debugPrint(true, "RESET HANDLER", "---------PERFORMING FACTORY RESET!!!-------------------------", false, 0);
    SPIFFS.format();
    WiFiManager wifiManager;
    wifiManager.resetSettings();
  }
  if (type == "SPIFFS") {
    debugPrint(true, "RESET HANDLER", "---------FORMATTING SPIFFS!!!--------------------------------", false, 0);
    SPIFFS.format();
  }
  if (type == "WIFI") {
    debugPrint(true, "RESET HANDLER", "---------RESETING WIFI!!!------------------------------------", false, 0);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
  }
  debugPrint(true, "RESET HANDLER", "---------PERFORMING RESTART!!!-------------------------------", false, 0);
  debugPrint(true, "RESET HANDLER", "---------PLEASE DO MANUAL RESTART IF THIS HANGS--------------", false, 0);
  debugPrint(true, "RESET HANDLER", "---------THIS IS KNOWN PROBLEM ON ESP8266--------------------", false, 0);
  debugPrint(true, "RESET HANDLER", "---------AND SHOULD APPEAR ONLY ONCE AFTER FW UPLOAD---------", false, 0);

  //ESP.restart();

  pinMode(D0, OUTPUT);
  digitalWrite(D0, LOW);
}

bool debugPrint(bool t, String ID, String message, bool newline, int level) {
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 1
#endif

  if (DEBUG_LEVEL >= level) {
    if (t) {
      Serial.print("[");
      Serial.print(hour());
      printDigits(minute());
      printDigits(second());
      Serial.print(" ");
      Serial.print(day());
      Serial.print(".");
      Serial.print(month());
      Serial.print(".");
      Serial.print(year());
      Serial.print("]");
    }
    if (ID != "") {
      Serial.print("[");
      Serial.print(ID);
      Serial.print("][");
      Serial.print(message);
      Serial.print("]");
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
