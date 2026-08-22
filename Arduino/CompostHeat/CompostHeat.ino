// CompostHeat
// Monitors compost pile internal temperature via a MAX6675 K-type
// thermocouple amplifier, shows it on a TM1637 4-digit display and
// publishes readings to the MUTHUR MQTT broker.
// Alexander Kuesel

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <max6675.h>
#include <TM1637Display.h>
#include <math.h>
#include "arduino_secrets.h"

// ------------------*****---------------------
// I/O mapping area
// MAX6675 uses bit-banged "software SPI" - any digital pins work.
// On the Nano 33 IoT the NINA WiFi module lives on SPI1 with its control
// lines on internal pins 24/27/28, so D5/D6/D7 are free and clear of it.
#define MAXCLK 5   // SCK
#define MAXCS  6   // CS
#define MAXSO  7   // SO / DO (MISO)

MAX6675 thermocouple(MAXCLK, MAXCS, MAXSO);

// TM1637 4-digit display. Also bit-banged, and its two lines are the only
// ones it needs, so D2/D3 keep it clear of both the MAX6675 (D5/D6/D7) and
// the broken-out hardware SPI pins.
#define DISPCLK 2  // CLK
#define DISPDIO 3  // DIO

TM1637Display display(DISPCLK, DISPDIO);

// "----" - shown while there is no valid reading to display.
const uint8_t SEG_DASHES[] = {SEG_G, SEG_G, SEG_G, SEG_G};

// ------------------*****---------------------
// WiFi setup area
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)

// ------------------*****---------------------
// MQTT Topic definition area
const char TEMP_C_topic[]  = "MUTHUR/NDATA/CMPST/TEMP_C";
const char TEMP_F_topic[]  = "MUTHUR/NDATA/CMPST/TEMP_F";
const char FAULT_topic[]   = "MUTHUR/DIAG/CMPST/FAULT";
const char STATUS_topic[]  = "MUTHUR/DIAG/CMPST/STATUS";
const char HB_topic[]      = "MUTHUR/DIAG/CMPST/HB";
// ------------------*****---------------------

const unsigned long sensorReadInterval = 1000;   // MAX6675 needs >=250ms between reads
const unsigned long publishInterval    = 60000;  // publish readings every 1 min
const unsigned long diagInterval       = 30000;  // publish diagnostics every 30s
const unsigned long displayInterval    = 1000;   // refresh the 4-digit display every 1s

// Retry pacing. Nothing in this sketch retries in a tight loop: every
// reconnect attempt is spaced out so loop() always keeps turning over.
const unsigned long wifiRetryInterval  = 15000;  // between WiFi join attempts
const unsigned long mqttRetryInterval  = 5000;   // between MQTT connect attempts
const unsigned long rssiPollInterval   = 2000;   // how often to ask NINA for RSSI
const unsigned long linkPollInterval   = 500;    // how often to ask NINA for link status

// How long to wait for a host to open the USB serial port before giving up
// and running headless. Must be bounded - a bare `while (!Serial);` would
// hang the board forever whenever it is powered from a USB charger.
const unsigned long serialWaitTimeout  = 5000;

// Cap on how long WiFi.begin() may block internally. The WiFiNINA default
// is 60s, which makes the board feel dead for a full minute per attempt.
const unsigned long wifiConnectTimeout = 15000;

unsigned long previousSensorMillis = 0;
unsigned long previousPublishMillis = 0;
unsigned long previousDiagMillis = 0;
unsigned long previousDisplayMillis = 0;
unsigned long previousMillisLED = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastRssiPoll = 0;
unsigned long lastLinkPoll = 0;

// Cached link state. Every WiFi.status() call is an SPI round-trip to the
// NINA module, so it is polled on an interval and the result reused for the
// rest of the pass rather than re-queried at each decision point.
uint8_t lastWiFiStatus = WL_IDLE_STATUS;
bool linkUp = false;
bool linkPolled = false;

bool wifiAttempted = false;
bool mqttAttempted = false;
bool wifiWasUp = false;

float lastTempC = NAN;
bool thermocoupleFault = false;
unsigned long heartBeat = 0;
long cachedRssi = 0;

int ledState = LOW;

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

// ------------------*****---------------------
// Connectivity. Both helpers return quickly and are safe to call every
// loop(); neither one ever blocks indefinitely.

bool wifiUp() {
  return linkUp;
}

void pollLink(bool force) {
  unsigned long now = millis();
  if (!force && linkPolled && (now - lastLinkPoll < linkPollInterval)) {
    return;
  }
  linkPolled = true;
  lastLinkPoll = now;
  lastWiFiStatus = WiFi.status();
  linkUp = (lastWiFiStatus == WL_CONNECTED);
}

void maintainWiFi() {
  if (wifiUp()) {
    if (!wifiWasUp) {
      wifiWasUp = true;
      Serial.println("WiFi: connected");
      Serial.print("WiFi: IP address ");
      Serial.println(WiFi.localIP());
      Serial.println("---------------------------------------");
    }
    return;
  }

  if (wifiWasUp) {
    wifiWasUp = false;
    Serial.println("WiFi: link lost, will retry");
  }

  unsigned long now = millis();
  if (wifiAttempted && (now - lastWiFiAttempt < wifiRetryInterval)) {
    return;
  }
  wifiAttempted = true;
  lastWiFiAttempt = now;

  // No module is a wiring/hardware fault, not a reason to halt the CPU.
  // Report it and keep looping so the USB serial port stays serviceable.
  if (lastWiFiStatus == WL_NO_MODULE) {
    Serial.println("WiFi: NINA module not responding - check board selection");
    return;
  }

  Serial.print("WiFi: attempting to join ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  // begin() blocks until it succeeds or times out, so the cached status is
  // stale by definition here - refresh it immediately.
  pollLink(true);
}

// Returns the current MQTT connection state, so the caller gets it without
// a second connected() call.
bool maintainMqtt() {
  if (!wifiUp()) {
    return false;
  }
  if (client.connected()) {
    return true;
  }

  unsigned long now = millis();
  if (mqttAttempted && (now - lastMqttAttempt < mqttRetryInterval)) {
    return false;
  }
  mqttAttempted = true;
  lastMqttAttempt = now;

  Serial.print("MQTT: connecting to ");
  Serial.print(server);
  Serial.print(" ... ");
  if (client.connect("CompostHeatClient")) {
    Serial.println("connected");
    return true;
  }

  Serial.print("failed, rc=");
  Serial.println(client.state());
  return false;
}

// ------------------*****---------------------

void setup() {
  Serial.begin(9600);

  // Give a host a few seconds to attach. The Nano 33 IoT's USB serial is
  // native CDC: opening the Serial Monitor does NOT reset the board, so
  // anything printed before the port is open is lost. Bounded so the
  // board still runs standalone on a battery or wall wart.
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < serialWaitTimeout)) {
    ; // wait for the host, but not forever
  }

  Serial.println();
  Serial.println("=======================================");
  Serial.println("CompostHeat booting");
  Serial.println("=======================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(MAXCS, OUTPUT);
  digitalWrite(MAXCS, HIGH);

  // Dashes until the first reading lands, so a wired-up display is
  // visibly alive from boot rather than looking dead until the sensor
  // settles.
  display.setBrightness(2);
  display.setSegments(SEG_DASHES);

  WiFi.setTimeout(wifiConnectTimeout);

  client.setServer(server, 1883);
  client.setCallback(callback);

  // MAX6675 needs ~500ms after power-up before the first valid read
  delay(500);

  Serial.println("Setup complete, entering main loop");
}

void loop() {
  unsigned long currentMillis = millis();

  pollLink(false);
  maintainWiFi();

  // One connected() check per pass, reused below for the same reason as
  // the link status above.
  bool mqttUp = maintainMqtt();

  // Cache RSSI rather than calling WiFi.RSSI() inline. Each call is a full
  // SPI round-trip to the NINA module, and the old code made one on every
  // single pass of loop().
  if (currentMillis - lastRssiPoll >= rssiPollInterval) {
    lastRssiPoll = currentMillis;
    cachedRssi = wifiUp() ? WiFi.RSSI() : 0;
  }

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

  // Local readout. Independent of WiFi and MQTT on purpose: the pile is
  // out in the garden, and the number should be readable standing over it
  // whether or not the network or the broker is up.
  if (currentMillis - previousDisplayMillis >= displayInterval) {
    previousDisplayMillis = currentMillis;

    if (thermocoupleFault || isnan(lastTempC)) {
      display.setSegments(SEG_DASHES);
    } else {
      // The module has one centre colon instead of per-digit decimal
      // points, and it sits exactly halfway, so the only split it can
      // punctuate is XX:XX. Show hundredths and read the colon as the
      // decimal point: 45.60 C is "45:60". That also lands neatly on the
      // MAX6675's 0.25 C resolution, so the last two digits are always a
      // real quarter-degree step rather than invented precision.
      // Leading zeros are kept - the colon form needs all four digits.
      int hundredths = (int)lroundf(lastTempC * 100.0f);
      if (hundredths >= 0 && hundredths <= 9999) {
        display.showNumberDecEx(hundredths, 0b01000000, true);
      } else {
        // Outside 0.00-99.99 C: a below-freezing probe, or an
        // implausibly hot one. Fall back to whole degrees, which is the
        // only form here that can show a minus sign.
        display.showNumberDec((int)lroundf(lastTempC));
      }
    }
  }

  // Publish readings. This block runs whether or not the network is up so
  // that the serial log always shows the board is alive and sampling.
  if (currentMillis - previousPublishMillis >= publishInterval) {
    previousPublishMillis = currentMillis;
    heartBeat++;

    bool haveReading = !thermocoupleFault && !isnan(lastTempC);

    char tempCChar[16];
    char tempFChar[16];
    char faultChar[8];
    char hbChar[16];

    if (haveReading) {
      snprintf(tempCChar, sizeof(tempCChar), "%.2f", lastTempC);
      snprintf(tempFChar, sizeof(tempFChar), "%.2f", lastTempC * 9.0 / 5.0 + 32.0);
    } else {
      strcpy(tempCChar, "--");
      strcpy(tempFChar, "--");
    }
    strcpy(faultChar, thermocoupleFault ? "1" : "0");
    snprintf(hbChar, sizeof(hbChar), "%lu", heartBeat);

    if (mqttUp) {
      if (haveReading) {
        client.publish(TEMP_C_topic, tempCChar);
        client.publish(TEMP_F_topic, tempFChar);
      }
      client.publish(FAULT_topic, faultChar);
      client.publish(HB_topic, hbChar);
    }

    Serial.print("HB ");
    Serial.print(hbChar);
    Serial.print(" | Compost Temp: ");
    Serial.print(tempCChar);
    Serial.print(" C / ");
    Serial.print(tempFChar);
    Serial.print(" F | WiFi ");
    Serial.print(wifiUp() ? "up" : "DOWN");
    Serial.print(" | MQTT ");
    Serial.println(mqttUp ? "up" : "DOWN");
  }

  // Publish diagnostics (JSON). Built with snprintf rather than String
  // concatenation - this runs every 30s for weeks at a time, and repeated
  // String reallocation fragments the SAMD21's 32KB heap.
  if (currentMillis - previousDiagMillis >= diagInterval) {
    previousDiagMillis = currentMillis;

    char statusMessage[160];
    snprintf(statusMessage, sizeof(statusMessage),
             "{\"device\": \"Arduino Nano 33 IoT\","
             "\"rssi\": %ld,"
             "\"uptime\": %lu,"
             "\"thermocouple_fault\": %s}",
             cachedRssi,
             currentMillis / 1000,
             thermocoupleFault ? "true" : "false");

    if (mqttUp) {
      client.publish(STATUS_topic, statusMessage);
    }
    Serial.print("Diagnostic data: ");
    Serial.println(statusMessage);
  }

  // Heartbeat LED. Blink rate doubles as an at-a-glance link indicator
  // when no serial monitor is attached:
  //   slow, RSSI-scaled blink = WiFi associated
  //   fast 150ms blink        = WiFi down
  unsigned long ledInterval;
  if (wifiUp() && cachedRssi < 0) {
    ledInterval = (unsigned long)(-cachedRssi) * 10;
    if (ledInterval < 300)  ledInterval = 300;
    if (ledInterval > 2000) ledInterval = 2000;
  } else {
    ledInterval = 150;
  }

  if (currentMillis - previousMillisLED >= ledInterval) {
    previousMillisLED = currentMillis;
    ledState = (ledState == LOW) ? HIGH : LOW;
    digitalWrite(LED_BUILTIN, ledState);
  }

  if (mqttUp) {
    client.loop();
  }
}
