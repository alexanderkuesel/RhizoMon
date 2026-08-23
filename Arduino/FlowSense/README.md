# FlowSense

Sensor station that measures irrigation water flow with a YF-S201
Hall-effect flow sensor, shows live rate and cumulative volume on a
TM1637 4-digit display at the tap, and publishes both to the MUTHUR MQTT
broker over WiFi.

## Hardware

- Arduino Nano 33 IoT
- YF-S201 water flow sensor (1/2" BSP, 1-30 L/min)
- TM1637 4-digit 7-segment display module
- Level shifting for the sensor's signal line - either a logic level
  converter module or two resistors (see Wiring)

## Wiring

### YF-S201

**The YF-S201 is a 5V sensor and the Nano 33 IoT is not 5V tolerant.**
Its Hall output idles at its own supply rail, so wiring the yellow signal
wire straight to a Nano 33 IoT pin puts 5V into a 3.3V input. Do not do
it. Either of the two options below solves it.

The sensor has three wires:

| YF-S201 wire | Goes to                                              |
|--------------|-------------------------------------------------------|
| Red          | 5V (see the VUSB note below)                          |
| Black        | GND (shared with the Nano)                            |
| Yellow       | Signal - through a level shifter or divider to **D4** |

**Option A - logic level converter (preferred).** Feed the module's HV
side from 5V and its LV side from 3V3, put the sensor's yellow wire on an
HV channel and that channel's LV pin on D4. The BSS138-style modules
already carry pull-ups on both sides, so nothing else is needed.

**Option B - pull-up plus resistor divider.** Two resistors and one more:

```
5V ----[10k]----+
                |
yellow ---------+----[10k]----+----> D4
                              |
                            [20k]
                              |
                             GND
```

The first 10k is the pull-up the open-collector Hall output needs; the
10k/20k pair then divides its 5V high down to 5 x 20/(10+20) = 3.3V.

Either way the sketch sets the pin as plain `INPUT`, not `INPUT_PULLUP` -
the external network already defines both levels, and the SAMD21's
internal ~40k pull-up to 3V3 would only lift the low level towards the
input threshold.

**The VUSB note.** The Nano 33 IoT's VUSB pin is **not connected by
default** - there is a solder jumper on the underside of the board
labelled `VUSB` that has to be bridged before that pin carries the 5V
coming in over USB. If you would rather not solder, power the sensor from
a separate 5V supply and tie its ground to the Nano's GND.

D4 is PA07 on the SAMD21 and owns its own EXTINT channel, so the
pin-change interrupt this sketch attaches does not collide with anything
else on the board.

### TM1637 display

Same wiring as the other sketches in this repo, so a display can be moved
between stations without rewiring. It is bit-banged over two lines, so any
digital pins work:

| TM1637 pin | Nano 33 IoT pin | Notes                              |
|------------|-----------------|-------------------------------------|
| VCC        | 3V3             | Do **not** feed it 5V from VUSB - the Nano 33 IoT's pins are 3.3V and are not 5V tolerant. TM1637 modules run happily at 3.3V, just a little dimmer |
| GND        | GND             |                                     |
| CLK        | D2              |                                     |
| DIO        | D3              |                                     |

The onboard NINA WiFi module is not affected by this wiring: on the Nano
33 IoT it hangs off `SPI1` with its SS/reset/ack lines on internal pins
24/27/28, none of which are broken out. The broken-out hardware SPI pins
(D11/D12/D13) are left free for any future SPI peripheral.

### Plumbing

The YF-S201 body is marked with a flow-direction arrow - fit it pointing
downstream. It is not rated for potable water or for continuous pressure
above 1.75 MPa, and its published accuracy (±10%) is a turbine spec, not a
laboratory one. Mount it with a straight run of pipe either side if you
can; an elbow immediately upstream puts swirl into the flow and the
turbine reads high.

## Libraries

Install via the Arduino Library Manager:

- **WiFiNINA** (Arduino) - WiFi connectivity for the Nano 33 IoT
- **PubSubClient** (Nick O'Leary) - MQTT client
- **TM1637** (Avishay Orpaz) - 4-digit display driver

The flow sensor needs no library - it is a bare pulse train, counted by an
interrupt in the sketch.

## Configuration

WiFi credentials are kept out of the sketch and out of git:

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` in this same
   folder.
2. Edit `arduino_secrets.h` and fill in your real `SECRET_SSID` and
   `SECRET_PASS`.

`arduino_secrets.h` is listed in the repo's `.gitignore`, so it won't be
committed.

The MQTT broker address is set in `FlowSense.ino`:

```cpp
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)
```

Update this if your broker's IP changes.

## Calibration

The YF-S201's published characteristic is `F = 7.5 * Q` - pulse frequency
in Hz against flow in L/min - which works out to **450 pulses per litre**.
That is the sketch's default:

```cpp
const float pulsesPerLitre = 450.0f;
```

Individual sensors land a few percent either side of it, and the error is
a straight scale factor, so one measured run trims it out:

1. Note the `PULSES` value on MQTT (or in the serial log).
2. Run a known volume through the meter into a bucket or a calibrated jug
   - 10 L or more, the bigger the better.
3. Note `PULSES` again.
4. `pulsesPerLitre = (pulses after - pulses before) / litres collected`.

Put the result in `FlowSense.ino` and re-upload. Both the rate and the
total are derived from this one constant, so they stay consistent.

## MQTT Topics

Root topic: `MUTHUR`

| Topic                        | Payload                          | Frequency |
|-------------------------------|-----------------------------------|-----------|
| `MUTHUR/NDATA/FLOW/RATE_LPM` | Current flow rate, L/min (float, 2dp) | 10s  |
| `MUTHUR/NDATA/FLOW/TOTAL_L`  | Cumulative volume since boot, litres (float, 3dp) | 10s |
| `MUTHUR/NDATA/FLOW/PULSES`   | Cumulative raw pulse count since boot | 10s |
| `MUTHUR/DIAG/FLOW/STATUS`    | JSON: `{device, rssi, uptime, rate_lpm, total_l, pulses}` | 30s |
| `MUTHUR/DIAG/FLOW/HB`        | Heartbeat counter                | 10s       |

`RATE_LPM` is the instantaneous rate over the last one-second sample
window - the same number the display is showing at that moment - so a
10-second publish interval samples the rate rather than integrating it.
**`TOTAL_L` is the authoritative volume.** It is accumulated from every
pulse the interrupt sees, in integer pulses, and converted to litres only
when it is published, so nothing is lost between publishes and no rounding
error builds up over a season.

`PULSES` is published alongside it so the totals can be re-derived against
a corrected `pulsesPerLitre` after calibration, without re-running the
water.

Both totals are **since boot**. The sketch keeps no persistent storage;
if you want a lifetime total, accumulate the deltas broker-side, where a
reboot shows up as the counter going backwards.

## Display

The TM1637 alternates between two pages: the flow rate for 4 seconds,
then the running total for 3. The colon tells them apart - the rate page
always shows it, the total page (below 9999 L) never does.

The page timer ticks every 250ms so a flip lands promptly, but the display
is only actually written when its contents change - when a sample window
closes or a page flips - rather than four times a second regardless.

| Shown     | Page  | Meaning                                              |
|-----------|-------|-------------------------------------------------------|
| `07:50`   | Rate  | 7.50 L/min. The module has a single centre colon instead of per-digit decimal points, and it sits exactly halfway across the four digits, so `XX:XX` is the only decimal split it can punctuate - read the colon as the decimal point |
| `00:00`   | Rate  | No flow. Leading zeros are kept because the colon form needs all four digits |
| ` 342`    | Total | 342 litres since boot. No colon, no leading zeros     |
| `12:34`   | Total | 12.34 kL = 12,340 L. Past 9999 L there is no room for whole litres, so the total page switches to kilolitres and borrows the colon as the decimal point again. Resolution drops to 10 L and the display pins at `99:99` (99,990 L) - by then read the total off MQTT |
| `----`    | Both  | The first second after boot, before the first sample window has closed |

Like the other stations, the display is driven straight from the sensor
and never touches the network, so the numbers stay live and correct while
standing over the tap even if WiFi or the broker is down.

Brightness is set in `setup()` via `display.setBrightness(2)` on the
library's 0-7 scale; raise it if the display sits in direct sun.

## Status LED

The built-in LED doubles as a link indicator, which is useful when no
serial monitor is attached:

| Blink pattern            | Meaning                                  |
|--------------------------|------------------------------------------|
| Slow (300-2000ms, scaled by signal strength) | WiFi associated |
| Fast, steady 150ms       | WiFi down, retrying                      |
| Off / not blinking       | Sketch is not running - see Troubleshooting |

## Build

1. In the Arduino IDE, select **Board: Arduino Nano 33 IoT**.
2. Install the libraries listed above.
3. Create `arduino_secrets.h` as described in Configuration.
4. Wire the YF-S201 (through a shifter or divider) and the TM1637 per the
   tables above.
5. Upload `FlowSense.ino`.
6. Open the Serial Monitor at 9600 baud to confirm WiFi connects, then
   MQTT connects, then readings start printing.

Or from the repo root, with `arduino-cli` installed:

```sh
make flash SKETCH=FlowSense          # compile and upload on /dev/ttyACM0
make monitor                          # 9600 baud serial monitor
```

On boot the sketch waits up to 5 seconds for a serial monitor to attach
before carrying on, so you get the startup banner even if you open the
monitor a moment after reset. It does not wait forever, so the board still
runs standalone on a battery or wall wart.

## Troubleshooting

### The total climbs while no water is running

The signal line is picking up noise. The interrupt already rejects any
edge closer than 1ms to the last one, which is well inside the sensor's
own 225Hz ceiling at 30 L/min, so anything getting past that is
substantial pickup rather than a stray spike. Check that the pull-up is
actually fitted (a floating open-collector output oscillates), that the
sensor's ground is tied to the Nano's, and that the signal run is not
cable-tied alongside a pump's mains lead.

### The rate reads zero but water is flowing

- Below roughly 1 L/min the YF-S201's turbine does not turn reliably -
  that is a sensor limit, not a wiring fault.
- Confirm the sensor is fitted the right way round; the arrow on the body
  points downstream.
- Check the divider maths if you rolled your own resistor values. The low
  level at D4 has to sit under 0.99V (0.3 x 3.3V) and the high over 2.31V
  (0.7 x 3.3V).
- If you are running the sensor from VUSB, confirm the `VUSB` solder
  jumper on the underside of the board is actually bridged - unbridged,
  that pin is dead and the sensor never powers up.

### The rate reads high

Usually plumbing rather than electronics: an elbow or a valve immediately
upstream puts swirl into the flow and the turbine over-reads. Failing
that, run the calibration above - a consistent scale error is exactly what
`pulsesPerLitre` is for.

### The board's port is not listed at all

The Nano 33 IoT's serial port is native USB (CDC) provided by the running
sketch, not by a separate USB-serial chip. That means the port only exists
while the sketch is alive. If the sketch hard-faults or wedges the CPU,
the USB stack stops answering the host and the port disappears from the
port list entirely.

You cannot fix this by uploading new code, because there is no port to
upload to. Recover the board first:

1. With the board plugged in, **press the RESET button twice, quickly**
   (a "double tap"). The orange LED should start pulsing/fading smoothly.
2. The board is now in its bootloader. A **new port appears** in the IDE's
   port list - it is usually a different port number than the sketch's.
   Select it.
3. Upload a known-good sketch (File > Examples > 01.Basics > BareMinimum,
   or this sketch) to that bootloader port.
4. After the upload completes, the sketch's own port returns.

If the double tap does not produce a pulsing LED, work through the
host-side causes before suspecting the board:

- **Cable.** Many USB cables are charge-only and carry no data lines. Try a
  different cable that you know has worked for data.
- **Hub.** Plug directly into the computer rather than through a hub or
  dock.
- **Board selection.** Confirm **Arduino Nano 33 IoT** is selected, not
  Nano or Nano Every. Uploading an AVR binary to the SAMD21 will not run
  and can leave the port missing.
- **Linux permissions.** If the port exists but is not selectable, add
  yourself to the `dialout` group: `sudo usermod -a -G dialout $USER`,
  then log out and back in.

### The port appears but nothing prints

Opening the Serial Monitor does **not** reset a Nano 33 IoT the way it
resets an UNO, so anything printed before you opened the monitor is gone.
Press RESET once with the monitor already open. Also confirm the monitor
is set to **9600 baud** to match `Serial.begin(9600)`.

### It prints for a while, then stops

Check the LED. A fast 150ms blink means the sketch is alive and looping
but cannot reach WiFi - check `arduino_secrets.h` and the 2.4GHz network.
The sketch never blocks indefinitely on WiFi or MQTT, so a genuinely
frozen LED points at a crash rather than a network problem.

## Notes

- The interrupt handler is deliberately tiny - one `micros()` call, a
  compare and two stores. At 30 L/min it fires 225 times a second, often
  while the network code is mid-SPI-transaction with the NINA module.
- The pulse counter is never reset. Resetting it would race with the
  interrupt and silently drop whatever arrived in between, so volume is
  accumulated from the difference between successive snapshots instead,
  in unsigned arithmetic that stays correct across the counter's wrap
  (~4.29e9 pulses, about 9.5 million litres).
- Volume is held as an integer pulse count and converted to litres only at
  the point of use, so repeatedly adding small floats never erodes the
  total.
