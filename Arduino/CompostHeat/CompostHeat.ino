// CompostHeat
// Monitors compost pile internal temperature via a MAX6675 K-type
// thermocouple amplifier and publishes readings to the MUTHUR MQTT broker.
// Alexander Kuesel

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <max6675.h>
#include "arduino_secrets.h"

// ------------------*****---------------------
// I/O mapping area
// MAX6675 uses bit-banged "software SPI" - any digital pins work.
// Kept off the hardware SPI pins (11/12/13) so they stay free for
// the NINA WiFi module and any future SPI peripherals.
#define MAXCLK 5   // SCK
#define MAXCS  6   // CS
#define MAXSO  7   // SO / DO (MISO)

MAX6675 thermocouple(MAXCLK, MAXCS, MAXSO);

// ------------------*****---------------------
// WiFi setup area
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)
int status = WL_IDLE_STATUS;          // wifi status

// ------------------*****---------------------
// MQTT Topic definition area
const char TEMP_C_topic[]  = "MUTHUR/NDATA/CMPST/TEMP_C";
const char TEMP_F_topic[]  = "MUTHUR/NDATA/CMPST/TEMP_F";
const char FAULT_topic[]   = "MUTHUR/DIAG/CMPST/FAULT";
const char STATUS_topic[]  = "MUTHUR/DIAG/CMPST/STATUS";
const char HB_topic[]      = "MUTHUR/DIAG/CMPST/HB";
// ------------------*****---------------------

const unsigned long sensorReadInterval = 1000;   // MAX6675 needs >=250ms between reads
const unsigned long publishInterval    = 5000;   // publish readings every 5s
const unsigned long diagInterval       = 30000;  // publish diagnostics every 30s

unsigned long previousSensorMillis = 0;
unsigned long previousPublishMillis = 0;
unsigned long previousDiagMillis = 0;

float lastTempC = NAN;
bool thermocoupleFault = false;
unsigned long heartBeat = 0;

int ledState = LOW;
unsigned long previousMillisLED = 0;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// MQTT Connection Logic
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("CompostHeatClient")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to network: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000); // wait 10 seconds for connection
  }

  Serial.println("You're connected to the network");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("---------------------------------------");
}

void setup() {
  Serial.begin(9600);

  pinMode(MAXCS, OUTPUT);
  digitalWrite(MAXCS, HIGH);

  connectWiFi();

  client.setServer(server, 1883);
  client.setCallback(callback);

  // MAX6675 needs ~500ms after power-up before the first valid read
  delay(500);
}

void loop() {
  unsigned long currentMillis = millis();

  // Read the thermocouple no more often than every sensorReadInterval
  if (currentMillis - previousSensorMillis >= sensorReadInterval) {
    previousSensorMillis = currentMillis;

    float tempC = thermocouple.readCelsius();

    // readCelsius() returns NAN when the thermocouple is open/disconnected
    thermocoupleFault = isnan(tempC);
    if (!thermocoupleFault) {
      lastTempC = tempC;
    } else {
      Serial.println("Thermocouple fault: open circuit / not connected");
    }
  }

  // Publish readings
  if (currentMillis - previousPublishMillis >= publishInterval) {
    previousPublishMillis = currentMillis;
    heartBeat++;

    char tempCChar[16];
    char tempFChar[16];
    char faultChar[8];
    char hbChar[16];

    dtostrf(lastTempC, 4, 2, tempCChar);
    dtostrf(lastTempC * 9.0 / 5.0 + 32.0, 4, 2, tempFChar);
    strcpy(faultChar, thermocoupleFault ? "1" : "0");
    ultoa(heartBeat, hbChar, 10);

    if (!thermocoupleFault) {
      client.publish(TEMP_C_topic, tempCChar);
      client.publish(TEMP_F_topic, tempFChar);
    }
    client.publish(FAULT_topic, faultChar);
    client.publish(HB_topic, hbChar);

    Serial.print("Compost Temp: ");
    Serial.print(tempCChar);
    Serial.print(" C / ");
    Serial.print(tempFChar);
    Serial.println(" F");
  }

  // Publish diagnostics (JSON)
  if (currentMillis - previousDiagMillis >= diagInterval) {
    previousDiagMillis = currentMillis;

    long rssi = WiFi.RSSI();
    String statusMessage = "{";
    statusMessage += "\"device\": \"Arduino Nano 33 IoT\",";
    statusMessage += "\"rssi\": " + String(rssi) + ",";
    statusMessage += "\"uptime\": " + String(currentMillis / 1000) + ",";
    statusMessage += "\"thermocouple_fault\": " + String(thermocoupleFault ? "true" : "false");
    statusMessage += "}";

    client.publish(STATUS_topic, statusMessage.c_str());
    Serial.print("Diagnostic data sent: ");
    Serial.println(statusMessage);
  }

  // Heartbeat LED, blink rate scaled by signal strength like other stations
  unsigned long currentMillisLED = millis();
  int intervalLED = WiFi.RSSI() * -10;
  if (currentMillisLED - previousMillisLED >= (unsigned long)intervalLED) {
    previousMillisLED = currentMillisLED;
    ledState = (ledState == LOW) ? HIGH : LOW;
    digitalWrite(LED_BUILTIN, ledState);
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
