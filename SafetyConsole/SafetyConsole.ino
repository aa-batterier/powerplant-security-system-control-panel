#include <SPI.h>
#include <RFID.h>
#include <SoftwareSerial.h>
#include <FlexiTimer2.h>
#include <LiquidCrystal.h>


#define NUMINIDENTIFICATION 4
#define PUSHBUTTONTHREASHOLD 500
#define RADPIN A4
#define BREAKROOMPIN A3
#define CONTROLROOMPIN A2
#define REACTORROOMPIN A1
#define HAZMATSUITPIN A0
#define LEDPINTECHNICHIN 0
#define LEDPINWARNING 0
#define RADBREAKROOM 0.1
#define BREAKROOM 1
#define CONTROLROOM 2
#define REACTORROOM 3
#define RADCONTROLROOM 0.5
#define RADREACTORROOM 1.6
#define RADSAFETYLIM 500000
#define HAZMATSUITCOF 5
#define NORMALCLOTHESCOF 1
#define BTCOMTIMESYNC 'T'
#define TECHSTARTRAD 0
#define MESWAR 0
#define MESIN 1
#define MESOUT 2
#define MESGETTIME 3
#define MESNEWSAFETIME 4
#define MESNEWROOM 5
#define MESHAZMAT 6
#define SYNCTIME 86400
#define SECONDSINONEDAY 86400
#define SECRADSTABLEBEFOREBTSEND 2

//D10:pin of card reader SDA. D9:pin of card reader RST
RFID rfid(10, 9);

SoftwareSerial bt(2,3);

unsigned char str[MAX_LEN]; //MAX_LEN is 16: size of the array
unsigned char cardID[4] = {105,159,199,86};
bool unauthorized = false;
long unauthorizedTime = 0; //Is long to be able to store the time and use it to show a message for a limited time.
bool technichIn;

//clock var
int hour, minute, clockSecond; 
long sysSeconds, seconds; //The reason to have multiple second variabels is to prevent race condition
bool updateTime, isSync;

//radiation variables
bool updateAccRad = false, updateSafetyLimTime = false;
bool techOverstayWarning = false;
int radLevel;
float radMean;
long secToRadLim;
long sendRadAtTime;
bool radRecentlyChanged = false;

//technichian variabels
float techAccRad = TECHSTARTRAD;
long clockInTime, clockOutTime;
int techCurrRoom = BREAKROOM;
int estSecBefLim;
bool gotHazmatSuit = false;

//BT
String btMessage="";
bool sendMessage[] = {false, false, false, false,false,false,false};
bool mesSent[] = {false, false, false, false,false, false, false};

//Display
LiquidCrystal lcd(8,7,6,5,4,1);
bool updateLCD = true;

void setup() {
  technichIn = false;
  isSync = false;

  //Technichian in or out led config
  pinMode(LEDPINTECHNICHIN, OUTPUT);
  pinMode(LEDPINWARNING,OUTPUT);
  
  //Computer serial
  //Serial.begin(9600);
  
  //RFID
  SPI.begin();
  rfid.init();

  //Init BlueTooth serial port
  bt.begin(9600);
  
  //Init LCD
  lcd.begin(16,2);
 
  lcd.print("Please wait for");
  lcd.setCursor(3,1);
  lcd.print("time sync");
  FlexiTimer2::set(1000, timerInt);
  FlexiTimer2::start();
  while(!isSync){ //Sync time with app before start
    sendMessage[MESGETTIME] = true;
    mesSent[MESGETTIME] = false;
    sendBTMes();
    delay(200);
    readBTCom();
  }
}

void loop() {
  //recieve BT-message
  updateClock();
  rfidCheck();
  adcRead();
  updateTechAccRad(); //updateTechAccRad() must be after adcRead() and before sendBTMes() 
  readBTCom();
  sendBTMes();
  updateDisplay();
} 

void rfidCheck(){
  if (rfid.findCard(PICC_REQIDL, str) == MI_OK) {
    //Anti-collision detection, read card serial number
    if (rfid.anticoll(str) == MI_OK) {
      int equalCounter = 0;
      for (int i = 0; i < NUMINIDENTIFICATION; i++) {
        if(str[i] == cardID[i])
          equalCounter++;   
        else
          break;
      }      
      if(equalCounter == NUMINIDENTIFICATION){ //the cardID is ok
          technichIn = !technichIn;
          unauthorized = false;
          if(technichIn){
              clockInTime = seconds;
              updateSafetyLimTime = true;
              sendMessage[MESIN] = true;
              mesSent[MESIN] = false;  
              sendMessage[MESNEWROOM] = true;
              mesSent[MESNEWROOM] = false;
              sendMessage[MESHAZMAT] = true;
              mesSent[MESHAZMAT] = false;
              sendMessage[MESNEWSAFETIME] = true;
              mesSent[MESNEWSAFETIME] = false; 
          }
          else{
              clockOutTime = seconds;
              techOverstayWarning = false;
              sendMessage[MESOUT] = true;
              mesSent[MESOUT] = false;              
          }
          updateLCD = true;
      }
      else{
          unauthorized = true;
          unauthorizedTime = seconds;
          updateLCD = true;
      }
    }
    //card selection (lock card to prevent redundant read, removing the line will make the sketch read cards continually)
    rfid.selectTag(str);
  }
  rfid.halt(); // command the card to enter sleeping state 
}

void adcRead(){
  int adcRadiation = analogRead(RADPIN);
  adcRadiation = (adcRadiation-350)/3;
  radMean = radMean-(radMean/5.0)+adcRadiation/5.0;
  int radMeanTrunk = radMean;
  if(radMeanTrunk > 100){
    radMeanTrunk = 100; 
  }
  else if(radMeanTrunk < 1){
    radMeanTrunk = 1;
  }
  if(radMeanTrunk != radLevel && technichIn){
      radRecentlyChanged = true;
      sendRadAtTime = seconds;
      updateSafetyLimTime = true; 
  } 
  if(radRecentlyChanged && sendRadAtTime < ( (seconds-SECRADSTABLEBEFOREBTSEND) % SECONDSINONEDAY) ){
      radRecentlyChanged = false;
      updateSafetyLimTime = true;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;   
  }
  radLevel = radMeanTrunk;
  if(technichIn){
    if(analogRead(BREAKROOMPIN) < PUSHBUTTONTHREASHOLD && techCurrRoom != BREAKROOM){
      techCurrRoom = BREAKROOM;
      updateSafetyLimTime = true;
      sendMessage[MESNEWROOM] = true;
      mesSent[MESNEWROOM] = false;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;    
    }
    if(analogRead(CONTROLROOMPIN) < PUSHBUTTONTHREASHOLD && techCurrRoom != CONTROLROOM){
      techCurrRoom = CONTROLROOM;
      updateSafetyLimTime = true;
      sendMessage[MESNEWROOM] = true;
      mesSent[MESNEWROOM] = false;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;
    }
    if(analogRead(REACTORROOMPIN) < PUSHBUTTONTHREASHOLD && techCurrRoom != REACTORROOM){
      techCurrRoom = REACTORROOM;
      updateSafetyLimTime = true;
      sendMessage[MESNEWROOM] = true;
      mesSent[MESNEWROOM] = false;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;
    }
    int adcHazmat = analogRead(HAZMATSUITPIN);
    if(adcHazmat < PUSHBUTTONTHREASHOLD && gotHazmatSuit){
      gotHazmatSuit = false;
      updateSafetyLimTime = true;
      sendMessage[MESHAZMAT] = true;
      mesSent[MESHAZMAT] = false;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;
    }
    else if(adcHazmat > PUSHBUTTONTHREASHOLD && !gotHazmatSuit){
      updateSafetyLimTime = true;
      gotHazmatSuit = true;
      sendMessage[MESHAZMAT] = true;
      mesSent[MESHAZMAT] = false;
      sendMessage[MESNEWSAFETIME] = true;
      mesSent[MESNEWSAFETIME] = false;
    }
  }
}
void sendBTMes(){
  if (sendMessage[MESWAR] && !mesSent[MESWAR]){
    bt.println("W ");
    mesSent[MESWAR] = true;    
  }
  if (sendMessage[MESIN] && !mesSent[MESIN]){
    String message = "I " + createTimeMess(clockInTime);
    message += " ";
    bt.println(message);       
    mesSent[MESIN] = true;
  }
  if (sendMessage[MESOUT] && !mesSent[MESOUT]){
    String message = "O " + createTimeMess(clockOutTime);
    message += " ";
    message += long(techAccRad);
    message += " ";
    bt.println(message);
    mesSent[MESOUT] = true;
    techAccRad = TECHSTARTRAD;
  }  
  if(sendMessage[MESGETTIME] && !mesSent[MESGETTIME]){
    bt.println("T ");
    mesSent[MESGETTIME] = true;
  }
  if(sendMessage[MESNEWROOM] && !mesSent[MESNEWROOM]){
    String message = "R ";
    message += techCurrRoom;
    message += " ";
    bt.println(message);
    mesSent[MESNEWROOM] = true;
  }
  if(sendMessage[MESHAZMAT] && !mesSent[MESHAZMAT]){
    String message = "P ";
    message += gotHazmatSuit;
    message += " ";
    bt.println(message);
    mesSent[MESHAZMAT] = true;
  }
  if(sendMessage[MESNEWSAFETIME] && !mesSent[MESNEWSAFETIME] ){
    String message = "L ";
    message += radLevel;
    message += " ";
    message += secToRadLim;
    message += " ";
    bt.println(message);
    mesSent[MESNEWSAFETIME] = true;
  }
}

void readBTCom(){
  if(bt.available() > 0)
    delay(1); //The delay is here to make sure that the hole message is revieved before reading it.
  while(bt.available()>0){
    btMessage += (char)bt.read();
  }
  char btCommand = btMessage[0];
  switch (btCommand){
  case BTCOMTIMESYNC:
    //digitalWrite(LEDPINTECHNICHIN, HIGH);
    
    long hours = btMessage.substring(2,4).toInt();
    int minutes = btMessage.substring(5,7).toInt();
    int seconds = btMessage.substring(8,10).toInt();   
    sysSeconds = hours*3600+minutes*60+seconds;;
    isSync = true;
    break;
  }  
  btMessage = "";    
}

// the timer interrupt function of FlexiTimer2 is executed every 1s 
void timerInt(){ 
  sysSeconds++;  
  updateTime = true;
  if(sysSeconds >= SYNCTIME){
    sendMessage[MESGETTIME] = true;
    mesSent[MESGETTIME] = false;    
  }
}

void updateClock(){
  if(updateTime){
    seconds = sysSeconds;
    clockSecond = seconds%60;
    minute = (seconds/60)%60;
    hour = (seconds%86400)/3600;
    updateTime = false;
    updateAccRad = true;
    updateLCD = true; 
  }
}

String createTimeMess(long secSinceMidNig){
  String message = "";
  int hourSinceMidNight = (secSinceMidNig%86400)/3600; 
  int minutesSinceMid = (secSinceMidNig/60)%60;
  secSinceMidNig %= 60;
  if(hourSinceMidNight < 1){
    message = "00";
  }
  else if(hourSinceMidNight < 10){
    message = "0";
    message += hourSinceMidNight;
  }
  else{
    message = hourSinceMidNight;
  }
  message += ":";
  if(minutesSinceMid < 1){
    message += "00";
  }
  else if(minutesSinceMid < 10){
    message += "0";
    message += minutesSinceMid;
  }
  else{
    message += minutesSinceMid;
  }
  message += ":";
  if(secSinceMidNig < 1){
    message += "00";
  }
  else if(secSinceMidNig < 10){
    message += "0";
    message += secSinceMidNig;
  }
  else{
    message += secSinceMidNig;
  }
  return message;
}

void updateTechAccRad(){
  if((updateAccRad || updateSafetyLimTime) && technichIn){    
    float roomRadCof;
    switch (techCurrRoom){
      case BREAKROOM:
        roomRadCof = RADBREAKROOM;
        break;
      case CONTROLROOM:
        roomRadCof = RADCONTROLROOM;
        break;
      case REACTORROOM:
        roomRadCof = RADREACTORROOM;
        break;
      default:
        roomRadCof = 1;
        break;
    }
    
    int clothRadProCof = NORMALCLOTHESCOF;
    if(gotHazmatSuit){
      clothRadProCof = HAZMATSUITCOF;
    }
    float radPerSec = (radLevel*roomRadCof)/clothRadProCof;
    if(updateAccRad){
      techAccRad += radPerSec;  
    }    
    secToRadLim = (RADSAFETYLIM - techAccRad)/radPerSec;
    if(techAccRad >= RADSAFETYLIM){
      sendMessage[MESWAR] = true;
      techOverstayWarning = true;
    }
    updateAccRad = false;
  }
}

void updateDisplay(){ 
    if (updateLCD){
        lcd.setCursor(0,0);
        if(technichIn){
            //recent clock in.
            if( ((clockInTime+3)%84600) > seconds ){
                lcd.clear();
                lcd.print("Welcome T1");
            }
            else{
                lcd.print("T1 in P=");
                if(gotHazmatSuit){
                    lcd.print("Hz ");
                }
                else{
                    lcd.print("No ");
                }
                if(techCurrRoom == BREAKROOM){
                    lcd.print("BreRo");
                }
                else if(techCurrRoom == CONTROLROOM){
                    lcd.print("ConRo");
                }
                else{
                    lcd.print("ReaRo");
                }
                lcd.setCursor(0,1);
                if(secToRadLim < 0){
                    if(secToRadLim%3){
                        lcd.print("T1 must out");
                    }
                    else{
                        lcd.print("WARNING!!! ");
                    }            
                }
                else{
                    lcd.print("L=");
                    int HH = secToRadLim/3600;
                    int MM = (secToRadLim%3600)/60;
                    lcd.print((secToRadLim/3600)/10);//Hour Tens
                    lcd.print((secToRadLim/3600)%10);//
                    lcd.print(":");
                    lcd.print((secToRadLim%3600)/600);//Min tens
                    lcd.print(((secToRadLim%3600)/60)%10);//Min tens
                    lcd.print(":");
                    lcd.print((secToRadLim%60)/10);
                    lcd.print(secToRadLim%10);
                }
                lcd.print(" ");
            }
        }
        //Technician is out
        else{ 
            //recent clock out
            if( ((clockOutTime+3)%84600) >seconds){
                lcd.clear();
                lcd.print("Good bye T1");
            }
            else{
                lcd.print("T1 out              ");
                lcd.setCursor(0,1);
                lcd.print("           ");
            }
        }

        if(unauthorized){
            if( ((unauthorizedTime + 4)%84600) > seconds){
                lcd.setCursor(0,0);
                lcd.print("Unauthorized    ");
                lcd.setCursor(0,1);
                lcd.print("card          ");
            } 
            else{
                unauthorized = false;
            }
        }
        
        lcd.setCursor(11,1);
        lcd.print("R=");
        lcd.print(radLevel);
        lcd.print("  ");
    }
     
    if(techOverstayWarning && seconds%2){
        digitalWrite(LEDPINWARNING,HIGH);
    }
    else{
        digitalWrite(LEDPINWARNING, LOW);
    }
    updateLCD = false;
}
