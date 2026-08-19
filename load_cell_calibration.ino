#include "HX711.h"

const int DOUT_PIN = 3;
const int SCK_PIN = 2;

HX711 scale;

void setup()
{
  Serial.begin(9600);

  scale.begin(DOUT_PIN, SCK_PIN);

  Serial.println("Remove all weight from the scale.");
  delay(3000);

  scale.tare();

  Serial.println("Scale has been zeroed.");
  Serial.println("Place a known weight on the load cell.");
  Serial.println("Then read the value below.");
}

void loop()
{
  Serial.print("Raw reading: ");
  Serial.println(scale.get_units(10));

  delay(50);
}