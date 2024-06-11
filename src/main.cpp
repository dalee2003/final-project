#include <SparkFunLSM6DSO.h>
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <MAX30105.h>
#include "heartRate.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <HttpClient.h>

TFT_eSPI tft = TFT_eSPI(); //Create a TTGO Screen instance

//Define variables for LED and force sensor
int fsrPIN = 39; // Force Sensitive Resistor is connected to analog pin 15
int ledPIN = 32;  // White LED connect to pin 2
int fsrReading;  // The analog reading from the FSR resistor divider
bool ledON = false; //boolean value indicate whether the LED is ON or OFF
long lastSample = 0;
long clockSample = 0;

//Define variables for heart rate sensor
MAX30105 particleSensor; //Create heart rate sensor instance
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int beatAvg;
String stage[] = {"STAGE_CLOCK", "STAGE_SENSORS", "STAGE_FLASHLIGHT"};
int stageIndex = 0;
String currStage = "STAGE_CLOCK";

/*LSM6DSO sensor;
int totalSteps = 0;
long calibrateSample = 0;
bool calibrated = false;
float threshold = 0;
int counter = 0;*/

//Define variables for LSM6DS0
#define LSM_CS  33
#define LSM_MOSI  26
#define LSM_MISO  27
#define LSM_CLK  25

LSM6DSO myIMU; 
int data; 
SPIClass spi1(HSPI);
float rms;
int totalSteps = 0;
long calibrateSample = 0;
bool calibrated = false;
float threshold = 0;
int counter = 0;
bool fallDetected = false;

// WiFi credentials
const char* ssid = "Avocado Bear 2.4G";
const char* password = "ab04193839";

// NTP server details
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -28800; // Adjust for your timezone, e.g., -18000 for EST (GMT-5)
const int   daylightOffset_sec = 3600; // Adjust for daylight saving time

// Name of the server we want to connect to
const char kHostname[] = "worldtimeapi.org";
// Path to download (this is the bit after the hostname in the URL
// that you want to download
const char kPath[] = "/api/timezone/Europe/London.txt";
// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

void setup() {
  Serial.begin(9600);
  delay(500);
  // put your setup code here, to run once:=
  pinMode(ledPIN, OUTPUT); //Set ledPin to OUTPUT

  //Initialize screen
  tft.init();  // Initialize the display
  tft.setRotation(1);  // Set the display rotation (1 means 90 degrees)
  tft.fillScreen(TFT_BLACK);  // Clear the screen with black color
  tft.setTextColor(TFT_WHITE, TFT_BLACK);  // Set text color to white with black background
  tft.setTextSize(3);  // Set the text size
  pinMode(0, INPUT_PULLUP); //left button 
  pinMode(35, INPUT_PULLUP); //right button

  //Initialize LSM6DS0
  spi1.begin(LSM_CLK, LSM_MISO, LSM_MOSI, LSM_CS);
  delay(500);

  if(myIMU.beginSPI(LSM_CS, 10000000, spi1))// Load software interrupt related settings
    Serial.println("Ready.");
  else {
    Serial.println("Could not connect to IMU.");
    Serial.println("Freezing");
  }

  if( myIMU.initialize(SOFT_INT_SETTINGS)) 
    Serial.println("Loaded Settings.");

  // Initialize heart rate sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30105 was not found. Please check wiring/power. ");
    while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  particleSensor.setup(); //Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); //Turn off Green LED

  // Initialize serial communication
  //Serial.begin(115200);
   // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");

  // Initialize and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void loop() {
  if (!calibrated){
    unsigned long currentTime = millis();
    if (currentTime-calibrateSample>=100){
      calibrateSample = currentTime;
      float x = myIMU.readFloatAccelX();
      float y = myIMU.readFloatAccelY();
      float result = sqrt(x*x + y*y);
      Serial.println(result, 3);
      if (result > threshold){
        threshold = result;
      }
      counter++;
      if (counter>=50){
        calibrated = true;
        threshold = threshold * 0.8;
        Serial.println("Finish calibrating");
      }
    }
  }

  if (currStage == "STAGE_CLOCK"){
    //Serial.println("update clock");
    //Serial.println(millis() - lastSample);
    if ((millis() - clockSample)>=1000){
      clockSample = millis();
      // Get the current time
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
      }

      // Display the time on the TFT display
      tft.fillScreen(TFT_BLACK); // Clear the screen
      tft.setCursor(10, 10);
      tft.setTextSize(4);

      // Format and display the time
      char timeString[10];
      strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
      tft.printf("Time: \n%s\n", timeString);
      tft.setTextSize(2);
      
      tft.setCursor(10, 80); 
      char dateString[64];
      strftime(dateString, sizeof(dateString), "%A, %B %d %Y", &timeinfo);
      tft.println(dateString);
    }
  }

  
  if ((millis() - lastSample)>=200){
    lastSample = millis();
    // LED and force sensor
    fsrReading = analogRead(fsrPIN);
    Serial.print("Analog reading = ");
    Serial.println(fsrReading);
  
    
    if (currStage == "STAGE_FLASHLIGHT" and fsrReading>=500){
      if (ledON){
        ledON = false;
        digitalWrite (ledPIN, LOW);
      }
      else{
        ledON = true;
        digitalWrite (ledPIN, HIGH);
      }
    }
    //button to switch stages
    int buttonOne = digitalRead(0);
    int buttonTwo = digitalRead(35);
    if (buttonOne == 0 or buttonTwo == 0){
      if (currStage == "STAGE_SENSORS"){
        int err = 0;
        WiFiClient c;
        HttpClient http(c);
        
        char front[] = "/?var=HeartRate:"; 
        char heartRate[50];

        snprintf(heartRate,sizeof(heartRate), "%s%d", front, beatAvg);
        Serial.println(heartRate);

        if (!fallDetected){
          err = http.get("3.95.216.4", 5000, heartRate, NULL);
        }
        else{
          //char result[100] = "/?var=60";
          
          char fall[] = ";FallDetected"; 
          char result[100];
          snprintf(result,sizeof(result), "%s%s", heartRate, fall);
          Serial.println(result);
          
          err = http.get("3.95.216.4", 5000, result, NULL);
        }
        
        if (err == 0) {
          Serial.println("startedRequest ok");
          err = http.responseStatusCode();
          if (err >= 0) {
            Serial.print("Got status code: ");
            Serial.println(err);
            // Usually you'd check that the response code is 200 or a
            // similar "success" code (200-299) before carrying on,
            // but we'll print out whatever response we get
            err = http.skipResponseHeaders();
            if (err >= 0) {
              int bodyLen = http.contentLength();
              Serial.println("Body returned follows:");
              // Now we've got to the body, so we can print it out
              unsigned long timeoutStart = millis();
              char c;
              // Whilst we haven't timed out & haven't reached the end of the body
              while ((http.connected() || http.available()) && ((millis() - timeoutStart) < kNetworkTimeout)) {
                if (http.available()) {
                  c = http.read();
                  // Print out this character
                  Serial.print(c);
                  bodyLen--;
                  // We read something, reset the timeout counter
                  timeoutStart = millis();
                } else {
                  // We haven't got any data, so let's pause to allow some to
                  // arrive
                  delay(kNetworkDelay);
                }
              }
            } else {
              Serial.print("Failed to skip response headers: ");
              Serial.println(err);
            }
          } else {
            Serial.print("Getting response failed: ");
            Serial.println(err);
          }
        } else {
          Serial.print("Connect failed: ");
          Serial.println(err);
        }
        http.stop();
        fallDetected = false;
      }

      if (buttonOne == 0){
        Serial.println("Left Button Pressed");
        if (stageIndex == 2){stageIndex = 0;}
        else{stageIndex++;}
        currStage = stage[stageIndex];
        Serial.println("Current stage: "+currStage);
      }
      if (buttonTwo == 0){
        Serial.println("Right Button Pressed");
        if (stageIndex == 0){stageIndex = 2;}
        else{stageIndex--;}
        currStage = stage[stageIndex];
        Serial.println("Current stage: "+currStage);
      }
      if (currStage == "STAGE_FLASHLIGHT"){
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 10);  // Set the cursor position
        tft.print("FLIGHTLIGHT READY");  // Print the text label
      }
      else if (currStage == "STAGE_CLOCK"){
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 10);  // Set the cursor position
        tft.print("CLOCK READY");  // Print the text label
      }
      else{
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 10);  // Set the cursor position
        tft.print("SENSORS READY");  // Print the text label
      }
    }
  }

  if (currStage == "STAGE_SENSORS"){
    //heart rate sensor
    long irValue = particleSensor.getIR();

    if (checkForBeat(irValue) == true)
    {
      //We sensed a beat!
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20)
      {
        rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
        rateSpot %= RATE_SIZE; //Wrap variable

        //Take average of readings
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
    Serial.print("IR=");
    Serial.print(irValue);
    Serial.print(", BPM=");
    Serial.print(beatsPerMinute);
    Serial.print(", Avg BPM=");
    Serial.print(beatAvg);

    tft.fillScreen(TFT_BLACK);
    if (irValue < 50000){
      tft.setCursor(10, 10);  // Set the cursor position
      tft.print("No finger?");  // Print the text label
    } else{
      tft.setCursor(10, 10);  // Set the cursor position
      tft.print("BPM: ");  // Print the text label
      tft.print(beatsPerMinute);  // Print the counter value

      tft.setCursor(10, 40);  // Set the cursor position
      tft.print("Avg BPM: ");  // Print the text label
      tft.print(beatAvg);
    }

    //Accelerometer 
    //bool fallDetected = false;
    if (millis()-calibrateSample >= 20) {
      calibrateSample = millis();
      data = myIMU.listenDataReady();
      if( data == ALL_DATA_READY ){
        rms = sqrt(pow(myIMU.readFloatAccelX(), 2) + pow(myIMU.readFloatAccelY(), 2));
      }

      tft.setCursor(10, 70);  // Set the cursor position
      tft.print("Accel: ");  // Print the text label
      tft.print(rms);
      
    
      if (rms > 0.7){
        tft.setCursor(10, 60);  // Set the cursor position
        tft.println("FALL DETECTED!!!");  // Print the text label
        tft.print("Accel: ");
        tft.print(rms);
        fallDetected = true;
      }

      if (rms > threshold) {
        totalSteps++;
        Serial.println(totalSteps);
      }

      tft.setCursor(10, 110);  // Set the cursor position
      tft.print("Steps: ");  // Print the text label
      tft.print(totalSteps);
    }
  }
}
