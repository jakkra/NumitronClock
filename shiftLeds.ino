
#include <stdlib.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h> // https://github.com/adafruit/RTClib 

/* ########################### Defines ########################### */
#define NUM_LEDS 6
#define STRIP_PIN 6
#define DEBUG true
#define BUFFER_SIZE 128
#define DEC2BCD(dec) (((dec / 10) << 4) + (dec % 10))

/* ########################### RTC ########################### */
RTC_DS3231 rtc;

/* ########################### ESP ########################### */
char buffer[BUFFER_SIZE];
SoftwareSerial esp8266(2,3);

/* ########################### TPIC6B595 ########################### */
const int shiftInterval = 1000;
unsigned long previousShiftMillis = 0;
boolean isClockRunning = true;
boolean initialClockStart = true;

//Pin connected to ST_CP of TPIC6B595
int latchPin = 8;
//Pin connected to SH_CP of TPIC6B595
int clockPin = 13;
//Pin connected to DS of TPIC6B595
int dataPin = 11;
//Pin connected to G of TPIC6B595
int G = 5;

/* ########################### LEDS ########################### */
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, STRIP_PIN, NEO_RGB + NEO_KHZ800);
byte rgbLEDState = LOW;
int brightness = 30;
uint32_t currentColor = strip.Color(0, 40, 255);
uint32_t prevColor = strip.Color(255, 100, 100);
uint8_t leds = 0x01;

typedef enum {
  NONE, COUNT, RAINBOW
} color_mode_t;
  
color_mode_t colorMode = COUNT;
uint8_t countModeCounter = 0;

/* ########################### Numitrons ########################### */
//holders for infromation you're going to pass to shifting function
byte dataLSB = 0x00;
byte dataMSB = 0x00;

/* ########################### Other ########################### */
unsigned long currentMillis = 0;    // stores the value of millis() in each iteration of loop()
uint16_t count = 0;


void setup() {
  Serial.begin(115200);
  
  initNumitron();
  initRTC();
  initESP();
  initWSBLeds();

}

void loop() {
  currentMillis = millis();
  doShift();
  doESP();
  parseSerial();
}


/* ########################### ESP Wi-Fi ########################### */

void initESP() {
  esp8266.begin(115200);
  sendData("AT+RESTORE\r\n",4000,DEBUG); // reset module
  sendData("AT+UART_DEF=9600,8,1,0,0\r\n",4000,DEBUG); // reset module
  esp8266.flush(); // wait for last transmitted data to be sent 
  esp8266.begin(9600);
  sendData("AT+CWMODE=3\r\n",2000,DEBUG); // configure as access point
  
  sendData("AT+CIPMUX=1\r\n",3000,DEBUG); // configure for multiple connections
  sendData("AT+CIPSERVER=1,80\r\n",1000,DEBUG); // turn on server on port 80
  sendData("AT+CWJAP=\"lolololol\",\"yourWifiPassword\"\r\n",7000,DEBUG); // connect Wi-Fi
  sendData("AT+CIFSR\r\n",3000,DEBUG); // Get IP
}

void doESP() {
  int ch_id, packet_len;
  char *pb;  
  if(esp8266.available())
  {
    esp8266.readBytesUntil('\n', buffer, BUFFER_SIZE);
    if(strncmp(buffer, "+IPD,", 5) == 0) {
      // request: +IPD,ch,len:data
      sscanf(buffer+5, "%d,%d", &ch_id, &packet_len);

      if(packet_len > 0){
        pb = buffer+5;
        while(*pb!=':') pb++;
        pb++;
        if (strncmp(pb, "GET /on", 7) == 0) {
          enableClock();
          sendOK(ch_id);
          
        } else if (strncmp(pb, "GET /off", 8) == 0) {
          disableClock();
          sendOK(ch_id);
          
        } else if (strncmp(pb, "GET /setTime/", 10) == 0) { // /setTime/hh:mm:ss
          parseNewTime(pb+13);
          sendOK(ch_id);
          
        } else if (strncmp(pb, "GET /setColor/", 14) == 0) { //setColor/r:g:b
          parseAndSetRGB(pb + 14);
          sendOK(ch_id);

        } else if (strncmp(pb, "GET /setMode/", 13) == 0) {
          parseAndSetMode(pb + 13);
          sendOK(ch_id);

        } else if (strncmp(pb, "GET /brightnessUp", 16) == 0) {
          setBrightness(brightness + 25); 
          sendOK(ch_id);
          
        } else if (strncmp(pb, "GET /brightnessDown", 18) == 0) {
          setBrightness(brightness - 25); 
          sendOK(ch_id);

        } else if (strncmp(pb, "GET /", 5) == 0) {
          Serial.println("YAY got /homepage");
          sendOK(ch_id);
          
        } else {
          sendOK(ch_id);
        }
      }
      
    }
     clearBuffer();
    }
}


void sendOK(int ch_id) {
  String webpage = "<HTML>\
  <BODY>\
  OK\
  </BODY>\
  </HTML>";
  
  String cipSend = "AT+CIPSEND=";
  cipSend += ch_id;
  cipSend += ",";
  cipSend +=webpage.length();
  cipSend +="\r\n";
  
  sendData(cipSend,1000,DEBUG);
  sendData(webpage,1000,DEBUG);
  
  
  String closeCommand = "AT+CIPCLOSE="; 
  closeCommand+=ch_id; // append connection id
  closeCommand+="\r\n";
  
  sendData(closeCommand,3000,DEBUG);
}

void parseNewTime(char* pb) {

  int h;
  sscanf(pb, "%d:", &h);
  int m;
  sscanf(pb + 2, ":%d:", &m);
  pb[8] = '\0';
  int s = atoi(pb + 6);

  DateTime now = rtc.now();
  DateTime newTime = DateTime(now.year(), now.month(), now.day(), h, m, s);
  rtc.adjust(newTime);
}

String sendData(String command, const int timeout, boolean debug) {
    String response = "";
    esp8266.print(command); // send the read character to the esp8266
    long int time = millis();

    while((time+timeout) > millis()) {
      while(esp8266.available()) {
        // The esp has data so display its output to the serial window 
        char c = esp8266.read(); // read the next character.
        response+=c;
      }  
    }
    if(debug)
    {
      Serial.print(response);
    }
    return response;
}

void clearSerialBuffer(void) {
  while ( esp8266.available() > 0 ) {
    esp8266.read();
  }
}

void clearBuffer(void) {
  for (int i =0;i<BUFFER_SIZE;i++ ) {
    buffer[i]=0;
  }
}

/* ########################### Numitron Shifting ########################### */

void initNumitron() {
  pinMode(latchPin, OUTPUT);
  pinMode(G, OUTPUT);
  digitalWrite(G, 1);
  digitalWrite(latchPin, 1);

}

byte getSegments(int nbr) {
  switch(nbr) {
    case 0:
      //edgfacb0
      //98765432
      return B01111011;
    case 1:
      return B01100000;
    case 2:
      return B01010111;
    case 3:
      return B01110110;
    case 4:
      return B01101100;
    case 5:
      return B00111110;
    case 6:
      return B00111111;
    case 7:
      return B01110000;
    case 8:
      return B01111111;
    case 9:
      return B01111110;
  }
}

void doShift() {
    if (currentMillis - previousShiftMillis >= shiftInterval && isClockRunning) {
      previousShiftMillis += shiftInterval;
      DateTime now = getTime();
      dataLSB = count;
      dataMSB = (byte) (count >> 8); ;

      if (initialClockStart == true) {
        //ground latchPin and hold low for as long as you are transmitting
        digitalWrite(latchPin, 0);
        digitalWrite(G, 1); // Hold output
        shiftOut(dataPin, clockPin, 0);
        shiftOut(dataPin, clockPin, 0);
        shiftOut(dataPin, clockPin, 0);
        shiftOut(dataPin, clockPin, 0);
        shiftOut(dataPin, clockPin, 0);
        shiftOut(dataPin, clockPin, 0);
        
        digitalWrite(latchPin, 1);
        digitalWrite(G, 0); // Show new output    
            
        digitalWrite(latchPin, 0);
        digitalWrite(G, 1); // Hold output
        
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.second()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.second()) >> 4));
        
        digitalWrite(latchPin, 1);
        digitalWrite(G, 0); // Show new output
        delay(1000);
        doMode();
        digitalWrite(latchPin, 0);
        digitalWrite(G, 1); // Hold output
        
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.minute()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.minute()) >> 4));
        
        digitalWrite(latchPin, 1);
        digitalWrite(G, 0); // Show new output
        delay(1000);
        doMode();
        digitalWrite(latchPin, 0);
        digitalWrite(G, 1); // Hold output
        
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.hour()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.hour()) >> 4));
        
        digitalWrite(latchPin, 1);
        digitalWrite(G, 0); // Show new output
        delay(1000);
        doMode();
        initialClockStart = false;
      } else {
        //ground latchPin and hold low for as long as you are transmitting
        digitalWrite(latchPin, 0);
        digitalWrite(G, 1); // Hold output
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.second()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.second()) >> 4));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.minute()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.minute()) >> 4));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.hour()) & 0x0F));
        shiftOut(dataPin, clockPin, getSegments(DEC2BCD(now.hour()) >> 4));
        //return the latch pin high to signal chip that it 
        //no longer needs to listen for information
        digitalWrite(latchPin, 1);
        digitalWrite(G, 0); // Show new output
      }
      
      doMode();
      count++;
    }
}

void disableClock(){
  isClockRunning = false;
  digitalWrite(latchPin, 0);
  digitalWrite(G, 1); //Hold the output

  shiftOut(dataPin, clockPin, 0);
  shiftOut(dataPin, clockPin, 0);
  shiftOut(dataPin, clockPin, 0);
  shiftOut(dataPin, clockPin, 0);
  shiftOut(dataPin, clockPin, 0);
  shiftOut(dataPin, clockPin, 0);
  
  //return the latch pin high to signal chip that it 
  //no longer needs to listen for information
  digitalWrite(latchPin, 1);
  digitalWrite(G, 0); // Show new output
}

void enableClock(){
  initialClockStart = true;
  isClockRunning = true;
}


void shiftOut(int myDataPin, int myClockPin, byte myDataOut) {
  // This shifts 8 bits out MSB first, 
  //on the rising edge of the clock,
  //clock idles low
  pinMode(myClockPin, OUTPUT);
  pinMode(myDataPin, OUTPUT);

  int i = 0;
  int pinState;

  //clear everything out just in case to
  //prepare shift register for bit shifting
  digitalWrite(myDataPin, 0);
  digitalWrite(myClockPin, 0);

  for (i=7; i>=0; i--)  {
    digitalWrite(myClockPin, 0);

    //if the value passed to myDataOut and a bitmask result 
    // true then... so if we are at i=6 and our value is
    // %11010100 it would the code compares it to %01000000 
    // and proceeds to set pinState to 1.
    if (myDataOut & (1<<i)) {
      pinState= 1;
    }
    else {  
      pinState= 0;
    }

    //Sets the pin to HIGH or LOW depending on pinState
    digitalWrite(myDataPin, pinState);
    //register shifts bits on upstroke of clock pin  
    digitalWrite(myClockPin, 1);
    //zero the data pin after shift to prevent bleed through
    digitalWrite(myDataPin, 0);
  }

  //stop shifting
  digitalWrite(myClockPin, 0);
}


/* ########################### Time keeping ########################### */

void initRTC() {
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  DateTime now = rtc.now();
  DateTime compiled = DateTime(__DATE__, __TIME__);
  if (now.unixtime() < compiled.unixtime()) {
    Serial.println("RTC is older than compile time! Updating");
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }
}

DateTime getTime() {
  return rtc.now();
}


void parseSerial() {
  if (Serial.available()){
    String cmd = Serial.readStringUntil(':');
    if(cmd.equals("brightness")){
      int brightness = Serial.readStringUntil('\n').toInt();
      setBrightness(brightness);
    } else if(cmd.equals("rgb")){
      setColor(parseRGB());
    } else if(cmd.equals("mode")){
      String mode = Serial.readStringUntil(':');
      int speed = Serial.readStringUntil('\n').toInt();
      setMode(mode, speed);
    } else if(cmd.equals("brightnessUp")){
      setBrightness(brightness + 25); 
    } else if(cmd.equals("brightnessDown")){
      setBrightness(brightness - 25);
    }
  }
}

/* ########################### WSB LEDS ########################### */

void initWSBLeds() {
  strip.begin();
  strip.setBrightness(brightness);
  strip.show();
  colorWipe(currentColor, 250);
}

void parseAndSetRGB(char* pb) {
  int r, g, b;
  sscanf(pb, "%d:%d:%d", &r, &g, &b);

  uint32_t c = strip.Color(r, g, b);
  prevColor = currentColor;
  currentColor = c;
  if (colorMode == NONE){
      colorWipe(c, 100);
  }
}

void parseAndSetMode(char* pb) {
  if (strncmp(pb, "count", 5) == 0) {
    colorMode = COUNT;
  } else if (strncmp(pb, "none", 4) == 0) {
    colorMode = NONE;
    colorWipe(currentColor, 0);
  }
}

void blinkRGBLeds() {
  if (rgbLEDState == LOW) {
    rgbLEDState = HIGH;
    colorWipe(currentColor, 0);
    
  } else {
    rgbLEDState = LOW;
    colorWipe(strip.Color(0, 0, 0), 0);
  }
}

void setColor(uint32_t c){
  prevColor = currentColor;
  currentColor = c;
  if (colorMode == NONE){
      colorWipe(c, 100);
  }
}

void setMode(String mode, int speed){
  if(mode.equals("rainbow")){
    rainbow(speed);    
  } else if(mode.equals("count")){
    mode = COUNT;
  } else if(mode.equals("theaterChaseRainbow")){
    theaterChaseRainbow(speed);
  }
}

void setPixelColor( uint16_t n, uint32_t c) {
   strip.setPixelColor(n, c);
}

uint32_t parseRGB(){
  int r = Serial.readStringUntil(':').toInt();
  int g = Serial.readStringUntil(':').toInt();
  int b = Serial.readStringUntil(':').toInt();
  return strip.Color(r, g, b);
}

void setBrightness(int b){
  int bri = b;
  if(bri > 255){
    bri = 255;
  } else if(bri < 1){
    bri = 10;
  }
  brightness = bri;
  strip.setBrightness(bri);
  strip.show();
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

void doMode(){
  if (colorMode == COUNT) {
    for (uint16_t i = 0; i < countModeCounter; i++) {
      setPixelColor(i, prevColor);
    }
    countModeCounter++;
    if (countModeCounter > 6){
      countModeCounter = 1;
      uint32_t tempColor = currentColor;
      currentColor = prevColor;
      prevColor = tempColor;
    }
    strip.show();
  }
}

/* ########################### LED Modes ###########################
  Modes are not modified to work in "parallell" with displaying time and such.
  This is TODO
*/

void rainbow(uint8_t wait) {
  uint16_t i, j;

  for(j=0; !Serial.available(); j++) {
    for(i=0; i<strip.numPixels(); i++) {
      setPixelColor(i, Wheel((i+j%255) & 255));
      if(Serial.available()) return;
    }
    strip.show();
    delay(wait);
  }
}

void rainbowCycle(uint8_t wait) {
  uint16_t i, j;

  for(j=0; !Serial.available(); j++) {
    for(i=0; i< strip.numPixels(); i++) {
      setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + (j % 255*5)) & 255));
    }
    strip.show();
    delay(wait);
  }
}

void theaterChase(uint32_t c, uint8_t wait) {
  for (int j=0; j<10; j++) {
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        setPixelColor(i+q, c);
      }
      if(Serial.available()) return;
      strip.show();

      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        setPixelColor(i+q, 0);
      }
    }
  }
}

void theaterChaseRainbow(uint8_t wait) {
  for (int j=0; j < 256; j++) {
    for (int q=0; q < 3; q++) {
      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        setPixelColor(i+q, Wheel( (i+j) % 255));
      }
      strip.show();
      if(Serial.available()) return;
      delay(wait);

      for (uint16_t i=0; i < strip.numPixels(); i=i+3) {
        setPixelColor(i+q, 0);
      }
    }
  }
}



// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}
