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
#define RADSAFETYLIM 5000 //Should be 500k
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
#define SECRADSTABLEBEFOREBTSEND 0 

//D10:pin of card reader SDA. D9:pin of card reader RST
RFID rfid(10, 9);

//Setting the Bluetooth communication pins
SoftwareSerial bt(2,3);

//Variables used for the rfid-reader
unsigned char str[MAX_LEN]; //MAX_LEN is 16: size of the array
unsigned char cardID[4] = {105,159,199,86};
bool unauthorized = false;
long unauthorizedTime = 0; //Is long to be able to store the time and use it to show a message for a limited time.
bool technichIn = false;

//clock var
int hour, minute, clockSecond; 
long sysSeconds, seconds; //The reason to have multiple second variabels is to prevent race condition
bool updateTime, isSync = false;

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
long techAccRadAtClockOut;
long clockInTime, clockOutTime;
int techCurrRoom = BREAKROOM;
int estSecBefLim;
bool gotHazmatSuit = false;

//BT
String btMessage="";
bool sendNow = false;
long mesSentAt;
bool sendMessage[] = {false, false, false, false,false,false,false};
bool mesSent[] = {false, false, false, false,false, false, false};

//Display
LiquidCrystal lcd(8,7,6,5,4,1);
bool updateLCD = true;

/* 
 *  setup() only runs when the safety console boots and it make sure that everything is setup and initialized before the 
 *  program continues
 */
void setup() {

  //Technichian in or out led config
  pinMode(LEDPINTECHNICHIN, OUTPUT);
  pinMode(LEDPINWARNING,OUTPUT);
    
  //RFID
  SPI.begin();
  rfid.init();

  //Init BlueTooth serial port
  bt.begin(9600);
  
  //Init LCD
  lcd.begin(16,2);

  //Show message on display
  lcd.print("Please wait for");
  lcd.setCursor(3,1);
  lcd.print("time sync");
  
  FlexiTimer2::set(1000, timerInt);
  FlexiTimer2::start();
  while(!isSync){ //Sync time with app before start
    sendNow = true;
    sendMessage[MESGETTIME] = true;
    mesSent[MESGETTIME] = false;
    sendBTMes();
    delay(200);
    readBTCom();
  }
}

/*
 * loop() is executed directly after setup() and is then executed between 25-33 times per second depending mostly on the amount of
 * BT-communication, until the safety console shuts down. 
*/
void loop() {
  updateClock(); 
  rfidCheck();
  adcRead();
  updateTechAccRad(); //updateTechAccRad() must be after adcRead() and before sendBTMes() 
  readBTCom(); //should be before sendBTMes() to make sure that messages recieved by the mobile application isn´t sent again.
  sendBTMes();
  updateDisplay();
} 

/*
 * rfidCheck reads the serial number on the rfid-tag and checks if it is the authorized card.
 * If it is it toggles the technichIn bool and depending on the result it sets a couple of 
 * system variables (such as the time of the event) and which BT-messages that should be send. 
 */
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
              techCurrRoom = BREAKROOM;
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
              techAccRadAtClockOut = long(techAccRad);
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

/*
 * adcRead() reads the analogue inputs of the arduino and decides if any system variables need to be updated and 
 * if any BT-commands need to be sent to the mobile application.
 */

void adcRead(){
  int adcRadiation = analogRead(RADPIN);
  adcRadiation = (adcRadiation-25)/9;
  radMean = radMean-(radMean/10.0)+adcRadiation/10.0;
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

/*
 * sendBTMes() sends commands to the mobile application. At the moment it only sends messages when seconds is updated. 
 * The reason is to reduce the amount of messages sent. One message can contain all the different commands. 
 * Every command will be resent every second until the mobile application has answered that it has received it.
 */

void sendBTMes(){
  bool sendMessageEnd = false;
  if(sendNow){
    if (sendMessage[MESWAR]){
      if(!mesSent[MESWAR]){
        bt.print("W;");
        mesSent[MESWAR] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESWAR] = false;
      }
    }
    if (sendMessage[MESIN]){
      if(!mesSent[MESIN]){ 
        String message = "I " + createTimeMess(clockInTime);
        message += ";";
        bt.print(message);       
        mesSent[MESIN] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESIN] = false;
      }
    }
    if (sendMessage[MESOUT]){
      if(!mesSent[MESOUT]){
        String message = "O " + createTimeMess(clockOutTime);
        message += " ";
        message += techAccRadAtClockOut;
        message += ";";
        bt.print(message);
        mesSent[MESOUT] = true;
        techAccRad = TECHSTARTRAD;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESOUT] = false;
      }
    }  
    if(sendMessage[MESGETTIME]){
      if(!mesSent[MESGETTIME]){
        bt.print("T;");
        mesSent[MESGETTIME] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESGETTIME] = false;
      }
    }
    if(sendMessage[MESNEWROOM]){
      if(!mesSent[MESNEWROOM]){
        String message = "R ";
        message += techCurrRoom;
        message += ";";
        bt.print(message);
        mesSent[MESNEWROOM] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESNEWROOM] = false;
      }
    }
    if(sendMessage[MESHAZMAT]){
      if(!mesSent[MESHAZMAT]){
        String message = "P ";
        message += gotHazmatSuit;
        message += ";";
        bt.print(message);
        mesSent[MESHAZMAT] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESHAZMAT] = false;
      }
    }
    if(sendMessage[MESNEWSAFETIME]){
      if(!mesSent[MESNEWSAFETIME] ){
        String message = "L ";
        message += radLevel;
        message += " ";
        message += secToRadLim;
        message += ";";
        bt.print(message);
        mesSent[MESNEWSAFETIME] = true;
        sendMessageEnd = true;
        mesSentAt = seconds;
      }
      else if( ((mesSentAt+1)%SECONDSINONEDAY) == seconds) {
        mesSent[MESNEWSAFETIME] = false;
      }
    }
    
    if(sendMessageEnd){
      bt.println();
    }
    
    sendNow = false;
  }
}
/*
 * readBTCom() Receives commands from the mobile application. Most of the commands is only a confirmation that the 
 * mobile application has received the message. One command contains data needed too sync both systems clocks.
 */
void readBTCom(){
  if(bt.available() > 0)
    delay(1); //The delay is here to make sure that the hole message is revieved before reading it.
  while(bt.available()>0){
    btMessage += (char)bt.read();
  }
  char btCommand = btMessage[0];
  switch (btCommand){
    case 'W':
      sendMessage[MESWAR] = false;
      break;
    case 'I':
      sendMessage[MESIN] = false;
      break;
    case 'O':
      sendMessage[MESOUT] = false;
      break;
    case 'R':
      sendMessage[MESNEWROOM] = false;
      break;
    case 'P':
      sendMessage[MESHAZMAT] = false;
      break;
    case 'L':
      sendMessage[MESNEWSAFETIME] = false;
      break; 
    case BTCOMTIMESYNC:
      long hours = btMessage.substring(2,4).toInt();
      int minutes = btMessage.substring(5,7).toInt();
      int seconds = btMessage.substring(8,10).toInt();   
      sysSeconds = hours*3600+minutes*60+seconds;;
      isSync = true;
      sendMessage[MESGETTIME] = false;
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
/*
 * Is used to reduce the amount of code in the interrupt function (reducing the risk of a race condition). 
 * The code inside of updateClock is executed once every second and updates the variables holding the time 
 * and makes sure that other parts of the code that should be executed once every second is executed.
 */
void updateClock(){
  if(updateTime){
    seconds = sysSeconds;
    clockSecond = seconds%60;
    minute = (seconds/60)%60;
    hour = (seconds%86400)/3600;
    updateTime = false;
    updateAccRad = true;
    updateLCD = true; 
    sendNow = true;
  }
}

/*
 * createTimeMess(long) takes a argument containing a long with the number of seconds since last midnight. 
 * It then transforms this into a time in the format HH:MM:SS
 */
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

/*
 * updateTechAccRad() updates the accumulated radiation that the technician has received since the clock in (techAccRad)
 * and for how long it is safe for the technician to stay (secToRadLim). The code runs once every second or when 
 * something changes (room/radiation level/protective clothes/clock out/clock in). 
 * 
 */

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
    if(techAccRad >= RADSAFETYLIM && !techOverstayWarning){
      sendMessage[MESWAR] = true;
      techOverstayWarning = true;
    }
    updateAccRad = false;
  }
}

/*
 * updateDisplay() updates the information on the LCD-display and warning LED. The display updates every second or when something  
 * changes (room/radiation level/protective clothes/clock out/clock in). The information shown is dependent on if the technician 
 * is clocked in or out. If clocked out the facilities radiation level is shown and the information that the technician is clocked out.
 * If clocked in information about the facilities radiation level is shown and information about the technician such as protective clothes, 
 * current room and a timer showing for how long the technician can stay before accumulated radiation level is to high. 
 * If the technician overstays the timer is substituted with a warning message and the warning LED starts to blink. 
 * When a technician clocks in or out a message is shown for a couple of seconds. If the rfid-reader has discovered a unauthorized card 
 * a message is shown for a couple of seconds.
 */

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
                    if(HH > 99){
                      lcd.print(HH);
                      lcd.print("h   ");
                    }
                    else{
                      lcd.print(HH/10);//Hour Tens
                      lcd.print(HH%10);//
                      lcd.print(":");
                      lcd.print((secToRadLim%3600)/600);//Min tens
                      lcd.print(((secToRadLim%3600)/60)%10);//Min tens
                      lcd.print(":");
                      lcd.print((secToRadLim%60)/10);
                      lcd.print(secToRadLim%10);
                    }
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
