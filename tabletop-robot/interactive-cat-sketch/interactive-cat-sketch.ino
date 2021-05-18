
#include <Servo.h>
#include <LedControl.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include "ledmatrix.h"

#define leftEye 4
#define rightEye 5
#define panicButtonPin 8
#define trigPin 9
#define echoPin 10

// Timeouts in millisec
#define apiTimeout 60000
#define panicButtonResetTimeout 1800000
#define pettingTimeout 720000
#define faceTimeout 5000

// Network connection
char ssid[] = "ENTER-WIFI-SSID";
char pass[] = "ENTER-WIFI-PASSWORD";
int status = WL_IDLE_STATUS; 
WiFiClient wifi;

// REST API connection
char serverAddress[] = "ENTER-BACKEND-IP";
int port = 9085;
HttpClient client = HttpClient(wifi, serverAddress, port);

// Servo, LED-matrix & state varaibles
Servo myservo;
LedControl lc = LedControl (3,1,2,1);
unsigned long timeKeeperApi;
unsigned long timeKeeperServo;
unsigned long timeKeeperPanic;
unsigned long timeKeeperFace;
long timeKeeperPetting = -pettingTimeout;
int panicButtonActive = 0;
int buttonState = 0;
int lastButtonState = 0;  
long duration;
int distance;
int servoPosition = 20;
int servoDirection = 1;
int servoSpeed = 3;
int eyeBrightness = 70;
int timeToNextEvent = -1;
int stressLevel = 0;
int servoTimeout = 50;
int isStandbyTime = 0;
int petted = 0;
int totalPanicCounter = 0;

void setup() {
  timeToNextEvent = 15;
  stressLevel = 30;
  
  Serial.begin (9600);
  pinMode(leftEye, OUTPUT);
  pinMode(rightEye, OUTPUT);
  pinMode(panicButtonPin,INPUT_PULLUP);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  myservo.attach(6);
  myservo.write(servoPosition);
  timeKeeperApi = millis();
  timeKeeperServo = millis();

  // LED matrix
  lc.shutdown (0,false);
  lc.setIntensity (0,1); // Brightness 0 = low; 8 = high
  delay (2000);
  lc.clearDisplay(0);
  displayMatrix(_smile);

  // wifi
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }
  setEyes(eyeBrightness);
}

void loop() {
  // check if network connected
  if (WiFi.SSID() != String(ssid)) {
    status = NULL;
    setup();
  }

  // reset panic button after timeout
  if (millis()-timeKeeperPanic > panicButtonResetTimeout) {
    Serial.println("panic button resetted");
    panicButtonActive = 0;
    timeKeeperPanic = millis();
  }

  // api request
  if(millis()-timeKeeperApi > apiTimeout) {
    Serial.println("making GET request");
    client.get("/mattis");
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();
    Serial.print("Status code: ");
    Serial.println(statusCode);
    Serial.print("Response: ");
    Serial.println(response);
    String stressLevelStr = getValue(response, ';', 1);
    String timeToEventStr = getValue(response, ';', 2);
    String standbyStr = getValue(response, ';', 3);
    if (stressLevelStr.length() > 0 && statusCode == 200) {
      stressLevel = stressLevelStr.toInt();
      Serial.print("New stress level: ");
      Serial.println(stressLevel);
      if (stressLevel != 0) {
        servoTimeout = 50 + (100 - stressLevel);
      }
      else {
        servoTimeout = 1000*60*2;
      }
    }
    if (timeToEventStr.length() > 0 && statusCode == 200) {
      timeToNextEvent = timeToEventStr.toInt();
      Serial.print("New time to next event: ");
      Serial.println(timeToNextEvent);

      //display counter before meeting
      if (timeToNextEvent == 9 && isStandbyTime == 0) {
        displayMatrix(_9);
      }
      if (timeToNextEvent == 8 && isStandbyTime == 0) {
        displayMatrix(_8);
      }
      if (timeToNextEvent == 7 && isStandbyTime == 0) {
        displayMatrix(_7);
      }
      if (timeToNextEvent == 6 && isStandbyTime == 0) {
        displayMatrix(_6);
      }
      if (timeToNextEvent == 5 && isStandbyTime == 0) {
        displayMatrix(_5);
      }
      if (timeToNextEvent == 4 && isStandbyTime == 0) {
        displayMatrix(_4);
      }
      if (timeToNextEvent == 3 && isStandbyTime == 0) {
        displayMatrix(_3);
      }
      if (timeToNextEvent == 2 && isStandbyTime == 0) {
        displayMatrix(_2);
      }
      if (timeToNextEvent == 1 && isStandbyTime == 0) {
        displayMatrix(_1);
      }
      if (timeToNextEvent == 0 && isStandbyTime == 0) {
        displayMatrix(_now);
      }
    }
    if (standbyStr.length() > 0 && statusCode == 200) {
      isStandbyTime = standbyStr.toInt();
      Serial.print("New is-standby-flag: ");
      Serial.println(isStandbyTime);
    }
    timeKeeperApi = millis();
  }

  if (isStandbyTime == 0) {
    // check sensors
    checkDistance();
    buttonState = digitalRead(panicButtonPin);
    checkButtonState();

    // check if recently petted
    if(millis()-timeKeeperPetting > pettingTimeout && panicButtonActive == 0) {
      // move servo
      if(millis()-timeKeeperServo > servoTimeout) {
        myservo.write(servoPosition);
        servoPosition = servoPosition + (servoSpeed*servoDirection);
        if (servoPosition > 110) {
          servoDirection = -1;
        }
        if (servoPosition < 10) {
          servoDirection = 1;
        }
        timeKeeperServo = millis();
      }
    }

    if(millis()-timeKeeperFace > faceTimeout && panicButtonActive == 0) {
      if (stressLevel < 20) {
        faceMatrix(_smile);
      }
      else if (stressLevel < 55) {
        faceMatrix(_normal);
      }
      else {
        faceMatrix(_sad);
      }
      timeKeeperFace = millis();
    }
    
    if (distance < 10 && panicButtonActive == 0) {
      setEyes(255);
      petted++;
      if (petted > 10) {
        timeKeeperPetting = millis();
        displayMatrix(_happy);
      }
    }
    else if (panicButtonActive) {
      setEyes(10);
      if (distance < 10) {
        petted++;
        if (petted > 10) {
          timeKeeperPetting = millis();
        }
      }
    }
    else {
      setEyes(eyeBrightness);
      petted = 0;
    }
  }
  else {
    // artefact is in night/standby mode (outside office hours)
    setEyes(0);
    lc.clearDisplay(0);
  }
}

String getValue(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void setEyes(int brightness) {
  analogWrite(leftEye, brightness);
  analogWrite(rightEye, brightness);
}

void checkDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;
}

void checkButtonState() {
  if (buttonState != lastButtonState) {
    if (buttonState == LOW) {
      // Button is pressed
      panicButtonActive=(panicButtonActive + 1)%2;
      timeKeeperPanic = millis();
      if (panicButtonActive) {
        lc.setIntensity (0,0);
        displayMatrix(_surprised);
        delay(1000);
        totalPanicCounter++;
        lc.clearDisplay(0);
      } else {
        lc.setIntensity (0,1);
        displayMatrix(_happy);
      }
    }
    delay(50);
  }
  lastButtonState = buttonState;
}

void displayMatrix(byte data[]) {
  for (int i = 0; i < 8; i++) {
    lc.setRow(0,i, data[i]);
  }
}

void faceMatrix(byte data[]) {
  if (timeToNextEvent < 0 || timeToNextEvent > 9) {
    displayMatrix(data);
  }
}
