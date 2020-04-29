/*
 * Wemos D1 Pro Sketch
 * 
 * Connects to a static WLAN configuration 
 * Allows to start, stop, send to base and furthermore to switch off and on the LandXcape lawn mower incl. activation with PIN
 * Furthmore supports Updates via WLAN via Web /updateLandXcape or via terminal -> curl -f "image=@firmware.bin" LandXcapeIPAddress/updateBin
 * 
 */
#include <FS.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>

//variables
const char* ssid     = "Linux_2";
const char* password = "linuxrulezz";
const int robiPinCode = 1881;
int baudrate = 115200;
int debugMode = 1; //0 = off, 1 = moderate debug messages, 2 = all debug messages
boolean onBoardLED = false; //(de)activates the usage of the onboard LED

boolean NTPUpdateSuccessful = false;
double version = 0.66012; //System: Battery Voltage meassurement slow dow -> measurement is now only every 5 seconds done, 
//System: WLAN status check implemented -> checks connection to DHCP Server every 30 seconds, if no longer available, disconnects and reconnects actively

int lastReadingSec=0;
int lastReadingMin=0;

int buttonPressTime = 500; //in ms
int PWRButtonPressTime = 1700; // in ms
int switchBetweenPinsDelay = 2500; // in ms

double A0reading = 0;
double A1reading = 0;
double batteryVoltage = 0;
double batteryVoltFactor = 0.0285119047619;

double lowestBatVoltage = 0;
double highestBatVoltage = 0;
double lowestCellVoltage = 0;
double highestCellVoltage = 0;

double batterVoltageHistory [600];
int batVoltHistCounter = 0;

boolean robiAtHome = false;
boolean robiOnTheWayHome = false;
boolean isCharging = false;
boolean hasCharged = false;
int hasChargedDelay = 6; //stabilize charging detection
boolean raining = false;
const int rainingDelay = 20; //in minutes delay after rain has been detected
int rainingDelay_ = rainingDelay; //delay counter to subtract from

int rainSensorShortcutTime = 10000; // 10sek shortcut the LandXcape Rain sensor cable with our relay since it only periodically checks for rain
boolean rainSensorResults[10]; //boolean array for the rain sensor checks and its past values
int rainSensorCounter = 0;

//admin variables
int lastXXminBatHist = 100;
const int maxBatHistValues = 600; // represents the max width of the statistic svg's
boolean earlyGoHome = false;
double earlyGoHomeVolt = 17.0;
boolean forwardRainInfoToLandXcape = false;
boolean ignoreRain = false;
int dailyTasks = -1;
boolean allDayMowing = false; //lawn mowing from sunrise to sunset
const int maxLogEntries = 50;
const int maxLogLength = 130;
char logRotateBuffer [maxLogEntries][maxLogLength]; // 130*50*1byte je character = ~6.5kb (max) size
int logRotateCounter = 0; 

int STOP = D1;
int START = D3;
int HOME = D5;
int OKAY = D7;
int BATVOLT = A0;
int PWR = D6;
int REGENSENSOR_LXC = D2;
int REGENSENSOR_WEMOS = D4;

ESP8266WebServer wwwserver(80);

char true_[] = "true";
char false_[] = "false";
boolean showWebsite = true;

//Debug messages
char connectTo []= "Connection to ";
char connectionEstablished [] = "WiFi connected: ";
char ipAddr [] = "IP Address: ";

//SunriseSunset variables -> https://www.zamg.ac.at/cms/de/klima/klimauebersichten/ephemeriden/graz/?jahr=2019
//or have a look here for the algorithm -> https://www.instructables.com/id/Calculating-Sunset-and-Sunrise-for-a-Microcontroll/
int earliest_sunrise = 303;//5:03am in minutes after midnight => 303min at Graz
int latest_sunrise = 464;//7:44am
int earliest_sunset = 969; // 16:09
int latest_sunset = 1258; //20:58
boolean SummerTimeActive = true;
int sunrise = -1; //init value
int sunset = -1; //init value

int UTCtimezone = 1;
boolean timeAdjusted = false;

//Filesystem variables
const char* batGraph = "/data/BatGraph.svg";
const char* adminSite = "/data/adminSite.html";
const char* CSSFile = "/data/style.css";

/**
 * Initialization process - setup ()
 */
void setup() {

  //initialize filesystem
  fs::SPIFFSConfig filesystem_cfg; // to overcome the current SPIFFS "bug" in 2.5.2
  filesystem_cfg.setAutoFormat(false);
  SPIFFS.setConfig(filesystem_cfg);
  SPIFFS.begin();

  //initialize rainSensorResults buffer
  for(int i=0; i<10;i++){
    rainSensorResults[i]=true;
  }

  if (debugMode>=1){
    Serial.begin(baudrate);
    delay(10);   
  }
 
  pinMode(LED_BUILTIN,OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); //LED off for now

  //WiFi Connection
  if (debugMode>=1){
    Serial.println((String)"[setup]"+connectTo + ssid);
    writeDebugMessageToInternalLog((String)"[setup]"+connectTo + ssid);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    if (debugMode>=2){
      Serial.println((String)"[setup]"+"WiFi Status Code:"+WiFi.status());
      writeDebugMessageToInternalLog((String)"[setup]"+"WiFi Status Code:"+WiFi.status());
    }
  }
  
  if (debugMode>=1){
      Serial.println("[setup]");
      Serial.println((String)"[setup]"+connectionEstablished + ssid);
      writeDebugMessageToInternalLog((String)"[setup]"+connectionEstablished + ssid);
      Serial.println((String)"[setup]"+ipAddr + WiFi.localIP().toString());
      writeDebugMessageToInternalLog((String)"[setup]"+ipAddr + WiFi.localIP().toString());
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
  wwwserver.on("/resetWemos",resetWemosBoard);
  wwwserver.on("/logFiles",presentLogEntriesFromInternalLog);

  wwwserver.begin();
  if (debugMode>=1){
    Serial.println("[setup]HTTP server started");
    writeDebugMessageToInternalLog((String)"[setup]HTTP server started");
  }
  NTPUpdateSuccessful = syncTimeViaNTP();

  //initialize Digital Pins
  pinMode(STOP,OUTPUT); //Stop Button
  pinMode(START,OUTPUT); //Start Button
  pinMode(HOME,OUTPUT); //Home Button
  pinMode(OKAY,OUTPUT); //OK Button
  pinMode(BATVOLT,INPUT); //Battery Voltage sensing via Analog Input
  pinMode(PWR,OUTPUT);
  pinMode(REGENSENSOR_LXC,OUTPUT);
  pinMode(REGENSENSOR_WEMOS,INPUT); // will be switched on every 10 seconds
  digitalWrite(STOP,HIGH);
  digitalWrite(START,HIGH);
  digitalWrite(HOME,HIGH);
  digitalWrite(OKAY,HIGH);
  digitalWrite(PWR,HIGH);
  digitalWrite(REGENSENSOR_LXC,HIGH);

  //prepare / init statistics
  //take smalest one of 20 readings to minimize jumping / noise -> WLAN is maybe the main coarse -> https://github.com/esp8266/Arduino/issues/2070
  A0reading = analogRead(BATVOLT);
  for (int i=0;i<20;i++){
    A1reading = analogRead(BATVOLT);
    if (A1reading < A0reading){
      A0reading = A1reading;
    }
  }

  batteryVoltage = A0reading * batteryVoltFactor;

  lowestBatVoltage = batteryVoltage;
  highestBatVoltage = batteryVoltage;
  lowestCellVoltage = batteryVoltage/5;
  highestCellVoltage = batteryVoltage/5;

  //initialize SVG Graphics array with just now values
  for (int i=0;i<maxBatHistValues;i++){
    storeBatVoltHistory(batteryVoltage);
  }
  
  computeGraphBasedOnBatValues();
  
  dailyTasks = day(); //store the current day for the daily tasks

  changeUTCtoLocalTime();//change time to local time
  computeSunriseSunsetInformation(); //compute the new sunrise and sunset for today
  
    
  if (debugMode>=1){
    Serial.println("[setup]finished...");
    writeDebugMessageToInternalLog((String)"[setup]finished...");
  }
}

void loop() {

  //Webserver section
  wwwserver.handleClient(); 

  if (NTPUpdateSuccessful==false){ //if syncing has failed do the doItOnceADay jobs again as well
    NTPUpdateSuccessful = syncTimeViaNTP();
    changeUTCtoLocalTime();//change time to local time
    computeSunriseSunsetInformation(); //compute the new sunrise and sunset for today
  }

  //update statistics every second
  if (lastReadingSec!=second()){

    //since the digital input are producing a lot of noise from the detector board we reduce the readings to every 10 seconds like on the LandXcape board as well and switch the digital input offline in the mean time
    if (second()%5==0){ //read every 5th second -> :05,:15,:25...
        delay(100);
        rainSensorCounter = rainSensorCounter%10;
        rainSensorResults[rainSensorCounter] = digitalRead(REGENSENSOR_WEMOS);
        rainSensorCounter++;
    }

    if(second()%5==0){ //read every 5th second -> :05,:15,:25...
      double oldBatValue = batteryVoltage; //old value saved
      //take smalest one of 100 readings to minimize jumping / noise -> WLAN is maybe the main coarse -> https://github.com/esp8266/Arduino/issues/2070
      A0reading = analogRead(BATVOLT);
      for (int i=0;i<100;i++){
        A1reading = analogRead(BATVOLT);
        delay(1);
        if (A1reading < A0reading){
          A0reading = A1reading;
        }
      }
      if (debugMode>=2){
        Serial.print("A0: ");
        Serial.println(A0reading);
        writeDebugMessageToInternalLog((String)"[System]Pwr-Reading:"+A0reading);
      }
   
        batteryVoltage = A0reading * batteryVoltFactor;
      
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
     }
      //every minute jobs
      if (lastReadingMin!=minute()){
        //store battery values every minute
        storeBatVoltHistory(batteryVoltage);
        computeGraphBasedOnBatValues();

        //check if we are charging, charging is needed or if we are done with charging ;)
        checkBatValues();

        //check is "Mowing from Sunrise to Sunset is activated and if yes trigger mowing if we are not charging and we are at home
        if(allDayMowing==true && robiAtHome==true && hasCharged == true && isCharging==false){

          //check if the sun is up ;)
          int currentTimeInMin = hour()*60+minute();

          if(sunrise<=currentTimeInMin && sunset >=currentTimeInMin){ //activate function only while the sun is up and running ;)

              if(robiAtHome==true && isCharging==false && hasCharged == true && raining == false){ //Charging finished, battery voltage drops again since no higher charging voltage is present
                  if (debugMode>=1){
                    Serial.println((String)"[loop]Mowing from Sunrise to Sunset - start next round at local time:"+hour()+":"+minute()+":"+second());
                    writeDebugMessageToInternalLog((String)"[loop]Mowing from Sunrise to Sunset - start next round at local time:"+hour()+":"+minute()+":"+second());
                  } 
                  showWebsite=false;
                  handleStartMowing(); //start mowing; boolean variables will be set within this function
                  showWebsite=true;
              }
       
          }else{
            if (debugMode>=2){
                    Serial.println((String)"[loop]Sunrise to Sunset mowing deactived because sunrise>=currentTimeInMin <=sunset"+sunrise+"<="+currentTimeInMin+"sunset>="+sunset);
                    writeDebugMessageToInternalLog((String)"[loop]Sunrise to Sunset mowing deactived because sunrise>=currentTimeInMin <=sunset"+sunrise+"<="+currentTimeInMin+"sunset>="+sunset);
            }
          }
        }  
        //check if it is time to bring robi home if allDayMowing is active only!
        int currentTimeInMin = hour()*60+minute();
        
        if (allDayMowing==true && robiAtHome==false && robiOnTheWayHome == false && (currentTimeInMin > sunset || currentTimeInMin < sunrise)){ 
          
          showWebsite=false;
          handleStopMowing();
          handleGoHome();
          showWebsite=true;

          if (debugMode>=1){
                    Serial.println((String)"[loop]Sunset detected and allDayMowing active... Sending Robi home to base...");
                    writeDebugMessageToInternalLog((String)"[loop]Sunset detected and allDayMowing active... Sending Robi home to base...");
            }
        }
        
        //check for rain
        if (getRainSensorStatus()){ //if true then rain has been detected -> send robi home
            
            if(robiOnTheWayHome == false && robiAtHome == false){ //-> send robi home if not already on the way or at home
              if (debugMode>=1){
                      Serial.println((String)"[loop]Rain has been detected. Sending Robi home to base...");
                      writeDebugMessageToInternalLog((String)"[loop]Rain has been detected. Sending Robi home to base...");
              }
               showWebsite=false;
               handleStopMowing(); //stop mowing to allow to send robi home
               handleGoHome(); //send Robi home
               showWebsite=true;
            }           
              if (debugMode>=1 && raining == false){ //show rain detection within log file
                      Serial.println((String)"[loop]Rain detected at:"+hour()+":"+minute()+":"+second());
                      writeDebugMessageToInternalLog((String)"[loop]Rain detected at:"+hour()+":"+minute()+":"+second());
              }
             raining = true;
             rainingDelay_ = rainingDelay;

            if (forwardRainInfoToLandXcape==true){ //forward raining information to LandXcape as wished
               reportRainToLandXCape();
               if (debugMode>=1){
                      Serial.println("[loop]Raining information has been forwarded to LandXcape as selected.");
                      writeDebugMessageToInternalLog("[loop]Raining information has been forwarded to LandXcape as selected.");
               }
            }
             
        }else{
          // check if need to update the raining variable
          if (raining == true && getRainSensorStatus()==false){
            rainingDelay_--; //subtracts 1 every minute
            if (rainingDelay_<=0){
              
              if (debugMode>=1 && raining == true){ //show rain gone detection within log file
                      Serial.println((String)"[loop]Rain gone - "+rainingDelay+"min delay has passed without rain at:"+hour()+":"+minute()+":"+second());
                      writeDebugMessageToInternalLog((String)"[loop]Rain gone - "+rainingDelay+"min delay has passed without rain at:"+hour()+":"+minute()+":"+second());
              }
              
              rainingDelay_=rainingDelay; //reset value
              raining = false; //Raining delay time has passed. Switch raining variable to false
            }
          }
        }
        lastReadingMin = minute();
      }
            
       if (debugMode>=2){
          Serial.println((String)"[loop]Last Sensor Reading:"+hour()+":"+minute()+":"+second());
          writeDebugMessageToInternalLog((String)"[loop]Last Sensor Reading:"+hour()+":"+minute()+":"+second());
       } 
    
       //check if Go Home Early is active
       if (earlyGoHome==true && robiAtHome==false && robiOnTheWayHome == false){
        //compare measured voltage against defined one
        if (earlyGoHomeVolt>=batteryVoltage){
             if (debugMode>=1){
                Serial.println((String)"[loop]Early Go Home triggered at local time:"+hour()+":"+minute()+":"+second());
                writeDebugMessageToInternalLog((String)"[loop]Early Go Home triggered at local time:"+hour()+":"+minute()+":"+second());
                Serial.println((String)"[loop]With sensored Battery Value:"+batteryVoltage+" and limit set:"+earlyGoHomeVolt);
                writeDebugMessageToInternalLog((String)"[loop]With sensored Battery Value:"+batteryVoltage+" and limit set:"+earlyGoHomeVolt);
             } 
             showWebsite=false;
             handleStopMowing(); //stop mowing to allow to send robi home
             handleGoHome(); //send Robi home
             showWebsite=true;
        }
       }
     lastReadingSec = second();
  }

  //check if the WLAN is still good every 30 seconds
  if(second()%30==0 ){
    if (checkWLANisGood() != true){
     if (debugMode>=1){
      Serial.println((String)"[loop] WLAN connection lost... reconnecting...");
      writeDebugMessageToInternalLog((String)"[loop] WLAN connection lost... reconnecting...");
     }
      reconnectWLAN();
    }
   }
  //check if a new day as started
  if (dailyTasks!=day()){ //and if so then do the daily housework
    doItOnceADay();
    dailyTasks=day();//store today as new dailyTasks day
  }
}

static boolean checkWLANisGood(){

  if (WiFi.status() != WL_CONNECTED){
    return false;
  }else{
    return true;
  }
}

static void reconnectWLAN(){

  //WiFi Connection
  if (debugMode>=1){
    Serial.println((String)"[setup]"+connectTo + ssid);
    writeDebugMessageToInternalLog((String)"[setup]"+connectTo + ssid);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (debugMode>=1){
      Serial.println((String)"[setup]"+"WiFi Status Code:"+WiFi.status());
      writeDebugMessageToInternalLog((String)"[setup]"+"WiFi Status Code:"+WiFi.status());
    }
  }
  
}

static void handleRoot(void){

  if (debugMode>=2){
    Serial.println((String)"[handleRoot]Connection from outside established at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[handleRoot]Connection from outside established at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED  
  }
  //preparation work
  char temp[1250];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  //Battery Voltage computing
  A0reading = analogRead(BATVOLT);
  //A0reading = A0reading / baseFor1V;
  //batteryVoltage = A0reading * faktorBat;
  batteryVoltage = A0reading * batteryVoltFactor;
  
  snprintf(temp, 1250,
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
          <p>Local time: %02d:%02d:%02d</p>\
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
      </html>",hr, min % 60, sec % 60,
       hour(),minute(),second(),version,batteryVoltage
      );
  wwwserver.send(200, "text/html", temp);
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
  }
}

/*
 * handleStartMowing triggers the Robi to start with his job :)
 * showWebsite true means presenting the website otherwise only an internal call without presenting the website
 */
static void handleStartMowing(void){

   if (debugMode>=1){
    Serial.println((String)"[handleStartMowing]Start with the Start relay for " + buttonPressTime + "followed with the OK button");
    writeDebugMessageToInternalLog((String)"[handleStartMowing]Start with the Start relay for " + buttonPressTime + "followed with the OK button");
  }
   
   digitalWrite(START,LOW);//Press Start Button 
   delay(buttonPressTime);
   digitalWrite(START,HIGH);//Release Start Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,LOW);//Press Okay Button
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release Okay Button

  if (debugMode>=1){
    Serial.println((String)"[handleStartMowing]Mowing started at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[handleStartMowing]Mowing started at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  if (showWebsite){
    char temp[480];
    snprintf(temp, 480,
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
            <p></p>\
            <p>Mowing started at local time: %02d:%02d:%02d</p>\
          </body>\
        </html>",
        hour(),minute(),second()
        );
    wwwserver.send(200, "text/html", temp);
  }

  robiAtHome = false; //Robi has just started so it can not be at home or on the way too ;)
  robiOnTheWayHome = false; // see above
  isCharging = false; //see above ;)
  hasCharged = false;
}

/*
 * handleStopMowing triggers the Robi to stop with his job and to wait for further instructions :)
 * showWebsite true means presenting the website otherwise only an internal call without presenting the website
 */
static void handleStopMowing(){
  
   if (debugMode>=1){
    Serial.println((String)"[handleStopMowing]Start with the Stop relay for " + buttonPressTime + " before releasing it again");
    writeDebugMessageToInternalLog((String)"[handleStopMowing]Start with the Stop relay for " + buttonPressTime + " before releasing it again");
  }
   
   digitalWrite(STOP,LOW);//Press Stop Button 
   delay(buttonPressTime);
   digitalWrite(STOP,HIGH);//Release Stop Button 

   
  if (debugMode>=1){
    Serial.println((String)"[handleStopMowing]Mowing stoped at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[handleStopMowing]Mowing stoped at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  if(showWebsite){
    char temp[450];
    snprintf(temp, 450,
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
          <p></p>\
          <p>Mowing stoped at local time: %02d:%02d:%02d</p>\
        </body>\
      </html>",
      hour(),minute(),second()
      );
    wwwserver.send(200, "text/html", temp);
  }
}

/*
 * handleGoHome triggers the Robi to go back to his station and to charge :)
 * showWebsite true means presenting the website otherwise only an internal call without presenting the website
 */
static void handleGoHome(){
  
  if (debugMode>=1){
    Serial.println((String)"[handleGoHome]Start with the Home relay for " + buttonPressTime + "followed with the OK button for the same time");
    writeDebugMessageToInternalLog((String)"[handleGoHome]Start with the Home relay for " + buttonPressTime + "followed with the OK button for the same time");
  }
   
   digitalWrite(HOME,LOW);//Press Home Button 
   delay(buttonPressTime);
   digitalWrite(HOME,HIGH);//Release Home Button 
   delay(buttonPressTime);
   digitalWrite(OKAY,LOW);//Press Okay Button
   delay(buttonPressTime);
   digitalWrite(OKAY,HIGH);//Release Okay Button
   
  if (debugMode>=1){
    Serial.println((String)"[handleGoHome]Robi sent home at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[handleGoHome]Robi sent home at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
  if (showWebsite){
  char temp[470];
    snprintf(temp, 470,
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
          <p></p>\
          <p>Mowing stoped and sent back to base at local time: %02d:%02d:%02d</p>\
        </body>\
      </html>",
      hour(),minute(),second()
      );
    wwwserver.send(200, "text/html", temp);
  }

  robiAtHome = false; //Robi has just sent home so it should not be at home currently or follow the line to mow it
  robiOnTheWayHome = true; // since robi is on the way home
  isCharging = false; //see above ;)
  hasCharged = false;
}


/**
 * showStatistics shows all kind of statistics for the nerdy user ;)
 */

static void showStatistics(void){
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  }
 
    if (debugMode>=2){
      Serial.println((String)"[showStatistics]showStatistics site requested at local time:"+hour()+":"+minute()+":"+second()+" " + year());
      writeDebugMessageToInternalLog((String)"[showStatistics]showStatistics site requested at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    }
    
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;
    int days = hr / 24;
    double cellVoltage = batteryVoltage/5;

    String sunrise_ = (String)""+sunrise/60+"h "+sunrise%60+"min";
    char sunrise__[10];
    sunrise_.toCharArray(sunrise__,10);
    String sunset_ = (String)""+sunset/60+"h "+sunset%60+"min";
    char sunset__[10];
    sunset_.toCharArray(sunset__,10);

    char* isChargingValue;
    char* robiAtHomeValue; 
    char* robiOnTheWayHomeValue; 
    char* hasChargedValue;

    if (isCharging){
      isChargingValue = true_;
    }else{
      isChargingValue = false_;
    }

    if (hasCharged){
      hasChargedValue = true_;
    }else{
      hasChargedValue = false_;
    }

    if (robiAtHome){
      robiAtHomeValue = true_;
    }else{
      robiAtHomeValue = false_;
    }

    if (robiOnTheWayHome){
      robiOnTheWayHomeValue = true_;
    }else{
      robiOnTheWayHomeValue = false_;
    }

    char rainStatus_ [] = "Not raining";
    char rainDelay_ [] ="20";
    char rainDelayText_ [37] = "";
    
    if(getRainSensorStatus()){
      strncpy(rainStatus_, "raining...", sizeof(rainStatus_));

      itoa(rainingDelay_,rainDelay_,10);
      strcat(rainDelayText_,"Waiting delay: ");
      strcat(rainDelayText_,rainDelay_);
      strcat(rainDelayText_," minutes");
    }else{
      if (raining){ //show as long as the raining flag is active
        strncpy(rainStatus_, "raining...", sizeof(rainStatus_));
        
        itoa(rainingDelay_,rainDelay_,10);
        strcat(rainDelayText_,"Waiting delay after rain: ");
        strcat(rainDelayText_,rainDelay_);
        strcat(rainDelayText_," minutes");
      }
    }
        
    char temp[1820];
    snprintf(temp, 1820,
     "<html>\
      <head>\
        <title>LandXcape Statistics</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
        <meta http-equiv='Refresh' content='10; url=\\stats'>\
      </head>\
        <body>\
          <h1>LandXcape Statistics</h1>\
          <p></p>\
          <p>Uptime: %02d days %02d hour %02d min %02d sec</p>\
          <p>Time: %02d:%02d:%02d</p>\
          <p>Date: %02d.%02d.%02d</p>\
          <p>Computed sunrise approx: %s</p>\
          <p>Computed sunset approx: %s</p>\
          <p>HasCharged/isCharging: %s/%s   (OnTheWay)Home: (%s)%s</p>\
          <p>Weather status: %s %s</p>\
          <p>Version: %02lf</p>\
          <br>\
          <table style='width:450px'>\
            <tr>\
              <th>Battery:</th>\
            </tr>\
            <tr>\
              <th>Actual voltage: %02lf</th>\
              <th>Lowest voltage: %02lf</th>\
              <th>Highest voltage: %02lf</th>\
            </tr>\
            <tr>\
              <th>Cell:</th>\
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
          <p><b>Memory Information in bytes:</b></p>\
          <p>Free Heap: %d - Fragmentation: %d - MaxFreeBlockSize: %d</p>\
          <form method='POST' action='/'><button type='submit'>Back to main menu</button></form>\
        </body>\
      </html>",
      days,hr%24, min % 60, sec % 60,hour(),minute(),second(),day(),month(),year(),sunrise__,sunset__,hasChargedValue,isChargingValue,robiOnTheWayHomeValue,robiAtHomeValue,rainStatus_,rainDelayText_,
      version,batteryVoltage,lowestBatVoltage,highestBatVoltage,cellVoltage,lowestCellVoltage,highestCellVoltage,lastXXminBatHist,ESP.getFreeHeap(),ESP.getHeapFragmentation(),ESP.getMaxFreeBlockSize()
      );

    wwwserver.send(200, "text/html", temp);

  if(onBoardLED){
    digitalWrite(LED_BUILTIN, HIGH); //show connection via LED   
  }
}

/*
 * handleAdministration allows to administrate your settings :) earlyGoHome
 */
static void handleAdministration(void){

  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  }

    if (debugMode>=1){
      Serial.println((String)"[handleAdmin]Administration site requested at local time:"+hour()+":"+minute()+":"+second()+" " + year());
      writeDebugMessageToInternalLog((String)"[handleAdmin]Administration site requested at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    }
    //preparations
    //char temp[2500];
    char earlyGoHomeCheckBoxValue [] = "unchecked";
    if (earlyGoHome==true){
      strncpy(earlyGoHomeCheckBoxValue, "checked  ", sizeof(earlyGoHomeCheckBoxValue));
    }
    int earlyGoHomeVolt_ = earlyGoHomeVolt;
    int earlyGoHome_mVolt_ = (double(earlyGoHomeVolt-earlyGoHomeVolt_)*1000); // subtract the voltage and multiply by 1000 to get the milivolts

    char allDayMowingCheckBoxValue [] = "unchecked";
    if (allDayMowing==true){
      strncpy(allDayMowingCheckBoxValue, "checked  ",sizeof(allDayMowingCheckBoxValue));
    }   

    char forwardRainInfoValue [] = "unchecked";
    if (forwardRainInfoToLandXcape==true){
      strncpy(forwardRainInfoValue, "checked  ",sizeof(forwardRainInfoValue));
    }
    
    char ignoreRainValue [] = "unchecked";
    if (ignoreRain==true){
      strncpy(ignoreRainValue, "checked  ",sizeof(ignoreRainValue));
    } 

    char temp[2300];
    snprintf(temp, 2300,
     "<html>\
     <head>\
     <title>LandXcape</title>\
      <style>\
        body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;}\
      </style>\
    </head>\
      <body>\
        <h1>LandXcape Administration Site</h1>\
        <p></p>\
        <form method='POST' action='/newAdminConfiguration'>\
        Battery history: Show <input type='number' name='batHistMinShown' value='%02d'  min=60 max=600> minutes<br>\
        Activate function \"Go Home Early\" <input type='checkbox' name='goHomeEarly' %s ><br>\
        If activated, send LandXcape home at: <input type='number' name='batVol' value='%02d' min=16 max=20> V <input type='number' name='batMiliVolt' value='%03d' min=000 max=999>mV<br>\
        If not activated, this value is used to define the battery voltage <br> where no new round of mowing should be started before charging again.<br>\
        <br>\
        Activate function \"Mowing from sunrise to sunset\" <input type='checkbox' name='allDayMowing_' %s ><br>\
        <br>\
        Forward rain information to LandXcape to trigger original behavior <input type='checkbox' name='forwardRainInfo_' %s ><br>\
        <br>\
        Ignore rain - just mow nevertheless if it rains or not <input type='checkbox' name='ignoreRain_' %s ><br>\
        <br>\
        FileSystem Functions: <br>\
        Format FileSystem <b>ATTENTION All persistent Data will be lost ATTENTION</b> <input type='checkbox' name='formatFlashStorage'><br>\
        This must be done once before the filesystem can be used.<br>\
        Will take about 60Seconds<br>\
        <br>\
        <input type='submit' value='Submit'></form>\
        <form method='POST' action='/'><button type='submit'>Cancel</button></form>\
        <p></p>\
        <br>\
        <table style='width:450px'>\
          <tr>\
            <th><form method='POST' action='/updateLandXcape'><button type='submit'>SW Update via WLAN</button></form></th>\
            <th><form method='POST' action='/resetWemos'><button type='submit'>Reset WEMOS board</button></form></th>\
            <th><form method='POST' action='/logFiles'><button type='submit'>Show Log-Entries</button></form></th>\
          </tr>\
        </table>\
      </body>\
    </html>",lastXXminBatHist,earlyGoHomeCheckBoxValue,earlyGoHomeVolt_,earlyGoHome_mVolt_,allDayMowingCheckBoxValue,forwardRainInfoValue,ignoreRainValue
      );

      wwwserver.send(200, "text/html", temp);        

    if(onBoardLED){
      digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
    }
}

/**
 * computeNewAdminConfig overwrites the present values by the given values from the user
 */
static void computeNewAdminConfig(void){
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  }
 
    // Check if the POST request has crendentials and if they are correct
    if(! wwwserver.hasArg("batHistMinShown") || wwwserver.arg("batHistMinShown") == NULL ||
       ! wwwserver.hasArg("batVol") || wwwserver.arg("batVol") == NULL || ! wwwserver.hasArg("batMiliVolt") || wwwserver.arg("batMiliVolt") == NULL ){ 
      wwwserver.send(400, "text/plain","400: Invalid Request"); // The request is invalid, so send HTTP status 400
      return;
    }

    //compute given values
    int batHistMinShown_ = wwwserver.arg("batHistMinShown").toInt();
    if (batHistMinShown_ >= 60 && batHistMinShown_ <=600){ //set it only if its valid
       lastXXminBatHist = batHistMinShown_;     
    }

    earlyGoHome = (boolean)wwwserver.hasArg("goHomeEarly");
    int batVolt = wwwserver.arg("batVol").toInt();
    int batMiliVolt = wwwserver.arg("batMiliVolt").toInt();
    earlyGoHomeVolt = (double)batVolt+(double) batMiliVolt/1000;

    allDayMowing = (boolean)wwwserver.hasArg("allDayMowing_");
    forwardRainInfoToLandXcape = (boolean)wwwserver.hasArg("forwardRainInfo_");
    ignoreRain = (boolean)wwwserver.hasArg("ignoreRain_");

    boolean formatFlashStorage = (boolean)wwwserver.hasArg("formatFlashStorage");
    
    char temp[820];
    snprintf(temp, 820,
     "<html>\
      <head>\
        <title>LandXcape</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
        </style>\
        <meta http-equiv='Refresh' content='3; url=\\'>\
      </head>\
        <body>\
          <h1>LandXcape - administration changes submited at local time: %02d:%02d:%02d</h1>\
          <p></p>\
          <p>LastXXminBatHist Variable changed to: %02d</p>\
          <p>GoHomeEarly Function: %d</p>\
          <p>GoHomeEarly Voltage:: %2.3f</p>\
          <p>ForwardRainInfoToLandXcape Function: %d</p>\
          <p>Ignore rain Function: %d</p>\
          <p>Mow from sunrise to Sunset Function: %d</p>\
          <p>Flash Storage will be formated: %d</p>\
      </body>\
    </html>",
    hour(),minute(),second(),lastXXminBatHist,earlyGoHome,earlyGoHomeVolt,forwardRainInfoToLandXcape,ignoreRain,allDayMowing,formatFlashStorage
    );
    wwwserver.send(200, "text/html", temp);

    if (debugMode>=1){
      Serial.println((String)"[computeNewAdminConfig]New Admin config transmitted at local time:"+hour()+":"+minute()+":"+second()+" " + year());
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]New Admin config transmitted at local time:"+hour()+":"+minute()+":"+second()+" " + year());
      Serial.println("[computeNewAdminConfig]With the following values:");
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]With the following values:");
      Serial.println("[computeNewAdminConfig]");
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]");
      Serial.println((String)"[computeNewAdminConfig]Battery History Showtime"+lastXXminBatHist);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]Battery History Showtime"+lastXXminBatHist);
      Serial.println((String)"[computeNewAdminConfig]GoHomeEarly Function:"+earlyGoHome);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]GoHomeEarly Function:"+earlyGoHome);
      Serial.println((String)"[computeNewAdminConfig]GoHomeEarly Voltage:"+earlyGoHomeVolt);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]GoHomeEarly Voltage:"+earlyGoHomeVolt);
      Serial.println((String)"[computeNewAdminConfig]ForwardRainInfoToLandXcape Function:"+forwardRainInfoToLandXcape);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]ForwardRainInfoToLandXcape Function:"+forwardRainInfoToLandXcape);
      Serial.println((String)"[computeNewAdminConfig]Ignore rain Function:"+ignoreRain);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]Ignore rain Function:"+ignoreRain);
      Serial.println((String)"[computeNewAdminConfig]Mow from sunrise to Sunset Function:"+allDayMowing);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]Mow from sunrise to Sunset Function:"+allDayMowing);
      Serial.println((String)"[computeNewAdminConfig]Flash storage shall be formated:"+formatFlashStorage);
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]MFlash storage shall be formated:"+formatFlashStorage);
    }
    
    computeGraphBasedOnBatValues();
    
    if (debugMode>=1){
      Serial.println("[computeNewAdminConfig]Update of the battery voltage picture realized.");
      writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]Update of the battery voltage picture realized.");
    }

    if (formatFlashStorage){ //Format Filesystem as whished
      if(debugMode>=1){      
        Serial.println("[computeNewAdminConfig]Formatting flash storage as selected...");
        writeDebugMessageToInternalLog((String)"[computeNewAdminConfig]Formatting flash storage as selected...");
      }
      formatFS();
    } 
    if(onBoardLED){
      digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
    }
}

/*
 * handleSwitchOnOff allows to Power the robi on or off as you wish
 */
static void handleSwitchOnOff(void){

 if (debugMode>=1){
    Serial.println((String)"[handleSwitchOnOff]handleSwitchOnOff triggered at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[handleSwitchOnOff]handleSwitchOnOff triggered at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
   
   digitalWrite(PWR,LOW);//Press Home Button 
   delay(PWRButtonPressTime);
   digitalWrite(PWR,HIGH);//Release Home Button 
   delay(PWRButtonPressTime);
   
  char temp[420];
  snprintf(temp, 420,
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
        <p></p>\
        <p>Robi switched off/on at local time: %02d:%02d:%02d</p>\
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
  if (debugMode>=1){
    Serial.println((String)"[enterPinCode]enterPinCode triggered at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[enterPinCode]enterPinCode triggered at local time:"+hour()+":"+minute()+":"+second()+" " + year());
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

  if (debugMode>=1){
    Serial.println((String)"[enterPinCode]enterPinCode finisched at local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[enterPinCode]enterPinCode finisched at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
}

//Get the current time via NTP over the Internet
static boolean syncTimeViaNTP(void){
  
  //local one time variables
  WiFiUDP udp;
  int localPort = 2390;      // local port to listen for UDP packets
  IPAddress timeServerIP; // time.nist.gov NTP server address
  char ntpServerName [] = "pool.ntp.org";
  int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
  byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

    if (debugMode>=1){
      Serial.println("[syncTimeViaNTP]Starting UDP");
      writeDebugMessageToInternalLog((String)"[syncTimeViaNTP]Starting UDP");
    }
    udp.begin(localPort);
  
    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP);

    if (debugMode>=1){
      Serial.println("[syncTimeViaNTP]sending NTP packet...");
      writeDebugMessageToInternalLog((String)"[syncTimeViaNTP]sending NTP packet...");
    }
  
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form request
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
    delay(1200);
    int cb = udp.parsePacket();
    if (!cb){
       if (debugMode){
        Serial.println((String)"[syncTimeViaNTP]Time setting failed. Received Packets="+cb);
        writeDebugMessageToInternalLog((String)"[syncTimeViaNTP]Time setting failed. Received Packets="+cb);
      }
      return false;
    }

    if (debugMode>=1){
      Serial.println((String)"[syncTimeViaNTP]packet received, length="+ cb);
      writeDebugMessageToInternalLog((String)"[syncTimeViaNTP]packet received, length="+ cb);
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
        Serial.println("[syncTimeViaNTP]Current time as UTC time updated.");
        writeDebugMessageToInternalLog((String)"[syncTimeViaNTP]Current time as UTC time updated.");

        // print the hour, minute and second:
        Serial.print("[syncTimeViaNTP]The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
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
   timeAdjusted = false; //mark current time as UTC time
   return true;
}

static void handleWebUpdate(void){
  
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  }

  //preparation work
  char temp[650];
  snprintf(temp, 650,
   "<html>\
    <head>\
      <title>LandXcape WebUpdate Site</title>\
      <style>\
        body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
      </style>\
    </head>\
      <body>\
        <h1>LandXcape WebUpdate Site</h1>\
        <p>Local time: %02d:%02d:%02d</p>\
        <p>Version: %02lf</p>\
         <form method='POST' action='/updateBin' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>\
         <form method='POST' action='/'><button type='submit'>Cancel</button></form>\
      </body>\
    </html>",
     hour(),minute(),second(),version
     );
          
  //present website 
  wwwserver.send(200, "text/html", temp);

    if (debugMode>=1){
          Serial.println("[handleWebUpdate]WebUpdate site requested...");
          writeDebugMessageToInternalLog((String)"[handleWebUpdate]WebUpdate site requested...");
    }

    if(onBoardLED){
      digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
    } 
}

static void handleUpdateViaBinary(void){
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  }
  if (debugMode>=1){
          Serial.println("[handleUpdateViaBinary]handleUpdateViaBinary function triggered...");
          writeDebugMessageToInternalLog((String)"[handleUpdateViaBinary]handleUpdateViaBinary function triggered...");
  }

  wwwserver.sendHeader("Connection", "close");
  wwwserver.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  
  if (debugMode>=1){
          Serial.println("[handleUpdateViaBinary]handleUpdateViaBinary function finished...");
          writeDebugMessageToInternalLog((String)"[handleUpdateViaBinary]handleUpdateViaBinary function finished...");
  }

  //unmount filesystem before rebooting
  SPIFFS.end();
  delay(200); //wait 200ms to ensure that the Filesystem has been unmounted
  
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
  }
  ESP.restart();
}

static void handleWebUpdateHelperFunction (void){
  if(onBoardLED){
    digitalWrite(LED_BUILTIN, LOW); //show connection via LED   
  } 

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
        char temp[500];
        snprintf(temp, 500,
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
              <p></p>\
              <p>Update successfull at Local time: %02d:%02d:%02d</p>\
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

    if(onBoardLED){
      digitalWrite(LED_BUILTIN, HIGH); //show successful answer to request    
    }
}

/**
 * batterVoltageHistory helper function to store battery values (max 600 entries <->maxBatHistValues)
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

  if (debugMode>=2){
          Serial.println("[drawGraphBasedOnBatValues]drawGraphBasedOnBatValues has been triggered...");
          writeDebugMessageToInternalLog((String)"[drawGraphBasedOnBatValues]drawGraphBasedOnBatValues has been triggered...");
  }
  File batGraphFile = SPIFFS.open(batGraph,"r");
  if(!batGraphFile){ //check if we have been able to open/create the file, if not abort
      if (debugMode>=1){
          Serial.println("[drawGraphBasedOnBatValues]batGraph.svg file opening failed. Aborting...");
          writeDebugMessageToInternalLog((String)"[drawGraphBasedOnBatValues]batGraph.svg file opening failed. Aborting...");
      }
      return;   
  }

  //stream the file :D
  wwwserver.streamFile(batGraphFile, "image/svg+xml");

  batGraphFile.close();
}

/**
 * computeGraphBasedOnBatValues as a SVG Graphics
 * To reduce memory consumption -18.2kb with 400 values and 6.4 with 100 values, the SVG will now always be stored/written and read to or from the flash memory. This alows us to use a decent amount of memory again.
 */
void computeGraphBasedOnBatValues(void) {

  if (debugMode>=2){
          Serial.println("[computeGraphBasedOnBatValues]computeGraphBasedOnBatValues has been triggered...");
          writeDebugMessageToInternalLog((String)"[computeGraphBasedOnBatValues]computeGraphBasedOnBatValues has been triggered...");
  }

  //open file at FileSystem
  File batGraphFile = SPIFFS.open(batGraph,"w");
  if(!batGraphFile){ //check if we have been able to open/create the file, if not abort
      if (debugMode>=1){
          Serial.println("[computeGraphBasedOnBatValues]batGraph.svg file creation failed. Aborting...");
          writeDebugMessageToInternalLog((String)"[computeGraphBasedOnBatValues]batGraph.svg file creation failed. Aborting...");
      }
      return;   
  }

  char temp[100];
  char svgBatHistGraph[230];
  snprintf(svgBatHistGraph, 230,
    "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"600\" height=\"155\">\
    <rect width=\"600\" height=\"155\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\
    <g stroke=\"black\">");

  //write initial part of the SVG
  batGraphFile.println(svgBatHistGraph);

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
    batGraphFile.print(temp);
    
    y = y2;
    counter++;
  }
  
  batGraphFile.println("</g>\n</svg>\n");
  batGraphFile.close();
  if (debugMode>=2){
          Serial.println("[computeGraphBasedOnBatValues]computeGraphBasedOnBatValues has been finished and as file saved...");
          writeDebugMessageToInternalLog("[computeGraphBasedOnBatValues]computeGraphBasedOnBatValues has been finished and as file saved...");
  }
}

/**
 * resetWemosBoard via SW reset
 * Send User afterwards back to the root site of the webserver
 */

void resetWemosBoard(void){
  if (debugMode>=1){
          Serial.println("[resetWemosBoard]Software reset triggered. Reseting...");
          writeDebugMessageToInternalLog("[resetWemosBoard]Software reset triggered. Reseting...");
  }

  char temp[450];
  snprintf(temp, 450,
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
        <p></p>\
        <p>Software reset triggered. Reseting... at Time: %02d:%02d:%02d</p>\
      </body>\
    </html>",
    hour(),minute(),second()
    );
  wwwserver.send(200, "text/html", temp);

  //unmount filesystem before rebooting
  SPIFFS.end(); //wait 200ms to ensure that the Filesystem has been unmounted
  delay(200);// and to allow the webserver to send the site before resetting ;)
  ESP.restart();
}

/**
 * Approximate sunrise and sunset based on UTC +1h for MESZ and according if summer time (DST) is valid or not
 */

static void computeSunriseSunsetInformation(void){

  if (debugMode>=1){
    Serial.println("[computeSunriseSunsetInformation]computeSunriseSunsetInformation function triggered...");
    writeDebugMessageToInternalLog("[computeSunriseSunsetInformation]computeSunriseSunsetInformation function triggered...");
  }

  SummerTimeActive = summertime_EU(year(),month(),day(),hour(),1);

  int day_ = (((month()-1)*30.5)+day());

  int average_sunrise = (earliest_sunrise+latest_sunrise)/2;
  int diff_sunrise = (latest_sunrise-earliest_sunrise);
  int average_sunset = (earliest_sunset+latest_sunset)/2;
  int diff_sunset = (latest_sunset-earliest_sunset);

  sunrise = (average_sunrise+(diff_sunrise/2)*cos((day_+8)/58.09));
  sunset = (average_sunset-(diff_sunset/2)*cos((day_+8)/58.09));

  //Sumertime-correction if necessary
  if(SummerTimeActive==false){
    sunrise=sunrise-60;//reduce 60min 
    sunset=sunset-60;//reduce 60min 
  }

  if (debugMode>=1){
    Serial.println((String)"[computeSunriseSunsetInformation]Summertime active:" + SummerTimeActive);
    writeDebugMessageToInternalLog((String)"[computeSunriseSunsetInformation]Summertime active:" + SummerTimeActive);
    Serial.println((String)"[computeSunriseSunsetInformation]Sunrise for today:"+sunrise/60+"h " + sunrise%60 + "min");
    writeDebugMessageToInternalLog((String)"[computeSunriseSunsetInformation]Sunrise for today:"+sunrise/60+"h " + sunrise%60 + "min");
    Serial.println((String)"[computeSunriseSunsetInformation]Sunset for today:"+sunset/60+"h " + sunset%60 + "min");
    writeDebugMessageToInternalLog((String)"[computeSunriseSunsetInformation]Sunset for today:"+sunset/60+"h " + sunset%60 + "min");
  }
}

/**
 * Summertime algo from a colleague :) Thank you :)
 * European Daylight Savings Time calculation by "jurs" for German Arduino Forum
 * input parameters: "normal time" for year, month, day, hour and tzHours (0=UTC, 1=MEZ)
 * return value: returns true during Daylight Saving Time, false otherwise
 */
boolean summertime_EU(int year, byte month, byte day, byte hour, byte tzHours)
{
 if (month<3 || month>10) return false; // keine Sommerzeit in Jan, Feb, Nov, Dez
 if (month>3 && month<10) return true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
 if ((month==3 && (hour + 24 * day)>=(1 + tzHours + 24*(31 - (5 * year /4 + 4) % 7))) || (month==10 && (hour + 24 * day)<(1 + tzHours + 24*(31 - (5 * year /4 + 1) % 7))))
   return true;
 else
   return false;
}

/**
 * doItOnceADay() - contains necesseray tasks to keep everything in sync
 */
 static void doItOnceADay(void){
  if (debugMode>=1){
    Serial.println((String)"[doItOnceADay]Summertime:"+SummerTimeActive);
    writeDebugMessageToInternalLog((String)"[doItOnceADay]Summertime:"+SummerTimeActive);
    Serial.println("[doItOnceADay]doItOnceADay has been triggered. Doing the daily work...");
    writeDebugMessageToInternalLog("[doItOnceADay]doItOnceADay has been triggered. Doing the daily work...");
  }
  NTPUpdateSuccessful = syncTimeViaNTP(); //resync time from the NTP just to ensure correctness
  changeUTCtoLocalTime();//change time to local time
  computeSunriseSunsetInformation(); //compute the new sunrise and sunset for today
 }

/**
 * checkBatValues if we are charging or within the charging station
 * sets automatically the isCharging boolean variable in the right state
 * -> sets it to not charging whenever the battery voltage is below the given return voltage initial 17.5V( -> since a new run should then be done after the next charge and not before
 *  * "§$% shit -> really -> https://stackoverflow.com/questions/11720656/modulo-operation-with-negative-numbers/42131603 gcc has a bad behavior of doing 'negative' modulos... argh -> google shows me differently ... aigh
 */

static void checkBatValues(void){

  if (debugMode>=2){
    Serial.println("[checkBatValues]check Bat Values triggered");
    writeDebugMessageToInternalLog("[checkBatValues]check Bat Values triggered");
    Serial.println((String)"[checkBatValues]earlyGoHomeVolt:"+earlyGoHomeVolt+" batteryVoltage"+batteryVoltage + " at Local time:"+hour()+":"+minute()+":"+second()+" " + year());
    writeDebugMessageToInternalLog((String)"[checkBatValues]earlyGoHomeVolt:"+earlyGoHomeVolt+" batteryVoltage"+batteryVoltage + " at local time:"+hour()+":"+minute()+":"+second()+" " + year());
  }
    if (earlyGoHomeVolt>=batteryVoltage){
      robiOnTheWayHome = true;
      return;
    }
    //compare current battery volt value against value 3minutes and then if positive 5 minutes ago to exclude high value during driving downwards or on even ground when before climbing has occured
    //to prevent gcc behavior as described above... sigh
    int batVoltHistCounter_tmp3=batVoltHistCounter-3;
    int batVoltHistCounter_tmp6=batVoltHistCounter-6;
    
    if (batVoltHistCounter_tmp3<=-1){
      batVoltHistCounter_tmp3=maxBatHistValues-batVoltHistCounter-4; //-4=-3-1 since an array with 400 entries goes from 0 to 399... 
    }
 
    if (batteryVoltage>batterVoltageHistory[batVoltHistCounter_tmp3]){
          if (batVoltHistCounter_tmp6<=-1){
            batVoltHistCounter_tmp6=maxBatHistValues-batVoltHistCounter-7;//-7=-6-1 since an array with 400 entries goes from 0 to 399... 
          }
      if (batterVoltageHistory[batVoltHistCounter_tmp3]>batterVoltageHistory[batVoltHistCounter_tmp6]){
               
        if (debugMode>=1 && hasCharged == false){ //write default charging information only once
          Serial.println((String)"[checkBatValues]Charging startet at local time:"+hour()+":"+(minute()-6)+":"+second()+" " + year());
          writeDebugMessageToInternalLog((String)"[checkBatValues]Charging startet at local time:"+hour()+":"+(minute()-6)+":"+second()+" " + year());
          Serial.println((String)"[checkBatValues]with values: Battery"+batteryVoltage+" > "+batterVoltageHistory[batVoltHistCounter_tmp3] + " Battery Voltage History 3min ago > " +batterVoltageHistory[batVoltHistCounter_tmp6] + "Battery Voltage History 5min ago");
          writeDebugMessageToInternalLog((String)"[checkBatValues]with values: Battery"+batteryVoltage+" > "+batterVoltageHistory[batVoltHistCounter_tmp3] + " Battery Voltage History 3min ago > " +batterVoltageHistory[batVoltHistCounter_tmp6] + "Battery Voltage History 5min ago");
        }else{
          if (debugMode>=2){ //write default charging information only once
          Serial.println((String)"[checkBatValues]Charging startet at local time:"+hour()+":"+(minute()-6)+":"+second()+" " + year());
          writeDebugMessageToInternalLog((String)"[checkBatValues]Charging startet at local time:"+hour()+":"+(minute()-6)+":"+second()+" " + year());
          Serial.println((String)"[checkBatValues]with values: Battery"+batteryVoltage+" > "+batterVoltageHistory[batVoltHistCounter_tmp3] + " Battery Voltage History 3min ago > " +batterVoltageHistory[batVoltHistCounter_tmp6] + "Battery Voltage History 5min ago");
          writeDebugMessageToInternalLog((String)"[checkBatValues]with values: Battery"+batteryVoltage+" > "+batterVoltageHistory[batVoltHistCounter_tmp3] + " Battery Voltage History 3min ago > " +batterVoltageHistory[batVoltHistCounter_tmp6] + "Battery Voltage History 5min ago");
        }
        }

        isCharging = true;
        hasCharged = true;
        robiAtHome = true;
        robiOnTheWayHome = false;
        hasChargedDelay = 6; //wait at least 6 min before switching the "isCharging" status
        
        return;
      }else{
        hasChargedDelay--;
        if(hasChargedDelay<=0){
          isCharging = false; //we are consuming energy so we are not charging any more
        }
      }
    }else{
       hasChargedDelay--;
        if(hasChargedDelay<=0){
          isCharging = false; //we are consuming energy so we are not charging any more
        }
    }
}

/**
 * changeUTCtoLocalTime as default time incl. DST / summer time adjusment
 */
static void changeUTCtoLocalTime(void){

      if(timeAdjusted == true || NTPUpdateSuccessful == false ){ //Time has been already adjusted or if the NTP Update is still ongoing
  
        if (debugMode>=1){
          Serial.println((String)"[changeUTCtoLocalTime]Time has been already adjusted or if the NTP Update is still ongoing at local time:"+hour()+":"+(minute()-5)+":"+second()+" " + year());
          writeDebugMessageToInternalLog((String)"[changeUTCtoLocalTime]Time has been already adjusted or if the NTP Update is still ongoing at local time:"+hour()+":"+(minute()-5)+":"+second()+" " + year());
        }
          
        return;
      }

      if (debugMode>=1){
        Serial.println((String)"[changeUTCtoLocalTime]changeUTCtoLocalTime called at current UTC Time:"+hour()+":"+(minute()-5)+":"+second()+" " + year());
        writeDebugMessageToInternalLog((String)"[changeUTCtoLocalTime]changeUTCtoLocalTime called at current UTC Time:"+hour()+":"+(minute()-5)+":"+second()+" " + year());
      }

      int timeChange = 0;

      if (SummerTimeActive==true){
        timeChange=timeChange+60*60; //1hour ahead so 60min x 60 seconds
      }
      if (UTCtimezone>=1){
        for (int i=0;UTCtimezone>i;i++){
          timeChange=timeChange+60*60; //add seconds for each hour ahead of UTC
        }
      }
      if (UTCtimezone<=0){
        for (int i=0;UTCtimezone<=i;i--){
          timeChange=timeChange-60*60; //subtract seconds for each hour behind UTC
        }
      }
      if (debugMode>=1){
        Serial.println((String)"[changeUTCtoLocalTime]Time will be adjusted by:"+timeChange);
        writeDebugMessageToInternalLog((String)"[changeUTCtoLocalTime]Time will be adjusted by:"+timeChange);
      }
      adjustTime(timeChange);
      timeAdjusted = true;
}

/**
 * writeDebugMessageToInternalLog - stores the last debug messages in an internal storage
 */ 
 static void writeDebugMessageToInternalLog(String tmp){

  if (logRotateCounter<=0){
    logRotateCounter=maxLogEntries-1; // 0 to 399 = 400 entries for example
  }

  char temp[maxLogLength];
  tmp.toCharArray(temp,maxLogLength);
  
  snprintf(logRotateBuffer[logRotateCounter],maxLogLength,temp);
  
  logRotateCounter=(logRotateCounter-1);
 }

 /**
  * presentLogEntriesFromInternalLog - shows the last stored log entries via a website for remote debugging from RAM
  */
static void presentLogEntriesFromInternalLog(void){

  int counter = ((logRotateCounter+1)%maxLogEntries); //to get the latest entry

  String tmp = 
    "<html>\
    <head>\
      <title>LandXcape - WEMOS - Debug Entries</title>\
      <style>\
        body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
      </style>\
    </head>\
      <body>\
        <h1>LandXcape - WEMOS - Debug Entries</h1>\
        <p></p>\
        <table style='width:450px'>\
          <tr>\
            <th><form method='POST' action='/logFiles'><button type='submit'>Reload</button></form></th>\
            <th><form method='POST' action='/configure'><button type='submit'>Exit</button></form></th>\
          </tr>\
        </table>\
       <br>";
                   
      for (int i=0;i<maxLogEntries;i++){
        tmp = tmp + logRotateBuffer[counter] + "<br>";
        counter = (counter+1)%maxLogEntries;
      }
                    
      tmp = tmp + "</body></html>";
      
  wwwserver.send(200, "text/html", tmp);
  
  if (debugMode>=2){
          Serial.println((String)"[presentLogEntriesFromInternalLog]presentLogEntries called and executed.");
          writeDebugMessageToInternalLog((String)"[presentLogEntriesFromInternalLog]presentLogEntries called and executed.");
  }
}

/**
 * reportRainToLandXCape - since the rain sensor is no longer directly connected we use a relay to shortcut the sensor cables to allow the LandXCape mainboard to detect the rain or at least it things it rains 
 * Currently not used, but will be used as an Option within the Admin optionsThanks
 */

static void reportRainToLandXCape (void){

  if (debugMode>=1){
          Serial.println((String)"[reportRainToLandXCape]Rain sensor will be triggered to report the LandXcape mainboard that it rains or at least it thinks so.");
          writeDebugMessageToInternalLog((String)"[reportRainToLandXCape]Rain sensor will be triggered to report the LandXcape mainboard that it rains or at least it thinks so.");
  }
  digitalWrite(REGENSENSOR_LXC,LOW);
  delay(rainSensorShortcutTime);
  digitalWrite(REGENSENSOR_LXC,HIGH);
}

/**
 * getRainSensorStatus returns true if at least 3 of 5 values indicate rain 
 * ofterwise false
 */

static boolean getRainSensorStatus(void){

  if (debugMode>=2){
          Serial.println((String)"[getRainSensorStatus]Analysis of the last 10 sensor values has been triggered...");
          writeDebugMessageToInternalLog((String)"[getRainSensorStatus]Analysis of the last 10 sensor values has been triggered...");
  }

  if(ignoreRain==true){
    if (debugMode>=2){
          Serial.println((String)"[getRainSensorStatus]rain detection ignored as wished/selected via Admin site...");
          writeDebugMessageToInternalLog((String)"[getRainSensorStatus]rain detection ignored as wished/selected via Admin site...");
    }
    return false;
  }

  int amountOfPositiveRainValues = 0;

  for(int i=0;i<10;i++){
    amountOfPositiveRainValues= amountOfPositiveRainValues + rainSensorResults[i];
  }

  if (amountOfPositiveRainValues<=1){
    if (debugMode>=2){
            Serial.println((String)"[getRainSensorStatus]Result: It Rains - "+amountOfPositiveRainValues);
            writeDebugMessageToInternalLog((String)"[getRainSensorStatus]Result: It Rains - "+amountOfPositiveRainValues);
    }
      return true;
  }else{
    if (debugMode>=2){
            Serial.println((String)"[getRainSensorStatus]Result: It does not rain - "+amountOfPositiveRainValues);
            writeDebugMessageToInternalLog((String)"[getRainSensorStatus]Result: It does not rain - "+amountOfPositiveRainValues);
    }
      return false;
    }
}

/**
 * formatFS - call this function if a resetting for the FS is needed
 * return: true if successful or false otherwise
 *
*/

 static boolean formatFS (void){

  SPIFFS.format();

  //Restart the WEMOS-FileSystem to ensure a clean start of the file system - a direct restart of the Wemos at this point should work as well
  SPIFFS.end();
  //initialize filesystem
  fs::SPIFFSConfig filesystem_cfg; // to overcome the current SPIFFS "bug" in 2.5.2
  filesystem_cfg.setAutoFormat(false);
  SPIFFS.setConfig(filesystem_cfg);
  SPIFFS.begin(); //Restart the WEMOS to ensure a clean start of the file system - a direct restart at this point should work as well

  return true;
 }

 /**
 * writeWebServerFiles - to save RAM all sites will be written during the setup mode to the flash memory and furthermore only streamed to clients without using the RAM
 */

// static void writeWebServerFiles(const char* file_, char * content , int length ){
//  if (debugMode>=2){
//          Serial.println("[writeWebServerFiles]writeWebServerFiles has been called...");
//          writeDebugMessageToInternalLog((String)"[writeWebServerFiles]writeWebServerFiles has been called...");
//  }
//
//  //open file at FileSystem
//  File file = SPIFFS.open(file_,"w");
//  
//  if(!file){ //check if we have been able to open/create the file, if not abort
//      if (debugMode>=1){
//          Serial.println((String)"[writeWebServerFiles]"+ file_ +" file creation failed. Aborting...");
//          writeDebugMessageToInternalLog((String)"[writeWebServerFiles]"+ file_ +" file creation failed. Aborting...");
//      }
//      return;   
//  }
//  //writing content to file
//  file.write(content,length);
//  file.close();
//
//  if (debugMode>=1){
//          Serial.println((String)"[writeWebServerFiles]"+ file_ +" file created...");
//          writeDebugMessageToInternalLog((String)"[writeWebServerFiles]"+ file_ +" file created...");
//  }
// }
