/*
OpenDNS Sysadmin Awards
 by Dmitriy "Dima" Kumets, a product manager that vastly overestimates how much of his coding skills are still there.
 and Doug "Doug" Tabacco
 
 Written using the Webduino server library
 https://github.com/sirleech/Webduino
 LCD library is for the 20x4 Sainsmart LCD screen. Included in product description at
 http://www.amazon.com/SainSmart-Serial-Module-Shield-Arduino/dp/B0080DYTZQ/
 
 What can you do?

 Send text to the LCD
   Query string parameters: line1, line2, line3, line4
 Change the LED color 
   Query String parameters: red=[0-255], green=[0-255], blue=[0-255]
 Make the LED blink
   Query string parameter: blink=[0|1]
 Enable/Disable the LCD backlight
   Query string parameter: backlight=[0|1]
 Write the current state to the EEPROM to use as startup defaults
   Query string parameter: defaults
 Clear the EEPROM defaults and return to the award's default state
   Query string parameter: reset

Note: The maximum length of the query string is 96 bytes due to memory limitations.
Exceeding 96 bytes will cause an HTTP 500 error to be returned.

If it doesn't work? 
  Have you tried turning it off and turning it back on again?
  Submit an issue on Github!


 Hardware.
 - Arduino Uno
 - Arduino Ethernet shield
 - proto shield for itnerface
 Pins:
 D3 -> 100 ohm resistor -> Blue LED pin
 D5 -> 100 ohm resistor -> Green LED pin
 D6 -> 100 ohm resistor -> Red LED pin
 GND -> LED ground pin
 GND -> LCD GND
 +5V -> LCD VCC
 A4 -> LCD SDA
 A5 -> LCD SCL
 
 */

#include <SPI.h>
#include <avr/pgmspace.h> 
#include <Ethernet.h>
#include <WebServer.h>
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <avr/eeprom.h>

// The name of the award category this trophy is for
#define RECIPIENT "Joe SysAdmin"
#define AWARD "Award Category"

// "Factory Defaults!"
#define DEFAULT_RED 255
#define DEFAULT_GREEN 255
#define DEFAULT_BLUE 255
#define DEFAULT_BLINK false
#define DEFAULT_BACKLIGHT true
#define DEFAULT_LINE1 RECIPIENT
#define DEFAULT_LINE2 AWARD
#define DEFAULT_LINE3 "2012 SysAdmin Award"
#define DEFAULT_LINE4 ""

// Buffer lengths
#define NAMELEN 10
#define VALUELEN 40
#define QSBUFFERLEN 96

#define LCDWIDTH 20
#define LCDHEIGHT 4

#define LINE_BYTES VALUELEN

static uint8_t mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Address and pins for the I2C LCD
// Don't mess with these unless you are replacing the LCD
#define I2C_ADDR    0x27  // I2C bus address
#define BACKLIGHT_PIN     3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7

LiquidCrystal_I2C	lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin);

// Define PWM (faux analog) Led pins used in the super sysadmin
int RedPin = 6;
int BluePin = 3;
int GreenPin = 5;

// flags for blinking and lcd update trigger
boolean blinkflag, blinkstate, screenupdate, writeDefaults, clearDefaults, hasDefaults = false;

String lines[LCDHEIGHT];

// intensity of each color.
int GreenBrightness, RedBrightness, BlueBrightness = 0;

// Backlight state
boolean backlight;

// Time variables for fade and blink. Set to 500ms. I am cheating by using the same interval and system clock for both.
// Using a sleep (delay) for blink and scroll would save us a lot of resources but would make web requests super slow
long lastBlinkMillis = 0;
long interval = 750;

// Track line scroll positions
int linePos[LCDHEIGHT] = {0};

// Handy data for building the web form
P(PageOpen) = "<!DOCTYPE html><html lang=\"en\"><head><title>2012 Sysadmin Award Trophy</title>"
  "<link href=\"http://twitter.github.com/bootstrap/1.4.0/bootstrap.min.css\" rel=\"stylesheet\"></head>"
  "<body><div class=\"container\"><div class=\"page-header\"><h1>2012 SysAdmin Award Trophy <small>";
  
P(PageOpenMid) = "</small></h1></div>";
P(PageClose) = "</div></body></html>";

P(RowOpen) = "<div class=\"row\">";
P(MainColOpen) = "<div class=\"span-two-thirds\">";

P(TextLegend) = "<legend>LCD Display Text</legend>";
P(OptionsLegend) = "<legend>Misc. Options</legend>";
P(ColorsLegend) = "<legend>LED Color</legend>";

P(FormOpen) = "<form method=\"GET\"><fieldset>";
P(FormClose) = "</fieldset></form>";

P(ActionsOpen) = "<div class=\"actions\"><input type=\"submit\" class=\"btn primary\" value=\"Save Settings\">";
P(SaveDefaultsButton) = " <input type=\"submit\" class=\"btn\" name=\"defaults\" value=\"Save as Defaults\">";
P(ClearDefaultsButton) = " <a href=\"/?reset=1\" class=\"btn danger\">Clear Defaults</a>";
P(SetTextButton) = " <input type=\"submit\" class=\"btn primary\" value=\"Set Line Text\">";


P(Sidebar) = "<div class=\"span-one-third\"><h3>Congratulations, Elite SysAdmin!</h3>"
  "<p>Being named as a 2012 OpenDNS SysAdmin Award winner is a huge achievement, and we're honored to recognize "
  "you as best in class. This custom-built trophy was created for you by members of the OpenDNS engineering team "
  "to celebrate your greatness. You can customize the text on the LCD screen or the color of the LED by using the "
  "form on the left. Or, you can pass the appropriate query string parameters from a script you write yourself.</p>"
  "<p>The trophy is powered by an Arduino Uno, and the source is available on "
  "<a href=\"http://github.com/opendns\">Github</a>. You are welcome to fork the original source or write something "
  "completely new. All we ask is that you take some time to enjoy your new trophy/toy and celebrate your well-"
  "deserved victory. And once again, a hearty congratulations from our team.</p></div>";

P(ClearfixOpen) = "<div class=\"clearfix\">";
P(DivClose) = "</div>";

P(LabelOpen) = "<label>";
P(LabelClose) = "</label>";

P(InputOpen) = "<div class=\"input\"><input type=\"text\" class=\"xlarge\" name=\"";
P(InputMid) = "\" value=\"";
P(InputClose) = "\">";


P(BlinkOpen) = "<label>LED Blink</label> ";
P(BacklightOpen) = "<label>LCD Backlight</label> ";

// labels!
P(RedName) = "red";
P(RedLabel) = "Red Value (0-255)";
P(GreenName) = "green";
P(GreenLabel) = "Green Value (0-255)";
P(BlueName) = "blue";
P(BlueLabel) = "Blue Value (0-255)";
P(Line1Name) = "line1";
P(Line1Label) = "Line 1 Text";
P(Line2Name) = "line2";
P(Line2Label) = "Line 2 Text";
P(Line3Name) = "line3";
P(Line3Label) = "Line 3 Text";
P(Line4Name) = "line4";
P(Line4Label) = "Line 4 Text";

// other stuff
P(ErrorURITooLong) = "The query string is too long.";

// If  we wanted to be clever, we could define different paths for the pins and skip all the if/else logic. 
// However we would then we couldn't change the color in one url. 
// Why don't we just keep it at one URL?
#define PREFIX ""
WebServer webserver(PREFIX, 80);

// Function to actually parse our values.
void processHttpRequest(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete)
{
  // Bail out if we didn't get the whole query string
  if(!tail_complete)
  {
    server.httpServerError();
    server.printP(ErrorURITooLong);
    return;
  }
  
  URLPARAM_RESULT rc;
  char name[NAMELEN];
  char value[VALUELEN];
    
  if (type == WebServer::GET)
  {
    if (strlen(url_tail))
    {
      while (strlen(url_tail))
      {
        rc = server.nextURLparam(&url_tail, name, NAMELEN, value, VALUELEN);
        if (rc != URLPARAM_EOS)
        {
          if(strcmp(name, "red") == 0)
          {
            RedBrightness = (int) min(strtoul(value, NULL, 10), 255);
          }
          else if(strcmp(name, "green") == 0)
          {
            GreenBrightness = (int) min(strtoul(value, NULL, 10), 255);
          }
          else if(strcmp(name, "blue") == 0)
          {
            BlueBrightness= (int) min(strtoul(value, NULL, 10), 255);
          }
          else if(strcmp(name, "line1") == 0)
          {
            lines[0] = String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line2") == 0)
          {
            lines[1] = String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line3") == 0)
          {
            lines[2] = String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line4") == 0)
          {
            lines[3] = String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "blink") == 0)
          {
            blinkflag = (strcmp(value, "1") == 0);
          }
          else if(strcmp(name, "backlight") == 0)
          {
            backlight = (strcmp(value, "0") != 0);
          }
          else if(strcmp(name, "defaults") == 0)
          {
            writeDefaults = true;
            hasDefaults = true;
          }
          else if(strcmp(name, "reset") == 0)
          {
            clearDefaults = true;
            hasDefaults = false;
          }
          
        } 
      }
            
      // Cleanup tasks after form processing
      setLedBrightness();
      setLcdBacklight();
    }
    
    server.httpSuccess();

    server.printP(PageOpen);
    server.print(AWARD);
    server.printP(PageOpenMid);
    
    server.printP(RowOpen);
    server.printP(MainColOpen);

    server.printP(FormOpen);
    server.printP(TextLegend);
    printInput(server, Line1Name, lines[0], Line1Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line2Name, lines[1], Line2Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line3Name, lines[2], Line3Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line4Name, lines[3], Line4Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    server.printP(ColorsLegend);

    printInput(server, RedName, String(RedBrightness), RedLabel, false);
    printInput(server, GreenName, String(GreenBrightness), GreenLabel, false);
    printInput(server, BlueName, String(BlueBrightness), BlueLabel, false);
    server.printP(ActionsOpen);
    server.printP(SaveDefaultsButton);
    if(hasDefaults)
    {
      server.printP(ClearDefaultsButton);
    }
    server.printP(DivClose); // end actions
    server.printP(FormClose);

    server.printP(FormOpen);

    server.printP(OptionsLegend);

    server.printP(ClearfixOpen);
    server.printP(BlinkOpen);
    server.radioButton("blink", "0", "Off ", !blinkflag);
    server.radioButton("blink", "1", "On", blinkflag);
    server.printP(DivClose);

    server.printP(ClearfixOpen);
    server.printP(BacklightOpen);
    server.radioButton("backlight", "0", "Off ", !backlight);
    server.radioButton("backlight", "1", "On", backlight);
    server.printP(DivClose);

    server.printP(ActionsOpen);
    server.printP(SaveDefaultsButton);
    if(hasDefaults)
    {
      server.printP(ClearDefaultsButton);
    }
    server.printP(DivClose); // end actions
    server.printP(FormClose);

    server.printP(DivClose); // end main column

    server.printP(Sidebar);
    
    server.printP(DivClose); // end row
    server.printP(PageClose);
    
  }

}

void printInput(WebServer &server, const prog_uchar *name, String value, const prog_uchar *label, boolean inline_submit)
{
  server.printP(ClearfixOpen);
  server.printP(LabelOpen);
  server.printP(label);
  server.printP(LabelClose);
  server.printP(InputOpen);
  server.printP(name);
  server.printP(InputMid);
  server.print(value);
  server.printP(InputClose);
  if(inline_submit)
  {
    server.printP(SetTextButton);
  }
  server.printP(DivClose);
  server.printP(DivClose);
}

void loadDefaults()
{
  // The first byte is used to determine if we've stored any defaults in the EEPROM
  hasDefaults = (eeprom_read_byte(0) == 1);
  
  // If it's not 1, use the const 'factory' defaults
  if(hasDefaults == false) {
   lines[0] = String(DEFAULT_LINE1);
   lines[1] = String(DEFAULT_LINE2);
   lines[2] = String(DEFAULT_LINE3);
   RedBrightness = DEFAULT_RED;
   GreenBrightness = DEFAULT_GREEN;
   BlueBrightness = DEFAULT_BLUE;
   blinkflag = DEFAULT_BLINK;
   backlight = DEFAULT_BACKLIGHT;
   return; 
  }
  
  // offset is the pointer to the current address we're reading in the 
  int offset = 1;

  // Read in the line data from eeprom
  char lineBuf[LINE_BYTES];

  eeprom_read_block(lineBuf, (const void*)offset, LINE_BYTES);
  lines[0] = String(lineBuf);
  offset += LINE_BYTES;

  eeprom_read_block(lineBuf, (const void*)offset, LINE_BYTES);
  lines[1] = String(lineBuf);
  offset += LINE_BYTES;
  
  eeprom_read_block(lineBuf, (const void*)offset, LINE_BYTES);
  lines[2] = String(lineBuf);
  offset += LINE_BYTES;
  
  free(lineBuf);
  
  RedBrightness = eeprom_read_byte((const uint8_t*)offset);
  offset++;
  
  GreenBrightness = eeprom_read_byte((const uint8_t*)offset);
  offset++;
  
  BlueBrightness = eeprom_read_byte((const uint8_t*)offset);
  offset++;

  blinkflag = (eeprom_read_byte((const uint8_t*)offset) == 1);
  offset++;

  backlight = (eeprom_read_byte((const uint8_t*)offset) == 1);
}

void saveDefaults()
{
  eeprom_write_byte(0, 1);
  hasDefaults = true;
  
  int offset = 1;
  
  // Read in the line data from eeprom
  char lineBuf[LINE_BYTES];
  
  lines[0].toCharArray(lineBuf, LINE_BYTES);
  eeprom_write_block(lineBuf, (void*)offset, LINE_BYTES);
  offset += LINE_BYTES;
  
  lines[1].toCharArray(lineBuf, LINE_BYTES);
  eeprom_write_block(lineBuf, (void*)offset, LINE_BYTES);
  offset += LINE_BYTES;
  
  lines[2].toCharArray(lineBuf, LINE_BYTES);
  eeprom_write_block(lineBuf, (void*)offset, LINE_BYTES);
  offset += LINE_BYTES;
  free(lineBuf);
  
  eeprom_write_byte((uint8_t*)offset, RedBrightness);
  offset++;
  
  eeprom_write_byte((uint8_t*)offset, GreenBrightness);
  offset++;
  
  eeprom_write_byte((uint8_t*)offset, BlueBrightness);
  offset++;
  
  eeprom_write_byte((uint8_t*)offset, blinkflag ? 1 : 0);
  offset++;
  
  eeprom_write_byte((uint8_t*)offset, backlight ? 1 : 0);
  }

void clearSavedDefaults()
{
  if(eeprom_read_byte(0) == 1)
  {
    eeprom_write_byte(0, 0);
  }
  hasDefaults = false;
}

void setLedBrightness() 
{
  analogWrite(RedPin, RedBrightness);
  analogWrite(GreenPin, GreenBrightness);
  analogWrite(BluePin, BlueBrightness);
}

void setLcdBacklight()
{
  lcd.setBacklight(backlight ? HIGH : LOW);
}

void setup()
{
  Serial.begin(9600);

  // Load default values for the award from EEPROM or defined constants
  loadDefaults();
  
  lcd.begin(LCDWIDTH, LCDHEIGHT);

  // Switch on the backlight
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  /* initialize the Ethernet adapter */

  setLcdBacklight();

  pinMode(RedPin, OUTPUT);
  pinMode(GreenPin, OUTPUT);
  pinMode(BluePin, OUTPUT);
  
  lines[3] = "...waiting for DHCP";

  setLedBrightness();
  screenUpdate();

  if(Ethernet.begin(mac) == 1)
  { 
    lines[3] = "";
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      
      // print the value of each byte of the IP address:
      lines[3] += String(Ethernet.localIP()[thisByte]);
      
      if(thisByte < 3)
      {
        lines[3] += ".";
      }
    }
    Serial.print("My IP address: ");
    Serial.println(lines[3]);
  }
  else
  {
    lines[3] = "No IP Address";
    Serial.println("No IP Address");
  }
  
  screenUpdate();

  webserver.begin();  
  webserver.setDefaultCommand(&processHttpRequest);

}

void screenUpdate()
{
    Serial.println(lines[0]);
    Serial.println(lines[1]);
    Serial.println(lines[2]);
    Serial.println(lines[3]);
  
    lcd.clear();
    // clear the screen and delay to let that command complete
    delay(200);
    
    for(int i = 0; i < LCDHEIGHT; i++) {
      // If the line fits, print it and sleep for 50ms before trying to update next line. Otherwise use the scroll code below
      if(!lineShouldScroll(lines[i]))
      {
        delay(50);
        lcd.setCursor (0, i);  
        delay(50);  
        lcd.print(lines[i]);
        delay(50);
      }
      else
      {
        linePos[i] = 0;
      }
    }
    
    screenupdate = false; 
}

void loop()
{
  unsigned long currentMillis = millis();
  char buff[QSBUFFERLEN];
  int len = QSBUFFERLEN;
  
  // Fetch the data and process it  
  webserver.processConnection(buff, &len);
  
  if(writeDefaults == true)
  {
    saveDefaults();
    writeDefaults = false;
  }
  else if(clearDefaults == true)
  {
    clearSavedDefaults();
    clearDefaults = false;
  }

  // Do we need to update the screen?
  if(screenupdate==true)
  {
    screenUpdate();           
  }
  
  if(currentMillis - lastBlinkMillis >  interval)
  {
    /*   Cheating. Using the same interval for blink and scroll
     */
    if(blinkflag == true)
    {
      if (blinkstate == false)
      {
        analogWrite(RedPin, RedBrightness);
        analogWrite(GreenPin, GreenBrightness);
        analogWrite(BluePin, BlueBrightness );
        blinkstate = true;
      }
      else
      {
        analogWrite(RedPin, 0);
        analogWrite(GreenPin, 0);
        analogWrite(BluePin, 0);
        blinkstate = false;
      }
    }
    
    for(int i = 0; i < LCDHEIGHT; i++)
    {
      if(lineShouldScroll(lines[i]))
      {
        lcd.setCursor(0, i);
        if(linePos[i] > (lines[i].length() - LCDWIDTH))
        {
          linePos[i] = 0;
        }
        delay(50);
        lcd.print(lines[i].substring(linePos[i], linePos[i] + LCDWIDTH));
        delay(50);
        
        linePos[i]++;
      }
    }
    
    lastBlinkMillis = currentMillis;

  }
}

boolean lineShouldScroll(String line) {
  return line.length() > LCDWIDTH;
}
