#include <TM1637Display.h>

#define CLK 3
#define DIO 4
#define button 2
volatile byte state = LOW; 
//int button = 2;
int i = 0;
TM1637Display display = TM1637Display(CLK, DIO);

const uint8_t allON[] = {0xff, 0xff, 0xff, 0xff}; //all segments ON
const uint8_t allOFF[] = {0x00, 0x00, 0x00, 0x00}; //all segments OFF
const uint8_t reset[] = {
  SEG_G | SEG_E, //r
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_D,
  SEG_A
};

void setup() {
  // put your setup code here, to run once:
  pinMode(button, INPUT);
  display.clear(); //clear display before next run
  delay(1000);
  display.showNumberDec(0000);
  attachInterrupt(digitalPinToInterrupt(button), interrupt, RISING); //interrupt if button is pressed, set button state to high and counter reset
}

void loop() {
  // put your main code here, to run repeatedly:
  display.setBrightness(3);
    
  do {
    display.showNumberDec(i);
    i++; 
    delay(1000);
  }
  while (i < 1000 && (state == LOW)); //if button is pressed, reset counter
}

void interrupt() {
  state = !state;
  display.setSegments(reset);
  i = 1; //reset counter to zero
}
