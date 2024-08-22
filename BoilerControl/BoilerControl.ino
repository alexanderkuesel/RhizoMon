//consider a 3rd thermistor to measure ambient temp with which to compare outletTemp
//caseTemp is useful for measuring relay heat
//What does the Master Switch do? Turn off the program or turn off everything?
//implement millis() instead of delay for non-blocking delay
//make function to convert anything to char for publishing over mqtt
//map lara_home to switch?
//display to show mode, ECO :: PERF

#include <SPI.h>
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <math.h>
#include <TM1637Display.h>
#include <RTC.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WIFI setup area
char ssid[] = "Kuecha";    // network SSID (name)
char pass[] = "Almajo730";    //  network password 
const char* server = "192.168.5.109";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP); //instantiate NTP class
// ------------------*****---------------------

// MQTT Topic definition area
const char CASE_TEMP_topic[]  = "spBv1.0/UTI/NDATA/BLR/Case_Temp";
const char OUT_TEMP_topic[] = "spBv1.0/UTI/NDATA/BLR/Outlet_Temp";
const char SW_STATE_topic[] = "spBv1.0/UTI/NDATA/BLR/Master_Switch";
const char RELAY_READ_topic[] = "spBv1.0/UTI/NDATA/BLR/Relay_State";
const char sub_topic[] = "testtopic";
const char HEART_BEAT_topic[] = "spBv1.0/UTI";
const char IS_LARA_HOME_topic[] = "spBv1.0/UTI/NCMD/BLR/Lara";
// ------------------*****---------------------

//int declaration space
int loop_cycle = 0; //enumerator of loop cycle
int loop_interval = 1000; //minimum delay
int one_sec_interval = 1; //will publish every second
int five_sec_interval = 5; //will publish every 5 seconds
int publish_ind = 0; //publish on first out
unsigned long heart_beat = 0;
int perf_time = 0;

// !!!!!!CONFIGURATION SPACE !!!!!!
int day_of_week = 0; // 0 = Sunday
String current_time = "need";
int off_days[2] = {6,7}; // Saturday and Sunday
bool lara_home = true; //lara is home that week (this could be an input from MQTT)
// ------------------*****---------------------


// Display definitions
#define CLK 3
#define DIO 4
TM1637Display display = TM1637Display(CLK, DIO); //initialize display
// ------------------*****---------------------

//I/O mapping area
int REL_WRITE = 7; //relay positive - control pin
int CASE_TEMP = A0; // temp element trans (therm) 1
int OUT_TEMP = A1; // temp element trans (therm) 2 
int SW_STATE = 2; // switch state input
int STARTS = 0; //count of times the relay switches from 0 to 1
const char* MASTER_SWITCH = "ON"; // switch explicit 



//Steinhart-Hart Equation function
float thermistor (int rawADC) {
  float temp;
  temp = log(10000.0/((1024.0/rawADC-1)));
  temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * temp * temp ))* temp );
  temp = temp - 273.15;            // Convert Kelvin to Celsius
 return temp;
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("arduinoClient")) {
      Serial.println("connected");
      // Once connected, publish an announcement...

      // ... and resubscribe
      client.subscribe(sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{
  Serial.begin(9600);
  pinMode(SW_STATE, INPUT_PULLUP); //switch state input pullup resistor. 
  //0 = CLOSED, 1 = OPEN.Inherently safe

  pinMode(REL_WRITE, OUTPUT); //relay pin to output

  client.setServer(server, 1883); 
  client.setCallback(callback);


  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  }
  // Allow the hardware to sort itself out
  delay(1500);
}

void loop()
{
  loop_cycle++;
  publish_ind = loop_cycle%five_sec_interval;
  
  //timing section
  //does it really matter what day of the week it is? yes, saturday/sunday



  perf_time = abs(millis() - heart_beat)-loop_interval; //measure performance (time taken to run cycle. Remove delay time)
  heart_beat = millis(); // heartbeat set to current program time

  //Priority, as fast as possible calls in this area
  if (digitalRead(SW_STATE) == LOW) {
    MASTER_SWITCH = "ON";
    digitalWrite(REL_WRITE, HIGH); //relay ON
  }
  else {
    MASTER_SWITCH = "OFF";
    digitalWrite(REL_WRITE, LOW); //relay OFF
  }
  // ------------------!!!!!!!!!---------------------
  int RELAY_STATE = digitalRead(REL_WRITE); //read pin state to confirm relay state

  //Temperature sensor read and convert
  int caseTempRaw = analogRead(CASE_TEMP);
  int outletTempRaw = analogRead(OUT_TEMP);
  int caseTemp = thermistor(caseTempRaw); // apply Steinhart-hart equation
  int outletTemp = thermistor(outletTempRaw);
  // ------------------*****---------------------

  ////publish function takes a const char*, not int. Map int to 16 character CHAR array
  char relayStateChar[16];
  char caseTempChar[16];
  char outTempChar[16];
  char heartBeat[16];
  itoa(caseTemp, caseTempChar, 10); 
  itoa(outletTemp, outTempChar, 10);
  itoa(RELAY_STATE, relayStateChar, 10);
  itoa(heart_beat, heartBeat, 10);
  // ------------------*****---------------------

  
  //Display
  display.setBrightness(3);
  display.showNumberDec(perf_time); //uses int

  //MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(loop_interval);
  client.publish(HEART_BEAT_topic, heartBeat);

  //Timing & Schedule Section
  //if lara_home = true && day_of_week is not in days_off, then start at 4am
  //if lara_home = true && day_of_week is days_off, then start at 9am
  //if lara_home = false && day_of_week is 1 (monday), then start at 6am


  //only publish every 5 cycles
  if (publish_ind == 0) {
    //time pulls
    timeClient.update();
    day_of_week = timeClient.getDay();
    current_time = timeClient.getFormattedTime();
    //Serial.println(day_of_week);
    //Serial.println(current_time);
    Serial.println(outletTemp);
    client.publish(CASE_TEMP_topic, caseTempChar);
    client.publish(OUT_TEMP_topic, outTempChar);
    client.publish(SW_STATE_topic, MASTER_SWITCH);
    client.publish(RELAY_READ_topic, relayStateChar); //confirm write to relay
  }

}