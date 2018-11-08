/*
   Time_NTP.pde
   Example showing time sync to NTP time source

   This sketch uses the ESP8266WiFi library
*/

#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Timezone.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

#include <LEDMatrixDriver.hpp>
// Define the ChipSelect pin for the led matrix (Dont use the SS or MISO pin of your Arduino!)
// Other pins are arduino specific SPI pins (MOSI=DIN of the LEDMatrix and CLK) see https://www.arduino.cc/en/Reference/SPI
const uint8_t LEDMATRIX_CS_PIN = D3;

// Define LED Matrix dimensions (0-n) - eg: 32x8 = 31x7
const int LEDMATRIX_WIDTH = 31;
const int LEDMATRIX_HEIGHT = 7;
const int LEDMATRIX_SEGMENTS = 4;

// The LEDMatrixDriver class instance
LEDMatrixDriver lmd(LEDMATRIX_SEGMENTS, LEDMATRIX_CS_PIN);

// NTP Servers:
//IPAddress timeServer; // time.nist.gov NTP server address
const char* ntpServerName = "europe.pool.ntp.org";

WiFiUDP Udp;
//unsigned int localPort = 8888;      // local port to listen for UDP packets
unsigned int localPort = 2390;      // local port to listen for UDP packets

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);

TimeChangeRule *tcr;

void setup()
{
  pinMode(A0,INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial) ; // Needed for Leonardo only
  delay(250);
  Serial.println("MatrixClock");
  WiFiManager wifiManager;
  wifiManager.autoConnect("MatrixClock");
  Serial.println("connected...yeey :)");


  Serial.print("IP number assigned by DHCP is ");
  Serial.println(WiFi.localIP());
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  delay(1000);
  setSyncProvider(getNtpTime);

  lmd.setEnabled(true);
  lmd.setIntensity(10);   // 0 = low, 10 = high
}

int x = 0, y = 0; // start top left

// This is the font definition. You can use http://gurgleapps.com/tools/matrix to create your own font or sprites.
// If you like the font feel free to use it. I created it myself and donate it to the public domain.
byte font[95][8] = { {0, 0, 0, 0, 0, 0, 0, 0}, // SPACE
  {0x10, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18}, // EXCL
  {0x28, 0x28, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, // QUOT
  {0x00, 0x0a, 0x7f, 0x14, 0x28, 0xfe, 0x50, 0x00}, // #
  {0x10, 0x38, 0x54, 0x70, 0x1c, 0x54, 0x38, 0x10}, // $
  {0x00, 0x60, 0x66, 0x08, 0x10, 0x66, 0x06, 0x00}, // %
  {0, 0, 0, 0, 0, 0, 0, 0}, // &
  {0x00, 0x10, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00}, // '
  {0x02, 0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04}, // (
  {0x40, 0x20, 0x10, 0x10, 0x10, 0x10, 0x10, 0x20}, // )
  {0x00, 0x10, 0x54, 0x38, 0x10, 0x38, 0x54, 0x10}, // *
  {0x00, 0x08, 0x08, 0x08, 0x7f, 0x08, 0x08, 0x08}, // +
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x08}, // COMMA
  {0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00}, // -
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}, // DOT
  {0x00, 0x04, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40}, // /
  {0x38, 0x44, 0x4c, 0x5c, 0x74, 0x64, 0x44, 0x38}, // 0
  {0x04, 0x0c, 0x14, 0x24, 0x04, 0x04, 0x04, 0x04}, // 1
  {0x78, 0x44, 0x04, 0x04, 0x3c, 0x40, 0x40, 0x7c}, // 2
  {0x7c, 0x44, 0x04, 0x3c, 0x04, 0x04, 0x44, 0x7c}, // 3
  {0x40, 0x40, 0x40, 0x48, 0x48, 0x7c, 0x08, 0x08}, // 4
  {0x7c, 0x40, 0x40, 0x78, 0x04, 0x04, 0x04, 0x78}, // 5
  {0x38, 0x44, 0x40, 0x78, 0x44, 0x44, 0x44, 0x38}, // 6
  {0x7c, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x10}, // 7
  {0x38, 0x44, 0x44, 0xF8, 0x38, 0x44, 0x44, 0x38}, // 8
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
  // (the font does not contain any lower case letters. you can add your own.)
};    // {}, //

// Marquee speed
const int ANIM_DELAY = 500;

// Marquee text
int h = 00;
int m = 00;
int s = 00;
int prevS = 00;

char text[] = "0000";
bool yps = false;

int len = strlen(text);

time_t prevDisplay = 0; // when the digital clock was displayed

void loop()
{
  lmd.setIntensity(map(analogRead(A0),0,1023,0,10));   // 0 = low, 10 = high
  
  String textStr = "";

  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      digitalClockDisplay();

      h = hour();
      m = minute();
      s = second();
    }
  }

   if (yps == false) { //time
    if (h < 10) {
      textStr += String(0);
    }
    textStr += String(h);
    if (m < 10) {
      textStr += String(0);
    }
    textStr += String(m);
  }
  else {  //seconds
    textStr = "  " + String(s);
    if (s < 10)
      textStr = "   " + String(s);

  }

  for (int i = 0; i < 4; i++) {
    text[i] = textStr[i];
  }

  // Draw the text to the current position
  drawString(text, len, 0, 0);
  // In case you wonder why we don't have to call lmd.clear() in every loop: The font has a opaque (black) background...


  if (yps == false) { //time
    if (x == 0) {
      lmd.setPixel(15, 1, true);
      lmd.setPixel(15, 2, true);
      lmd.setPixel(15, 5, true);
      lmd.setPixel(15, 6, true);
      x = 1;
    }
    else {
      x = 0;
    }
  }


  // Toggle display of the new framebuffer
  lmd.display();

  // Wait to let the human read the display
  delay(ANIM_DELAY);

  // // Advance to next coordinate
  // if( --x < len * -8 )
  //   x = LEDMATRIX_WIDTH;
}

void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();
}



void printDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket();
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //printTime(CE.toLocal(utc, &tcr), tcr -> abbrev, "Bratislava");
      //return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
      return CE.toLocal(secsSince1900 - 2208988800UL, &tcr);
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(void)
{
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
  Udp.beginPacket(ntpServerName, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void drawString(char* text, int len, int x, int y )
{
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

void drawSprite( byte* sprite, int x, int y, int width, int height )
{
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
