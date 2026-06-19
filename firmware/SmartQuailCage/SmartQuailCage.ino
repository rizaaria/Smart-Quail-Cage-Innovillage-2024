#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include "RTClib.h"

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define WIFI_SSID ""         
#define WIFI_PASSWORD "" 

#define API_KEY ""
#define DATABASE_URL ""

#define DHTPIN 13          
#define DHTTYPE DHT22      
#define AMMONIA_PIN 35     
#define FAN1_RELAY_PIN 25  
#define FAN2_RELAY_PIN 26  
#define CLEAN_MOTOR_PIN 27 
#define FEED_MOTOR_PIN 18  

#define BUTTON_FAN_PIN 32   
#define BUTTON_FEED_PIN 33  
#define BUTTON_CLEAN_PIN 15 

LiquidCrystal_I2C lcd(0x27, 16, 2); 
DHT dht(DHTPIN, DHTTYPE);
Servo feedServo;
RTC_DS3231 rtc;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

float suhu, kelembapan;
int sensorValue;
int ppm;
bool fanState = false;
bool feedMotorState = false;
bool cleanMotorState = false;
bool wifiConnected = false;
bool autoMode = false;

unsigned long previousMillis = 0;
const long interval = 2000;

void displayStatus(const char *message){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(message);
  delay(1000);
  lcd.clear();
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 5000) {
    delay(1000);
  }
  wifiConnected = WiFi.status() == WL_CONNECTED;
}

void initializeFirebase() {
  if (wifiConnected) {
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    signupOK = Firebase.signUp(&config, &auth, "", "");
  }
}

void initializeHardware() {
  if (!rtc.begin()) {
    Serial.println("RTC tidak ditemukan");
    while (1);
  }

  if(rtc.lostPower()){
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  dht.begin();
  lcd.init();
  lcd.backlight();

  pinMode(FAN1_RELAY_PIN, OUTPUT);
  pinMode(FAN2_RELAY_PIN, OUTPUT);
  pinMode(CLEAN_MOTOR_PIN, OUTPUT);
  pinMode(BUTTON_FAN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_FEED_PIN, INPUT_PULLUP);
  pinMode(BUTTON_CLEAN_PIN, INPUT_PULLUP);

  feedServo.attach(FEED_MOTOR_PIN);
  feedServo.write(0);
  digitalWrite(FAN1_RELAY_PIN, HIGH);
  digitalWrite(FAN2_RELAY_PIN, HIGH);
  digitalWrite(CLEAN_MOTOR_PIN, HIGH);
}

int readMQ135() {
  sensorValue = analogRead(AMMONIA_PIN);
  ppm = pow(10, (log10((((3.3 * 10.0) / ((sensorValue / 4095.0) * 3.3)) - 10.0) / 10.0)) - 0.430) / -0.417;
  return max(ppm, 0);
}

void readSensors() {
  suhu = dht.readTemperature();
  kelembapan = dht.readHumidity();
  lcd.setCursor(0, 0);
  lcd.print("Suhu: ");
  lcd.print(suhu);
  lcd.print(" C");

  lcd.setCursor(0, 1);
  lcd.print("Amonia:");
  lcd.print(readMQ135());
  lcd.print(" Hum:");
  lcd.print(kelembapan);
}

void updateFirebase() {
  if (Firebase.ready() && signupOK) {
    Firebase.RTDB.setFloat(&fbdo, "DHT/kelembapan", kelembapan);
    Firebase.RTDB.setFloat(&fbdo, "DHT/suhu", suhu);
    Firebase.RTDB.setFloat(&fbdo, "MQ135/ammonia", readMQ135());
  }
}

void readFirebase() {
  if (Firebase.RTDB.getString(&fbdo, "/Control/AutoMode")) {
    String firebaseAutoMode = fbdo.stringData();
    bool firebaseAutoModeState = (firebaseAutoMode == "true") || (firebaseAutoMode == "1");
    if (firebaseAutoModeState != autoMode) {
      autoMode = firebaseAutoMode;
      Serial.println(autoMode ? "Mode diubah ke otomatis melalui Firebase" : "Mode diubah ke manual melalui Firebase");
    }
  }
  if(!autoMode){
    if (Firebase.RTDB.getString(&fbdo, "/Control/Fan")) {
    String fanData = fbdo.stringData();
    bool firebaseFanState = (fanData == "true") || (fanData == "1");
      if (firebaseFanState != fanState) {
        fanState = firebaseFanState;
        digitalWrite(FAN1_RELAY_PIN, fanState ? LOW : HIGH);
        digitalWrite(FAN2_RELAY_PIN, fanState ? LOW : HIGH);
        Serial.println(fanState ? "Kipas dinyalakan melalui Firebase." : "Kipas dimatikan melalui Firebase.");
      }
    }

    if (Firebase.RTDB.getString(&fbdo, "/Control/FeedMotor")) {
      String feedMotorData = fbdo.stringData();
      bool firebaseFeedMotorState = (feedMotorData == "true") || (feedMotorData == "1");
      if (firebaseFeedMotorState != feedMotorState) {
        feedMotorState = firebaseFeedMotorState;
        feedServo.write(feedMotorState ? 60 : 0); // Posisi servo (90 derajat untuk ON, 0 derajat untuk OFF)
        Serial.println(feedMotorState ? "Servo pakan dinyalakan melalui Firebase." : "Servo pakan dimatikan melalui Firebase.");
      }
    } 

    if (Firebase.RTDB.getString(&fbdo, "/Control/CleanMotor")) {
      String cleanMotorData = fbdo.stringData();
      bool firebaseCleanMotorState = (cleanMotorData == "true") || (cleanMotorData == "1");
      if (firebaseCleanMotorState != cleanMotorState) {
        cleanMotorState = firebaseCleanMotorState;
        digitalWrite(CLEAN_MOTOR_PIN, cleanMotorState ? LOW : HIGH);
        Serial.println(cleanMotorState ? "Motor pembersih dinyalakan melalui Firebase." : "Motor pembersih dimatikan melalui Firebase.");
      }
    } 
  }
}

void checkButtons() {
  if (digitalRead(BUTTON_FAN_PIN) == LOW && digitalRead(BUTTON_FEED_PIN) == LOW && digitalRead(BUTTON_CLEAN_PIN) == LOW) {
    delay(300);
    autoMode = !autoMode;
    displayStatus(autoMode ? "Mode: Otomatis" : "Mode: Manual");
    if (wifiConnected && Firebase.ready()) {
      Firebase.RTDB.setBool(&fbdo, "/Control/AutoMode", autoMode);
    }
    delay(500);
  }

  if(!autoMode){
    if (digitalRead(BUTTON_FAN_PIN) == LOW) {
      fanState = !fanState;
      digitalWrite(FAN1_RELAY_PIN, fanState ? LOW : HIGH);
      digitalWrite(FAN2_RELAY_PIN, fanState ? LOW : HIGH);
      // displayStatus(fanState ? "Kipas: ON" : "Kipas: OFF");
      if (wifiConnected && Firebase.ready()) {
      Firebase.RTDB.setBool(&fbdo, "/Control/Fan", fanState);
      }
      delay(500);
    }
    if (digitalRead(BUTTON_FEED_PIN) == LOW) {
      feedMotorState = !feedMotorState;
      feedServo.write(feedMotorState ? 60 : 0);
      // displayStatus(feedMotorState ? "Feed: ON" : "Feed: OFF");
      if (wifiConnected && Firebase.ready()) {
      Firebase.RTDB.setBool(&fbdo, "/Control/FeedMotor", feedMotorState);
      }
      delay(500);
    }
    if (digitalRead(BUTTON_CLEAN_PIN) == LOW) {
      cleanMotorState = !cleanMotorState;
      digitalWrite(CLEAN_MOTOR_PIN, cleanMotorState ? LOW : HIGH);
      // displayStatus(cleanMotorState ? "Clean: ON" : "Clean: OFF");
      if (wifiConnected && Firebase.ready()) {
      Firebase.RTDB.setBool(&fbdo, "/Control/CleanMotor", cleanMotorState);
      }
      delay(500);
    }
  }
}

void otomatis() {
  DateTime now = rtc.now();
  if (readMQ135() > 30) {
    digitalWrite(FAN1_RELAY_PIN, LOW);
    digitalWrite(FAN2_RELAY_PIN, LOW);
  } else {
    digitalWrite(FAN1_RELAY_PIN, HIGH);
    digitalWrite(FAN2_RELAY_PIN, HIGH);
  }

  if ((now.hour() == 9 && now.minute() == 59) || (now.hour() == 15 && now.minute() == 59)) {
    feedServo.write(45);
    delay(5000);
    feedServo.write(0);
  }

  if ((now.hour() == 14 && now.minute() == 22)) {
    digitalWrite(CLEAN_MOTOR_PIN, LOW);
    delay(5000);
    digitalWrite(CLEAN_MOTOR_PIN, HIGH);
  }
}

void setup() {
  Serial.begin(115200);
  connectToWiFi();
  initializeFirebase();
  initializeHardware();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    readSensors();
    updateFirebase();
    readFirebase();
    if (autoMode) {
      otomatis();
    }
  }

  checkButtons();
}
