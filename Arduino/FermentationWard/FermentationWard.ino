
// FermentationWard
// Keeps track of variables relevant to fermentation
// Ambient Temp, Humidity, UV intensity and Jar Moisture.
//Alexander Kuesel

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <math.h>
#include <TM1637Display.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>

using namespace std;

//I/O mapping area
int UV_READ = A0; //UV is using 3.3V
// int AMB_TEMP = A4; //temp sensor using 3.3V
int MOISTURE = A2; //moisture sensor using 3.3V
#define DHTPIN A1
#define DHTTYPE DHT22

DHT_Unified dht(DHTPIN, DHTTYPE);


// WIFI setup area
char ssid[] = "Kuecha";    // network SSID (name)
char pass[] = "Almajo730";    //  network password 
const char* server = "192.168.5.110"; //MQTT server (Raspberry Pi)
int status = WL_IDLE_STATUS; // wifi status

// ------------------*****---------------------

// MQTT Topic definition area
// const char AMB_TEMP_topic[]  = "MUTHUR/PND/PLNT/Amb_Temp";
const char MOISTURE_topic[] = "MUTHUR/PND/PLNT/Moisture";
const char TEM_topic[] = "MUTHUR/PND/PLNT/Temp";
const char HUM_topic[] = "MUTHUR/PND/PLNT/Humidity";
const char UV_topic[] = "MUTHUR/PND/PLNT/UV_Int";
const char UV_Ind_topic[] = "MUTHUR/PND/PLNT/UV_Index";
const char HB_topic[] = "MUTHUR/DIAG/PLNT/HB";
// ------------------*****---------------------


int loop_cycle = 0; //enumerator of loop cycle
int loop_interval = 1000; //minimum delay
int one_sec_interval = 1; //will publish every second
int five_sec_interval = 5; //will publish every 5 seconds
int publish_ind = 0; //publish on first out
int ledState = LOW;                       //ledState used to set the LED
unsigned long previousMillisInfo = 0;     //will store last time Wi-Fi information was updated
unsigned long previousMillisLED = 0;      // will store the last time LED was updated
const int intervalInfo = 30000;          // interval at which to update Wi-Fi information  
int heart_beat = 0;
/* 
Display definitions
#define CLK 3
#define DIO 4
TM1637Display display = TM1637Display(CLK, DIO); //initialize display
*/ 


//Steinhart-Hart Equation function for temp (requires 5V)
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


//MQTT Connection Logic
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("arduinoClient")) {
      Serial.println("connected");
      // Once connected, publish an announcement...

      // ... and resubscribe
      client.subscribe(HB_topic);
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
  //0 = CLOSED, 1 = OPEN.Inherently safe
  // Wifi Connect loop
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to network: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  dht.begin();

  // you're connected now, so print out the data:
  Serial.println("You're connected to the network");
  Serial.println("---------------------------------------");

  client.setServer(server, 1883); 
  client.setCallback(callback);
}


void loop()
{

  heart_beat++;
  unsigned long currentMillisInfo = millis();


  // check if the time after the last update is bigger the interval
  if (currentMillisInfo - previousMillisInfo >= intervalInfo) {
    previousMillisInfo = currentMillisInfo;

    Serial.println("Board Information:");
    // print your board's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // print your network's SSID:
    Serial.println();
    Serial.println("Network Information:");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.println(rssi);
    Serial.println("---------------------------------------");

    //float UV_Voltage = analogRead(UV_READ); //read UV raw voltage
    //int UV_Int = (int)(UV_Voltage*(3.3/1023)/0.1);

    float UV_Voltage = analogRead(UV_READ) * (3.3/1023.0); // Convert to actual voltage
    int UV_Int = (int)(UV_Voltage / 0.1); // Assuming 0.1V per UV index unit

    //int UV_Int = (int)(UV_Voltage/1024*0.5); // convert to int
    Serial.print("UV Raw mv reading is:  ");
    Serial.println(UV_Voltage);
    Serial.print("UV Intensity is:  ");
    Serial.println(UV_Int);

      //Temperature sensor read and convert
    // int ambTempRaw = analogRead(AMB_TEMP);
    // int ambTemp = thermistor(ambTempRaw); // apply Steinhart-hart equation
    // Serial.print("Temperature is:    ");
    // Serial.println(ambTemp);

    //LM393 Moisture Sensor
    //A2
    int moistureRaw = analogRead(MOISTURE);
    int moisture = map(moistureRaw, 0, 1023, 100, 0); // Map to percentage
    Serial.print("Moisture Level is: ");
    Serial.println(moisture);

    //Temp & humidity read
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    if (isnan(event.temperature)) {
      Serial.println(F("Error reading temperature!"));
    }
    else {
      Serial.print(F("Temperature: "));
      Serial.print(event.temperature);
      Serial.println(F("°C"));
    }
    // Get humidity event and print its value.
    dht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity)) {
      Serial.println(F("Error reading humidity!"));
    }
    else {
      Serial.print(F("Humidity: "));
      Serial.print(event.relative_humidity);
      Serial.println(F("%"));
    }

    // ------------------*****---------------------

    ////publish function takes a const char*, not int. Map int to 16 character CHAR array
    // char ambTempChar[16];
    char UVChar[16];
    char UVVoltageChar[16];
    char HBChar[16];
    char moistureChar[16];
    char tempChar[16];
    char humChar[16];

    itoa(moisture, moistureChar, 10);
    //sprintf(event.temperature, 6, 2, tempChar);
    //sprintf(event.relative_humidity, 6, 2, humChar);

    itoa(event.temperature, tempChar, 6);
    itoa(event.relative_humidity, humChar, 6);
    itoa(UV_Int, UVChar, 10);
    itoa(UV_Voltage, UVVoltageChar, 10);
    //sprintf(UV_Voltage, 6, 2, UVVoltageChar); // 6 chars total, 2 decimal places
    itoa(heart_beat, HBChar, 10);

    // client.publish(AMB_TEMP_topic, ambTempChar);
    client.publish(UV_topic, UVChar);
    client.publish(UV_Ind_topic, UVVoltageChar);
    client.publish(TEM_topic, tempChar);
    client.publish(HUM_topic, humChar);
    client.publish(HB_topic, HBChar);
    client.publish(MOISTURE_topic, moistureChar);

  }
  unsigned long currentMillisLED = millis();
  
  // measure the signal strength and convert it into a time interval
  int intervalLED = WiFi.RSSI() * -10;
 
  // check if the time after the last blink is bigger the interval 
  if (currentMillisLED - previousMillisLED >= intervalLED) {
    previousMillisLED = currentMillisLED;

    // if the LED is off turn it on and vice-versa:
    if (ledState == LOW) {
      ledState = HIGH;
    } else {
      ledState = LOW;
    }

    // set the LED with the ledState of the variable:
    digitalWrite(LED_BUILTIN, ledState);
  }



  //MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(loop_interval);

  //only publish every 5 cycles




}