#include "HX711.h"

const int DOUT_PIN = 3;
const int SCK_PIN = 2;

// CHANGE THIS after calibration
//float calibration_factor = 100;
float calibration_factor = 42;

HX711 scale;

void setup()
{
  Serial.begin(9600);

  scale.begin(DOUT_PIN, SCK_PIN);

  // Set calibration
  scale.set_scale(calibration_factor);

  // Zero the scale
  scale.tare();

  Serial.println("Scale ready!");
}

void loop()
{
  float weight = scale.get_units(10);

  Serial.print("Weight: ");
  Serial.print(weight, 1);
  Serial.println(" g");

  delay(50);
}