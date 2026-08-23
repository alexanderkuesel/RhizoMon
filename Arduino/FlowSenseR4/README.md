# FlowSenseR4

UNO R4 WiFi build of the [FlowSense](../FlowSense) water meter: a YF-S201
Hall-effect flow sensor, a TM1637 4-digit display at the tap, and
readings published to the MUTHUR MQTT broker.

Functionally identical to the Nano 33 IoT build — same measurement, same
display behaviour, same topics — but on a board with room to grow. See
[Why a separate sketch](#why-a-separate-sketch) and
[Expansion notes](#expansion-notes).

## Hardware

- Arduino UNO R4 WiFi
- YF-S201 water flow sensor (1/2" BSP, 1-30 L/min)
- TM1637 4-digit 7-segment display module

The board's onboard 12x8 LED matrix is used too, and costs no extra parts
and no header pins — see [LED matrix](#led-matrix).

No level shifter, no divider, no soldering.

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
- **D3 (IRQ0) and D8 (IRQ9) are deliberately left free.** They are the
  only two interrupt-capable pins whose channel collides with nothing, so
  they are the obvious home for a second flow meter later.

Note that `BaseStation.ino` uses D5 for a switch and D7 for a relay. Those
are separate boards today, but if you ever consolidate onto one R4, D7 is
the collision to watch.

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

#### On a rainwater downpipe

Metering roof catchment rather than a tap changes what the sensor is good
for. Three things to know before trusting the numbers:

- **It saturates in heavy rain.** The turbine tops out at 30 L/min
  (1800 L/h). On a 50 m² roof that is reached at about **42 mm/h** of
  rainfall; on 60 m² at 35 mm/h. Costa Rican convective storms exceed
  that. Above the ceiling the meter under-reads and the excess backs up —
  and since the meter is itself a restriction in the line, an overwhelmed
  one will make the collection point overflow. Fit a bypass or overflow
  above the meter so a storm spills rather than floods, and treat the
  hours that pin row 8 as "at least this much".
- **It misses drizzle.** Below roughly 1 L/min the turbine does not turn
  reliably — about **1.4 mm/h** on a 50 m² roof. Light rain contributes
  nothing to the total.
- **Calibrate it in place.** The 450 pulses/litre figure assumes
  pressurised flow. A gravity feed with only a metre or two of head spins
  the turbine differently, so the constant will be off until you run the
  [calibration](#calibration) with the meter mounted where it will live —
  a bucket under the outlet and a before/after read of `PULSES`.

For context, Heredia averages roughly 2,965 mm a year, so a 50 m² roof at
0.85 runoff sheds on the order of **126,000 L annually**, with about
19,000 L in October alone. The lifetime pulse counter wraps at ~9.5
million litres, so that is decades away — but note the total is still
*since boot*, so lifetime accounting belongs on the broker, accumulated
from `HOURLY_L`.

## Libraries

Install via the Arduino Library Manager:

- **PubSubClient** (Nick O'Leary) — MQTT client
- **TM1637** (Avishay Orpaz) — 4-digit display driver

**WiFiS3** and **Arduino_LED_Matrix** are both bundled with the UNO R4
board package — do not install either separately. The flow sensor needs no
library; it is a bare pulse train, counted by an interrupt in the sketch.

## Configuration

WiFi credentials are kept out of the sketch and out of git:

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` in this same
   folder.
2. Edit `arduino_secrets.h` and fill in your real `SECRET_SSID` and
   `SECRET_PASS`.

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

## MQTT Topics

**Same topics as the Nano 33 IoT build**, so this board is a drop-in
replacement and existing dashboards keep working:

| Topic                        | Payload                          | Frequency |
|-------------------------------|-----------------------------------|-----------|
| `MUTHUR/NDATA/FLOW/RATE_LPM` | Current flow rate, L/min (float, 2dp) | 10s  |
| `MUTHUR/NDATA/FLOW/TOTAL_L`  | Cumulative volume since boot, litres (float, 3dp) | 10s |
| `MUTHUR/NDATA/FLOW/PULSES`   | Cumulative raw pulse count since boot | 10s |
| `MUTHUR/NDATA/FLOW/HOURLY_L` | Litres drawn in the hour that just closed (float, 3dp) | 1h |
| `MUTHUR/DIAG/FLOW/STATUS`    | JSON: `{device, rssi, uptime, rate_lpm, total_l, hour_l, last_hour_l, pulses}` | 30s |
| `MUTHUR/DIAG/FLOW/HB`        | Heartbeat counter                | 10s       |

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

The TM1637 shows one thing: **the current flow rate in whole litres per
minute**, right-aligned, refreshed once a second when a sample window
closes.

| Shown  | Meaning |
|--------|---------|
| `   8` | 8 L/min |
| `  12` | 12 L/min |
| `   0` | No flow |
| `----` | The first second after boot, before the first sample window closed |

**No colon.** The module's only punctuation is a single centre colon, and
an earlier version lit it as a stand-in decimal point — `07:50` for
7.50 L/min. It reads as a clock, not a decimal, so it is gone. The
hardware cannot place a decimal point where one belongs, so the rate is
shown as a plain whole number instead of being dressed up with a separator
that misleads.

Rounding to the litre is deliberate. This station trends on the hourly
series, not the instant — and the matrix beside it shows the shape of the
day — so a precise instantaneous figure was never what the 7-segment was
for.

> **The running total is no longer on this display.** The colon was the
> only thing distinguishing the rate page from the total page; with it
> gone, `8` alternating with `342` is ambiguous in a way the colon at
> least was not. Rather than reintroduce a separator, the display now does
> one job. The total is still on MQTT as `TOTAL_L`, the hourly series as
> `HOURLY_L`, and the last 12 hours on the matrix.

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
const float matrixFullScaleLitres = 800.0f;
```

Each row is one eighth of that, so with the default:

| Litres in the hour | Rows lit |
|--------------------|----------|
| 0                  | 0        |
| 0 – 150            | 1        |
| 150 – 250          | 2        |
| 250 – 350          | 3        |
| 350 – 450          | 4        |
| 450 – 550          | 5        |
| 550 – 650          | 6        |
| 650 – 750          | 7        |
| 750 and above      | 8        |

Rows 2-8 are evenly spaced, 100 L apart, each band straddling its centre
by half a row because the height rounds rather than truncates. **Row 1 is
deliberately wider** — it covers everything from a trickle to 150 L,
because any water at all is floored to one row so that "a little" never
looks like "none".

The top row is open-ended: 800 L and 1500 L are the same eight pixels.
`HOURLY_L` always carries the real number.

#### Sizing it for a roof

A roof delivers

```
litres = rainfall_mm x roof_area_m2 x runoff
```

with runoff about **0.85** for tile or metal after first-flush and wetting
losses (1 mm of rain on 1 m² is exactly 1 litre). So set full scale to
whatever a strong-but-not-freak rain hour gives you:

| Roof (plan area) | 20 mm/h downpour | Suggested full scale |
|------------------|------------------|-----------------------|
| 20 m²            | 340 L            | 400 L                 |
| 30 m²            | 510 L            | 500 L                 |
| **50 m²**        | **850 L**        | **800 L** (the default) |
| 80 m²            | 1360 L           | 1500 L                |
| 120 m²           | 2040 L           | 1800 L (sensor-capped) |

**Never set it above 1800.** That is 30 L/min, the YF-S201's own ceiling,
so no hour can physically report more and the extra rows could never
light.

The default suits a ~50 m² catchment, where the eight rows span roughly
0 to 18 mm/h of rainfall.

#### Better: size it from your own data

The sketch publishes `HOURLY_L` from the first hour, so the accurate route
is to leave it running through a few storms and set full scale near the
90th-percentile rain hour you actually see. Estimated roof areas and
runoff coefficients are both rough; measured hours are not.

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

## Status LED

| Blink pattern | Meaning |
|---------------|---------|
| Slow (300-2000ms, scaled by signal strength) | WiFi associated |
| Fast, steady 150ms | WiFi down, retrying |
| Off / not blinking | Sketch is not running |

## Build

1. In the Arduino IDE, select **Board: Arduino UNO R4 WiFi**.
2. Install PubSubClient and TM1637.
3. Create `arduino_secrets.h` as described in Configuration.
4. Wire the YF-S201 and TM1637 per the tables above.
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
arduino-cli lib install "PubSubClient" "TM1637"

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

The measurement core — the ISR, the rate maths, the hourly bucket and the
TM1637 logic — is still byte-for-byte identical between the two sketches.
The R4 build adds to it (a `matrixSetCurrentHour()` call at the end of the
sample window, a `matrixRollHour()` call when an hour closes, and the
matrix helpers) but changes none of it, so `diff` remains the tool for
keeping the two in step:

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
