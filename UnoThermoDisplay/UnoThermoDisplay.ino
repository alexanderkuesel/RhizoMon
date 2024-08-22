// this example is public domain. enjoy! https://learn.adafruit.com/thermocouple/

#include "max6675.h"
#include <TM1637Display.h>
#include <Servo.h>
//display variables
#define CLK 2
#define DIO 3
//Display needs 5v

//thermo vars
int thermoDO = 9;
int thermoCS = 8;
int thermoCLK = 13;

//servo vars
Servo servo;
int open_position = 15;
int closed_position = 110;
int delay_time = 20;
int servControl = 10; //yellow
int PV = 50;


TM1637Display display = TM1637Display(CLK, DIO);
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

void setup() {
  Serial.begin(9600);
  servo.attach(servControl);
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
  PV = thermocouple.readFahrenheit() - 20;
  //display.showNumberDec(666);
  // For the MAX6675 to update, you must delay AT LEAST 250ms between reads!
  servo.write(PV);
  delay(1000);
 
  
}