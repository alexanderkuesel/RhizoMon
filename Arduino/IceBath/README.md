# IceBath

UNO R4 WiFi ice-bath monitor: a MAX6675 K-type thermocouple in the water,
a DHT22 for the air above it, a TM1637 4-digit display beside the tub, the
onboard LED matrix as a 12-hour temperature trend, and readings published
to the MUTHUR MQTT broker.

This started as a copy of [FlowSenseR4](../FlowSenseR4) with the flow
meter taken out. The connectivity, display, matrix and OTA scaffolding is
carried over rather than rewritten, deliberately — see
[What changed from FlowSenseR4](#what-changed-from-flowsenser4) — and the
pin map is arranged so a board already running FlowSenseR4 needs **no
rewiring**, only three added wires.

## Hardware

- Arduino UNO R4 WiFi
- MAX6675 breakout board + K-type thermocouple, **stainless-sheathed and
  sealed** — see [The probe](#the-probe)
- DHT22 / AM2302 temperature and humidity sensor
- TM1637 4-digit 7-segment display module

The board's onboard 12x8 LED matrix is used too, and costs no extra parts
and no header pins — see [LED matrix](#led-matrix).

Read [Accuracy near zero](#accuracy-near-zero) before trusting a number
off this station. A K-type thermocouple is not a precision thermometer at
bath temperatures, and this README does not pretend otherwise.

## Wiring

### Coming from FlowSenseR4

Everything that board already has stays exactly where it is:

| Wire | Pin | Status |
|------|-----|--------|
| TM1637 CLK | D4 | unchanged |
| TM1637 DIO | D7 | unchanged |
| DHT22 DATA | D5 | unchanged |
| 5V / GND rails | — | unchanged, shared by all three modules |
| YF-S201 signal | D2 | **leave it or pull it, either works** |
| MAX6675 SCK | D6 | new |
| MAX6675 CS | D9 | new |
| MAX6675 SO | D10 | new |

**D2 is never configured by this sketch** — not as an input, not as an
output. So the flow sensor's yellow wire can stay landed on it doing
nothing, and a pin the sketch never drives is a pin that wire cannot
fight. Unplug the meter if you want the tidiness; the sketch does not
care either way.

That makes this three added signal wires, plus the MAX6675's VCC and GND
onto the rails the DHT22 and display are already on.

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
levels line up with the header pins exactly. No pull-ups, no resistors, no
level shifting.

It is read-only SPI — there is no MOSI — and the library bit-bangs all
three lines, which is why they sit on ordinary GPIOs rather than on the
board's SPI pins.

The thermocouple goes into the module's screw terminals, and **polarity
matters**: `+` takes the yellow lead on ANSI-coded type K wire, or green
on IEC-coded wire; `-` takes red (ANSI) or white (IEC). Wired backwards it
still reads, but the number *falls* as the probe warms.

### DHT22

| DHT22 pin | UNO R4 WiFi pin |
|-----------|-----------------|
| 1 VCC     | **5V**          |
| 2 DATA    | **D5**          |
| 3 —       | *not connected* |
| 4 GND     | GND             |

The DATA line is open-drain and **needs a 10 kΩ pull-up to VCC**. Three-pin
AM2302 breakout boards (`+` / `OUT` / `-`) have one fitted; a bare 4-pin
DHT22 does not.

Mount it above the water, not in the splash. The part is not sealed, and a
wet element reads 100% humidity for hours — which beside a tub of water is
an easy mistake to make.

### TM1637 display

| TM1637 pin | UNO R4 WiFi pin | Notes |
|------------|-----------------|-------|
| VCC        | **5V**          | The TM1637's native supply; noticeably brighter than at 3.3V |
| GND        | GND             | |
| CLK        | D4              | |
| DIO        | D7              | |

Keep the sensor off the **Qwiic connector** — that is a 3.3V I2C bus and,
unlike the header pins, it is *not* 5V tolerant.

### Why these three pins

The pin choice is inherited from FlowSenseR4, whose
[pin table](../FlowSenseR4/README.md#why-d2-and-why-the-display-moved)
reads the interrupt channels straight out of the core's variant files. In
short:

- **D9 and D10** raise no interrupt at all, so they cost one PWM channel
  each and nothing else. They go first.
- **D6** is the cheapest pin left for the third line: its interrupt
  channel (IRQ4) is shared with D11, so spending D6 leaves IRQ4 still
  reachable there and loses no channel at all.
- **D3, D8 and A2** — the three pins with unshared interrupt channels —
  stay free, as does **A0**, the board's only DAC. Nothing in this sketch
  uses an interrupt, so those are spare for a lid switch, a chiller relay
  or a second probe.
- **D11, D12 and D13** stay free. D10 is the hardware-SPI chip select, but
  nothing here uses the SPI peripheral and a chip select is only ever a
  plain GPIO, so a real SPI device can still be added later with its CS on
  any spare pin.

`BaseStation.ino` uses D5 for a switch and D7 for a relay — the DHT22 and
a display pin here. Separate boards today, but collisions to watch if you
ever consolidate onto one R4.

### Power and the tub

Nothing in this build is mains-powered: the board runs off a 5 V USB
supply, and the only thing in the water is a sheathed thermocouple whose
leads carry microvolts. **Keep it that way.** Put the board and its supply
somewhere they cannot be knocked in, and route the probe lead so nobody
sits on it getting in.

## The probe

Use a **stainless-steel-sheathed, sealed** K-type probe — the kind sold
for sous-vide or kilns, with a crimped or welded tip. A bare bead junction
in water corrodes within days and then reads erratically rather than
failing outright, which is the worst way for a sensor to die.

Prefer an **ungrounded (insulated) junction** if you have the choice. A
grounded-junction probe ties the thermocouple to the sheath, and the
sheath to whatever the water is touching, which couples any pump or
chiller noise straight into a microvolt-level signal.

Where it sits matters more than the sensor does. Water stratifies hard in
a still tub — several degrees between the surface and the bottom is normal
— so clip the probe **mid-depth, away from the wall**, and keep it in the
same place between sessions if you want readings that compare. Half an
hour after the ice goes in, the top and bottom of an unstirred bath are
not the same bath.

Keep the MAX6675 breakout itself out of the tub's microclimate, for a
reason that is not obvious: see
[Cold-junction compensation](#cold-junction-compensation).

## Accuracy near zero

| Property | Figure |
|----------|--------|
| Range | **0 to +1024 °C** |
| Resolution | 0.25 °C (12-bit) |
| Amplifier accuracy | ±8 LSB, so about **±2 °C** from 0 to +700 °C |
| Thermocouple tolerance | another **±1.5 °C** for class-1 type K wire |
| Read cadence | every 1 s (`probeInterval`), published every 10 s |

Be clear-eyed about what that means for a bath: **the absolute number is
good to a couple of degrees at best, on a range that is only a few degrees
wide.** A K-type thermocouple is the wrong instrument for 0–10 °C water,
and it is what this station uses because the part was already in the
drawer next to [CompostHeat](../CompostHeat)'s.

What the channel is nonetheless good at is *change*. The offset is stable
and mostly constant, so "it has come up 3 degrees since I put the ice in"
is trustworthy even when "it is 4.25 °C" is not. The hourly means and the
matrix trend are built on exactly that, and are the numbers to believe.

**It cannot read below 0 °C at all.** The MAX6675's output word is
unsigned, so a bath that genuinely goes sub-zero — salt, or a chiller —
reads 0 °C and stays there. The only way this sketch ever reports negative
water is via a negative `probeOffsetC`.

If you want real precision at these temperatures:

- A **DS18B20** is ±0.5 °C, reads negatives, is sealed and waterproof in
  its usual probe form, and costs less than the MAX6675 module. It is the
  right sensor for this job.
- A **MAX31855** is a drop-in on the same three pins and the same three
  wires, reads down to -270 °C, and adds explicit short-to-VCC and
  short-to-GND faults. `readProbe()` is the only function that changes.

Both are in [Expansion notes](#expansion-notes).

### Cold-junction compensation

The MAX6675 compensates using its **own die temperature**, so the chip is
one half of every measurement. That has a specific consequence here: the
breakout must sit in stable, ordinary room air. Tape it to the tub, leave
it in a draught off the water, or let it catch afternoon sun, and the
readings move because the *amplifier* moved, not the bath.

Inside the enclosure with the board, away from the water, is right.

### Checking the probe against the bath

This station has a free reference point, which is the nice thing about
measuring ice water: **a well-stirred ice-and-water slurry is 0.00 °C**, to
within a hundredth of a degree, regardless of how much ice is in it.

1. Fill a jug with crushed ice, top up with cold water, stir for a minute.
2. Put the probe in mid-slurry, not touching the jug, and stir gently.
3. Read `WATER_C` (or the display's `C` page) once it settles — about ten
   seconds.

Whatever it reads is this channel's offset at 0 °C. Put the negative of it
in the sketch:

```cpp
const float probeOffsetC = 0.0f;   // e.g. -1.75 if the ice slurry read 1.75
```

Left at zero it changes nothing, which is the default. The correction is
applied once in `readProbe()`, so the display, `WATER_C`, the hourly means
and the extremes all carry the same corrected number.

Two caveats. A slurry reading of exactly `0.00` does not prove the channel
is right — the part cannot go below zero, so an offset that reads *low* is
invisible at this point. Check a second point if that matters: water at a
rolling boil is 100 °C at sea level, about 0.3 °C lower per 100 m of
altitude. And an offset measured at 0 °C is only strictly an offset at
0 °C; over a bath's range that is a fine assumption, over a compost pile's
it would not be.

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
board package — do not install either separately.

This is the same library set as [FlowSenseR4](../FlowSenseR4) plus the
MAX6675 driver, so a machine set up for that sketch needs one more
install.

## Configuration

WiFi credentials are kept out of the sketch and out of git:

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` in this same
   folder.
2. Edit it and fill in your real `SECRET_SSID`, `SECRET_PASS` and
   `SECRET_OTA_PASS` (the over-the-air upload password).

`arduino_secrets.h` is listed in the repo's `.gitignore`, so it won't be
committed.

The MQTT broker address is set in `IceBath.ino`:

```cpp
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)
```

## MQTT Topics

Its own station prefix, `ICE`, so nothing here collides with the `FLOW`
topics a FlowSenseR4 board is already publishing. The two can run side by
side, or one board can move between the two sketches, without a dashboard
having to guess which it is looking at.

| Topic | Payload | Frequency |
|-------|---------|-----------|
| `MUTHUR/NDATA/ICE/WATER_C` | Water temperature, °C (float, 2dp) | 10s |
| `MUTHUR/NDATA/ICE/HOURLY_MEAN_C` | Mean water temperature over the hour that just closed, °C (float, 2dp) | 1h |
| `MUTHUR/NDATA/ICE/TEMP_C` | Air temperature above the bath, °C (float, 1dp) | 10s |
| `MUTHUR/NDATA/ICE/HUMIDITY_PCT` | Relative humidity above the bath, % (float, 1dp) | 10s |
| `MUTHUR/DIAG/ICE/PROBE_FAULT` | `1` when there is no current water reading, `0` when there is | 10s |
| `MUTHUR/DIAG/ICE/STATUS` | JSON: `{device, rssi, uptime, water_c, water_min_c, water_max_c, hour_mean_c, hour_samples, last_hour_mean_c, probe_fails, temp_c, humidity_pct, climate_fails}` | 30s |
| `MUTHUR/DIAG/ICE/HB` | Heartbeat counter | 10s |

`TEMP_C` and `HUMIDITY_PCT` keep FlowSenseR4's names, so the same
dashboard panel works against either station's prefix.

`WATER_C` is the instantaneous reading — the same number the display is
showing, to the MAX6675's own 0.25 °C step. It is published **only when
the read succeeds**: after three consecutive failures the sketch stops
publishing it rather than repeating a stale value, and `PROBE_FAULT` goes
to `1` so the gap is attributable. A dashboard can tell a dead probe from
a dead broker without guessing.

`HOURLY_MEAN_C` is **the series to trend on**, and unlike FlowSenseR4's
hourly litres it is a genuine average: every reading the probe produced in
that hour, summed and divided at the boundary — up to 3600 of them. An
hour with no successful read publishes nothing at all rather than a
fabricated point, and says so on the serial line.

These are rolling hours since boot, not wall-clock hours — nothing here is
time-synced, so "the last hour" means the last 3600 seconds of uptime. A
reboot starts a fresh bucket and loses the partial hour in progress. The
one in flight is visible meanwhile as `hour_mean_c` in the diagnostics,
with `hour_samples` saying how many readings are behind it.

In the status JSON, five fields can be `null`, and each null means
something different:

| Field | `null` means |
|-------|--------------|
| `water_c` | the probe is not answering right now |
| `water_min_c`, `water_max_c` | it has never answered since boot |
| `hour_mean_c` | nothing has landed in the hour currently open |
| `last_hour_mean_c` | no hour has closed with any readings in it yet — including the first hour after boot |

`water_min_c` and `water_max_c` are the coldest and warmest the water has
been **since boot**, which is the pair a session is actually judged on:
how cold it got, and how far it has drifted back since.

`device` in the status JSON reads `Arduino UNO R4 WiFi`, the same as
FlowSenseR4 — the topic prefix is what tells the two apart.

## Display

The TM1637 rotates through three pages, **five seconds each**, so the full
cycle is fifteen seconds:

| Page | Shown | Meaning |
|------|-------|---------|
| Water | `  4C` | 4 °C in the bath |
| Air | ` 21A` | 21 °C above it |
| Humidity | ` 55H` | 55 %RH |

**The rightmost digit is a subject tag, not a value.** The module has no
decimal point, and its one piece of punctuation is a centre colon that
reads as a clock rather than a separator, so a bare number is the whole of
what four digits can say. With two of the three pages carrying a
temperature, the tag labels *what* is being measured rather than the unit:
the water — the reason the station exists — keeps the plain degree tag,
and the air is the one that gets spelled out.

Both glyphs are unmistakable for a digit, which is why they are these two:
`A` has no bottom segment and `C` has no right-hand pair, whereas a `b` or
a `U` would differ from `6` and `0` by a single segment and misread at a
glance.

| Shown | Meaning |
|-------|---------|
| ` --C` | No water reading — the first second after boot, or three failed reads running |
| `  0C` | A real reading: the MAX6675 floors at 0 °C, and a dead `SO` line lands here too |
| ` --A` / ` --H` | The DHT22 has not answered for three reads running |

A page with no reading still shows its tag, so the display says which
value is missing rather than going blank.

Values are rounded to whole degrees. That is coarse for a bath whose
interesting range is five degrees wide, and it is still the right call: the
module cannot place a decimal point, and a bare `35` that might be 3.5 °C
is worse than a rounded `4`. `WATER_C` carries the hundredths.

Page dwell is `displayPageInterval`; the pages themselves are the `PAGE_*`
enum and `renderDisplay()`. Brightness is `display.setBrightness(2)` in
`setup()`, on the library's 0-7 scale.

Like the other stations, the display is driven straight from the sensor and
never touches the network, so the number stays live and correct while you
stand over the tub deciding whether to get in, even if WiFi or the broker
is down.

## LED matrix

The onboard 12x8 matrix shows **the last 12 hours of water temperature** —
one column per hour, height proportional to that hour's mean, newest on the
right. The rightmost column is the hour currently being filled, so it
tracks the running mean and then shifts left when the hour closes.

The TM1637 answers "how cold is it right now". The matrix answers "how has
it held today", which is the question a bath that is warming up actually
poses.

```
   three warm hours, ice in at hour 4, then slow recovery

   |###.........|      each column = one hour
   |###.........|      height      = that hour's mean temperature
   |###.........|      rightmost   = hour in progress
   |###.........|
   |###.......##|
   |###.....####|
   |###...######|
   |############|      <- bottom row lit wherever there were readings
```

### Scale

The vertical scale is a fixed temperature window, not auto-ranging, so the
same height always means the same temperature and two glances a day apart
are comparable:

```cpp
const float matrixScaleMinC = 0.0f;
const float matrixScaleMaxC = 24.0f;
```

Eight rows across that span is **3 °C per row**:

| Hour's mean | Rows lit |
|-------------|----------|
| no reading that hour | 0 (column blank) |
| 0 – 4.5 °C | 1 |
| 4.5 – 7.5 °C | 2 |
| 7.5 – 10.5 °C | 3 |
| 10.5 – 13.5 °C | 4 |
| 13.5 – 16.5 °C | 5 |
| 16.5 – 19.5 °C | 6 |
| 19.5 – 22.5 °C | 7 |
| 22.5 °C and above | 8 |

**Zero rows is reserved for "no readings in that hour".** Anything in
range lights at least one row, which is why the bottom band is wider than
the rest: a bath sitting at the bottom of the scale is exactly what this
display exists to show, and a blank column there would read as a dead
sensor instead.

`0` to `24 °C` covers an ice bath from fresh ice to
left-out-and-warmed-to-room-temperature, which is a sensible default and a
coarse one. **Narrow it to sharpen the trace:** `0` to `8` makes each row
half a degree, which is what you want once the bath is actually in use and
you care about the difference between 2 °C and 6 °C.

### Hours are since boot, not wall-clock

Nothing here is time-synced, so the buckets are rolling 3600-second
windows measured from boot. A reboot starts a fresh bucket and loses the
partial hour in progress. The `RTC` library bundled with the board package
could align these to real clock hours — see
[Expansion notes](#expansion-notes).

### Cost

Nothing, in pins or parts. The matrix is charlieplexed across D28-D38,
which are internal to the board and not broken out, so it cannot collide
with the probe or the display. `matrix.begin()` claims one free FSP timer
and multiplexes the display from a 10 kHz periodic interrupt; nothing else
in this sketch wants a timer.

## Over-the-air updates

The sketch listens for OTA uploads and appears in the IDE's port list as
**IceBath**. The mechanics, the one-time
`platform.local.txt` step that catches everyone, and the ~128 KB size
ceiling are all identical to FlowSenseR4 — see
[its README](../FlowSenseR4/README.md#over-the-air-updates) rather than a
second copy here.

From the repo root:

```sh
make flash-ota SKETCH=IceBath OTA_IP=192.168.5.43
```

The address is whatever the serial log printed on the last connect. The
password is read out of this folder's gitignored `arduino_secrets.h`.

Once an image is received and verified, `otaBeforeApply()` blanks the
display and the matrix, so nothing shows a frozen number beside the tub
while the flash is rewritten. Then the board resets. **The since-boot
extremes and the hour in progress do not survive the update** — see
[Expansion notes](#expansion-notes).

> **The first flash is always over USB.** OTA only works once a sketch
> that calls `ArduinoOTA` is already running on the board.

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
3. Create `arduino_secrets.h` as described in
   [Configuration](#configuration).
4. Wire the MAX6675, DHT22 and TM1637 per the tables above.
5. Upload `IceBath.ino`.
6. Open the Serial Monitor at 9600 baud.

### With arduino-cli

From the repo root:

```sh
make deps    SKETCH=IceBath      # core + libraries, one time
make compile SKETCH=IceBath      # build only
make flash   SKETCH=IceBath      # build and upload on /dev/ttyACM0
make flash   SKETCH=IceBath PORT=/dev/ttyACM1
make monitor                      # 9600 baud serial monitor
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

cp Arduino/IceBath/arduino_secrets.h.example Arduino/IceBath/arduino_secrets.h
$EDITOR Arduino/IceBath/arduino_secrets.h

arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Arduino/IceBath
arduino-cli compile --upload --port /dev/ttyACM0 \
            --fqbn arduino:renesas_uno:unor4wifi Arduino/IceBath
arduino-cli monitor --port /dev/ttyACM0 --config baudrate=9600
```

See the [root README](../../README.md#building-with-arduino-cli) for the
full toolchain notes.

## What changed from FlowSenseR4

This is a copy, not a fork with ambitions — but be realistic about how
much of it is still shared. `diff` is the tool for keeping the two in
step, and today it reports about 50 hunks of code, three quarters of the
lines unchanged:

```sh
diff Arduino/FlowSenseR4/FlowSenseR4.ino Arduino/IceBath/IceBath.ino
```

| Function | State |
|----------|-------|
| `callback`, `wifiUp`, `pollLink`, `readClimate` | byte-for-byte identical |
| `displayValueWithUnit`, `matrixRender`, `matrixRollHour` | byte-for-byte identical |
| `maintainWiFi`, `maintainMqtt` | identical but for the station's own name |
| `renderDisplay`, `matrixBarHeight`, `matrixSetCurrentHour` | same shape, degrees instead of litres |
| `otaBeforeApply`, `setup` | shorter — no interrupt to attach or detach |
| `loop` | the sample window is gone, the hourly bucket averages, the publish and diagnostics blocks are rewritten |
| `pulseISR`, `totalLitres`, `hourLitres` | gone |
| `readProbe`, `hourMeanC` | new |

So a fix to the connectivity or display-layout code still ports across
unchanged; a fix to the measurement half does not, and is not meant to.

Two sets of markers survive in the sketch. The inherited **`R4:`** notes
mark what differs from the Nano 33 IoT [FlowSense](../FlowSense) build —
they are still true and still worth reading. Everything that differs from
FlowSenseR4 is marked **`Ice:`**.

What went:

- The YF-S201, and with it `FLOWPIN`, the pulse ISR, `pulsesPerLitre`, the
  debounce floor, the pulse counters, the one-second sample window and the
  rate maths. **There is no interrupt anywhere in this sketch**, which is
  why the DHT22's interrupt-masked read costs nothing here — the pulse it
  could merge on FlowSenseR4 does not exist.
- `RATE_LPM`, `TOTAL_L`, `PULSES` and `HOURLY_L`, and the flow-rate
  display page.

What arrived:

- The MAX6675 on D6/D9/D10, `readProbe()`, `probeOffsetC`, and
  `WATER_C` / `PROBE_FAULT`.
- Hourly **means** instead of hourly totals. A volume can be differenced
  from a counter at the bucket's two ends; a temperature cannot, so the
  bucket genuinely accumulates — a running sum and a sample count, divided
  at the point of use.
- Since-boot min and max water temperature.
- A matrix scaled in degrees rather than litres, with `0` rows meaning
  "no readings" rather than "no water".
- Subject tags on the display pages (`C` water, `A` air) in place of
  FlowSenseR4's unit tags.

## Expansion notes

None of these are implemented.

- **A sensor that suits the job.** A **DS18B20** (±0.5 °C, sealed,
  negatives, one-wire) is the right thermometer for 0–10 °C water, and a
  **MAX31855** is a drop-in on these same three pins if you would rather
  keep the thermocouple. See [Accuracy near zero](#accuracy-near-zero).
  Running one alongside the MAX6675 for a week would also measure this
  channel's real-world error rather than trusting the datasheet.
- **Persistent extremes.** `water_min_c` and `water_max_c` reset on
  reboot, as does the hour in progress. The board package bundles `EEPROM`
  (8 KB emulated in the RA4M1's data flash) and `Preferences`, so both
  could survive a power cut. Write on a change threshold rather than a
  timer — data flash endurance is finite.
- **Session detection.** A bath is used in sessions, not hours: a sharp
  drop when the ice goes in, a plateau, a slow recovery. Detecting the
  drop and publishing a session's start, minimum and duration would say
  more than any fixed-window average.
- **RTC, to align the hourly buckets.** Bundled. The matrix columns and
  `HOURLY_MEAN_C` are rolling hours since boot, so "the 7am column" does
  not exist.
- **A chiller or pump relay.** D3, D8 and A2 are free and interrupt-capable;
  any spare pin drives a relay module. Holding a setpoint from this probe
  would want the offset trim done first, and a wide hysteresis given the
  ±2 °C.
- **Command topics.** The sketch already installs an MQTT `callback()`
  that only logs. Subscribing to `MUTHUR/DDATA/ICE/...` would let the
  broker reset the extremes or re-trim `probeOffsetC` without a reflash.
  `client.setBufferSize(512)` in `setup()` already buys the headroom.

## Troubleshooting

### The water page shows ` --C`, and `PROBE_FAULT` is `1`

An open circuit, which is what the MAX6675 reports when there is no
thermocouple across its terminals. Check the screw terminals are tight on
bare wire and not on insulation, that the probe's tip is intact, and that
`SO` is actually landed on **D10** — a floating `SO` line reads as all
ones, which sets the same open-circuit bit.

### It reads 0.00 °C and never moves

Two possibilities, and in an ice bath they look identical:

- `SO` is stuck low — not connected, or shorted to ground. Every bit
  clocks in as zero, which is a *valid* word reading exactly 0 °C rather
  than a fault, so `PROBE_FAULT` stays at `0` and nothing looks wrong.
- The water really is at or below 0 °C. The MAX6675 cannot represent a
  negative temperature, so a salted or chilled bath reads 0 °C and sits
  there.

Lift the probe out and hold the tip in your hand: a live channel moves
within a second or two. If it does not, it is the wiring.

### The reading falls as the probe warms

The thermocouple is in backwards. Swap the two leads in the screw
terminals — `+` is yellow on ANSI-coded type K wire, green on IEC-coded.

### The reading wanders or jumps by tens of degrees

Thermocouple wire is a millivolt-level source and the MAX6675 is a
high-impedance amplifier, so this is usually pickup:

- A **grounded-junction** probe in a tub with a pump or chiller couples
  that gear's noise straight in. An ungrounded/insulated probe fixes it.
- Keep the probe lead away from mains leads, and the module's ground tied
  to the board's.
- A wander that tracks the time of day is cold-junction drift, not noise:
  the breakout is somewhere with unstable air. See
  [Cold-junction compensation](#cold-junction-compensation).

### The water reads warmer than the bath feels

Probably true rather than wrong. Water stratifies, and a probe near the
surface of an unstirred tub reads several degrees above the bottom. Stir
the bath and watch the number close the gap — if it does, the sensor is
fine and the bath was not mixed. Failing that, run the
[ice-slurry check](#checking-the-probe-against-the-bath).

### The hourly series has gaps

An hour in which the probe never answered publishes nothing rather than a
made-up point, by design. `probe_fails` in the diagnostics JSON counts the
consecutive failures, and `hour_samples` says how many readings are behind
the hour currently open.

### The port appears but nothing prints

The R4's serial port is native USB (CDC) provided by the running sketch,
so opening the Serial Monitor does not reset the board and anything printed
beforehand is gone. Press RESET once with the monitor already open, at
**9600 baud**.

If the port vanishes entirely, double-tap RESET to enter the bootloader —
a new port appears — and upload to that.

### It prints for a while, then stops

Check the LED. A fast 150ms blink means the sketch is alive and looping
but cannot reach WiFi — check `arduino_secrets.h` and the 2.4GHz network.
The sketch never blocks indefinitely on WiFi or MQTT, so a genuinely
frozen LED points at a crash rather than a network problem.

## Notes

- The MAX6675 read is cheap: 16 bits of bit-banging at 10 µs a half-cycle,
  about **0.35 ms of CPU**, with interrupts enabled throughout. The 1 s
  cadence comes from the part's ~220 ms conversion time, not from any cost
  to the sketch — and reading ten times faster than the publish interval is
  what makes the hourly mean worth anything.
- The MAX6675 library sets its three pin modes in its **constructor**,
  which on this core runs from `__libc_init_array` before the board's
  `init()` has touched the port registers — so those calls quietly do
  nothing. `setup()` re-applies them, which is why `pinMode(MAXCLK, …)`
  and friends appear there as well as in the library.
  [CompostHeat](../CompostHeat) re-applies CS for the same reason.
- The probe's ~500 ms power-up settling needs no `delay()`: the first read
  is a full `probeInterval` away, which covers it.
- `probeOffsetC` is applied once, in `readProbe()`, so the display, the
  published reading, the hourly mean and the extremes can never disagree
  about what the correction was.
