// this example is public domain. enjoy! https://learn.adafruit.com/thermocouple/

#include "max6675.h"
#include <TM1637Display.h>
#define CLK 2

#define DIO 3

//Display needs 5v

int thermoDO = 9;
int thermoCS = 8;
int thermoCLK = 13;


TM1637Display display = TM1637Display(CLK, DIO);
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

void setup() {
  Serial.begin(9600);
  Serial.println("MAX6675 test");
  // wait for MAX chip to stabilize
  delay(500);
}

void loop() {
  // basic readout test, just print the current temp
  display.setBrightness(3);
  //Serial.print("C = "); 
  display.showNumberDec(thermocouple.readFahrenheit());
  Serial.print("F = ");
  Serial.println(thermocouple.readFahrenheit());
  //display.showNumberDec(666);
  // For the MAX6675 to update, you must delay AT LEAST 250ms between reads!
  delay(1000);
}