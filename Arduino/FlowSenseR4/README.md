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
| `MUTHUR/DIAG/FLOW/STATUS`    | JSON: `{device, rssi, uptime, rate_lpm, total_l, pulses}` | 30s |
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

Both totals are **since boot** — see [Expansion notes](#expansion-notes),
this is the one the R4 can actually fix.

## Display

Identical to the Nano build. Two pages alternate: the flow rate for 4
seconds, then the running total for 3. The colon tells them apart — the
rate page always shows it, the total page (below 9999 L) never does.

| Shown   | Page  | Meaning |
|---------|-------|---------|
| `07:50` | Rate  | 7.50 L/min. The module's single centre colon sits exactly halfway across the four digits, so `XX:XX` is the only decimal split it can punctuate — read the colon as the decimal point |
| `00:00` | Rate  | No flow. Leading zeros are kept because the colon form needs all four digits |
| ` 342`  | Total | 342 litres since boot. No colon, no leading zeros |
| `12:34` | Total | 12.34 kL = 12,340 L. Past 9999 L the total page switches to kilolitres and borrows the colon again. Resolution drops to 10 L and it pins at `99:99` (99,990 L) |
| `----`  | Both  | The first second after boot, before the first sample window closed |

The page timer ticks every 250ms so a flip lands promptly, but the display
is only written when its contents change. It is driven straight from the
sensor and never touches the network, so the numbers stay live while
standing over the tap even if WiFi or the broker is down.

## LED matrix

The board's onboard 12x8 matrix shows a **rolling sparkline of the last 12
seconds of flow** — one column per one-second sample window, newest on the
right, so the trace scrolls leftwards as time passes.

The TM1637 already gives the exact instantaneous rate, so the matrix earns
its place by showing *shape over time* instead: whether a watering run is
ramping, holding steady, tapering off or pulsing. A blank matrix means no
flow.

```
   ramping up, holding at full, then dropping away

   |.........##.|      column height = flow rate
   |.........##.|      newest sample --^
   |........###.|
   |........###.|
   |.......####.|
   |.......#####|
   |......######|
   |.....#######|
```

**Any flow at all lights at least one row.** A trickle that would otherwise
round to zero is floored to one pixel, because "barely flowing" and
"stopped" are the one pair the sparkline must never confuse.

### Scale

Vertical scale is fixed, not auto-ranging, so the same height always means
the same rate and two glances a minute apart are comparable:

```cpp
const float matrixFullScaleLpm = 30.0f;
```

The default is the YF-S201's 30 L/min ceiling. **Trim it to your own
typical flow for more vertical resolution** — against 30, a 7.5 L/min
garden hose only ever lights two of the eight rows. Setting it to `10.0f`
would give that same hose six rows.

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

Or from the repo root, with `arduino-cli` installed:

```sh
make flash SKETCH=FlowSenseR4        # compile and upload on /dev/ttyACM0
make monitor                          # 9600 baud serial monitor
```

The Makefile already knows this sketch targets `arduino:renesas_uno:unor4wifi`.

## Why a separate sketch

This could have been `#ifdef`-ed into `Arduino/FlowSense`, but the two
builds differ in pin map, WiFi stack, pull-up strategy and display supply
voltage — enough branching to make both harder to read for no gain, and
this board is meant to grow features the Nano has no room for — the
sparkline above is the first of them.

The measurement core — the ISR, the rate maths and the TM1637 logic — is
still byte-for-byte identical between the two sketches. The R4 build adds
to it (one `sparklinePush()` call at the end of the sample window, and the
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
  and a 10s write cycle would burn through it.
- **More from the LED matrix.** The sparkline uses it already; a
  leak-alert glyph or a totals view could share it as a second page.
- **Leak detection.** Continuous non-zero flow for longer than some
  threshold is a burst pipe or a stuck valve. Wants persistent state and a
  retained MQTT alert topic.
- **A second meter on D3 or D8.** Both are interrupt-capable with no
  channel collision (see the pin table above). Supply and return lines on
  the same station would let you meter actual consumption by difference.
- **RTC.** Bundled, so readings could carry a wall-clock timestamp
  instead of an uptime counter — useful for per-day consumption totals.
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
