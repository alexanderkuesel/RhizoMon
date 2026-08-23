// FlowSenseR4
// UNO R4 WiFi build of the FlowSense water meter: a YF-S201 Hall-effect
// flow sensor, a TM1637 4-digit display and MQTT publishing to MUTHUR.
// Kept deliberately close to Arduino/FlowSense (Nano 33 IoT) so the two
// stay diffable; everything that differs is marked "R4:".
// Alexander Kuesel

// R4: WiFiS3 replaces WiFiNINA, and is bundled with the board package
// rather than installed from the Library Manager. SPI.h is not needed -
// the ESP32-S3 radio talks to the RA4M1 over a UART, not SPI.
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <TM1637Display.h>
// R4: bundled with the board package, like WiFiS3 - not a Library Manager
// install. Pixel work needs nothing else; ArduinoGraphics is only required
// if you want text on the matrix.
#include "Arduino_LED_Matrix.h"
#include <math.h>
#include "arduino_secrets.h"

// ------------------*****---------------------
// I/O mapping area
// R4: no level shifting. The RA4M1's VDD is tied to the 5V rail, so the
// header pins are true 5V logic and the YF-S201's 5V output is just a
// normal high - the divider the Nano 33 IoT build needs is not wanted
// here. The yellow wire goes straight to the pin.
//
// D2 is one of only two header pins carrying a dedicated external
// interrupt channel that nothing else contends for (D2 = IRQ1, D3 =
// IRQ0). D3 is deliberately left free for a second meter.
#define FLOWPIN 2

// R4: the display moves off D2/D3. On this variant D4 and D7 are the only
// two header pins that are neither interrupt-capable nor PWM, which makes
// them the cheapest pins on the board to spend on a bit-banged display -
// nothing else would miss them.
#define DISPCLK 4  // CLK
#define DISPDIO 7  // DIO

TM1637Display display(DISPCLK, DISPDIO);

// "----" - shown while there is no valid reading to display.
const uint8_t SEG_DASHES[] = {SEG_G, SEG_G, SEG_G, SEG_G};

// ------------------*****---------------------
// R4: onboard 12x8 LED matrix, showing a rolling sparkline of the last
// 12 seconds of flow - one column per sample window, newest on the right.
// The TM1637 already gives the exact instantaneous number, so the matrix
// earns its place by showing shape over time instead: whether a watering
// run is ramping, steady, tapering or pulsing.
//
// It costs no header pins. The matrix is charlieplexed across D28-D38,
// which are internal to the board and not broken out, so it cannot
// collide with the flow input or the display.
ArduinoLEDMatrix matrix;

const uint8_t matrixCols = 12;
const uint8_t matrixRows = 8;

// Full-scale deflection: the rate that fills a column to all 8 rows.
// Defaults to the YF-S201's 30 L/min ceiling. Trim it to your own typical
// flow for more vertical resolution - against 30, a 7 L/min garden hose
// only ever lights two rows.
const float matrixFullScaleLpm = 30.0f;

// Column heights, 0..matrixRows. Index 0 is the oldest sample and
// matrixCols-1 the newest, so the trace scrolls leftwards as time passes.
uint8_t sparkline[matrixCols] = {0};
bool sparklineDirty = true;

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
// Same topics as the Nano 33 IoT build, so this board is a drop-in
// replacement for it and existing dashboards keep working. If you ever
// run both meters at once, change the FLOW segment on one of them.
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
const unsigned long rssiPollInterval   = 2000;   // how often to ask the radio for RSSI
const unsigned long linkPollInterval   = 500;    // how often to ask the radio for link status

// How long to wait for a host to open the USB serial port before giving up
// and running headless. Must be bounded - a bare `while (!Serial);` would
// hang the board forever whenever it is powered from a USB charger.
const unsigned long serialWaitTimeout  = 5000;

// Cap on how long WiFi.begin() may block internally.
const unsigned long wifiConnectTimeout = 15000;

// R4: PubSubClient's default buffer is 256 bytes for the whole packet,
// topic and header included. The status JSON below fits, but this station
// is meant to grow - one more field and a silent publish failure is the
// only symptom. Buy the headroom now.
const uint16_t mqttBufferSize = 512;

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

// Cached link state. Every WiFi.status() call is a round-trip over the
// UART to the ESP32-S3, so it is polled on an interval and the result
// reused for the rest of the pass rather than re-queried at each decision
// point.
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
// while the network code is mid-transaction with the radio.
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

  // No radio is a wiring/hardware fault, not a reason to halt the CPU.
  // Report it and keep looping so the meter keeps totalising and the USB
  // serial port stays serviceable.
  if (lastWiFiStatus == WL_NO_MODULE) {
    Serial.println("WiFi: ESP32-S3 radio not responding - check board selection / firmware");
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
  if (client.connect("FlowSenseR4Client")) {
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

// R4: shift the trace one column left and drop the newest sample in on
// the right.
void sparklinePush(float lpm) {
  for (uint8_t c = 0; c + 1 < matrixCols; c++) {
    sparkline[c] = sparkline[c + 1];
  }

  uint8_t height = 0;
  if (lpm > 0.0f) {
    long scaled = lroundf((lpm / matrixFullScaleLpm) * (float)matrixRows);
    // Any flow at all lights one row. Without this a trickle rounds to
    // zero and reads as "stopped", which is the one thing the sparkline
    // must never get wrong.
    if (scaled < 1)                 scaled = 1;
    if (scaled > (long)matrixRows)  scaled = matrixRows;
    height = (uint8_t)scaled;
  }
  sparkline[matrixCols - 1] = height;
  sparklineDirty = true;
}

void sparklineRender() {
  // Every cell must be exactly 0 or 1: the library ORs each byte into a
  // bit-packed frame and then shifts, so any other value would bleed into
  // neighbouring pixels rather than just lighting this one brighter.
  uint8_t frame[matrixRows][matrixCols] = {{0}};

  for (uint8_t c = 0; c < matrixCols; c++) {
    // Columns grow upwards from the bottom row.
    for (uint8_t r = 0; r < sparkline[c]; r++) {
      frame[matrixRows - 1 - r][c] = 1;
    }
  }

  matrix.renderBitmap(frame, matrixRows, matrixCols);
}

void setup() {
  Serial.begin(9600);

  // Give a host a few seconds to attach. Like the Nano 33 IoT, the R4's
  // USB serial is native CDC provided by the running sketch, so opening
  // the Serial Monitor does NOT reset the board and anything printed
  // before the port is open is lost. Bounded so the board still runs
  // standalone on a wall wart.
  unsigned long serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < serialWaitTimeout)) {
    ; // wait for the host, but not forever
  }

  Serial.println();
  Serial.println("=======================================");
  Serial.println("FlowSenseR4 booting");
  Serial.println("=======================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // R4: INPUT_PULLUP, unlike the Nano 33 IoT build's plain INPUT. The
  // YF-S201's open-collector output needs a pull-up to its own 5V rail,
  // and here the internal pull-up is on that rail, so it is the right one.
  // (On the 3.3V Nano it would have been the wrong rail, which is why that
  // build specifies an external resistor instead.) The internal pull-up is
  // weak - tens of kOhm - so on a long run next to a pump fit an external
  // 4.7k to 5V as well; see README > Wiring.
  pinMode(FLOWPIN, INPUT_PULLUP);

  // The YF-S201 output idles high and is pulled low once per blade pass,
  // so the falling edge is the one to count.
  attachInterrupt(digitalPinToInterrupt(FLOWPIN), pulseISR, FALLING);

  // Dashes until the first sample window closes, so a wired-up display is
  // visibly alive from boot rather than looking dead for the first second.
  display.setBrightness(2);
  display.setSegments(SEG_DASHES);

  // R4: begin() claims a free FSP timer and multiplexes the matrix from a
  // 10kHz periodic interrupt. Nothing else in this sketch wants a timer.
  // The render below just clears it; the trace starts blank because no
  // flow has been seen yet, which is also what no flow looks like later.
  matrix.begin();
  sparklineRender();

  WiFi.setTimeout(wifiConnectTimeout);

  client.setServer(server, 1883);
  client.setCallback(callback);
  client.setBufferSize(mqttBufferSize);

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
  // round-trip to the radio.
  if (currentMillis - lastRssiPoll >= rssiPollInterval) {
    lastRssiPoll = currentMillis;
    cachedRssi = wifiUp() ? WiFi.RSSI() : 0;
  }

  // Close off a sample window and turn the pulses it caught into a rate.
  if (currentMillis - previousSampleMillis >= sampleInterval) {
    unsigned long elapsedMs = currentMillis - previousSampleMillis;
    previousSampleMillis = currentMillis;

    // A 32-bit load is a single instruction on the Cortex-M4, but the
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
    sparklinePush(flowRateLpm);
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

  // R4: the sparkline only changes when a sample window closes, so the
  // repack-and-render happens on that edge rather than on a timer.
  if (sparklineDirty) {
    sparklineDirty = false;
    sparklineRender();
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
  // String reallocation fragments the heap.
  if (currentMillis - previousDiagMillis >= diagInterval) {
    previousDiagMillis = currentMillis;

    char statusMessage[192];
    snprintf(statusMessage, sizeof(statusMessage),
             "{\"device\": \"Arduino UNO R4 WiFi\","
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
