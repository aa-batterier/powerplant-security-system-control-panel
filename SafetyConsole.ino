#include <SPI.h>
#include <RFID.h>
#include <SoftwareSerial.h>
#include <FlexiTimer2.h>
#include <LiquidCrystal.h>


#define NUMINIDENTIFICATION 4
#define LEDPINTECHNICHIN 2
#define LEDPINWARNING 2
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
#define TECHSTARTRAD 499950
#define MESWAR 0
#define MESIN 1
#define MESOUT 2
#define MESGETTIME 3
#define MESNEWSAFETIME 4

//D10:pin of card reader SDA. D9:pin of card reader RST
RFID rfid(10, 9);

SoftwareSerial bt(0,1);

unsigned char str[MAX_LEN]; //MAX_LEN is 16: size of the array
unsigned char cardID[4] = {105,159,199,86};

bool technichIn;

//clock var
int hour, minute, second;
bool isSync;

//radiation variables
bool updateAccRad = false;
bool techOverstayWarning = false;
int radLevel = 30;
int secToRadLim;

//technichian variabels
float techAccRad = TECHSTARTRAD;
int techCurrRoom = BREAKROOM;
int estSecBefLim;
bool gotHazmatSuit = false;

//BT
String btMessage="";
bool sendMessage[] = {false, false, false, false,false};
bool mesSent[] = {false, false, false, false,false};

//Display
LiquidCrystal lcd(8,7,6,5,4,3);

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
  lcd.print("Test");
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
  updateTime();
  rfidCheck();
  updateTechAccRad();
  sendBTMes();
  readBTCom();
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
        if(technichIn){
          sendMessage[MESIN] = true;
          mesSent[MESIN] = false;  
          sendMessage[MESNEWSAFETIME] = true;
          mesSent[MESNEWSAFETIME] = false;
        }
        else{
          techOverstayWarning = false;
          techAccRad = TECHSTARTRAD;  
          sendMessage[MESOUT] = true;
          mesSent[MESOUT] = false;      
        }
      }
      //else
       // Serial.println("Not authorized");
      //Serial.println("");
    }
    //card selection (lock card to prevent redundant read, removing the line will make the sketch read cards continually)
    rfid.selectTag(str);
  }
  rfid.halt(); // command the card to enter sleeping state 
}

void sendBTMes(){
  if (sendMessage[MESWAR] && !mesSent[MESWAR]){
    bt.println("W");
    mesSent[MESWAR] = true;    
  }
  if (sendMessage[MESIN] && !mesSent[MESIN]){
    String message = "I " + createTimeMess();
    bt.println(message);       
    mesSent[MESIN] = true;
  }
  if (sendMessage[MESOUT] && !mesSent[MESOUT]){
    String message = "O " + createTimeMess();
    message += " ";
    message += techAccRad;
    bt.println(message);
    mesSent[MESOUT] = true;
  }
  if(sendMessage[MESNEWSAFETIME] && !mesSent[MESNEWSAFETIME]){
    String message = "L ";
    message += +secToRadLim;
    bt.println(message);
    mesSent[MESNEWSAFETIME] = true;
  }
  if(sendMessage[MESGETTIME] && !mesSent[MESGETTIME]){
    bt.println("T");
    mesSent[MESGETTIME] = true;
  }
}

void readBTCom(){
  if(bt.available()>0)
    delay(1); //The delay is here to make sure that the hole message is revieved before reading it.
  while(bt.available()>0){
    btMessage += (char)bt.read();
  }
  char btCommand = btMessage[0];
  switch (btCommand){
  case BTCOMTIMESYNC:
    digitalWrite(LEDPINTECHNICHIN, HIGH);
    hour = btMessage.substring(2,4).toInt();
    minute = btMessage.substring(5,7).toInt();
    second = btMessage.substring(8,10).toInt();
    isSync = true;
    break;
  }  
  btMessage = "";    
}

void updateDisplay(){   
  if(technichIn)
    digitalWrite(LEDPINTECHNICHIN, HIGH);
  else
    digitalWrite(LEDPINTECHNICHIN, LOW);
  if(techOverstayWarning && second%2){
    digitalWrite(LEDPINWARNING,HIGH);
  }
  else{
    digitalWrite(LEDPINWARNING, LOW);
  }
}

// the timer interrupt function of FlexiTimer2 is executed every 1s 
void timerInt(){ 
  second++; 
  updateAccRad = true;
  //Serial.println(techAccRad);// second plus 1 
}

void updateTime(){
  if(second >= 60){
    second = 0;
    minute++;
    if(minute >=60){
      minute = 0;
      hour++;
      if(hour >=24){
        hour = 0;
      }
    }
  }
}

String createTimeMess(){
  String message = "";
  if(hour<1){
    message = "00";
  }
  else if(hour<10){
    message = "0";
    message += hour;
  }
  else{
    message = hour;
  }
  message += ":";
  if(minute<1){
    message += "00";
  }
  else if(minute<10){
    message += "0";
    message += minute;
  }
  else{
    message += minute;
  }
  message += ":";
  if(second<1){
    message += "00";
  }
  else if(second<10){
    message += "0";
    message += second;
  }
  else{
    message += second;
  }
  return message;
}

void updateTechAccRad(){
  if(updateAccRad && technichIn){    
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
    if(gotHazmatSuit)
      clothRadProCof =HAZMATSUITCOF;
    float radPerSec = (radLevel*roomRadCof)/clothRadProCof;
    techAccRad += radPerSec;
    secToRadLim = (RADSAFETYLIM - techAccRad)/radPerSec;
    if(techAccRad >= RADSAFETYLIM){
      sendMessage[MESWAR] = true;
      techOverstayWarning = true;
    }
    updateAccRad = false;
  }
}
