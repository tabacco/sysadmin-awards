/*
OpenDNS Sysadmin Awards
by Dmitriy "Dima" Kumets, a product manager that vastly overestimates how much of his coding skills are still there.

Written using the Webduino server library
https://github.com/sirleech/Webduino
LCD library is for the 20x4 Sainsmart LCD screen. Included in product description at
http://www.amazon.com/SainSmart-Serial-Module-Shield-Arduino/dp/B0080DYTZQ/

What can you do?
  Send text to the LCD
  Change the LED color 
  Make the LED blink
  
If it doesn't work? 
  Have you tried turning it off and turning it back on again?
  email me :)
  Examples of what you can do:
 
 Hardware.
   - Arduino Uno
  - Arduino Ethernet shield
  - proto shield or breadboard
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

static uint8_t mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// static uint8_t mac[] = { 0x13, 0x37, 0xAC, 0xC0, 0x7A, 0xDE };
// Commented out for DHCP. Look in void setup() for the other change necessary for static IP
// IPAddress ip(192,168,1, 177);


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
boolean blinkflag = false;
boolean blinkstate = false;
boolean screenupdate = false;
// Lets define some strings, shall we?
String line1, line2,line3,line4;

int GreenBrightness, RedBrightness, BlueBrightness =0; // intensity of each color.
// Time variables for fade and blink. Set to 500ms. I am cheating by using the same interval and system clock for both.
// Using a sleep (delay) for blink and scroll would save us a lot of resources but would make web requests super slow
long lastBlinkMillis = 0;
long interval = 750;
// Flags - do we need to scroll these lines? If they're long the answer is yes!
boolean line1scroll,line2scroll,line3scroll,line4scroll = false;
// What is our position in each line for scrolling?
int line1pos,line2pos,line3pos,line4pos = 0;

// How big is your buffer space
#define NAMELEN 64
#define VALUELEN 64

// If  we wanted to be clever, we could define different paths for the pins and skip all the if/else logic. 
// However we would then we couldn't change the color in one url. 
// Why don't we just keep it at one URL?

#define PREFIX ""
WebServer webserver(PREFIX, 80);
// Function to ctually parse our values.
void Parseled(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete)
{
  server.httpSuccess();
   URLPARAM_RESULT rc;
  char name[NAMELEN];
  int  name_len;
  char value[VALUELEN];
  int value_len;
   if (type == WebServer::GET)
   {
     if (strlen(url_tail))
     {
       while (strlen(url_tail))
       {
         rc = server.nextURLparam(&url_tail, name, NAMELEN, value, VALUELEN);
         if (rc == URLPARAM_EOS)
         {
           // You may want to comment this out if you find your fellow admins trying to troll you with blinking and text.
  
         }

         else
         {
           
           if(strcmp(name,"red")==0)
           {
            RedBrightness = (int) strtoul(value,NULL,10);           
            analogWrite(RedPin,RedBrightness);
 /*           server.print("red intensity is:<br>");
            server.print(RedBrightness);
            server.print("<br><br>");
            */
           }
           else if(strcmp(name,"green")==0)
           {
             GreenBrightness = (int) strtoul(value,NULL,10);
            analogWrite(GreenPin,GreenBrightness);
   /*         server.print("green intensity is:<br>");
            server.print(GreenBrightness);
            server.print("<br><br>");
            */
           }
           else if(strcmp(name,"blue")==0)
           {
             BlueBrightness= (int) strtoul(value,NULL,10);
            analogWrite(BluePin, BlueBrightness);
            /*
            server.print("blue intensity is:<br>");
            server.print(BlueBrightness);
            server.print("<br><br>");
            */
           }
           else if(strcmp(name,"line1")==0)
           {
            line1=String(value);
            /*

            server.print("line 1 is:<br>");
            server.print(line1);
            server.print("<br><br>");
            */

            screenupdate = true;
           }
             else if(strcmp(name,"line2")==0)
           {
            line2=String(value);
            /*
             server.print("line 2 is:<br>");
       
            server.print(line2);
            server.print("<br><br>");
            */
            screenupdate = true;
           }
          else if(strcmp(name,"line3")==0)
           {
            line3=String(value);
            /*
            server.print("line 3 is:<br>");
            server.print(line3);
            server.print("<br><br>");
            */
            screenupdate = true;
           }
           else if(strcmp(name,"line4")==0)
           {
             line4=String(value);
/*
            server.print("line 4 is:<br>");
            server.print(line4);
            server.print("<br><br>");
            */
            screenupdate = true;
           }
           else if(strcmp(name,"blink")==0)
           {
            server.print("Blink is set to:<br>");
            server.print(value);
            if(strcmp(value,"1")==0)
            {
              blinkflag=true;
            }
            else if(strcmp(value,"0")==0)
            {
              blinkflag=false;
            }
           }
         }
       }
       server.print("</body></html>");
     }
   }
}

void setup()
{
  
  lcd.begin (20,4);
  
// Switch on the backlight
  lcd.setBacklightPin(BACKLIGHT_PIN,POSITIVE);
 /* initialize the Ethernet adapter */
  lcd.setBacklight(HIGH);
  lcd.clear();
  delay(100);
  lcd.home ();                   // go home
  lcd.print("Sysadmin Of the Year");
  lcd.setCursor(0,1);
  lcd.print("2012");
  lcd.setCursor(0,2);
  lcd.print("IT Pro Award");
// DHCP presumed
  Ethernet.begin(mac);
// Uncomment if you are using a static IP
// Ethernet.begin(mac,ip);

  pinMode(RedPin,OUTPUT);
  pinMode(GreenPin,OUTPUT);
  pinMode(BluePin,OUTPUT);

  webserver.begin();
  Serial.begin(9600);
  
  Serial.print("My IP address: ");
  lcd.setCursor(0,3);
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
   Serial.print(".");     
    lcd.print(Ethernet.localIP()[thisByte], DEC);
    lcd.print(".");

  }
  webserver.setDefaultCommand(&Parseled);
//  webserver.addCommand("index.html", &Parseled);
  webserver.begin();
  analogWrite(RedPin,0);
  analogWrite(GreenPin,0);
  analogWrite(BluePin,0  );
}


void loop()
{
 
  
  char buff[64];
  int len = 64;
  unsigned long currentMillis = millis();
  // Fetch the data and process it  
  webserver.processConnection(buff, &len);
  // Do we need to update the screen?
  if(screenupdate==true)
  {
    lcd.clear();
    // clear the screen and delay to let that command complete
    delay(200);
    Serial.println("Looking at line");
    Serial.println(line1.length());

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
  //    line1 = " " + line1;
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
 //     line2 = " " + line2;

    }
    if(line3.length()<21)
    {
      Serial.print(line3.length());
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
 //     line3 = " " + line3;

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
 //     line4 = " " + line4;
    }
    screenupdate = false;            
  }
  if(currentMillis - lastBlinkMillis >  interval)
  {
/*   Cheating. Using the same interval for blink and scroll
    */
    if(blinkflag == true)
    {
      if (blinkstate == false)
      {
        analogWrite(RedPin,RedBrightness);
        analogWrite(GreenPin,GreenBrightness);
        analogWrite(BluePin,BlueBrightness );
   //     lcd.setBacklight(HIGH);
        blinkstate = true;
      }
      else
      {
        analogWrite(RedPin,0);
        analogWrite(GreenPin,0);
        analogWrite(BluePin,0);
     //  lcd.setBacklight(LOW);

        blinkstate = false;
      }
    }
// line 1 scrolling
   if(line1scroll==true)
   {
  
 
     lcd.setCursor(0,0);
     delay(100);
     lcd.print(line1.substring(line1pos,line1pos+19));
     delay(50);
     if(line1pos > (line1.length()-20))
     {
       line1pos = 0;
     }
     else
     {
       line1pos +=1;
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

  
         
       
     
    

