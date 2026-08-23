// FlowSense
// Measures irrigation water flow with a YF-S201 Hall-effect flow sensor,
// shows live rate and cumulative volume on a TM1637 4-digit display and
// publishes readings to the MUTHUR MQTT broker.
// Alexander Kuesel

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <TM1637Display.h>
#include <math.h>
#include "arduino_secrets.h"

// ------------------*****---------------------
// I/O mapping area
// YF-S201 pulse output. The sensor runs on 5V and its output swings to
// 5V, which the Nano 33 IoT's pins are NOT tolerant of - the signal must
// come in through a level shifter or a divider (see README). D4 is PA07
// on the SAMD21, which owns its own EXTINT channel, so the pin-change
// interrupt below never collides with anything else on the board.
#define FLOWPIN 4

// TM1637 4-digit display, same wiring as the other sketches in this repo
// so a display can be moved between stations without rewiring. Bit-banged,
// so any two digital pins work; D2/D3 keep it clear of the flow input and
// of the broken-out hardware SPI pins.
#define DISPCLK 2  // CLK
#define DISPDIO 3  // DIO

TM1637Display display(DISPCLK, DISPDIO);

// "----" - shown while there is no valid reading to display.
const uint8_t SEG_DASHES[] = {SEG_G, SEG_G, SEG_G, SEG_G};

// ------------------*****---------------------
// Sensor calibration area
// The YF-S201's published characteristic is F = 7.5 * Q, with F in Hz and
// Q in L/min, which works out to 450 pulses per litre. Individual units
// drift a few percent from that, so this is the one number to trim after
// running a measured volume through the meter (see README > Calibration).
const float pulsesPerLitre = 450.0f;

// Shortest interval between two pulses that is treated as real. At the
// sensor's 30 L/min ceiling the pulse train is 225Hz (~4.4ms period), so a
// 1ms floor leaves better than 4x headroom while still rejecting the
// contact bounce and EMI spikes that a long run of unshielded cable next
// to a pump will pick up.
const unsigned long minPulseIntervalUs = 1000;

// ------------------*****---------------------
// WiFi setup area
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)

// ------------------*****---------------------
// MQTT Topic definition area
const char RATE_topic[]   = "MUTHUR/NDATA/FLOW/RATE_LPM";
const char TOTAL_topic[]  = "MUTHUR/NDATA/FLOW/TOTAL_L";
const char PULSES_topic[] = "MUTHUR/NDATA/FLOW/PULSES";
const char STATUS_topic[] = "MUTHUR/DIAG/FLOW/STATUS";
const char HB_topic[]     = "MUTHUR/DIAG/FLOW/HB";
// ------------------*****---------------------

const unsigned long sampleInterval  = 1000;   // recompute the flow rate every 1s
const unsigned long publishInterval = 10000;  // publish readings every 10s
const unsigned long diagInterval    = 30000;  // publish diagnostics every 30s
const unsigned long displayInterval = 250;    // display page timer tick

// How long each display page stays up before the other takes over.
const unsigned long ratePageDuration  = 4000;
const unsigned long totalPageDuration = 3000;

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

unsigned long previousSampleMillis = 0;
unsigned long previousPublishMillis = 0;
unsigned long previousDiagMillis = 0;
unsigned long previousDisplayMillis = 0;
unsigned long previousMillisLED = 0;
unsigned long pageStartMillis = 0;
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

// Written by the ISR, read by loop(). Both must be volatile or the
// compiler is entitled to cache them in a register across the whole loop.
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseMicros = 0;

// Snapshot of pulseCount at the end of the previous sample window. The ISR
// counter is never reset - a reset races with the interrupt and silently
// drops whatever arrives in between - so volume is accumulated from the
// difference between successive snapshots instead.
unsigned long lastPulseSnapshot = 0;

// Authoritative volume, kept as an integer pulse count and converted to
// litres only at the point of use, so no rounding error accumulates.
// Wraps after ~4.29e9 pulses, which is ~9.5 million litres.
unsigned long totalPulses = 0;

float flowRateLpm = 0.0f;
bool haveSample = false;   // false until the first full sample window closes
bool showingRate = true;   // which display page is up

// The TM1637 is bit-banged at ~1ms per full four-digit write, and its
// contents only change when a sample window closes or the page flips.
// Redrawing on every display tick regardless would be four times the bus
// traffic for the same digits, so the write is gated on this instead.
bool displayDirty = true;
unsigned long heartBeat = 0;
long cachedRssi = 0;

int ledState = LOW;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// ------------------*****---------------------
// Pulse capture. Kept to the bare minimum: one micros() call, a compare
// and two stores. Anything heavier here would be running at up to 225Hz
// while the network code is mid-SPI-transaction with the NINA module.
void pulseISR() {
  unsigned long now = micros();
  if (now - lastPulseMicros < minPulseIntervalUs) {
    return; // bounce or pickup, not a real blade pass
  }
  lastPulseMicros = now;
  pulseCount++;
}

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
  // Report it and keep looping so the meter keeps totalising and the USB
  // serial port stays serviceable.
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
  if (client.connect("FlowSenseClient")) {
    Serial.println("connected");
    return true;
  }

  Serial.print("failed, rc=");
  Serial.println(client.state());
  return false;
}

// ------------------*****---------------------

float totalLitres() {
  return (float)totalPulses / pulsesPerLitre;
}

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
  Serial.println("FlowSense booting");
  Serial.println("=======================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Plain INPUT, not INPUT_PULLUP. The external pull-up and the divider
  // (or the level shifter) already define both levels; adding the SAMD21's
  // internal ~40k pull-up to 3V3 on top of them only lifts the low level
  // towards the input threshold.
  pinMode(FLOWPIN, INPUT);

  // The YF-S201 output idles high and is pulled low once per blade pass,
  // so the falling edge is the one to count.
  attachInterrupt(digitalPinToInterrupt(FLOWPIN), pulseISR, FALLING);

  // Dashes until the first sample window closes, so a wired-up display is
  // visibly alive from boot rather than looking dead for the first second.
  display.setBrightness(2);
  display.setSegments(SEG_DASHES);

  WiFi.setTimeout(wifiConnectTimeout);

  client.setServer(server, 1883);
  client.setCallback(callback);

  previousSampleMillis = millis();
  pageStartMillis = previousSampleMillis;

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
  // SPI round-trip to the NINA module.
  if (currentMillis - lastRssiPoll >= rssiPollInterval) {
    lastRssiPoll = currentMillis;
    cachedRssi = wifiUp() ? WiFi.RSSI() : 0;
  }

  // Close off a sample window and turn the pulses it caught into a rate.
  if (currentMillis - previousSampleMillis >= sampleInterval) {
    unsigned long elapsedMs = currentMillis - previousSampleMillis;
    previousSampleMillis = currentMillis;

    // A 32-bit load is a single instruction on the Cortex-M0+, but the
    // guard costs a couple of cycles and keeps the intent explicit.
    noInterrupts();
    unsigned long snapshot = pulseCount;
    interrupts();

    // Unsigned subtraction, so this stays correct across the counter's
    // eventual wrap.
    unsigned long deltaPulses = snapshot - lastPulseSnapshot;
    lastPulseSnapshot = snapshot;
    totalPulses += deltaPulses;

    // pulses/litre * litres/min = pulses/min, so scale the window's pulse
    // count up to a minute and divide out the calibration constant.
    flowRateLpm = ((float)deltaPulses * 60000.0f) / (pulsesPerLitre * (float)elapsedMs);
    haveSample = true;
    displayDirty = true;
  }

  // Local readout. Independent of WiFi and MQTT on purpose: the meter sits
  // out at the tap, and the numbers should be readable standing over it
  // whether or not the network or the broker is up. The two pages take
  // turns; the colon tells them apart (see README > Display).
  if (currentMillis - previousDisplayMillis >= displayInterval) {
    previousDisplayMillis = currentMillis;

    unsigned long pageDuration = showingRate ? ratePageDuration : totalPageDuration;
    if (currentMillis - pageStartMillis >= pageDuration) {
      pageStartMillis = currentMillis;
      showingRate = !showingRate;
      displayDirty = true;
    }
  }

  // The 250ms tick above exists to land page flips promptly; the write
  // itself only happens when there is something new to show.
  if (displayDirty) {
    displayDirty = false;

    if (!haveSample) {
      display.setSegments(SEG_DASHES);
    } else if (showingRate) {
      // Rate page: L/min to two decimals, colon read as the decimal point.
      // The module has one centre colon instead of per-digit decimal
      // points and it sits exactly halfway, so XX:XX is the only split it
      // can punctuate. Leading zeros are kept - the colon form needs all
      // four digits. The YF-S201 tops out at 30 L/min, so 99.99 is only
      // ever reached by a miswired input counting noise.
      long hundredths = lroundf(flowRateLpm * 100.0f);
      if (hundredths < 0)    hundredths = 0;
      if (hundredths > 9999) hundredths = 9999;
      display.showNumberDecEx((int)hundredths, 0b01000000, true);
    } else {
      // Total page: whole litres, no colon, no leading zeros, so it reads
      // clearly differently from the rate page above.
      float litres = totalLitres();
      if (litres <= 9999.0f) {
        display.showNumberDec((int)lroundf(litres));
      } else {
        // Past 9999 L there is no room left for whole litres, so switch
        // to kilolitres and borrow the colon as the decimal point again:
        // "12:34" is 12.34 kL. Resolution drops to 10 L, and the display
        // pins at 99:99 (99,990 L) - by then the MQTT total is the number
        // to read anyway.
        long hundredthsKl = lroundf(litres / 10.0f);
        if (hundredthsKl > 9999) hundredthsKl = 9999;
        display.showNumberDecEx((int)hundredthsKl, 0b01000000, true);
      }
    }
  }

  // Publish readings. This block runs whether or not the network is up so
  // that the serial log always shows the board is alive and metering.
  if (currentMillis - previousPublishMillis >= publishInterval) {
    previousPublishMillis = currentMillis;
    heartBeat++;

    char rateChar[16];
    char totalChar[16];
    char pulsesChar[16];
    char hbChar[16];

    snprintf(rateChar, sizeof(rateChar), "%.2f", flowRateLpm);
    snprintf(totalChar, sizeof(totalChar), "%.3f", totalLitres());
    snprintf(pulsesChar, sizeof(pulsesChar), "%lu", totalPulses);
    snprintf(hbChar, sizeof(hbChar), "%lu", heartBeat);

    if (mqttUp) {
      if (haveSample) {
        client.publish(RATE_topic, rateChar);
      }
      client.publish(TOTAL_topic, totalChar);
      client.publish(PULSES_topic, pulsesChar);
      client.publish(HB_topic, hbChar);
    }

    Serial.print("HB ");
    Serial.print(hbChar);
    Serial.print(" | Flow: ");
    Serial.print(rateChar);
    Serial.print(" L/min | Total: ");
    Serial.print(totalChar);
    Serial.print(" L (");
    Serial.print(pulsesChar);
    Serial.print(" pulses) | WiFi ");
    Serial.print(wifiUp() ? "up" : "DOWN");
    Serial.print(" | MQTT ");
    Serial.println(mqttUp ? "up" : "DOWN");
  }

  // Publish diagnostics (JSON). Built with snprintf rather than String
  // concatenation - this runs every 30s for weeks at a time, and repeated
  // String reallocation fragments the SAMD21's 32KB heap.
  if (currentMillis - previousDiagMillis >= diagInterval) {
    previousDiagMillis = currentMillis;

    char statusMessage[192];
    snprintf(statusMessage, sizeof(statusMessage),
             "{\"device\": \"Arduino Nano 33 IoT\","
             "\"rssi\": %ld,"
             "\"uptime\": %lu,"
             "\"rate_lpm\": %.2f,"
             "\"total_l\": %.3f,"
             "\"pulses\": %lu}",
             cachedRssi,
             currentMillis / 1000,
             flowRateLpm,
             totalLitres(),
             totalPulses);

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
