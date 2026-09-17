// IceBath
// UNO R4 WiFi ice-bath monitor: a MAX6675 K-type thermocouple in the
// water, a DHT22 for the air above it, a TM1637 4-digit display, the
// onboard LED matrix as a 12-hour temperature trend, and MQTT publishing
// to MUTHUR.
//
// This started as a copy of Arduino/FlowSenseR4 with the flow meter taken
// out, and stays as close to it as the change allows: the connectivity,
// display-layout and matrix-render code is carried over as-is, so a fix
// there still ports across by hand. The measurement half is necessarily
// its own - see README > What changed from FlowSenseR4.
//
// Two sets of markers, therefore - the inherited "R4:" notes still mark
// what differs from the Nano 33 IoT Arduino/FlowSense build, and
// everything that differs from Arduino/FlowSenseR4 is marked "Ice:".
//
// The display and the wiring are arranged so a board already running
// FlowSenseR4 needs no rewiring: see README > Wiring.
// Alexander Kuesel

// R4: WiFiS3 replaces WiFiNINA, and is bundled with the board package
// rather than installed from the Library Manager. SPI.h is not needed -
// the ESP32-S3 radio talks to the RA4M1 over a UART, not SPI.
#include <WiFiS3.h>
#include <PubSubClient.h>
#include <TM1637Display.h>
// DHT22 ambient temperature and humidity - Ice: the air above the bath
// rather than at a tap, which is what tells you whether a bath is warming
// up because the ice is gone or because the room is. Install "DHT
// sensor library" (Adafruit) from the Library Manager; it pulls in
// "Adafruit Unified Sensor" as a dependency. The plain DHT class is used
// rather than the DHT_Unified wrapper in Arduino/FermentationWard - one
// bus transaction and two floats, with no sensor-event structs to thread
// through a loop that only wants the numbers.
#include <DHT.h>
// Ice: MAX6675 K-type thermocouple amplifier - the same part, and the same
// "MAX6675 library" (Adafruit), as Arduino/CompostHeat, so both probes on
// this network behave identically. The library is plain Arduino API with
// architectures=*, so it builds on renesas_uno unchanged. It declares
// LiquidCrystal as a dependency, which the Library Manager installs
// alongside it; nothing here includes it.
#include <max6675.h>
// R4: bundled with the board package, like WiFiS3 - not a Library Manager
// install. Pixel work needs nothing else; ArduinoGraphics is only required
// if you want text on the matrix.
#include "Arduino_LED_Matrix.h"
// R4: over-the-air sketch upload. This include must come *after* WiFiS3.h -
// the library picks its network classes by testing for the WiFiS3_h guard,
// and included first it silently selects the wrong ones. Not bundled with
// the board package; install "ArduinoOTA" from the Library Manager.
#include <ArduinoOTA.h>
#include <math.h>
#include "arduino_secrets.h"

// ------------------*****---------------------
// I/O mapping area
// Ice: no flow meter, so no FLOWPIN and no interrupt anywhere in this
// sketch. D2 is deliberately left untouched rather than reused: on a board
// coming from FlowSenseR4 the YF-S201's yellow wire is still landed there,
// and a pin this sketch never configures is a pin that wire cannot fight.
// Leave the sensor plugged in or take it off - either way nothing here
// reads or drives D2.
//
// Everything else keeps FlowSenseR4's pin map exactly, so the display and
// the DHT22 stay where they are. Only the three thermocouple lines below
// are new.

// R4: the display moves off D2/D3. On this variant D4 and D7 are the only
// two header pins that are neither interrupt-capable nor PWM, which makes
// them the cheapest pins on the board to spend on a bit-banged display -
// nothing else would miss them.
#define DISPCLK 4  // CLK
#define DISPDIO 7  // DIO

TM1637Display display(DISPCLK, DISPDIO);

// R4: DHT22 data line. The display took D4 and D7 because they are the
// only header pins that are neither interrupt-capable nor PWM; by that
// same rule the cheapest pin left would be A0 - except A0 is this board's
// only DAC output, which is the more expensive thing to spend. D5 costs
// one PWM channel out of six and no interrupt channel, so it is the
// cheaper pin. (Ice: the pins FlowSenseR4 held back for a second flow
// meter - D3, D8 and A2, the three with unshared interrupt channels - stay
// free here too. Nothing in this sketch wants an interrupt, so they are
// spare for a lid switch, a chiller relay or a second probe.)
//
// The DHT22 is a single-wire bidirectional bus and needs a pull-up to its
// supply; most breakout modules have one fitted, a bare 4-pin sensor does
// not. See README > Wiring.
#define DHTPIN  5
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// Ice: MAX6675 thermocouple amplifier - the water temperature, and the
// reason this station exists. Read-only SPI, bit-banged by the library
// over any three digital pins, the same arrangement as
// Arduino/CompostHeat.
//
// These three are the only new wires. D9 and D10 raise no interrupt at all
// and cost one PWM channel each; the third line has to cost something, and
// D6 is the cheapest of what is left - its interrupt channel (IRQ4) is
// shared with D11, so spending D6 leaves IRQ4 reachable there and loses no
// channel at all. Nothing here uses an interrupt anyway, so this is about
// leaving the board useful rather than protecting anything in this sketch.
//
// D10 is also the hardware-SPI chip select, but nothing here uses the SPI
// peripheral and a chip select is only ever a plain GPIO. D11/D12/D13 stay
// free, so a real SPI device can still be added later with its CS on any
// spare pin.
#define MAXCLK 6   // SCK
#define MAXCS  9   // CS
#define MAXSO  10  // SO / DO (MISO)

MAX6675 thermocouple(MAXCLK, MAXCS, MAXSO);

// "----" - shown while the display is being taken out of service.
const uint8_t SEG_DASHES[] = {SEG_G, SEG_G, SEG_G, SEG_G};

// R4: unit tags for the rotating pages, drawn in the rightmost digit. The
// module has no decimal point and its one piece of punctuation is a centre
// colon that reads as a clock, so a bare number is the whole of what a
// digit can say - and three pages of bare numbers is exactly the ambiguity
// that got an earlier two-page rotation removed. Spending the last digit
// on a letter buys back the labelling the hardware otherwise cannot do.
//
// Ice: two of the three pages are now temperatures, so the tags label the
// subject rather than the unit. The water is what this station is for, so
// it keeps the plain degree tag; the air, the secondary reading, is the one
// that gets spelled out. Both glyphs are unmistakable for a digit - A has
// no bottom segment and C has no right-hand pair, whereas a 'b' or a 'U'
// would differ from 6 and 0 by one segment and misread at a glance.
const uint8_t SEG_UNIT_C = SEG_A | SEG_D | SEG_E | SEG_F;                  // C, water degrees
const uint8_t SEG_UNIT_A = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;  // A, air degrees
const uint8_t SEG_UNIT_H = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;          // H, humidity

// ------------------*****---------------------
// Ice: onboard 12x8 LED matrix, showing the last 12 hours of water
// temperature - one column per hour, height proportional to that hour's
// mean, newest on the right. The TM1637 answers "how cold is it right
// now"; the matrix answers "how has it held today", which is the question
// a bath that is warming up actually poses.
//
// It costs no header pins. The matrix is charlieplexed across D28-D38,
// which are internal to the board and not broken out, so it cannot
// collide with the probe or the display.
ArduinoLEDMatrix matrix;

const uint8_t matrixCols = 12;
const uint8_t matrixRows = 8;

// Ice: the vertical scale is a temperature window rather than a volume.
// The bottom row is matrixScaleMinC, the top row matrixScaleMaxC, and the
// eight rows divide the span between them - 3 C per row at the current
// setting. Fixed, not auto-ranging, so the same height always means the
// same temperature and two glances a day apart are comparable.
//
// 0 to 24 C covers an ice bath from fresh ice to abandoned-and-warmed-to-
// room-temperature. Narrow it to sharpen the trace: 0 to 8 C makes every
// row half a degree, which is what you want once the bath is actually in
// use. See README > LED matrix.
const float matrixScaleMinC = 0.0f;
const float matrixScaleMaxC = 24.0f;

// Column heights, 0..matrixRows. Index 0 is the oldest completed hour.
// The last column is the hour currently being filled, so it tracks that
// hour's mean and then shifts left when the hour closes. Ice: 0 means "no
// reading in that hour", which is why an in-range reading always lights at
// least one row - see matrixBarHeight().
uint8_t hourlyBars[matrixCols] = {0};
bool matrixDirty = true;

// ------------------*****---------------------
// Sensor calibration area
// Ice: nothing here needs a scale factor the way the flow meter did - the
// amplifier's slope is fixed in silicon and the thermocouple's is set by
// physics. What the channel does have is a few degrees of offset, from the
// amplifier (+/-2 C at 0-700 C) and the wire's own tolerance (+/-1.5 C for
// class-1 type K), and on a bath whose whole interesting range is five
// degrees wide that offset is the error that matters.
//
// It is also the one error a station like this can measure for free,
// because a well-stirred ice-and-water slurry *is* 0.00 C: read the probe
// in one, and whatever it says is this channel's offset. Put the negative
// of that here. See README > Checking the probe against the bath.
//
// Left at zero, this changes nothing. Note that a negative offset is the
// only way this sketch ever reports water below freezing - the MAX6675
// itself cannot, so a genuinely sub-zero bath saturates the raw reading at
// 0 C and the corrected value bottoms out at the offset.
const float probeOffsetC = 0.0f;

// ------------------*****---------------------
// WiFi setup area
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)

// ------------------*****---------------------
// MQTT Topic definition area
// Ice: its own station prefix, ICE, so nothing here collides with the FLOW
// topics a FlowSenseR4 board is already publishing - the two sketches can
// run side by side, or one board can be moved between them, without a
// dashboard having to guess which is which.
//
// WATER_C is the headline: the thermocouple in the bath. TEMP_C and
// HUMIDITY_PCT are the DHT22 above it, named as in FlowSenseR4 so the same
// dashboard panel works on either station. The fault flag rides along under
// DIAG like Arduino/CompostHeat's, because a gap in WATER_C that a
// dashboard cannot attribute is worse than no gap at all.
const char WATER_topic[]  = "MUTHUR/NDATA/ICE/WATER_C";
const char HOURLY_topic[] = "MUTHUR/NDATA/ICE/HOURLY_MEAN_C";
const char TEMP_topic[]   = "MUTHUR/NDATA/ICE/TEMP_C";
const char HUM_topic[]    = "MUTHUR/NDATA/ICE/HUMIDITY_PCT";
const char FAULT_topic[]  = "MUTHUR/DIAG/ICE/PROBE_FAULT";
const char STATUS_topic[] = "MUTHUR/DIAG/ICE/STATUS";
const char HB_topic[]     = "MUTHUR/DIAG/ICE/HB";
// ------------------*****---------------------

const unsigned long publishInterval = 10000;  // publish readings every 10s
const unsigned long diagInterval    = 30000;  // publish diagnostics every 30s

// Length of one averaging bucket. These are rolling hours since boot,
// not wall-clock hours - nothing here is time-synced, so "the last hour"
// means the last 3600 seconds of uptime.
const unsigned long hourInterval    = 3600000UL;

// How often to read the DHT22. The sensor will not produce a fresh
// conversion more often than once every two seconds, and air temperature
// moves far slower than anything else here, so this is deliberately no
// faster than the publish tick.
//
// Ice: in FlowSenseR4 this interval also governed how often the DHT22's
// interrupt-masked read could merge a flow pulse. There is no interrupt in
// this sketch, so that constraint is gone and the 5ms of masking costs
// nothing at all - the number stays where it is because the sensor and the
// physics have not changed.
const unsigned long climateInterval = 10000;

// Ice: how often to read the MAX6675. The part takes ~220ms to convert and
// hands back the same word if asked sooner, and the library's bit-bang is
// 16 bits of delayMicroseconds(10) - about 0.35ms of CPU per read, with
// interrupts left enabled throughout. One second is clear of both figures,
// matches Arduino/CompostHeat, and gives each hourly mean up to 3600
// samples to average over.
const unsigned long probeInterval = 1000;

// R4: how long each page holds the display before the next one takes over.
// Three pages, so the full cycle is three times this. Long enough to read
// and look away, short enough that the number you want is never far off.
const unsigned long displayPageInterval = 5000;

// Consecutive failed reads before the cached values are declared stale and
// stop being published. A DHT22 drops the occasional frame on a long run
// and one bad checksum is not a dead sensor - but half a minute of silence
// is, and republishing an old temperature forever is worse than publishing
// nothing and letting the dashboard show a gap.
const unsigned long climateFailuresBeforeStale = 3;

// Ice: the same staleness rule for the thermocouple, for the same reason -
// a marginal SO line drops the occasional word, an unplugged probe never
// comes back. This one is a fault flag rather than a checksum: the MAX6675
// reports an open circuit explicitly in bit D2 of its 16-bit word, which
// the library turns into NAN. Three reads is three seconds of it.
const unsigned long probeFailuresBeforeStale = 3;

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

// R4: how often to service the OTA listener. ArduinoOTA.poll() costs two
// round-trips to the ESP32-S3 - a socket check and an mDNS read - so
// calling it every pass would flood the UART the rest of the sketch
// depends on. Five times a second is far quicker than the upload tool's
// timeout and leaves the modem free.
const unsigned long otaPollInterval = 200;

// The name the board advertises over mDNS, and so how it appears in the
// IDE's port list.
const char otaName[] = "IceBath";

// R4: PubSubClient's default buffer is 256 bytes for the whole packet,
// topic and header included. The status JSON below fits, but this station
// is meant to grow - one more field and a silent publish failure is the
// only symptom. Buy the headroom now.
const uint16_t mqttBufferSize = 512;

unsigned long previousPublishMillis = 0;
unsigned long previousDiagMillis = 0;
unsigned long previousMillisLED = 0;

// Ice: hourly bucket. A volume could be differenced from a counter at the
// bucket's two ends; a temperature cannot, so this one genuinely does
// accumulate - a running sum and a sample count, divided at the point of
// use. At one reading a second the count tops out around 3600 per hour and
// the sum around 90000 for a warm bath, both of which a float carries with
// room to spare.
unsigned long hourStartMillis = 0;
float hourSumC = 0.0f;
unsigned long hourSamples = 0;
float lastHourMeanC = 0.0f;
bool haveLastHourMean = false;

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

// R4: OTA is started once, the first time the link comes up. begin() binds
// a listening socket and an mDNS socket; re-running it on every reconnect
// would stack those up.
bool otaStarted = false;
unsigned long lastOtaPoll = 0;

// R4: last good climate reading. Cached rather than re-read at the point
// of use, because the read is the expensive part - see readClimate().
float ambientTempC = 0.0f;
float ambientHumidityPct = 0.0f;
bool haveClimate = false;             // false until the first good read
unsigned long climateFailures = 0;    // consecutive; a good read clears it
unsigned long previousClimateMillis = 0;

// Ice: last good thermocouple reading, cached on the same terms as the
// climate pair above. This is the number the whole station is for.
float probeTempC = 0.0f;
bool haveProbe = false;             // false until the first good read
unsigned long probeFailures = 0;    // consecutive; a good read clears it
unsigned long previousProbeMillis = 0;

// Ice: coldest and warmest the water has been since boot. Cheap to keep and
// the two figures a bath session is actually judged on - how cold it got,
// and how far it has drifted back since.
float waterMinC = 0.0f;
float waterMaxC = 0.0f;
bool haveExtremes = false;

// The TM1637 is bit-banged at ~1ms per full four-digit write, and its
// contents only change when a reading lands or the page flips. Redrawing on
// every display tick regardless would be four times the bus traffic for the
// same digits, so the write is gated on this instead.
bool displayDirty = true;

// R4: which of the three pages is currently up. A fresh reading only dirties
// the display if the page showing it is the one on screen.
enum DisplayPage : uint8_t {
  PAGE_WATER = 0,   // Ice: the headline, so it is the page on screen at boot
  PAGE_TEMP,
  PAGE_HUM,
  PAGE_COUNT
};
uint8_t displayPage = PAGE_WATER;
unsigned long previousDisplayMillis = 0;

unsigned long heartBeat = 0;
long cachedRssi = 0;

int ledState = LOW;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// ------------------*****---------------------

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// R4: called once the uploaded sketch has been received and verified, just
// before it is written to flash and the board resets. Blank both readouts
// rather than leaving a frozen number sitting by the bath for the duration
// of the write. Ice: nothing to detach - there is no interrupt here.
void otaBeforeApply() {
  display.setSegments(SEG_DASHES);
  matrix.clear();
  Serial.println("OTA: image verified, writing flash and rebooting");
  Serial.flush();
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

      // R4: the listener needs the address, so it can only start once the
      // link is actually up.
      if (!otaStarted) {
        ArduinoOTA.beforeApply(otaBeforeApply);
        ArduinoOTA.begin(WiFi.localIP(), otaName, SECRET_OTA_PASS, InternalStorage);
        otaStarted = true;
        Serial.print("OTA: listening as \"");
        Serial.print(otaName);
        Serial.println("\"");
      }

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
  // Report it and keep looping so the probe keeps being read and the USB
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
  if (client.connect("IceBathClient")) {
    Serial.println("connected");
    return true;
  }

  Serial.print("failed, rc=");
  Serial.println(client.state());
  return false;
}

// ------------------*****---------------------

// Ice: mean water temperature so far in the hour currently being filled.
// The caller must check hourSamples first - there is no sensible value to
// return for an hour that has seen no readings, and 0 would be a lie a
// bath could plausibly produce.
float hourMeanC() {
  if (hourSamples == 0) {
    return 0.0f;
  }
  return hourSumC / (float)hourSamples;
}

// Ice: a temperature -> column height in rows, over the fixed window
// between matrixScaleMinC and matrixScaleMaxC.
//
// Zero rows is reserved for "no reading in that hour", so anything in range
// lights at least one row: a bath at the bottom of the scale is exactly
// what this display exists to show, and a blank column would read as a
// dead sensor instead.
uint8_t matrixBarHeight(float tempC) {
  float span = matrixScaleMaxC - matrixScaleMinC;
  if (span <= 0.0f) {
    return 1;  // misconfigured scale; still show that a reading exists
  }
  long scaled = lroundf(((tempC - matrixScaleMinC) / span) * (float)matrixRows);
  if (scaled < 1)                scaled = 1;
  if (scaled > (long)matrixRows) scaled = matrixRows;
  return (uint8_t)scaled;
}

// Ice: refresh the in-progress hour, the rightmost column.
void matrixSetCurrentHour(float tempC) {
  uint8_t height = matrixBarHeight(tempC);
  if (hourlyBars[matrixCols - 1] != height) {
    hourlyBars[matrixCols - 1] = height;
    matrixDirty = true;
  }
}

// Ice: an hour closed - shift everything left, drop the oldest, and start
// the new hour empty. Empty means "no reading yet", and the column fills in
// on the next good read rather than at the next hour boundary.
void matrixRollHour() {
  for (uint8_t c = 0; c + 1 < matrixCols; c++) {
    hourlyBars[c] = hourlyBars[c + 1];
  }
  hourlyBars[matrixCols - 1] = 0;
  matrixDirty = true;
}

void matrixRender() {
  // Every cell must be exactly 0 or 1: the library ORs each byte into a
  // bit-packed frame and then shifts, so any other value would bleed into
  // neighbouring pixels rather than just lighting this one brighter.
  uint8_t frame[matrixRows][matrixCols] = {{0}};

  for (uint8_t c = 0; c < matrixCols; c++) {
    // Columns grow upwards from the bottom row.
    for (uint8_t r = 0; r < hourlyBars[c]; r++) {
      frame[matrixRows - 1 - r][c] = 1;
    }
  }

  matrix.renderBitmap(frame, matrixRows, matrixCols);
}

// R4: lay out one page - up to three digits of value, right-aligned, with
// the unit letter fixed in the rightmost digit. Blank leading digits rather
// than leading zeros, so 8 C reads as "8" and not "008".
//
// Out-of-range values are clamped rather than allowed to wrap. A display
// pinned at "999" is visibly pinned; a wrapped number looks like a real
// reading and is the more dangerous of the two failures.
void displayValueWithUnit(long value, uint8_t unitSegments, bool valid) {
  uint8_t seg[4] = {0, 0, 0, unitSegments};

  // No reading yet, or the sensor has gone quiet: "--" against the unit
  // letter, so the page still says which reading is missing.
  if (!valid) {
    seg[1] = SEG_G;
    seg[2] = SEG_G;
    display.setSegments(seg);
    return;
  }

  bool negative = (value < 0);
  if (negative) {
    value = -value;
    if (value > 99) value = 99;    // the minus sign costs one of the three
  } else {
    if (value > 999) value = 999;
  }

  int8_t pos = 2;
  do {
    seg[pos--] = display.encodeDigit((uint8_t)(value % 10));
    value /= 10;
  } while (value > 0 && pos >= 0);

  if (negative && pos >= 0) {
    seg[pos] = SEG_G;
  }

  display.setSegments(seg);
}

// R4: draw whichever page is currently up. Every value is rounded to a
// whole unit - the display has four digits and no decimal point, and this
// station trends on the MQTT series and the matrix, not on the instant.
//
// Ice: whole degrees is coarse for a bath whose interesting range is only a
// few degrees wide, and it is still the right call here: the module cannot
// place a decimal point, and a bare "35" that might be 3.5 C is worse than
// a rounded "4". WATER_C carries the hundredths for anyone who needs them.
void renderDisplay() {
  switch (displayPage) {
    case PAGE_TEMP:
      displayValueWithUnit(lroundf(ambientTempC), SEG_UNIT_A, haveClimate);
      break;
    case PAGE_HUM:
      displayValueWithUnit(lroundf(ambientHumidityPct), SEG_UNIT_H, haveClimate);
      break;
    case PAGE_WATER:
    default:
      displayValueWithUnit(lroundf(probeTempC), SEG_UNIT_C, haveProbe);
      break;
  }
}

// R4: sample the DHT22. readHumidity() performs the bus transaction and
// caches the frame; readTemperature() then reads that same cached frame
// rather than starting a second one, so this pair costs one exchange with
// the sensor, not two.
//
// Either value coming back NaN means a dropped or corrupt frame. Keep the
// previous reading over it, since a single bad checksum is routine, but
// stop publishing once the failures stack up so a sensor that has been
// unplugged goes quiet instead of repeating its last number forever.
void readClimate() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();  // Celsius

  if (isnan(humidity) || isnan(temperature)) {
    climateFailures++;
    if (climateFailures >= climateFailuresBeforeStale) {
      haveClimate = false;
    }
    return;
  }

  climateFailures = 0;
  ambientHumidityPct = humidity;
  ambientTempC = temperature;
  haveClimate = true;
}

// Ice: sample the MAX6675. readCelsius() drops CS, clocks 16 bits out and
// returns NAN when the open-circuit bit is set - an unplugged or broken
// thermocouple, or an SO line left floating high. Treated exactly like a
// dropped DHT22 frame: keep the last reading over a single bad word, go
// quiet once they stack up.
//
// Note the part is unsigned: its range is 0 to +1024 C and it cannot read
// below freezing at all. For this station that matters twice over - a bath
// with salt or a chiller in it can genuinely go below zero and will read
// 0 C, and an SO line stuck low returns 0 C as well, which in an ice bath
// is a perfectly plausible number. See README > Accuracy near zero.
//
// A good read also feeds the hourly mean and the since-boot extremes, so
// those only ever see values the sensor actually produced - and all three
// see the same corrected number, since probeOffsetC is applied once here
// rather than at each point of use.
void readProbe() {
  float tempC = thermocouple.readCelsius();

  if (isnan(tempC)) {
    probeFailures++;
    if (probeFailures >= probeFailuresBeforeStale) {
      haveProbe = false;
    }
    return;
  }

  tempC += probeOffsetC;

  probeFailures = 0;
  probeTempC = tempC;
  haveProbe = true;

  hourSumC += tempC;
  hourSamples++;

  if (!haveExtremes) {
    waterMinC = tempC;
    waterMaxC = tempC;
    haveExtremes = true;
  } else {
    if (tempC < waterMinC) waterMinC = tempC;
    if (tempC > waterMaxC) waterMaxC = tempC;
  }
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
  Serial.println("IceBath booting");
  Serial.println("=======================================");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Ice: D2 is never configured here - see the I/O mapping area. A
  // FlowSenseR4 board's flow sensor can stay landed on it untouched.

  // R4: draw the first page immediately, so a wired-up display is visibly
  // alive from boot rather than looking dead for the first second. Ice: it
  // reads " --C" until the first conversion lands - already saying which
  // page is up and that the value is not in yet.
  display.setBrightness(2);
  renderDisplay();

  // R4: begin() claims a free FSP timer and multiplexes the matrix from a
  // 10kHz periodic interrupt. Nothing else in this sketch wants a timer.
  // Ice: the render below just clears it - every bucket starts empty
  // because no reading has landed yet, and an empty column means exactly
  // that rather than "cold".
  matrix.begin();
  matrixRender();

  // R4: the DHT22 wants a moment after power-up before it will answer, and
  // the first read is a full climateInterval away, which covers it.
  dht.begin();

  // Ice: the MAX6675 library sets its three pin modes in its constructor,
  // which runs from __libc_init_array before this core's init() has touched
  // the port registers - so re-apply them here, where they stick. CS idles
  // high; pulling it low is what starts a read.
  //
  // The part also wants ~500ms after power-up before its first conversion
  // is good. The first read is a full probeInterval away, which covers it
  // without the blocking delay(500) that Arduino/CompostHeat needs in its
  // setup().
  pinMode(MAXCLK, OUTPUT);
  pinMode(MAXSO, INPUT);
  pinMode(MAXCS, OUTPUT);
  digitalWrite(MAXCS, HIGH);

  WiFi.setTimeout(wifiConnectTimeout);

  client.setServer(server, 1883);
  client.setCallback(callback);
  client.setBufferSize(mqttBufferSize);

  unsigned long now = millis();
  hourStartMillis = now;
  previousClimateMillis = now;
  previousProbeMillis = now;
  previousDisplayMillis = now;

  Serial.println("Setup complete, entering main loop");
}

void loop() {
  unsigned long currentMillis = millis();

  pollLink(false);
  maintainWiFi();

  // One connected() check per pass, reused below for the same reason as
  // the link status above.
  bool mqttUp = maintainMqtt();

  // R4: service the OTA listener. Gated behind its own interval for the
  // same reason RSSI is - see otaPollInterval. During an actual upload
  // poll() blocks until the image has been received. Ice: nothing runs
  // underneath it here, so the readings simply pause for the length of the
  // transfer and the hour in progress ends up averaged over fewer samples.
  if (otaStarted && wifiUp() && (currentMillis - lastOtaPoll >= otaPollInterval)) {
    lastOtaPoll = currentMillis;
    ArduinoOTA.poll();
  }

  // Cache RSSI rather than calling WiFi.RSSI() inline. Each call is a full
  // round-trip to the radio.
  if (currentMillis - lastRssiPoll >= rssiPollInterval) {
    lastRssiPoll = currentMillis;
    cachedRssi = wifiUp() ? WiFi.RSSI() : 0;
  }

  // Ice: rotate the readout through water temperature, air temperature and
  // humidity, five seconds each. Each page carries its subject in the last
  // digit, which is what makes a rotation readable at all here - see the
  // SEG_UNIT_* glyphs and README > Display.
  if (currentMillis - previousDisplayMillis >= displayPageInterval) {
    previousDisplayMillis = currentMillis;
    displayPage = (uint8_t)((displayPage + 1) % PAGE_COUNT);
    displayDirty = true;
  }

  // Local readout. Independent of WiFi and MQTT on purpose: the numbers
  // should be readable standing over the bath, deciding whether to get in,
  // whether or not the network or the broker is up.
  //
  // The write is gated on the dirty flag rather than issued on a timer: the
  // digits only change when the page flips or when a fresh reading lands on
  // the page that is currently up.
  if (displayDirty) {
    displayDirty = false;
    renderDisplay();
  }

  // Ice: the bars change when the live hour's mean moves or an hour rolls,
  // so the repack-and-render happens on those edges rather than on a timer.
  if (matrixDirty) {
    matrixDirty = false;
    matrixRender();
  }

  // Close off an hour and publish its mean. This is the series to trend on:
  // one figure per hour, averaged over every reading the probe actually
  // produced in it rather than sampled once at the boundary.
  if (currentMillis - hourStartMillis >= hourInterval) {
    // Advance by exactly one interval rather than snapping to now, so the
    // bucket boundaries do not creep later every hour.
    hourStartMillis += hourInterval;

    // Ice: an hour with no good read has no mean, and inventing one would
    // put a fabricated point in the middle of the series. Publish nothing,
    // say so on the serial line, and let the dashboard show the gap.
    bool hourHadSamples = (hourSamples > 0);
    unsigned long samples = hourSamples;
    if (hourHadSamples) {
      lastHourMeanC = hourMeanC();
      haveLastHourMean = true;
    }

    hourSumC = 0.0f;
    hourSamples = 0;

    // Ice: the hour that just closed becomes a fixed bar and the live
    // column starts again empty.
    matrixRollHour();

    if (hourHadSamples) {
      char hourChar[16];
      snprintf(hourChar, sizeof(hourChar), "%.2f", lastHourMeanC);
      if (mqttUp) {
        client.publish(HOURLY_topic, hourChar);
      }
      Serial.print("Hour closed: mean ");
      Serial.print(hourChar);
      Serial.print(" C over ");
      Serial.print(samples);
      Serial.println(" readings");
    } else {
      Serial.println("Hour closed: no probe readings, nothing published");
    }
  }

  // Sample ambient temperature and humidity. On its own tick rather than
  // folded into the publish block below, so the read cadence can be changed
  // without also changing the cadence of everything that is published.
  if (currentMillis - previousClimateMillis >= climateInterval) {
    previousClimateMillis = currentMillis;
    readClimate();
    if (displayPage == PAGE_TEMP || displayPage == PAGE_HUM) {
      displayDirty = true;
    }
  }

  // Ice: sample the thermocouple, on its own tick for the same reason the
  // climate pair has one - read cadence and publish cadence are separate
  // concerns. A third of a millisecond of bit-banging, ten times faster
  // than the interval it is published on, which is what makes the hourly
  // mean worth anything.
  if (currentMillis - previousProbeMillis >= probeInterval) {
    previousProbeMillis = currentMillis;
    readProbe();
    if (displayPage == PAGE_WATER) {
      displayDirty = true;
    }
    // The live column tracks the hour's mean as it accumulates.
    if (hourSamples > 0) {
      matrixSetCurrentHour(hourMeanC());
    }
  }

  // Publish readings. This block runs whether or not the network is up so
  // that the serial log always shows the board is alive and sampling.
  if (currentMillis - previousPublishMillis >= publishInterval) {
    previousPublishMillis = currentMillis;
    heartBeat++;

    char hbChar[16];
    char waterChar[16];
    char tempChar[16];
    char humChar[16];
    char faultChar[2];

    snprintf(hbChar, sizeof(hbChar), "%lu", heartBeat);

    // Ice: two decimal places, which is exactly what the MAX6675's 0.25 C
    // steps resolve to - the same form Arduino/CompostHeat publishes the
    // same part in, so the last digit is always a real quarter-degree step
    // rather than invented precision.
    if (haveProbe) {
      snprintf(waterChar, sizeof(waterChar), "%.2f", probeTempC);
    } else {
      snprintf(waterChar, sizeof(waterChar), "--");
    }

    // R4: one decimal place, which is the DHT22's own resolution - more
    // would dress up precision the sensor does not have. The serial line
    // keeps a column for these either way; MQTT gets nothing at all when
    // there is no reading, rather than a placeholder a dashboard would
    // happily plot as a number.
    if (haveClimate) {
      snprintf(tempChar, sizeof(tempChar), "%.1f", ambientTempC);
      snprintf(humChar, sizeof(humChar), "%.1f", ambientHumidityPct);
    } else {
      snprintf(tempChar, sizeof(tempChar), "--");
      snprintf(humChar, sizeof(humChar), "--");
    }

    // The gap in WATER_C and the reason for it, published together: "1"
    // means there is no current reading, so a dashboard can tell a dead
    // probe from a dead broker. Written by hand rather than with strcpy,
    // which would be the sketch's only <string.h> call.
    faultChar[0] = haveProbe ? '0' : '1';
    faultChar[1] = '\0';

    if (mqttUp) {
      if (haveProbe) {
        client.publish(WATER_topic, waterChar);
      }
      // Unconditional, unlike the readings: the whole point of this one is
      // to be there when the reading is not.
      client.publish(FAULT_topic, faultChar);
      if (haveClimate) {
        client.publish(TEMP_topic, tempChar);
        client.publish(HUM_topic, humChar);
      }
      client.publish(HB_topic, hbChar);
    }

    Serial.print("HB ");
    Serial.print(hbChar);
    Serial.print(" | Water: ");
    Serial.print(waterChar);
    Serial.print(" C | Air: ");
    Serial.print(tempChar);
    Serial.print(" C ");
    Serial.print(humChar);
    Serial.print(" %RH | WiFi ");
    Serial.print(wifiUp() ? "up" : "DOWN");
    Serial.print(" | MQTT ");
    Serial.println(mqttUp ? "up" : "DOWN");
  }

  // Publish diagnostics (JSON). Built with snprintf rather than String
  // concatenation - this runs every 30s for weeks at a time, and repeated
  // String reallocation fragments the heap.
  if (currentMillis - previousDiagMillis >= diagInterval) {
    previousDiagMillis = currentMillis;

    // R4: JSON has no NaN, so a missing reading is a literal null rather
    // than a number nobody can distinguish from a real one. Formatted
    // first, then substituted with %s.
    //
    // Ice: the probe contributes five nullable fields on top of the climate
    // pair, and each null means something different. A null water_c is the
    // probe not answering right now; null extremes mean it has never
    // answered at all; a null hour_mean_c means nothing has landed in the
    // hour currently open; and a null last_hour_mean_c means no hour has
    // closed with any readings in it yet - which is also the state for the
    // first hour after boot.
    char waterField[12];
    char minField[12];
    char maxField[12];
    char hourField[12];
    char lastHourField[12];
    char tempField[12];
    char humField[12];

    if (haveClimate) {
      snprintf(tempField, sizeof(tempField), "%.1f", ambientTempC);
      snprintf(humField, sizeof(humField), "%.1f", ambientHumidityPct);
    } else {
      snprintf(tempField, sizeof(tempField), "null");
      snprintf(humField, sizeof(humField), "null");
    }

    if (haveProbe) {
      snprintf(waterField, sizeof(waterField), "%.2f", probeTempC);
    } else {
      snprintf(waterField, sizeof(waterField), "null");
    }

    if (haveExtremes) {
      snprintf(minField, sizeof(minField), "%.2f", waterMinC);
      snprintf(maxField, sizeof(maxField), "%.2f", waterMaxC);
    } else {
      snprintf(minField, sizeof(minField), "null");
      snprintf(maxField, sizeof(maxField), "null");
    }

    if (hourSamples > 0) {
      snprintf(hourField, sizeof(hourField), "%.2f", hourMeanC());
    } else {
      snprintf(hourField, sizeof(hourField), "null");
    }

    if (haveLastHourMean) {
      snprintf(lastHourField, sizeof(lastHourField), "%.2f", lastHourMeanC);
    } else {
      snprintf(lastHourField, sizeof(lastHourField), "null");
    }

    // 400 rather than PubSubClient's 256-byte default for the whole packet:
    // snprintf truncates in silence, which on this topic would look like a
    // broker fault rather than a buffer one. Ice: the worst case here is a
    // little over 300 bytes, and it stays well inside mqttBufferSize, which
    // has to cover this payload plus the topic and the fixed header.
    char statusMessage[400];
    snprintf(statusMessage, sizeof(statusMessage),
             "{\"device\": \"Arduino UNO R4 WiFi\","
             "\"rssi\": %ld,"
             "\"uptime\": %lu,"
             "\"water_c\": %s,"
             "\"water_min_c\": %s,"
             "\"water_max_c\": %s,"
             "\"hour_mean_c\": %s,"
             "\"hour_samples\": %lu,"
             "\"last_hour_mean_c\": %s,"
             "\"probe_fails\": %lu,"
             "\"temp_c\": %s,"
             "\"humidity_pct\": %s,"
             "\"climate_fails\": %lu}",
             cachedRssi,
             currentMillis / 1000,
             waterField,
             minField,
             maxField,
             hourField,
             hourSamples,
             lastHourField,
             probeFailures,
             tempField,
             humField,
             climateFailures);

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
