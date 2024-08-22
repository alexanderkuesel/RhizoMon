#include <TM1637Display.h>

#define CLK 3
#define DIO 4

TM1637Display display = TM1637Display(CLK, DIO);

const uint8_t allON[] = {0xff, 0xff, 0xff, 0xff}; //all segments ON
const uint8_t allOFF[] = {0x00, 0x00, 0x00, 0x00};

const uint8_t done[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,           // d
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
  SEG_C | SEG_E | SEG_G,                           // n
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G            // E
};

const uint8_t lara[] = {
  SEG_F | SEG_E | SEG_D, //L
  SEG_A | SEG_B | SEG_G | SEG_E | SEG_D | SEG_C, //a
  SEG_E | SEG_G, //r
  SEG_A | SEG_B | SEG_G | SEG_E | SEG_D | SEG_C, //a
};

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

  display.setBrightness(3);

  display.setSegments(lara);

  delay(2000);
  display.clear();
  delay(2000);
  //display.showNumberDec(-666);
  //delay(2000);
}
