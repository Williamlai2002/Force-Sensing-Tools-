#include "HX711.h"

// ===============================
// HX711 LOAD CELL
// ===============================
#define HX711_DOUT 3
#define HX711_SCK  2

HX711 scale;

// Your load-cell calibration factor
float calibration_factor = 42.0;


// ===============================
// STRAIN GAUGE
// ===============================
#define STRAIN_PIN 0

// Calibration:
// 0.38 Nm = 1100 ADC counts
float torqueCalibration = 0.38 / 1100.0;

// Zero-torque ADC value
int strainZero = 0;


// ===============================
// TIMING
// ===============================
unsigned long previousMillis = 0;
const unsigned long interval = 100;  // 100 ms = 0.1 seconds


// ===============================
// SETUP
// ===============================
void setup() {

  Serial.begin(115200);

  delay(1000);

  // ESP32-C3 ADC resolution
  analogReadResolution(12);


  // ===============================
  // INITIALISE HX711
  // ===============================

  scale.begin(HX711_DOUT, HX711_SCK);

  scale.set_scale(calibration_factor);
  scale.tare();


  // ===============================
  // STRAIN GAUGE ZERO
  // ===============================

  Serial.println("Starting strain gauge zero calibration...");
  Serial.println("Do NOT apply torque.");

  delay(3000);

  long total = 0;

  for (int i = 0; i < 100; i++) {

    total += analogRead(STRAIN_PIN);

    delay(5);
  }

  strainZero = total / 100;

  Serial.print("Strain gauge zero ADC = ");
  Serial.println(strainZero);

  Serial.println("System ready.");
  Serial.println();
}


// ===============================
// LOOP
// ===============================
void loop() {

  unsigned long currentMillis = millis();


  // Only take a reading every 100 ms
  if (currentMillis - previousMillis >= interval) {

    previousMillis = currentMillis;


    // ==========================================
    // LOAD CELL
    // ==========================================

    float weight = scale.get_units(5);


    // ==========================================
    // STRAIN GAUGE
    // ==========================================

    long strainTotal = 0;

    for (int i = 0; i < 10; i++) {

      strainTotal += analogRead(STRAIN_PIN);

      delay(1);
    }

    int strainADC = strainTotal / 10;


    // ==========================================
    // ADC CHANGE FROM ZERO
    // ==========================================

    int ADCchange = strainADC - strainZero;


    // ==========================================
    // CALCULATE TORQUE
    // ==========================================

    float torque = ADCchange * torqueCalibration;


    // ==========================================
    // CALCULATE VOLTAGE
    // ==========================================

    float voltage = strainADC * (3.3 / 4095.0);


    // ==========================================
    // PRINT RESULTS
    // ==========================================

    Serial.print("Weight:");
    Serial.print(weight, 2);

    Serial.print("\tTorque:");
    Serial.print(torque, 4);

    Serial.print("\tADC:");
    Serial.print(strainADC);

    Serial.print("\tADCChange:");
    Serial.print(ADCchange);

    Serial.print("\tVoltage:");
    Serial.println(voltage, 4);
  }
}
