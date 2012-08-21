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

Note: The maximum length of the query string is 128 bytes due to memory limitations.
Exceeding 128 bytes will cause an HTTP 500 error to be returned.

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
#define AWARD "Neat Freak Award"

// "Factory Defaults!"
#define DEFAULT_RED 0
#define DEFAULT_GREEN 0
#define DEFAULT_BLUE 0
#define DEFAULT_BLINK false
#define DEFAULT_BACKLIGHT true
#define DEFAULT_LINE1 "SysAdmin of the Year"
#define DEFAULT_LINE2 "2012"
#define DEFAULT_LINE3 AWARD
#define DEFAULT_LINE4 ""

// Buffer lengths
#define NAMELEN 10
#define VALUELEN 64
#define QSBUFFERLEN 128

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

// Lets define some strings, shall we?
String line1, line2, line3, line4;

// intensity of each color.
int GreenBrightness, RedBrightness, BlueBrightness = 0;

// Backlight state
boolean backlight;

// Time variables for fade and blink. Set to 500ms. I am cheating by using the same interval and system clock for both.
// Using a sleep (delay) for blink and scroll would save us a lot of resources but would make web requests super slow
long lastBlinkMillis = 0;
long interval = 750;

// Flags - do we need to scroll these lines? If they're long the answer is yes!
boolean line1scroll,line2scroll,line3scroll,line4scroll = false;

// What is our position in each line for scrolling?
int line1pos,line2pos,line3pos,line4pos = 0;

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


P(Sidebar) = "<div class=\"span-one-third\"><h3>Congratulations on Winning!</h3>"
  "<p>Hi there, contest winner! Congratulations on your victory. You're one of the best of the best, so we hope you'll enjoy this custom-built trophy. You can "
  "customize the text on the LCD screen or the color of the LED by using the form on the left, or by passing the appropriate query string parameters from a "
  "script of your own devising.</p>"
  "<p>The trophy is powered by an Arduino Uno, and the source is available on <a href=\"http://github.com/opendns\">Github</a>. You are, of course, welcome to "
  "fork the original source or write something completely new if you like. Have fun, and congratulations again on winning!</p>"
  "</div>";

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
            line1=String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line2") == 0)
          {
            line2=String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line3") == 0)
          {
            line3=String(value);
            screenupdate = true;
          }
          else if(strcmp(name, "line4") == 0)
          {
            line4=String(value);
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
    printInput(server, Line1Name, line1, Line1Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line2Name, line2, Line2Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line3Name, line3, Line3Label, true);
    server.printP(FormClose);

    server.printP(FormOpen);
    printInput(server, Line4Name, line4, Line4Label, true);
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
   line1 = String(DEFAULT_LINE1);
   line2 = String(DEFAULT_LINE2);
   line3 = String(DEFAULT_LINE3);
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
  line1 = String(lineBuf);
  offset += LINE_BYTES;

  eeprom_read_block(lineBuf, (const void*)offset, LINE_BYTES);
  line2 = String(lineBuf);
  offset += LINE_BYTES;
  
  eeprom_read_block(lineBuf, (const void*)offset, LINE_BYTES);
  line3 = String(lineBuf);
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
  
  line1.toCharArray(lineBuf, LINE_BYTES);
  eeprom_write_block(lineBuf, (void*)offset, LINE_BYTES);
  offset += LINE_BYTES;
  
  line2.toCharArray(lineBuf, LINE_BYTES);
  eeprom_write_block(lineBuf, (void*)offset, LINE_BYTES);
  offset += LINE_BYTES;
  
  line3.toCharArray(lineBuf, LINE_BYTES);
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
  
  lcd.begin(20, 4);

  // Switch on the backlight
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  /* initialize the Ethernet adapter */

  setLcdBacklight();

  pinMode(RedPin, OUTPUT);
  pinMode(GreenPin, OUTPUT);
  pinMode(BluePin, OUTPUT);

  if(Ethernet.begin(mac) == 1)
  { 
    line4 = "";
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      
      // print the value of each byte of the IP address:
      line4 += String(Ethernet.localIP()[thisByte]);
      
      if(thisByte < 3)
      {
        line4 += ".";
      }
    }
    Serial.print("My IP address: ");
    Serial.println(line4);
  }
  else
  {
    line4 = "No IP Address";
    Serial.println("No IP Address");
  }
  
  screenUpdate();
  setLedBrightness();

  webserver.begin();  
  webserver.setDefaultCommand(&processHttpRequest);

}

void screenUpdate()
{
    Serial.println(line1);
    Serial.println(line2);
    Serial.println(line3);
    Serial.println(line4);
  
    lcd.clear();
    // clear the screen and delay to let that command complete
    delay(200);
    
    // If the line fits, print it and sleep for 50ms before trying to update next line. Otherwise use the scroll code below
    if(line1.length()<21)
    {
      delay(50);
      lcd.setCursor (0, 0);  
      delay(50);  
      lcd.print(line1);
      delay(50);
      line1scroll = false;
    }
    else
    { 
      line1scroll = true;
      line1pos = 0;
    }

    if(line2.length()<21)
    {
      delay(50);
      lcd.setCursor (0, 1);  
      delay(50);
      lcd.print(line2);
      line2scroll = false;
    }
    else
    { 
      line2scroll = true;
      line2pos = 0;
    }
    if(line3.length()<21)
    {
      delay(50);
      lcd.setCursor (0, 2);
      delay(50);
      lcd.print(line3);
      line3scroll = false;

    }
    else
    { 
      line3scroll = true;
      line3pos = 0;
    }

    if(line4.length()<21)
    { 
      lcd.setCursor (0, 3);    
      delay(50);
      lcd.print(line4);
      delay(50);
      line4scroll= false;
    }
    else
    { 
      line4scroll = true;
      line4pos = 0;
    }

    screenupdate = false; 
}

// Fake a soft reset by reloading the default values
void softReset()
{
  loadDefaults();
  screenupdate = true;
  screenUpdate();
  setLedBrightness();
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
    
    // line 1 scrolling
    if(line1scroll==true)
    {


      lcd.setCursor(0, 0);
      delay(100);
      lcd.print(line1.substring(line1pos, line1pos + 19));
      delay(50);
      if(line1pos > (line1.length() - 20))
      {
        line1pos = 0;
      }
      else
      {
        line1pos += 1;
      }
    }
    
    // line 2 scrolling
    if(line2scroll==true)
    {
      lcd.setCursor(0,1);
      lcd.print(line2.substring(line2pos,line2pos+19));
      if(line2pos > (line2.length()-20))
      {
        line2pos = 0;     
        delay(50);
        lcd.print("          ");
        delay(50);
      }
      else
      {
        line2pos +=1;
      }

    }
    
    // line 3 scrolling
    if(line3scroll==true)
    {
      delay(50);
      lcd.setCursor(0,2);
      delay(50);
      lcd.print(line3.substring(line3pos,line3pos+19));
      if(line3pos > (line3.length()-20))
      {
        line3pos = 0;
        delay(50);
        lcd.print("          ");
        delay(50);
      }
      else
      {
        line3pos +=1;
      }

    }
    
    // line 4 scrolling - geez, Why didn't I make this into a function?
    if(line4scroll==true)
    {

      lcd.setCursor(0,3);
      lcd.print(line4.substring(line4pos,line4pos+19));

      if(line4pos > (line4.length()-20))
      {
        line4pos = 0;
        delay(50);
        lcd.print("          ");
        delay(50);
      }
      else
      {
        line4pos +=1;
      }

    }

    lastBlinkMillis = currentMillis;

  }
}
