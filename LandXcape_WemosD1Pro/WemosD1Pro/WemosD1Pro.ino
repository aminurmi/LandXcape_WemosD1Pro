/*
 * Wemos D1 Pro Sketch
 * 
 * Connects to a static WLAN configuration 
 * Allows to start, stop, send to base and furthermore to switch off and on the LandXcape lawn mower incl. activation with PIN
 * Furthmore supports Updates via WLAN via Web /updateLandXcape or via terminal -> curl -f "image=@firmware.bin" LandXcapeIPAddress/updateBin
 * 
 */
 
#include <TimeLib.h>
#include <Time.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>

//variables
const char* ssid     = "Linux_2";
const char* password = "linuxrulezz";
const int robiPinCode = 1881;
int baudrate = 115200;
int delayToReconnectTry = 15000;
boolean debugMode = false; //only useful as long as the WEMOS is connected to the PC ;)
boolean NTPUpdateSuccessful = false;
double version = 0.614;

int lastReadingSec=0;
int lastReadingMin=0;

int buttonPressTime = 700; //in ms
int PWRButtonPressTime = 2000; // in ms
int switchBetweenPinsDelay = 3000; // in ms

double A0reading = 0;
double batteryVoltage = 0;
double baseFor1V = 329.9479166;
double faktorBat = 9.322916;

double lowestBatVoltage = 0;
double highestBatVoltage = 0;
double lowestCellVoltage = 0;
double highestCellVoltage = 0;

double batterVoltageHistory [400];
int batVoltHistCounter = 0;

//admin variables
int lastXXminBatHist = 100;
const int maxBatHistValues = 400; // represents the max width of the statistic svg's
String svgBatHistGraph = "";

int STOP = D1;
int START = D3;
int HOME = D5;
int OKAY = D7;
int BATVOLT = A0;
int PWR = D6;

ESP8266WebServer wwwserver(80);
String content = "";

//Debug messages
String connectTo = "Connection to ";
String connectionEstablished = "WiFi connected: ";
String ipAddr = "IP Address: ";

void setup() {
  if (debugMode){
    Serial.begin(baudrate);
    delay(10);   
  }
  pinMode(LED_BUILTIN,OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); //LED off for now

  //WiFi Connection
  if (debugMode){
    Serial.println();
    Serial.println(connectTo + ssid);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (debugMode){
      Serial.print(WiFi.status());
    }
  }
  
  if (debugMode){
      Serial.println();
      Serial.println(connectionEstablished + ssid);
      Serial.println(ipAddr + WiFi.localIP().toString());
    }

  //Activate and configure Web-Server
  wwwserver.on("/", handleRoot);
  wwwserver.on("/updateLandXcape", handleWebUpdate);
  wwwserver.on("/updateBin", HTTP_POST, handleUpdateViaBinary, handleWebUpdateHelperFunction);
  wwwserver.on("/start", handleStartMowing);
  wwwserver.on("/stop", handleStopMowing);
  wwwserver.on("/goHome", handleGoHome);
  wwwserver.on("/stats",showStatistics);
  wwwserver.on("/configure", handleAdministration);
  wwwserver.on("/PWRButton", handleSwitchOnOff);
  wwwserver.on("/BatGraph.svg",drawGraphBasedOnBatValues);
  wwwserver.on("/newAdminConfiguration", HTTP_POST, computeNewAdminConfig);

  wwwserver.begin();
  if (debugMode){
    Serial.println("HTTP server started");
  }
  NTPUpdateSuccessful = syncTimeViaNTP();

  //initialize Digital Pins
  pinMode(STOP,OUTPUT); //Stop Button
  pinMode(START,OUTPUT); //Start Button
  pinMode(HOME,OUTPUT); //Home Button
  pinMode(OKAY,OUTPUT); //OK Button
  pinMode(BATVOLT,INPUT); //Battery Voltage sensing via Analog Input
  pinMode(PWR,OUTPUT);
  digitalWrite(STOP,HIGH);
  digitalWrite(START,HIGH);
  digitalWrite(HOME,HIGH);
  digitalWrite(OKAY,HIGH);
  digitalWrite(PWR,HIGH);

  //prepare / init statistics
  A0reading = analogRead(BATVOLT);
  A0reading = A0reading / baseFor1V;
  batteryVoltage = A0reading * faktorBat;

  lowestBatVoltage = batteryVoltage;
  highestBatVoltage = batteryVoltage;
  lowestCellVoltage = batteryVoltage/5;
  highestCellVoltage = batteryVoltage/5;

  //initialize SVG Graphics array with just now values
  for (int i=0;i<maxBatHistValues;i++){
    storeBatVoltHistory(batteryVoltage);
  }
  computeGraphBasedOnBatValues();
}

void loop() {
  
  //Webserver section
  wwwserver.handleClient(); 

  if (NTPUpdateSuccessful==false){
    NTPUpdateSuccessful = syncTimeViaNTP();
  }

  //update statistics every second
  if (lastReadingSec!=second()){

    double oldBatValue = batteryVoltage; //old value saved
    //new value read in
    A0reading = analogRead(BATVOLT); 
    A0reading = A0reading / baseFor1V;
    batteryVoltage = A0reading * faktorBat;
    
      if (oldBatValue != batteryVoltage) { //compute only if the reading has changed
        if (batteryVoltage > highestBatVoltage){
          highestBatVoltage = batteryVoltage;
          highestCellVoltage = batteryVoltage/5;
        }
        if (batteryVoltage < lowestBatVoltage){
          lowestBatVoltage = batteryVoltage;
          lowestCellVoltage = batteryVoltage/5;
        }
      }

      //store battery values every minute
      if (lastReadingMin!=minute()){

        storeBatVoltHistory(batteryVoltage);
        computeGraphBasedOnBatValues();
        lastReadingMin = minute();
      }
      
      lastReadingSec = second();
   if (debugMode){
      Serial.println((String)"Last Sensor Reading:"+hour()+":"+minute()+":"+second());
   } 
  }
}

static void handleRoot(void){

  if (debugMode){
    Serial.println((String)"Connection from outside established at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  digitalWrite(LED_BUILTIN, LOW); //show connection via LED  

  //preparation work
  char temp[1500];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  //Battery Voltage computing
  A0reading = analogRead(BATVOLT);
  A0reading = A0reading / baseFor1V;
  batteryVoltage = A0reading * faktorBat;
  
  snprintf(temp, 1500,

           "<html>\
            <head>\
              <title>LandXcape</title>\
              <style>\
                body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
              </style>\
              <meta http-equiv='Refresh' content='10; url=\\'>\
            </head>\
              <body>\
                <h1>LandXcape</h1>\
                <p>Uptime: %02d:%02d:%02d</p>\
                <p>UTC Time: %02d:%02d:%02d</p>\
                <p>Version: %02lf</p>\
                <p>Battery Voltage: %02lf</p>\
                <br>\
                <form method='POST' action='/start'><button type='submit'>Start</button></form>\
                <br>\
                <form method='POST' action='/stop'><button type='submit'>Stop</button></form>\
                <br>\
                <form method='POST' action='/goHome'><button type='submit'>go Home</button></form>\
                <br>\
                <form method='POST' action='/stats'><button type='submit'>Statistics</button></form>\ 
                <br>\
                <form method='POST' action='/configure'><button type='submit'>Administration</button></form>\ 
                <br>\
                <form method='POST' action='/PWRButton'><button type='submit'>Power Robi off / on</button></form>\
                <br>\
              </body>\
            </html>",

           hr, min % 60, sec % 60,
           hour(),minute(),second(),version,batteryVoltage
          );
  wwwserver.send(200, "text/html", temp);

  digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request  
}

/*
 * handleStartMowing triggers the Robi to start with his job :)
 */
static void handleStartMowing(void){

   if (debugMode){
    Serial.println((String)"Start with the Start relay for " + buttonPressTime + "followed with the OK button for the same time");
  }
   
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,LOW);//Press Okay Button
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release Okay Button

  if (debugMode){
    Serial.println((String)"Mowing started at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  char temp[700];
  snprintf(temp, 700,
           "<html>\
            <head>\
              <title>LandXcape</title>\
              <style>\
                body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
              </style>\
              <meta http-equiv='Refresh' content='2; url=\\'>\
            </head>\
              <body>\
                <h1>LandXcape</h1>\
                <p><\p>\
                <p>Mowing started at UTC Time: %02d:%02d:%02d</p>\
              </body>\
            </html>",
            hour(),minute(),second()
            );
  wwwserver.send(200, "text/html", temp);
}

/*
 * handleStopMowing triggers the Robi to stop with his job and to wait for further instructions :)
 */
static void handleStopMowing(void){
  
   if (debugMode){
    Serial.println((String)"Start with the Stop relay for " + buttonPressTime + " before releasing it again");
  }
   
   digitalWrite(STOP,LOW);//Press Stop Button 
   delay(buttonPressTime);
   digitalWrite(STOP,HIGH);//Release Stop Button 

   
  if (debugMode){
    Serial.println((String)"Mowing stoped at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  char temp[700];
  snprintf(temp, 700,
           "<html>\
            <head>\
              <title>LandXcape</title>\
              <style>\
                body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
              </style>\
              <meta http-equiv='Refresh' content='2; url=\\'>\
            </head>\
              <body>\
                <h1>LandXcape</h1>\
                <p><\p>\
                <p>Mowing stoped at UTC Time: %02d:%02d:%02d</p>\
              </body>\
            </html>",
            hour(),minute(),second()
            );
  wwwserver.send(200, "text/html", temp);
}

/*
 * handleGoHome triggers the Robi to go back to his station and to charge :)
 */
static void handleGoHome(void){
  
  if (debugMode){
    Serial.println((String)"Start with the Home relay for " + buttonPressTime + "followed with the OK button for the same time");
  }
   
   digitalWrite(HOME,LOW);//Press Home Button 
   delay(buttonPressTime);
   digitalWrite(HOME,HIGH);//Release Home Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,LOW);//Press Okay Button
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release Okay Button
   
  if (debugMode){
    Serial.println((String)"Robi sent home at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  char temp[700];
  snprintf(temp, 700,
           "<html>\
            <head>\
              <title>LandXcape</title>\
              <style>\
                body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
              </style>\
              <meta http-equiv='Refresh' content='2; url=\\'>\
            </head>\
              <body>\
                <h1>LandXcape</h1>\
                <p><\p>\
                <p>Mowing stoped and sent back to base at UTC Time: %02d:%02d:%02d</p>\
              </body>\
            </html>",
            hour(),minute(),second()
            );
  wwwserver.send(200, "text/html", temp);
}


/**
 * showStatistics shows all kind of statistics for the nerdy user ;)
 */

static void showStatistics(void){
 digitalWrite(LED_BUILTIN, LOW); //show connection via LED 
 
    if (debugMode){
      Serial.println((String)"showStatistics site requested at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
    }
    
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;
    double cellVoltage = batteryVoltage/5;
    
    char temp[2000];
    snprintf(temp, 2000,
             "<html>\
              <head>\
                <title>LandXcape Statistics</title>\
                <style>\
                  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
                </style>\
                <meta http-equiv='Refresh' content='2; url=\\stats'>\
              </head>\
                <body>\
                  <h1>LandXcape Statistics</h1>\
                  <p></p>\
                  <p>Uptime: %02d:%02d:%02d</p>\
                  <p>UTC Time: %02d:%02d:%02d</p>\
                  <p>Version: %02lf</p>\
                  \
                  <table style='width:90%'>\
                    <tr>\
                      <th><b>Battery:</b></th>\
                    </tr>\
                    <tr>\
                      <th>Actual voltage: %02lf</th>\
                      <th>Lowest voltage: %02lf</th>\
                      <th>Highest voltage: %02lf</th>\
                    </tr>\
                    <tr>\
                      <th><b>Cell:</b></th>\
                      <th></th>\
                      <th></th>\
                    </tr>\
                    <tr>\
                      <th>Actual voltage: %02lf</th>\
                      <th>Lowest voltage: %02lf</th>\
                      <th>Highest voltage: %02lf</th>\
                    </tr>\
                  </table>\
                  <p></p>\
                  <p><b>Battery history of the last %02dmin</b></p>\
                  <img src=\"/BatGraph.svg\" />\
                  <p></p>\
                  <form method='POST' action='/'><button type='submit'>Back to main menu</button></form>\
                </body>\
              </html>",
              hr, min % 60, sec % 60,hour(),minute(),second(),version,batteryVoltage,lowestBatVoltage,highestBatVoltage,cellVoltage,lowestCellVoltage,highestCellVoltage,lastXXminBatHist
              );
    wwwserver.send(200, "text/html", temp);

 digitalWrite(LED_BUILTIN, HIGH); //show connection via LED 
}

/*
 * handleAdministration allows to administrate your settings :)
 */
static void handleAdministration(void){

  digitalWrite(LED_BUILTIN, LOW); //show connection via LED 

    if (debugMode){
      Serial.println((String)"Administration site requested at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
    }
    char temp[1200];
    
    snprintf(temp, 1200,
             "<html>\
              <head>\
                <title>LandXcape</title>\
                <style>\
                  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
                </style>\
              </head>\
                <body>\
                  <h1>LandXcape Administration Site</h1>\
                  <p><\p>\
                  <form method='POST' action='/newAdminConfiguration'>\
                  Battery history: Show <input type='number' name='batHistMinShown' value='%02d' min=60 max=400> minutes<br>\                  
                  <input type='submit' value='Submit'></form>\
                  <form method='POST' action='/'><button type='submit'>Cancel</button></form>\
                  <p><\p>\
                  <br>\
                  <form method='POST' action='/updateLandXcape'><button type='submit'>SW Update via WLAN</button></form>\
                </body>\
              </html>",lastXXminBatHist
              );
    wwwserver.send(200, "text/html", temp);
    digitalWrite(LED_BUILTIN, HIGH); //show connection via LED 
}

/**
 * computeNewAdminConfig overwrites the present values by the given values from the user
 */
static void computeNewAdminConfig(void){
  digitalWrite(LED_BUILTIN, LOW); //show connection via LED 

    if(! wwwserver.hasArg("batHistMinShown") || wwwserver.arg("batHistMinShown") == NULL){ // Check if the POST request has crendentials and if they are correct
      wwwserver.send(400, "text/plain","400: Invalid Request"); // The request is invalid, so send HTTP status 400
      return;
    }

    //compute given values
    int batHistMinShown_ = wwwserver.arg("batHistMinShown").toInt();
    if (batHistMinShown_ >= 60 && batHistMinShown_ <=400){ //set it only if its valid
       lastXXminBatHist = batHistMinShown_;     
    }

    if (debugMode){
      Serial.println((String)"New Admin config transmitted at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
    }
    char temp[1200];
    snprintf(temp, 1200,
             "<html>\
              <head>\
                <title>LandXcape</title>\
                <style>\
                  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
                </style>\
                <meta http-equiv='Refresh' content='2; url=\\'>\
              </head>\
                <body>\
                  <h1>LandXcape - administration changes submited at UTC Time: %02d:%02d:%02d</h1>\
                  <p><\p>\
                  <p>LastXXminBatHist Variable changed to: %02d</p>\
              </body>\
            </html>",
            hour(),minute(),second(),lastXXminBatHist
            );
  wwwserver.send(200, "text/html", temp);
    digitalWrite(LED_BUILTIN, HIGH); //show connection via LED 
}

/*
 * handleSwitchOnOff allows to Power the robi on or off as you wish
 */
static void handleSwitchOnOff(void){

 if (debugMode){
    Serial.println((String)"handleSwitchOnOff triggered at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
   
   digitalWrite(PWR,LOW);//Press Home Button 
   delay(PWRButtonPressTime);
   digitalWrite(PWR,HIGH);//Release Home Button 
   delay(PWRButtonPressTime);
   
  char temp[700];
  snprintf(temp, 700,
           "<html>\
            <head>\
              <title>LandXcape</title>\
              <style>\
                body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
              </style>\
              <meta http-equiv='Refresh' content='4; url=\\'>\
            </head>\
              <body>\
                <h1>LandXcape</h1>\
                <p><\p>\
                <p>Robi switched off/on at UTC Time: %02d:%02d:%02d</p>\
              </body>\
            </html>",
            hour(),minute(),second()
            );
  wwwserver.send(200, "text/html", temp);

  enterPinCode();
}
/**
 * enterPinCode enters automatically the correct pin as statically given
 */
static void enterPinCode(void){
  if (debugMode){
    Serial.println((String)"enterPinCode triggered at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }

  //start with a delay before entering the pin code
  delay(3000);

  int fstNumber = robiPinCode / 1000;
  int sndNumber = (robiPinCode - fstNumber*1000) / 100;
  int trdNumber = (robiPinCode - (fstNumber*1000 + sndNumber*100)) / 10;
  int lstNumber = (robiPinCode - (fstNumber*1000 + sndNumber*100 + trdNumber*10)); 
 
  //set the PinCode first row
  for (int i=0; i!=fstNumber; i++){
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
  }

  //Switch to next pin
   digitalWrite(OKAY,LOW);//Press OK Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release OK Button 
   delay(buttonPressTime);

  //set the PinCode second row
  for (int i=fstNumber;  i!=sndNumber; i=(i+1)%10){
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
  }

   //Switch to next pin
   digitalWrite(OKAY,LOW);//Press OK Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release OK Button 
   delay(buttonPressTime);

  //set the PinCode third row
  for (int i=sndNumber; i!=trdNumber; i=(i+1)%10){
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
  }

   //Switch to last pin
   digitalWrite(OKAY,LOW);//Press OK Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release OK Button 
   delay(buttonPressTime);;

  //set the PinCode last row
  for (int i=trdNumber;  i!=lstNumber ; i=(i+1)%10){
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
  }

   //Confirm Pin Code
   digitalWrite(OKAY,LOW);//Press OK Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release OK Button 
   delay(buttonPressTime);

  if (debugMode){
    Serial.println((String)"enterPinCode finisched at UTC Time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
}

//Get the current time via NTP over the Internet
static boolean syncTimeViaNTP(void){
  
  //local one time variables
  WiFiUDP udp;
  int localPort = 2390;      // local port to listen for UDP packets
  IPAddress timeServerIP; // time.nist.gov NTP server address
  char* ntpServerName = "time.nist.gov";
  int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
  byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

    if (debugMode){
      Serial.println("Starting UDP");
    }
    udp.begin(localPort);
  
    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP);

    if (debugMode){
      Serial.println("sending NTP packet...");
    }
  
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
    udp.beginPacket(timeServerIP, 123); //NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();

    // wait for a sure reply or otherwise cancel time setting process
    delay(1000);
    int cb = udp.parsePacket();
    if (!cb){
       if (debugMode){
        Serial.println((String)"Time setting failed. Received Packets="+cb);
      }
      return false;
    }

    if (debugMode){
      Serial.println((String)"packet received, length="+ cb);
    }
        
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    
      // now convert NTP time into everyday time:
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      // subtract seventy years:
      unsigned long epoch = secsSince1900 - seventyYears;
      //set current time on device
      setTime(epoch);
      if (debugMode){
        Serial.println("Current time updated.");

        // print the hour, minute and second:
        Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
        Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
        Serial.print(':');
        if (((epoch % 3600) / 60) < 10) {
          // In the first 10 minutes of each hour, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
        Serial.print(':');
        if ((epoch % 60) < 10) {
          // In the first 10 seconds of each minute, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.println(epoch % 60); // print the second
      }
    
   udp.stop();
   return true;
}

static void handleWebUpdate(void){
  digitalWrite(LED_BUILTIN, LOW); //show connection via LED 

  //preparation work
  char temp[700];
  snprintf(temp, 700,
           "<html>\
  <head>\
    <title>LandXcape WebUpdate Site</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
    <body>\
      <h1>LandXcape WebUpdate Site</h1>\
      <p>UTC Time: %02d:%02d:%02d</p>\
      <p>Version: %02lf</p>\
           <form method='POST' action='/updateBin' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>\
           <form method='POST' action='/'><button type='submit'>Cancel</button></form>\
           </body> </html>",
           hour(),minute(),second(),version
          );
          
  //present website 
  wwwserver.send(200, "text/html", temp);

    if (debugMode){
          Serial.println("WebUpdate site requested...");
    }

  digitalWrite(LED_BUILTIN, HIGH); //show connection via LED 
}

static void handleUpdateViaBinary(void){
  digitalWrite(LED_BUILTIN, LOW); //show working process via LED 
  if (debugMode){
          Serial.println("handleUpdateViaBinary function triggered...");
  }

  wwwserver.sendHeader("Connection", "close");
  wwwserver.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
  if (debugMode){
          Serial.println("handleUpdateViaBinary function finished...");
  }
  
  digitalWrite(LED_BUILTIN, HIGH); //show working process via LED 
}

static void handleWebUpdateHelperFunction (void){
  digitalWrite(LED_BUILTIN, LOW); //show working process via LED 

  HTTPUpload& upload = wwwserver.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);

      //send the user back to the main page
        char temp[700];
        snprintf(temp, 700,
               "<html>\
                <head>\
                  <title>LandXcape</title>\
                  <style>\
                    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
                  </style>\
                  <meta http-equiv='Refresh' content='5; url=\\'>\
                </head>\
                  <body>\
                    <h1>LandXcape</h1>\
                    <p><\p>\
                    <p>Update successfull at UTC Time: %02d:%02d:%02d</p>\
                  </body>\
                </html>",
                hour(),minute(),second()
                );
      wwwserver.send(200, "text/html", temp);
          
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();

  digitalWrite(LED_BUILTIN, HIGH); //show working process via LED 
}

/**
 * batterVoltageHistory helper function to store battery values (max 400 entries <->maxBatHistValues)
 */

static void storeBatVoltHistory (double actualBatVolt){

  if (batVoltHistCounter<maxBatHistValues){
    batterVoltageHistory[batVoltHistCounter] = actualBatVolt;
  }else{
    batVoltHistCounter=0; //loopstorage / ringbuffer
    batterVoltageHistory[batVoltHistCounter] = actualBatVolt;
  }
  
  batVoltHistCounter++;
}

/**
 * drawGraphBasedOnBatValues as a SVG Graphics 
 * SVG is computed only once every minute and stored as a String for presentation
 */
void drawGraphBasedOnBatValues(void) {
  wwwserver.send(200, "image/svg+xml", svgBatHistGraph);
}

/**
 * computeGraphBasedOnBatValues as a SVG Graphics
 */
void computeGraphBasedOnBatValues(void) {

  svgBatHistGraph = ""; //delete old SVG Graphics
  char temp[100];
  
  svgBatHistGraph += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"155\">\n";
  svgBatHistGraph += "<rect width=\"400\" height=\"155\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
  svgBatHistGraph += "<g stroke=\"black\">\n";

  int counter = batVoltHistCounter%maxBatHistValues;
  
  //compute amplifier
  //highestBatVoltage - lowestBatVoltage = z -> modifcation number for the amplifier
  double z=highestBatVoltage-lowestBatVoltage;
  
  if (z<=0.01){ //to prevent a division by zero after powering up with no changes to the battery
    z=5;  //lowest Bat value ~ 16V max 21V -> 5V span between max and min bat voltage
  }
  double amplifier = 150/z;

  int y = (batterVoltageHistory[counter]-lowestBatVoltage)*amplifier;

  //compute dot width based on configured lastXXminBatHist (initial 100min)
  double dotWidth = (double)maxBatHistValues/(double)lastXXminBatHist; //take care of the .xx part after the casting

  //and adapt counter to show only the selected amount of values
  if (lastXXminBatHist!=maxBatHistValues) { // if max size (400pix) != gewählte minuten Anzahl
    counter = (counter+(maxBatHistValues-lastXXminBatHist))%maxBatHistValues;
  }

  for (double x = 0; x < (double)maxBatHistValues; x=x+dotWidth) { 
  
    int y2 = (batterVoltageHistory[counter%maxBatHistValues]-lowestBatVoltage)*amplifier;
    
    sprintf(temp, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke-width=\"1\" />\n", (int)x, 150-y, (int)(x + dotWidth), 150-y2);
    svgBatHistGraph += temp;
    y = y2;
    counter++;
  }
  svgBatHistGraph += "</g>\n</svg>\n";

}
 
