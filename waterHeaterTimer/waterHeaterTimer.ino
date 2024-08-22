#include <TM1637Display.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <math.h>

//Network setup
char ssid[] = "Kuecha";    // network SSID (name)
char pass[] = "Almajo730";    //  network password 

//MQTT setup
char mqtt_user[] = "";
char mqtt_pass[] = "";

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.5.137"; //IP address of the EMQX broker.
int        port     = 1883;
const char subscribe_topic[]  = "spBv1.0/UTI/NDATA/BLR/TET01"; //following sparkplug B spec

const char TET01_topic[]  = "spBv1.0/UTI/NDATA/BLR/TET01";
const char TET02_topic[] = "spBv1.0/UTI/NDATA/BLR/TET02";

//Timer circuit
//protection circuit
// Display definitions
#define CLK 3
#define DIO 4

TM1637Display display = TM1637Display(CLK, DIO); //initialize display

//I/O circuit
int ssrPos = 7; //relay positive - control pin
int TET115_1 = A0; // temp element trans (therm) 1
int TET115_2 = A1; // temp element trans (therm) 2 

//Steinhart-Hart Equation function
float Thermistor (int rawADC) {
  float temp;
  temp = log(10000.0/((1024.0/rawADC-1)));
  temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * temp * temp ))* temp );
  temp = temp - 273.15;            // Convert Kelvin to Celsius
 return temp;

}

void setup() {
  Serial.begin(9600); //initialize serial bus
  pinMode(ssrPos, OUTPUT); //initialize write out to relay

  //Start Wifi/MQTT connections
  //while (!Serial) { //causes issues with the UNO R4
    //; 
  //}

  // Connect to WiFi
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  } 
  Serial.println("You're connected to the network");
  Serial.println();

  mqttClient.setUsernamePassword(mqtt_user, mqtt_pass);
  Serial.print("Attempting to connect to the MQTT broker.");
  Serial.println();
  
  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    //while (1); // this was in the original code but causes the program to stay stuck if there is no MQTT connection
  } else { //added this to stop the 

    Serial.println("You're connected to the MQTT broker!");
    Serial.print("Subscribing to topic: ");
    Serial.println(subscribe_topic);
    mqttClient.subscribe(subscribe_topic);

    Serial.print("Waiting for messages on topic: ");
    Serial.println(subscribe_topic);

  }
}

void loop() {
  //Temperature Sensing section

  int caseTempRaw = analogRead(TET115_1);
  int outletTempRaw = analogRead(TET115_2);
  int caseTemp = Thermistor(caseTempRaw); // apply Steinhart-hart equation
  int outletTemp = Thermistor(outletTempRaw);

  Serial.println();
  delay(1000);
  //Display Section
  display.setBrightness(5);
  display.showNumberDec(caseTemp);
  delay(1000);
  /////Relay Control Loop
  //digitalWrite(ssrPos, HIGH);
  //delay(2000);

  //MQTT Section
  int messageSize = mqttClient.parseMessage();
  if (messageSize) {
    // we received a message, print out the topic and contents
    // Reader
    while (mqttClient.available()) {
      Serial.print((char)mqttClient.read());
    }
    Serial.println();
  }
  Serial.print("Outlet Temp: ");
  Serial.print(outletTemp);
  Serial.println();
  Serial.print("Case Temp: ");
  Serial.print(caseTemp);
  Serial.println();
  // send message, the Print interface can be used to set the message contents
  delay(3000);
  mqttClient.beginMessage(TET01_topic);
  Serial.print("Published a message with topic '");
  Serial.print(TET01_topic);
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");
  //mqttClient.print("Case Temperature: ");
  mqttClient.print(caseTemp);
  mqttClient.endMessage();
  //TET02
  //mqttClient.beginMessage(TET02_topic);
  //Serial.print("Published a message with topic '");
  //Serial.print(mqttClient.messageTopic());
  //Serial.print("', length ");
  //Serial.print(messageSize);
  //Serial.println(" bytes:");
  //mqttClient.print("Outlet Temperature: ");
  //mqttClient.print(outletTemp);
  //mqttClient.endMessage();

}
