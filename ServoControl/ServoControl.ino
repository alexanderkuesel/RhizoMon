#include <Servo.h>
#include <TM1637Display.h>

#define BUTTON_PIN 2 // button on digital pin 2
#define CLK 3
#define DIO 4

TM1637Display display(CLK, DIO);

Servo servo; // create servo object to control a servo

int open_position = 15;
int closed_position = 110;
int delay_time = 20;


void setup() {
  servo.attach(9); // attaches the servo on pin 9 to the servo object
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(9600);
  //servo.write(closed_position); // start position
  
  //display.showNumberDec(closed_position);
}

void loop() {
  display.setBrightness(3);
  //servo.write(30);
  if (digitalRead(BUTTON_PIN) == LOW) { // check if button is pressed
    if (servo.read() == closed_position) { // if servo is closed, open it
      for (int pos = closed_position; pos >= open_position; pos--) {
        servo.write(pos);
        display.showNumberDec(map(pos, open_position, closed_position, 0, 100));
        delay(delay_time);
      }
    } else { // if servo is open, close it
        for (int pos = open_position; pos <= closed_position; pos++) {
          servo.write(pos);
          display.showNumberDec(map(pos, open_position, closed_position, 0, 100));
          delay(delay_time);
      }
    }
  } else {
      display.showNumberDec(map(servo.read(), open_position, closed_position, 0, 100));
  }
}

