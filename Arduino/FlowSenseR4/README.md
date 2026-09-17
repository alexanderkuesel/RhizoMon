# FlowSenseR4

UNO R4 WiFi build of the [FlowSense](../FlowSense) water meter: a YF-S201
Hall-effect flow sensor, a DHT22 for the air at the tap, a MAX6675 K-type
thermocouple probe, a TM1637 4-digit display at the tap, and readings
published to the MUTHUR MQTT broker.

The measurement core is identical to the Nano 33 IoT build — same pulse
counting, same rate maths, same flow topics, so this board is a drop-in
replacement for it. Everything past that is what the R4 has room for that
the Nano does not: the climate pair, the thermocouple probe, the hourly
matrix and over-the-air updates. See
[Why a separate sketch](#why-a-separate-sketch) and
[Expansion notes](#expansion-notes).

## Hardware

- Arduino UNO R4 WiFi
- YF-S201 water flow sensor (1/2" BSP, 1-30 L/min)
- DHT22 / AM2302 temperature and humidity sensor
- MAX6675 breakout board + K-type thermocouple — see
  [Thermocouple probe](#thermocouple-probe)
- TM1637 4-digit 7-segment display module

The board's onboard 12x8 LED matrix is used too, and costs no extra parts
and no header pins — see [LED matrix](#led-matrix).

No level shifter and no divider. The only soldering is the MAX6675
breakout's own header strip, if yours shipped loose.

## Wiring

### YF-S201

**No level shifting needed on this board.** The RA4M1's VDD is tied to the
5V rail, so the header pins are true 5V logic and the YF-S201's 5V output
is just a normal high. (The Nano 33 IoT build needs a divider or a level
converter because its pins are 3.3V and not 5V tolerant — that is the main
practical reason to prefer the R4 for this sensor.)

| YF-S201 wire | Goes to |
|--------------|----------|
| Red          | 5V       |
| Black        | GND      |
| Yellow       | **D2**   |

The R4's `5V` pin is live out of the box — there is no VUSB solder jumper
to bridge, unlike the Nano 33 IoT.

The sketch uses `INPUT_PULLUP`. The YF-S201's open-collector output needs
a pull-up to its own 5V rail, and on this board the internal pull-up *is*
on that rail, so no external resistor is required. The internal pull-up is
weak though (tens of kΩ). On a long unshielded run, especially one
cable-tied alongside a pump's mains lead, fit an external **4.7 kΩ from D2
to 5V** as well for noise margin — the sketch works either way.

### Why D2, and why the display moved

On the UNO R4 WiFi only some header pins carry an external interrupt
channel, and several of them share a channel with another pin. Read
straight from the core's variant tables
(`variants/UNOWIFIR4/pinmux.inc` + `variant.cpp`):

| Pin | IRQ channel | | Pin | IRQ channel |
|-----|-------------|-|-----|-------------|
| D0  | IRQ6 (shared with A1) | | D8  | IRQ9 |
| D1  | IRQ5 (shared with D12) | | D11 | IRQ4 (shared with D6) |
| **D2** | **IRQ1** (shared with A4/SDA) | | D12 | IRQ5 (shared with D1) |
| D3  | IRQ0 | | A1  | IRQ6 (shared with D0) |
| D6  | IRQ4 (shared with D11) | | A2  | IRQ7 |
|     |      | | A3  | IRQ2 (shared with A5/SCL) |

**D4, D5, D7, D9, D10, D13 and A0 have no interrupt capability at all.**

So the pin plan is:

- **D2** for the flow sensor. It is one of the two classic UNO interrupt
  pins and its channel only collides with A4/SDA, which is I2C and never
  wants `attachInterrupt()`.
- **D4 (CLK) and D7 (DIO)** for the display. These are the only two header
  pins that are neither interrupt-capable nor PWM, so they are the
  cheapest pins on the board to spend on a bit-banged display — nothing
  else would miss them.
- **D5** for the DHT22. By the rule above the cheapest pin left would be
  A0, except A0 is this board's only DAC output. D5 costs one PWM channel
  out of six and no interrupt channel.
- **D6 (SCK), D9 (CS) and D10 (SO)** for the MAX6675. D9 and D10 are the
  last two header pins that raise no interrupt, so they go first. The
  third line has to cost something, and D6 is the cheapest of what is
  left: its channel (IRQ4) is shared with D11, so spending D6 leaves IRQ4
  still reachable there and loses no channel at all.
- **D3 (IRQ0) and D8 (IRQ9) are deliberately left free.** They are the
  only two interrupt-capable pins whose channel collides with nothing, so
  they are the obvious home for a second flow meter later. **A2 (IRQ7)**
  is the third such pin and stays free too.

D10 is also the hardware-SPI chip select, but nothing here uses the SPI
peripheral and a chip select is only ever a plain GPIO — **D11, D12 and
D13 stay free**, so a real SPI device can still be added later with its CS
on any spare pin. What is left over after all of the above is D3, D8, D11,
D12 and A0-A5, plus D0/D1 if you are willing to give up the serial log.

Note that `BaseStation.ino` uses D5 for a switch and D7 for a relay — now
the DHT22 and a display pin. Those are separate boards today, but if you
ever consolidate onto one R4, both are collisions to watch.

### DHT22

| DHT22 pin | UNO R4 WiFi pin |
|-----------|-----------------|
| 1 VCC     | **5V**          |
| 2 DATA    | **D5**          |
| 3 —       | *not connected* |
| 4 GND     | GND             |

The DATA line is open-drain and **needs a 10 kΩ pull-up to VCC**. Three-pin
AM2302 breakout boards (`+` / `OUT` / `-`) have one fitted; a bare 4-pin
DHT22 does not. Keep it out of the spray — the part is not sealed, and a
wet element reads 100% humidity for hours.

### MAX6675 thermocouple amplifier

| MAX6675 module pin | UNO R4 WiFi pin |
|--------------------|-----------------|
| VCC                | **5V**          |
| GND                | GND             |
| SCK                | **D6**          |
| CS                 | **D9**          |
| SO (sometimes `DO`) | **D10**     |

The MAX6675 runs on anything from 3.0 V to 5.5 V and its `SO` output
swings to its own supply, so on this board feed it **5 V** and the logic
levels line up with the header pins exactly. No pull-ups, no resistors,
no level shifting.

It is read-only SPI — there is no MOSI — and the library bit-bangs all
three lines, which is why they sit on ordinary GPIOs rather than on the
board's SPI pins.

The thermocouple itself goes into the module's screw terminals, and
**polarity matters**: `+` takes the yellow lead on ANSI-coded type K wire,
or green on IEC-coded wire; `-` takes red (ANSI) or white (IEC). Wired
backwards it still reads, but the number *falls* as the probe heats up.

### TM1637 display

| TM1637 pin | UNO R4 WiFi pin | Notes |
|------------|-----------------|-------|
| VCC        | **5V**          | Unlike the Nano 33 IoT build, feed this one 5V — that is the TM1637's native supply and it runs noticeably brighter than at 3.3V |
| GND        | GND             | |
| CLK        | D4              | |
| DIO        | D7              | |

Keep the sensor off the **Qwiic connector** — that is a 3.3V I2C bus and,
unlike the header pins, it is *not* 5V tolerant.

### Plumbing

The YF-S201 body is marked with a flow-direction arrow — fit it pointing
downstream. It is not rated for potable water or for continuous pressure
above 1.75 MPa, and its published accuracy (±10%) is a turbine spec, not a
laboratory one. Mount it with a straight run of pipe either side if you
can; an elbow immediately upstream puts swirl into the flow and the
turbine reads high.

## Libraries

Install via the Arduino Library Manager:

- **PubSubClient** (Nick O'Leary) — MQTT client
- **TM1637** (Avishay Orpaz) — 4-digit display driver
- **ArduinoOTA** (Juraj Andrassy) — over-the-air sketch upload
- **DHT sensor library** (Adafruit) — DHT22 driver, which pulls in
  **Adafruit Unified Sensor** as a dependency
- **MAX6675 library** (Adafruit) — thermocouple amplifier driver, the same
  one [CompostHeat](../CompostHeat) uses. It declares **LiquidCrystal** as
  a dependency, which the Library Manager installs alongside it; nothing
  in this sketch includes it

**WiFiS3** and **Arduino_LED_Matrix** are both bundled with the UNO R4
board package — do not install either separately. The flow sensor needs no
library; it is a bare pulse train, counted by an interrupt in the sketch.

## Configuration

WiFi credentials are kept out of the sketch and out of git:

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` in this same
   folder.
2. Edit `arduino_secrets.h` and fill in your real `SECRET_SSID`,
   `SECRET_PASS`, and `SECRET_OTA_PASS` (the over-the-air upload
   password — see [Over-the-air updates](#over-the-air-updates)).

`arduino_secrets.h` is listed in the repo's `.gitignore`, so it won't be
committed.

The MQTT broker address is set in `FlowSenseR4.ino`:

```cpp
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)
```

## Calibration

Identical to the Nano build. The YF-S201's published characteristic is
`F = 7.5 * Q`, which works out to **450 pulses per litre**:

```cpp
const float pulsesPerLitre = 450.0f;
```

Individual sensors land a few percent either side, and the error is a
straight scale factor, so one measured run trims it out:

1. Note the `PULSES` value on MQTT (or in the serial log).
2. Run a known volume through the meter into a bucket or calibrated jug —
   10 L or more, the bigger the better.
3. Note `PULSES` again.
4. `pulsesPerLitre = (pulses after - pulses before) / litres collected`.

Both the rate and the total derive from this one constant, so they stay
consistent.

## Thermocouple probe

The MAX6675 channel is deliberately unopinionated about what it is
measuring — it is a K-type probe on a station that already has air
temperature, published as `PROBE_C` and shown on the display as `P`. Clip
it to the water line, to a solar coil feeding the tank, or into a compost
pile beside the tap; the sketch does not care, and nothing about it is
specific to water.

To relabel it for a fixed job, change `PROBE_topic` and
`PROBE_FAULT_topic` in the sketch, and the `SEG_UNIT_P` glyph if a
different letter reads better — the 7-segment alphabet that survives four
digits is roughly `A b C d E F H L n o P r t U`.

### What it can and cannot read

| Property | Figure |
|----------|--------|
| Range | **0 to +1024 °C** |
| Resolution | 0.25 °C (12-bit) |
| Accuracy | ±8 LSB, so about **±2 °C** from 0 to +700 °C — *plus* the thermocouple's own tolerance, which for class-1 type K wire is another ±1.5 °C |
| Read cadence | every 1 s (`probeInterval`), published every 10 s |

**It cannot read below 0 °C at all.** The MAX6675's output word is
unsigned, so there is no frost reading here and no negative number to
alarm on — a probe below freezing reads 0 °C. If you want frost detection
on a water line, the pin-compatible **MAX31855** does negatives (and
reports short-to-VCC and short-to-GND faults besides), or use the DHT22's
air temperature, which goes to -40 °C.

Be honest about the accuracy too, because K-type is the wrong tool for
lukewarm water: a couple of degrees of amplifier error plus a degree and a
half of wire tolerance is fine on a compost pile at 65 °C and poor on a
pipe at 12 °C. **A DS18B20 is more accurate below 100 °C, and cheaper.**
The MAX6675 earns its place when the probe might see real heat, or when
you already own the part — which, this repo having a
[CompostHeat](../CompostHeat) station, is the case here.

### Cold-junction compensation

The MAX6675 compensates using its **own die temperature**, so the chip is
one half of the measurement. Mount the breakout somewhere with stable,
ordinary ambient — inside the enclosure, not bolted to a hot pipe and not
in direct sun — and let only the probe see the interesting temperature. A
module warming in the afternoon sun skews every reading taken while it
does.

### Faults

`readCelsius()` returns `NAN` when the amplifier reports an open circuit,
which covers an unplugged probe, a broken thermocouple and a floating `SO`
line. One bad word is kept quiet and the previous reading held, exactly as
with the DHT22; after **three consecutive failures**
(`probeFailuresBeforeStale`) the reading is declared stale, `PROBE_C`
stops being published, the display page shows ` --P` and
`PROBE_FAULT` goes to `1`.

Unlike the DHT22, the read is cheap and safe: 16 bits of bit-banging at
10 µs a half-cycle is about **0.35 ms of CPU**, with interrupts enabled
throughout, so it can never merge a flow pulse the way the DHT22's
interrupt-masked exchange can. The 1 s cadence is set by the part's ~220 ms
conversion time, not by any cost to the meter.

## MQTT Topics

**The flow topics are the same as the Nano 33 IoT build's**, so this board
is a drop-in replacement and existing dashboards keep working. The sensors
the Nano does not have publish alongside them:

| Topic                        | Payload                          | Frequency |
|-------------------------------|-----------------------------------|-----------|
| `MUTHUR/NDATA/FLOW/RATE_LPM` | Current flow rate, L/min (float, 2dp) | 10s  |
| `MUTHUR/NDATA/FLOW/TOTAL_L`  | Cumulative volume since boot, litres (float, 3dp) | 10s |
| `MUTHUR/NDATA/FLOW/PULSES`   | Cumulative raw pulse count since boot | 10s |
| `MUTHUR/NDATA/FLOW/HOURLY_L` | Litres drawn in the hour that just closed (float, 3dp) | 1h |
| `MUTHUR/NDATA/FLOW/TEMP_C`   | Air temperature at the tap, °C (float, 1dp) | 10s |
| `MUTHUR/NDATA/FLOW/HUMIDITY_PCT` | Relative humidity at the tap, % (float, 1dp) | 10s |
| `MUTHUR/NDATA/FLOW/PROBE_C`  | Thermocouple temperature, °C (float, 2dp) | 10s |
| `MUTHUR/DIAG/FLOW/PROBE_FAULT` | `1` when there is no current probe reading, `0` when there is | 10s |
| `MUTHUR/DIAG/FLOW/STATUS`    | JSON: `{device, rssi, uptime, rate_lpm, total_l, hour_l, last_hour_l, pulses, temp_c, humidity_pct, climate_fails, probe_c, probe_fails}` | 30s |
| `MUTHUR/DIAG/FLOW/HB`        | Heartbeat counter                | 10s       |

`TEMP_C`, `HUMIDITY_PCT`, `PROBE_C` and `PROBE_FAULT` have no counterpart
on the Nano build, which simply never publishes them — the flow topics stay
a drop-in either way.
They are read every 10s and published only when the read succeeds; after
three consecutive failures they stop being published rather than repeat a
stale value, and the JSON fields go to `null` with `climate_fails`
counting the run. Note the DHT22's bit-banged protocol masks interrupts
for ~5ms per read, so at sustained flow above ~27 L/min a pulse can be
merged — under 0.05% at the sensor's 30 L/min ceiling, against its own
±10% accuracy. Raise `climateInterval` if that matters.

`PROBE_C` follows the same publish-only-when-fresh rule, with `probe_c`
and `probe_fails` in the JSON. Because a missing reading there is more
often a real fault than a dropped frame, `PROBE_FAULT` says so outright,
so a dashboard can tell a dead probe from a dead broker. Its read costs
the meter nothing — see [Thermocouple probe](#thermocouple-probe).

`device` in the status JSON reads `Arduino UNO R4 WiFi`, so you can tell
the two boards apart on the wire. **If you ever run both meters at once,
change the `FLOW` segment on one of them** or they will fight over the
same topics.

`RATE_LPM` is the instantaneous rate over the last one-second window — the
same number the display is showing. **`TOTAL_L` is the authoritative
volume**: accumulated from every pulse in integer pulses and converted to
litres only when published, so nothing is lost between publishes and no
rounding error builds up.

`HOURLY_L` is **the series to trend on**: one figure per hour, published
the moment that hour closes. Like `TOTAL_L` it is derived from the pulse
counter at the bucket's two ends rather than accumulated as floats, so it
cannot drift.

These are rolling hours since boot, not wall-clock hours — nothing here is
time-synced, so "the last hour" means the last 3600 seconds of uptime. A
reboot starts a fresh bucket, and the partial hour in progress at that
moment is lost. The in-progress figure is visible meanwhile as `hour_l` in
the diagnostics JSON, with the last completed hour alongside it as
`last_hour_l`.

Both totals are **since boot** — see [Expansion notes](#expansion-notes),
this is the one the R4 can actually fix.

## Display

The TM1637 rotates through four pages, **four seconds each**, so the full
cycle is sixteen seconds:

| Page | Shown | Meaning |
|------|-------|---------|
| Flow rate | `  8L` | 8 L/min |
| Air temperature | ` 21C` | 21 °C at the tap |
| Humidity | ` 55H` | 55 %RH |
| Probe | ` 64P` | 64 °C at the thermocouple |

The dwell was five seconds while there were three pages. The probe made a
fourth, and four times five is twenty seconds of waiting for the flow rate
to come round again — which is the one thing this readout must not do, so
the dwell came down instead and the whole rotation stayed put.

**The rightmost digit is a unit tag, not a value.** This is what makes the
rotation readable: the module has no decimal point, and its one piece of
punctuation is a centre colon that reads as a clock rather than a
separator, so a bare number is the whole of what four digits can say —
and three pages of bare numbers is exactly the ambiguity that got an
earlier two-page rotation removed. Spending the last digit on a letter
buys back the labelling the hardware cannot otherwise do.

That leaves three digits for the value, which is enough for almost
everything this station measures — `-40C` to ` 80C` across the DHT22's
range, and `100H` at saturation. The probe is the one exception: it reads
to 1024 °C, so anything above 999 °C shows as `999P` and the real figure
is on `PROBE_C`. Values are rounded to whole units, and anything wider
than three digits is clamped rather than wrapped: a display pinned at
`999L` is visibly pinned, whereas a wrapped number looks like a real
reading.

| Shown | Meaning |
|-------|---------|
| `  0L` | No flow |
| ` --L` | No sample yet — the first second after boot |
| ` --C` / ` --H` | The DHT22 has not answered for three reads running |
| ` --P` | Open circuit — no thermocouple, or a broken one |
| `  0P` | A real reading: the MAX6675 cannot go below 0 °C, and a dead `SO` line also lands here |

A page with no reading still shows its unit letter, so the display says
which value is missing rather than going blank.

Rounding to the whole unit is deliberate. This station trends on the MQTT
series and on the matrix beside it, not on the instant, so a precise
figure was never what the 7-segment was for.

The running total is not one of the pages. It is on MQTT as `TOTAL_L`, the
hourly series as `HOURLY_L`, and the last 12 hours on the matrix.

Page dwell time is `displayPageInterval` in the sketch; the pages
themselves are the `PAGE_*` enum and `renderDisplay()`.

Like the other stations, the display is driven straight from the sensor
and never touches the network, so the number stays live and correct while
standing over the tap even if WiFi or the broker is down.

Brightness is set in `setup()` via `display.setBrightness(2)` on the
library's 0-7 scale; raise it if the display sits in direct sun.

## LED matrix

The board's onboard 12x8 matrix shows **the last 12 hours of water use** —
one column per hour, height proportional to the litres drawn in that hour,
newest on the right. The rightmost column is the hour currently being
filled, so it grows through the hour and then shifts left when the hour
closes.

The TM1637 answers "what is flowing right now". The matrix answers "what
has this tap used today", which is the question a garden actually poses.

```
   a quiet morning, a long watering run, then a short top-up

   |....#.....#.|      each column = one hour
   |....#.....#.|      height      = litres that hour
   |....#.....#.|      rightmost   = hour in progress
   |....#.....#.|
   |....#....##.|
   |....#....##.|
   |.#..#....##.|
   |.#..#..#.##.|
    020080010480        <- heights, oldest hour on the left
```

**Any water at all lights at least one row.** An hour with a couple of
litres in it would otherwise round to zero and read as "nothing happened",
which is the one thing this display must never get wrong.

An empty matrix means twelve hours with no water — which, on a rain week,
is correct and not a fault.

### Scale

Vertical scale is fixed, not auto-ranging, so the same height always means
the same volume and two glances a day apart are comparable:

```cpp
const float matrixFullScaleLitres = 100.0f;
```

Each row is one eighth of that — **12.5 L**:

| Litres in the hour | Rows lit |
|--------------------|----------|
| 0                  | 0        |
| 0 – 18.75          | 1        |
| 18.75 – 31.25      | 2        |
| 31.25 – 43.75      | 3        |
| 43.75 – 56.25      | 4        |
| 56.25 – 68.75      | 5        |
| 68.75 – 81.25      | 6        |
| 81.25 – 93.75      | 7        |
| 93.75 and above    | 8        |

Rows 2–8 are evenly spaced, each band straddling its centre by half a row
because the height rounds rather than truncates. **Row 1 is deliberately
wider** — it covers everything from a trickle to 18.75 L, because any
water at all is floored to one row so that "a little" never looks like
"none".

The top row is open-ended: 100 L and 1000 L are the same eight pixels.
`HOURLY_L` always carries the real number.

**Trim this to what the station actually collects.** Too high and ordinary
hours sit flat along the bottom; too low and everything pins at eight rows
and the trace stops saying anything. The honest way to set it is from your
own data — leave it running through a few rain events, then put full scale
near the 90th-percentile hour you actually see in `HOURLY_L`.

For a roof rather than a tap the arithmetic is
`litres = rainfall_mm × roof_area_m² × runoff` (about 0.85 for tile or
metal after losses; 1 mm on 1 m² is exactly 1 litre). Note that at 100 L
full scale a 50 m² roof reaches the top row at roughly **2.2 mm/h** of
rainfall, so anything beyond light rain will pin it — fine if what you
want is "collecting / not collecting", worth raising if you want to see
the shape of a storm.

### Hours are since boot, not wall-clock

Nothing here is time-synced, so the buckets are rolling 3600-second
windows measured from boot. A reboot starts a fresh bucket and loses the
partial hour in progress. The `RTC` library bundled with the board package
could align these to real clock hours — see [Expansion
notes](#expansion-notes).

### Cost

Nothing, in pins or parts. The matrix is charlieplexed across D28-D38,
which are internal to the board and not broken out to the header, so it
cannot collide with the flow input or the TM1637. `Arduino_LED_Matrix` is
bundled with the board package like WiFiS3 — no Library Manager install.
Pixel work needs nothing else; `ArduinoGraphics` is only required if you
want *text* on the matrix.

`matrix.begin()` claims one free FSP timer and multiplexes the display
from a 10 kHz periodic interrupt, lighting one LED per tick (about 104 Hz
per LED). Nothing else in this sketch wants a timer.

That interrupt does **not** threaten pulse counting: its handler is a
couple of register writes, external interrupts are latched in the RA4M1's
ICU so an edge cannot be lost merely by being serviced a few microseconds
late, and the shortest real gap between pulses is 4.4 ms at the sensor's
30 L/min ceiling — three orders of magnitude of headroom.

## Over-the-air updates

The station sits at a rainwater downpipe. Reflashing it should not mean
carrying a laptop out there, so the sketch listens for OTA uploads and
appears in the IDE's port list as **FlowSenseR4**.

### One-time setup

`ArduinoOTA` is **not** bundled with the board package — install it from
the Library Manager. Then the step that catches everyone:

> Copy `extras/renesas/platform.local.txt` from the ArduinoOTA library
> next to `platform.txt` in the renesas boards package — typically
> `~/.arduino15/packages/arduino/hardware/renesas_uno/<version>/`.
> Restart the IDE.

Without it the IDE uses the wrong upload command and fails in ways that
look like a network problem. The file only defines upload tooling, so it
is not needed to *compile* — which is why CI builds this sketch fine
without it.

Unlike the RP2040 and ESP cores, the renesas package ships no bundled
`ArduinoOTA`, so there is nothing to delete first.

### Uploading from the IDE

Select **FlowSenseR4** from the IDE's network ports and upload as normal.
The IDE prompts for the password, which is `SECRET_OTA_PASS` from
`arduino_secrets.h`. **Set a real one** — the OTA port is reachable by
anything on your network.

If the network port doesn't appear, mDNS discovery on this board is
occasionally flaky. Uploading by IP address still works, and the serial
log prints the address on every connect.

### Uploading from the command line

From the repo root:

```sh
make flash-ota SKETCH=FlowSenseR4 OTA_IP=192.168.5.42
```

The address is whatever the serial log printed on the last connect. The
password is read out of this folder's gitignored `arduino_secrets.h`, so
it is never typed on the command line or stored in the Makefile.

The raw commands, if you prefer them:

```sh
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Arduino/FlowSenseR4
arduino-cli upload --port 192.168.5.42 --protocol network \
            --fqbn arduino:renesas_uno:unor4wifi \
            --upload-field password=YourOtaPassword Arduino/FlowSenseR4
```

Two commands rather than one: `compile --upload` has no `--upload-field`,
so the password can only be handed to `upload`.

> **The first flash is always over USB.** OTA only works once a sketch
> that calls `ArduinoOTA` is already running on the board — and if a bad
> upload ever takes the network down, the cable is how you recover. Keep
> physical access in mind before deploying a station somewhere awkward.

### The size ceiling

The sketch binary is limited to **half the available flash**. The library
computes it as `(MAX_FLASH - SKETCH_START_ADDRESS) / 2`: the incoming
image is buffered in the upper half of flash before being copied down over
the running sketch. On the RA4M1's 256 KB that means roughly **128 KB**.

This is the constraint to watch as the sketch grows. `make compile
SKETCH=FlowSenseR4` reports flash usage, and CI prints it on every PR —
if it approaches 128 KB, OTA stops being an option before the sketch stops
fitting on the board.

### What happens during an update

Polling is throttled to five times a second rather than run every loop
pass. `ArduinoOTA.poll()` costs two round-trips to the ESP32-S3 — a socket
check and an mDNS read — and this sketch is careful about UART traffic to
the radio for the same reason it caches `WiFi.status()` and RSSI.

Once an image is received and verified, `otaBeforeApply()` detaches the
pulse interrupt and blanks both readouts, so nothing shows a frozen number
while the flash is rewritten. Then the board resets.

**Counts do not survive the update.** Flash rewriting ends in a reset, and
the totals are since-boot anyway — the lifetime figure lives on the broker,
accumulated from `HOURLY_L`. Persisting them across reboots is in
[Expansion notes](#expansion-notes).

## Status LED

| Blink pattern | Meaning |
|---------------|---------|
| Slow (300-2000ms, scaled by signal strength) | WiFi associated |
| Fast, steady 150ms | WiFi down, retrying |
| Off / not blinking | Sketch is not running |

## Build

1. In the Arduino IDE, select **Board: Arduino UNO R4 WiFi**.
2. Install PubSubClient, TM1637, ArduinoOTA, the DHT sensor library and
   the MAX6675 library.
3. Create `arduino_secrets.h` as described in Configuration.
4. Wire the YF-S201, DHT22, MAX6675 and TM1637 per the tables above.
5. Upload `FlowSenseR4.ino`.
6. Open the Serial Monitor at 9600 baud.

### With arduino-cli

From the repo root:

```sh
make deps    SKETCH=FlowSenseR4      # core + libraries, one time
make compile SKETCH=FlowSenseR4      # build only
make flash   SKETCH=FlowSenseR4      # build and upload on /dev/ttyACM0
make flash   SKETCH=FlowSenseR4 PORT=/dev/ttyACM1
make monitor                          # 9600 baud serial monitor
```

The Makefile already knows this sketch targets
`arduino:renesas_uno:unor4wifi`.

The same by hand:

```sh
arduino-cli core update-index
arduino-cli core install arduino:renesas_uno
arduino-cli lib install "PubSubClient" "TM1637" "ArduinoOTA" \
                       "DHT sensor library" "Adafruit Unified Sensor" \
                       "MAX6675 library"

cp Arduino/FlowSenseR4/arduino_secrets.h.example Arduino/FlowSenseR4/arduino_secrets.h
$EDITOR Arduino/FlowSenseR4/arduino_secrets.h

arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Arduino/FlowSenseR4
arduino-cli compile --upload --port /dev/ttyACM0 \
            --fqbn arduino:renesas_uno:unor4wifi Arduino/FlowSenseR4
arduino-cli monitor --port /dev/ttyACM0 --config baudrate=9600
```

Only two libraries: **WiFiS3** and **Arduino_LED_Matrix** ship with the
`arduino:renesas_uno` core, so installing them from the Library Manager is
unnecessary and can shadow the bundled copies.

See the [root README](../../README.md#building-with-arduino-cli) for the
full toolchain notes.

## Why a separate sketch

This could have been `#ifdef`-ed into `Arduino/FlowSense`, but the two
builds differ in pin map, WiFi stack, pull-up strategy and display supply
voltage — enough branching to make both harder to read for no gain, and
this board is meant to grow features the Nano has no room for — the
hourly matrix above is the first of them.

The measurement core — the ISR, the rate maths and the hourly bucket — is
still byte-for-byte identical between the two sketches, and the R4 build
only adds to it (a `matrixSetCurrentHour()` call at the end of the sample
window, a `matrixRollHour()` call when an hour closes, `readClimate()` and
`readProbe()` on their own ticks, and the matrix helpers).

**The TM1637 logic is the one part that has genuinely diverged.** The Nano
build still shows a single page of flow rate; this one rotates four and
carries the unit glyphs and layout helper that go with it. There is
nothing to port back — the Nano has no climate sensor to page to — so the
two display blocks are now expected to differ rather than to match.

`diff` remains the tool for keeping the rest in step:

```sh
diff Arduino/FlowSense/FlowSense.ino Arduino/FlowSenseR4/FlowSenseR4.ino
```

Every intentional difference in the R4 sketch is marked with an `R4:`
comment.

## Expansion notes

Things this board offers that the Nano 33 IoT build cannot, roughly in
order of usefulness for a water meter. None of these are implemented yet.

- **Persistent totals.** The single biggest win. Both boards currently
  reset their total on reboot. The R4 board package bundles `EEPROM`
  (8 KB emulated in the RA4M1's data flash) and `Preferences`, so the
  running total could survive a power cut. Write it on a volume threshold
  (say every 10 L) rather than on a timer — data flash endurance is finite
  and a 10s write cycle would burn through it. The 12 hourly bars are worth
  persisting alongside it, so a reboot does not blank the day's history.
- **More from the LED matrix.** The hourly bars use it already; a
  leak-alert glyph or a live-rate view could share it as a second page.
- **A negative-capable probe.** The MAX6675 stops at 0 °C, so the one
  thing a water line most wants to alarm on — freezing — is the one thing
  it cannot see. The **MAX31855** is a drop-in on the same three pins and
  reads down to -270 °C, with explicit short-to-VCC and short-to-GND
  faults on top of the open-circuit bit. `readProbe()` is the only
  function that would change.
- **Leak detection.** Continuous non-zero flow for longer than some
  threshold is a burst pipe or a stuck valve. Wants persistent state and a
  retained MQTT alert topic.
- **A second meter on D3 or D8.** Both are interrupt-capable with no
  channel collision (see the pin table above). Supply and return lines on
  the same station would let you meter actual consumption by difference.
- **RTC, to align the hourly buckets.** Bundled. The matrix columns and
  `HOURLY_L` are currently rolling hours since boot, so "the 9am column"
  does not exist. Aligning the bucket boundary to the wall clock would
  make the matrix a real 12-hour-of-day chart and let the broker bucket
  by calendar day.
- **Command topics.** The sketch already installs an MQTT `callback()`
  that only logs. Subscribing to a `MUTHUR/DDATA/FLOW/...` topic would let
  the broker reset the total or re-trim `pulsesPerLitre` without a
  reflash. Note `client.setBufferSize(512)` in `setup()` — PubSubClient's
  256-byte default covers the whole packet including topic and header, and
  a silent publish failure is the only symptom of overflowing it, so the
  headroom is already there for a larger status payload.

## Troubleshooting

### The total climbs while no water is running

The signal line is picking up noise. The interrupt rejects any edge closer
than 1ms to the last one, well inside the sensor's own 225Hz ceiling at
30 L/min, so anything getting past that is substantial pickup. On this
board the most likely cause is relying on the weak internal pull-up over a
long run — fit the external 4.7 kΩ to 5V. Also check the sensor's ground
is tied to the board's, and that the signal run is not cable-tied
alongside a pump's mains lead.

### The rate reads zero but water is flowing

- Below roughly 1 L/min the YF-S201's turbine does not turn reliably —
  a sensor limit, not a wiring fault.
- Confirm the sensor is fitted the right way round; the arrow points
  downstream.
- Confirm the signal is on **D2**. On this board D4, D5, D7, D9, D10, D13
  and A0 cannot raise an interrupt at all, so `attachInterrupt()` on any
  of them silently never fires.

### The rate reads high

Usually plumbing rather than electronics: an elbow or valve immediately
upstream puts swirl into the flow and the turbine over-reads. Failing
that, run the calibration above.

### The probe shows ` --P`, and `PROBE_FAULT` is `1`

An open circuit, which is what the MAX6675 reports when there is no
thermocouple across its terminals. Check the screw terminals are tight on
bare wire and not on insulation, that the thermocouple's weld at the tip is
intact, and that `SO` is actually landed on **D10** — a floating `SO` line
reads as all ones, which sets the same open-circuit bit.

### The probe reads 0.00 °C and never moves

Two possibilities, and they look identical on the wire:

- `SO` is stuck low — not connected, or shorted to ground. Every bit
  clocks in as zero, which is a valid word reading exactly 0 °C rather
  than a fault.
- The probe really is at or below 0 °C. The MAX6675's output is unsigned
  and **cannot represent a negative temperature**, so anything below
  freezing reads 0 °C. See [Thermocouple probe](#thermocouple-probe).

Warm the tip in your hand: a good channel moves within a second or two.

### The probe reading falls as the tip heats up

The thermocouple is in backwards. Swap the two leads in the screw
terminals — `+` is yellow on ANSI-coded type K wire, green on IEC-coded.

### The probe reading wanders or jumps by tens of degrees

Thermocouple wire is a millivolt-level source and the MAX6675 is a
high-impedance amplifier, so this is usually pickup:

- Keep the thermocouple run away from the pump's mains lead — the same
  advice as for the flow signal, and more important here.
- Keep the MAX6675 module's ground tied to the board's, and its supply on
  the 5V rail rather than at the end of a long thin lead.
- If the probe tip is electrically bonded to something live-ish (a pump
  housing, a grounded tank), a grounded-junction probe couples that in;
  an ungrounded/insulated-junction probe fixes it.
- A reading that only wanders in the afternoon is cold-junction drift, not
  noise: the module is in the sun. See
  [Cold-junction compensation](#cold-junction-compensation).

### The port appears but nothing prints

Like the Nano 33 IoT, the R4's serial port is native USB (CDC) provided by
the running sketch, so opening the Serial Monitor does not reset the board
and anything printed beforehand is gone. Press RESET once with the monitor
already open, at **9600 baud**.

If the port vanishes entirely, double-tap RESET to enter the bootloader —
a new port appears — and upload to that.

### It prints for a while, then stops

Check the LED. A fast 150ms blink means the sketch is alive and looping
but cannot reach WiFi — check `arduino_secrets.h` and the 2.4GHz network.
The sketch never blocks indefinitely on WiFi or MQTT, so a genuinely
frozen LED points at a crash rather than a network problem.

## Notes

- The interrupt handler is deliberately tiny — one `micros()` call, a
  compare and two stores. At 30 L/min it fires 225 times a second.
- The pulse counter is never reset. Resetting it would race with the
  interrupt and silently drop whatever arrived in between, so volume is
  accumulated from the difference between successive snapshots, in
  unsigned arithmetic that stays correct across the counter's wrap.
  `unsigned long` is 32 bits on the RA4M1 just as on the SAMD21, so that
  wrap is the same ~4.29e9 pulses (~9.5 million litres) on both boards.
- Volume is held as an integer pulse count and converted to litres only at
  the point of use, so repeatedly adding small floats never erodes the
  total.
- The MAX6675 library sets its three pin modes in its **constructor**,
  which on this core runs from `__libc_init_array` before the board's
  `init()` has touched the port registers — so those calls quietly do
  nothing. `setup()` re-applies them, which is why `pinMode(MAXCLK, …)`
  and friends appear there as well as in the library. The same is true of
  [CompostHeat](../CompostHeat), which re-applies CS for the same reason.
