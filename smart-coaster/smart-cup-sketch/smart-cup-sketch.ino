#include "HX711.h"
#include "DHT.h"
#include <Wire.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

#define DHTPIN 2
#define DHTTYPE DHT11

// Network
char ssid[] = "ENTER-WIFI-SSID";
char pass[] = "ENTER-WIFI-PASSWORD";
int status = WL_IDLE_STATUS; 

int redPin= 5;
int greenPin = 4;
int bluePin = 3;

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 6;
const int LOADCELL_SCK_PIN = 7;

HX711 scale;
float X_out, Y_out;
char serverAddress[] = "ENTER-BACKEND-IP";
int port = 9085;

WiFiClient wifi;
HttpClient client = HttpClient(wifi, serverAddress, port);

DHT dht(DHTPIN, DHTTYPE);


void setup() {
  Serial.begin(9600);

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  setColor(255, 0, 0); // Red Color
  delay(1000);
  setColor(0, 255, 0); // Green Color
  delay(1000);
  setColor(0, 0, 255); // Blue Color
  delay(1000);

  // Scale setup
  Serial.println("Initializing the scale");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(2280.f);

  //Temp sensor setup
  dht.begin();
  
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }
  
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);

    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }
}

void loop() {
  // Network check
  if (WiFi.SSID() != String(ssid)) {
    status = NULL;
    setup();
  }
  Serial.println(WiFi.SSID());
  Serial.println("-----");
  
  // Scale reading
  int scaleInt = scale.get_units(10)+20;
  String scaleValue = String(scaleInt);
  Serial.print("Scale:\t");
  Serial.println(scaleValue);
  scale.power_down();

  // Temperature reading
  float t = dht.readTemperature();
  Serial.print("Temperature (celsius): ");
  Serial.println(t);

  // HTTP Request
  Serial.println("making GET request");
  client.get("/smart-cup?scale="+scaleValue+"&temperature="+String(t));

  // read the status code and body of the response
  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  Serial.println("----------------------------");


  if (scaleInt < -110) {
    if (t > 60) {
      setColor(100, 0, 0);
    }
    else if (t > 30) {
      setColor(0, 100, 0);
    }
    else {
      setColor(0, 0, 100);
    }
  }
  else {
    setColor(0, 0, 0);
  }

  // Wait
  delay(10000);
  scale.power_up();
}

void setColor(int redValue, int greenValue, int blueValue) {
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}
