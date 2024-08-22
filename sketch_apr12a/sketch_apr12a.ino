 //button reset
 int Button=2;       //connect button to D2
 int LED=13;
 int counter = 0;
 void setup()
 {
 //pinMode(LED, OUTPUT);
 Serial.begin(9600);
 pinMode(Button, INPUT);  
 }
 
 void loop()
  
 {   
  delay(1000);
  counter +=1;
  if(digitalRead(Button)==HIGH)   //when the digital output value of button is high, turn on the LED.
  {
   //digitalWrite(LED, HIGH);
   //Serial.println("0");   
  }   
  if(digitalRead(Button)==LOW)  //when the digital output value of button is low, turn off the LED.
  {
  counter = 1;
  } 
  Serial.println(counter);
 }