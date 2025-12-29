/*
 * ESP32 with MAX30102 Heart Rate and SpO2 Sensor + I2C LCD Display
 * 
 * Wiring:
 * MAX30102:
 *   VIN  -> ESP32 3.3V
 *   GND  -> ESP32 GND
 *   SDA  -> ESP32 GPIO21 (default I2C SDA)
 *   SCL  -> ESP32 GPIO22 (default I2C SCL)
 * 
 * I2C LCD (16x2 or 20x4):
 *   VCC  -> ESP32 5V (or 3.3V depending on LCD module)
 *   GND  -> ESP32 GND
 *   SDA  -> ESP32 GPIO21 (shared with MAX30102)
 *   SCL  -> ESP32 GPIO22 (shared with MAX30102)
 * 
 * Libraries Required:
 * 1. "MAX30105" library by SparkFun
 * 2. "LiquidCrystal I2C" library by Frank de Brabander
 *    (Library Manager -> Search "LiquidCrystal I2C")
 */

#include <Wire.h>
#include "MAX30105.h"
#include <LiquidCrystal_I2C.h>

MAX30105 particleSensor;

// LCD setup - adjust address if needed (common: 0x27, 0x3F, 0x20)
// Format: LiquidCrystal_I2C(address, columns, rows)
LiquidCrystal_I2C lcd(0x27, 16, 2);  // 16x2 LCD at address 0x27

// Heart rate variables
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;

// Manual beat detection variables
long irValueOld = 0;
long irValueNew = 0;
bool beatDetected = false;
int threshold = 200;

// SpO2 variables
double avered = 0;
double aveir = 0;
int i = 0;
int Num = 100;
double ESpO2 = 95.0;
double frate = 0.95;

// LED power adjustment
byte ledPower = 0x20;
bool calibrated = false;

// Display update control
unsigned long lastDisplayUpdate = 0;
int heartbeatAnimation = 0;

// Custom heart character for LCD
byte heartChar[8] = {
  0b00000,
  0b01010,
  0b11111,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n====================================");
  Serial.println("MAX30102 + LCD Heart Rate Monitor");
  Serial.println("====================================\n");
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  
  // Create custom heart character at position 0
  lcd.createChar(0, heartChar);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Heart Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error!");
    lcd.setCursor(0, 1);
    lcd.print("Check Wiring");
    while (1);
  }
  
  Serial.println("✓ Sensor detected");
  Serial.println("✓ LCD initialized");
  
  // Initial setup
  particleSensor.setup(ledPower, 4, 2, 100, 411, 4096);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Place Finger");
  lcd.setCursor(0, 1);
  lcd.print("on Sensor...");
  
  Serial.println("\n→ Place your finger on the sensor\n");
}

void calibrateLEDs() {
  long irValue = particleSensor.getIR();
  
  if (irValue < 70000 && ledPower < 0xFF) {
    ledPower += 0x10;
    if (ledPower > 0xFF) ledPower = 0xFF;
    particleSensor.setPulseAmplitudeRed(ledPower);
    particleSensor.setPulseAmplitudeIR(ledPower);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibrating...");
    lcd.setCursor(0, 1);
    lcd.print("Brightness: ");
    lcd.print(ledPower, HEX);
    
    Serial.print("⚙ Calibrating... LED: 0x");
    Serial.println(ledPower, HEX);
    delay(500);
  } 
  else if (irValue > 220000 && ledPower > 0x10) {
    ledPower -= 0x10;
    if (ledPower < 0x10) ledPower = 0x10;
    particleSensor.setPulseAmplitudeRed(ledPower);
    particleSensor.setPulseAmplitudeIR(ledPower);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibrating...");
    lcd.setCursor(0, 1);
    lcd.print("Brightness: ");
    lcd.print(ledPower, HEX);
    
    Serial.print("⚙ Calibrating... LED: 0x");
    Serial.println(ledPower, HEX);
    delay(500);
  }
  else if (irValue >= 70000 && irValue <= 220000) {
    if (!calibrated) {
      calibrated = true;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ready!");
      lcd.setCursor(0, 1);
      lcd.print("Measuring...");
      
      Serial.println("\n✓ Calibration complete!");
      Serial.println("✓ Starting measurements...\n");
      delay(1000);
    }
  }
}

void updateLCD() {
  // Update LCD every 500ms to avoid flicker
  if (millis() - lastDisplayUpdate < 500) return;
  lastDisplayUpdate = millis();
  
  lcd.clear();
  
  // Line 1: Heart Rate with animation
  lcd.setCursor(0, 0);
  if (beatAvg > 0) {
    // Heartbeat animation using custom character
    if (heartbeatAnimation == 0) {
      lcd.write(0); // Display custom heart character
    } else {
      lcd.print(" ");
    }
    heartbeatAnimation = !heartbeatAnimation;
    
    lcd.print("BPM: ");
    lcd.print(beatAvg);
    lcd.print(" bpm");
  } else {
    lcd.print("BPM: --");
  }
  
  // Line 2: SpO2
  lcd.setCursor(0, 1);
  lcd.print("SpO2: ");
  lcd.print(ESpO2, 1);
  lcd.print("%");
  
  // Signal indicator
  long irValue = particleSensor.getIR();
  if (irValue >= 80000 && irValue <= 180000) {
    lcd.print(" OK");
  } else if (irValue > 180000) {
    lcd.print(" HI");
  } else if (irValue >= 50000) {
    lcd.print(" LO");
  }
}

void loop() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();
  
  // Check if finger is detected
  if (irValue < 50000) {
    Serial.println("⚠ No finger detected");
    beatAvg = 0;
    calibrated = false;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No Finger");
    lcd.setCursor(0, 1);
    lcd.print("Place on Sensor");
    
    // Reset data
    for (byte x = 0; x < RATE_SIZE; x++)
      rates[x] = 0;
    rateSpot = 0;
    ESpO2 = 95.0;
    
    delay(1000);
    return;
  }
  
  // Auto-calibrate if needed
  if (!calibrated) {
    calibrateLEDs();
    return;
  }
  
  // Check for saturation
  if (redValue > 250000 || irValue > 250000) {
    Serial.println("⚠ Signal saturated! Recalibrating...");
    calibrated = false;
    ledPower -= 0x10;
    if (ledPower < 0x10) ledPower = 0x10;
    particleSensor.setPulseAmplitudeRed(ledPower);
    particleSensor.setPulseAmplitudeIR(ledPower);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Too Strong!");
    lcd.setCursor(0, 1);
    lcd.print("Adjusting...");
    
    delay(500);
    return;
  }
  
  // Manual Heart Rate Detection
  irValueOld = irValueNew;
  irValueNew = irValue;
  long irDelta = irValueNew - irValueOld;
  
  if (irDelta > threshold && !beatDetected) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    if (delta > 300 && delta < 2000) {
      beatsPerMinute = 60 / (delta / 1000.0);
      
      if (beatsPerMinute < 200 && beatsPerMinute > 40) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
    
    beatDetected = true;
  }
  
  if (irDelta < -threshold) {
    beatDetected = false;
  }
  
  // SpO2 Calculation
  if (i < Num) {
    avered += redValue;
    aveir += irValue;
    i++;
  } else {
    avered = avered / Num;
    aveir = aveir / Num;
    
    double R = (avered / aveir);
    ESpO2 = frate * ESpO2 + (1.0 - frate) * (110.0 - 25.0 * R);
    
    if (ESpO2 > 100) ESpO2 = 99.9;
    if (ESpO2 < 0) ESpO2 = 0;
    
    avered = 0;
    aveir = 0;
    i = 0;
  }
  
  // Update LCD display
  updateLCD();
  
  // Serial output
  Serial.print("IR: ");
  Serial.print(irValue);
  Serial.print(" | Red: ");
  Serial.print(redValue);
  Serial.print(" | ");
  
  if (beatAvg > 0) {
    Serial.print("❤ BPM: ");
    Serial.print(beatAvg);
  } else {
    Serial.print("❤ BPM: --");
  }
  
  Serial.print(" | SpO2: ");
  Serial.print(ESpO2, 1);
  Serial.print("%");
  
  if (irValue >= 80000 && irValue <= 180000) {
    Serial.print(" | ✓ Good");
  } else if (irValue > 180000) {
    Serial.print(" | ⚠ Strong");
  } else {
    Serial.print(" | ⚠ Weak");
  }
  
  Serial.println();
  
  delay(100);
}
