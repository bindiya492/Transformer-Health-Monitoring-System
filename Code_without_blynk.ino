#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>


/* ========= PIN DEFINITIONS ========= */
#define RELAY_PIN 26
#define BUZZER_PIN 33
#define TRIG_PIN 18
#define ECHO_PIN 19
#define VIB_PIN 27
#define ONE_WIRE_BUS 4
#define DHTPIN 5
#define DHTTYPE DHT22
#define RESET_PIN 32   // RESET BUTTON


/* ========= LCD ADDRESSES ========= */
LiquidCrystal_I2C lcd1(0x27, 16, 2);
LiquidCrystal_I2C lcd2(0x26, 16, 2);


/* ========= OBJECTS ========= */
Adafruit_ADS1115 ads;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature oilSensor(&oneWire);
DHT dht(DHTPIN, DHTTYPE);


/* ========= CONSTANTS ========= */
#define ADS_LSB 0.000125
#define AC_SCALE 770.0
#define ACS_SENS 0.100
#define EMPTY_DIST 7.0
#define FULL_DIST 2.0


#define MIN_VOLTAGE 11.0
#define MAX_VOLTAGE 16.0
#define MAX_CURRENT 0.7
#define MAX_OIL_TEMP 60.0
#define MAX_WIND_TEMP 70.0
#define MIN_OIL_LEVEL 30.0


/* ========= VARIABLES ========= */
float currentZeroV = 0;
String faultMessage = "NORMAL";


unsigned long paramTimer = 0;
unsigned long dashTimer = 0;


int paramPage = 0;
int dashPage = 0;


bool systemTripped = false;   // 🔥 LATCH


/* ================= SETUP ================= */
void setup() {
 Serial.begin(9600);


 Wire.begin(21, 22);
 ads.begin();
 ads.setGain(GAIN_ONE);


 lcd1.init(); lcd1.backlight();
 lcd2.init(); lcd2.backlight();


 pinMode(RELAY_PIN, OUTPUT);
 digitalWrite(RELAY_PIN, HIGH);


 pinMode(BUZZER_PIN, OUTPUT);
 pinMode(TRIG_PIN, OUTPUT);
 pinMode(ECHO_PIN, INPUT);
 pinMode(VIB_PIN, INPUT);


 pinMode(RESET_PIN, INPUT_PULLUP); // RESET button


 oilSensor.begin();
 dht.begin();


 long sum = 0;
 for (int i = 0; i < 100; i++)
   sum += ads.readADC_SingleEnded(1);


 currentZeroV = (sum / 100.0) * ADS_LSB;


 lcd1.print("Parameters");
 lcd2.print("Dashboard");
 delay(1000);
 lcd1.clear();
 lcd2.clear();
}


/* ================= LOOP ================= */
void loop() {


 float acVoltage = readACVoltage();
 float dcCurrent = readDCCurrent();


 oilSensor.requestTemperatures();
 float oilTemp = oilSensor.getTempCByIndex(0);
 float windTemp = dht.readTemperature();


 float oilPercent = getOilLevelPercent();
 int vibration = digitalRead(VIB_PIN);


 bool fault = checkFault(acVoltage, dcCurrent, oilTemp, windTemp, oilPercent, vibration);


 /* ===== LATCH LOGIC ===== */
 if (fault) {
   systemTripped = true;
 }


 /* ===== RESET BUTTON (LONG PRESS) ===== */
 static unsigned long pressTime = 0;


 if (digitalRead(RESET_PIN) == LOW) {
   if (pressTime == 0) pressTime = millis();


   if (millis() - pressTime > 1000) { // 1 sec hold
     systemTripped = false;
     faultMessage = "RESET DONE";
     pressTime = 0;
   }
 } else {
   pressTime = 0;
 }


 /* ===== RELAY CONTROL ===== */
 if (systemTripped) {
   digitalWrite(RELAY_PIN, LOW);   // OFF
   tone(BUZZER_PIN, 2000, 200);
 } else {
   digitalWrite(RELAY_PIN, HIGH);  // ON
 }


 float health, life, risk;
 calculateHealth(acVoltage, dcCurrent, oilTemp, windTemp, oilPercent, vibration,
                 health, life, risk);


 /* ===== LCD1 ===== */
 if (millis() - paramTimer > 2000) {
   paramPage = (paramPage + 1) % 3;
   lcd1.clear();
   paramTimer = millis();
 }


 if (paramPage == 0) {
   lcd1.setCursor(0,0);
   lcd1.print("AC:");
   lcd1.print(acVoltage,1);
   lcd1.print("V");


   lcd1.setCursor(0,1);
   lcd1.print("DC:");
   lcd1.print(dcCurrent,2);
   lcd1.print("A");
 }
 else if (paramPage == 1) {
   lcd1.setCursor(0,0);
   lcd1.print("OilT:");
   lcd1.print(oilTemp,1);
   lcd1.print("C");


   lcd1.setCursor(0,1);
   lcd1.print("Wind:");
   lcd1.print(windTemp,1);
   lcd1.print("C");
 }
 else {
   lcd1.setCursor(0,0);
   lcd1.print("Oil:");
   lcd1.print(oilPercent,0);
   lcd1.print("%");


   lcd1.setCursor(0,1);
   lcd1.print("Vib:");
   lcd1.print(vibration == HIGH ? "FAULT " : "Normal");
 }


 /* ===== LCD2 ===== */
 if (millis() - dashTimer > 3000) {
   dashPage = (dashPage + 1) % 3;
   lcd2.clear();
   dashTimer = millis();
 }


 if (dashPage == 0) {
   lcd2.setCursor(0,0);
   lcd2.print("Health:");
   lcd2.print(health,0);
   lcd2.print("%");


   lcd2.setCursor(0,1);
   lcd2.print("Life:");
   lcd2.print(life,0);
   lcd2.print("%");
 }
 else if (dashPage == 1) {
   lcd2.setCursor(0,0);
   lcd2.print("Status:");
   lcd2.print(systemTripped ? "TRIPPED" : "NORMAL");


   lcd2.setCursor(0,1);
   lcd2.print("Diag:");
   lcd2.setCursor(5,1);
   lcd2.print(faultMessage);
 }
 else {
   lcd2.setCursor(0,0);
   lcd2.print("Predict:");
   lcd2.print(health < 50 ? "Maintain" : "Healthy ");


   lcd2.setCursor(0,1);
   lcd2.print("Risk:");
   lcd2.print(risk,0);
   lcd2.print("%");
 }
}


/* ================= FAULT CHECK ================= */
bool checkFault(float v, float c, float ot, float wt, float ol, int vib) {


 if (v < MIN_VOLTAGE) { faultMessage = "LOW VOLT"; return true; }
 if (v > MAX_VOLTAGE) { faultMessage = "HIGH VOLT"; return true; }
 if (c > MAX_CURRENT) { faultMessage = "OVER CURR"; return true; }
 if (ot > MAX_OIL_TEMP) { faultMessage = "OIL TEMP"; return true; }
 if (wt > MAX_WIND_TEMP) { faultMessage = "WIND TEMP"; return true; }
 if (ol < MIN_OIL_LEVEL) { faultMessage = "LOW OIL"; return true; }
 if (vib == HIGH) { faultMessage = "VIBRATION"; return true; }


 faultMessage = "NORMAL";
 return false;
}


/* ================= HEALTH MODEL ================= */
void calculateHealth(float v, float c, float ot, float wt, float ol, int vib,
                    float &health, float &life, float &risk) {


 health = 100;


 if (c > 0.6) health -= 15;
 if (ot > 55) health -= 20;
 if (wt > 65) health -= 20;
 if (ol < 40) health -= 15;
 if (vib == HIGH) health -= 20;


 if (health < 0) health = 0;


 life = 100 - ((ot + wt) / 3);
 if (life < 0) life = 0;


 risk = 100 - health;
}


/* ================= SENSOR FUNCTIONS ================= */


float readACVoltage() {
 const int samples = 40;
 float mid = 0, sumAbs = 0;


 for (int i = 0; i < samples; i++)
   mid += ads.readADC_SingleEnded(0);


 mid /= samples;


 for (int i = 0; i < samples; i++)
   sumAbs += abs(ads.readADC_SingleEnded(0) - mid);


 float meanAbs = (sumAbs / samples) * ADS_LSB;
 float rms = meanAbs * 1.1107 * AC_SCALE;


 if (rms < 0.8) rms = 0;


 return rms;
}


float readDCCurrent() {
 long sum = 0;


 for (int i = 0; i < 50; i++)
   sum += ads.readADC_SingleEnded(1);


 float voltage = (sum / 50.0) * ADS_LSB;
 float current = (voltage - currentZeroV) / ACS_SENS;


 if (current < 0.02) current = 0;


 return current;
}


float getOilLevelPercent() {


 digitalWrite(TRIG_PIN, LOW);
 delayMicroseconds(2);


 digitalWrite(TRIG_PIN, HIGH);
 delayMicroseconds(10);


 digitalWrite(TRIG_PIN, LOW);


 long duration = pulseIn(ECHO_PIN, HIGH, 15000);


 float distance = duration * 0.034 / 2.0;


 if (distance > EMPTY_DIST) distance = EMPTY_DIST;
 if (distance < FULL_DIST) distance = FULL_DIST;


 return ((EMPTY_DIST - distance) / (EMPTY_DIST - FULL_DIST)) * 100.0;
}

