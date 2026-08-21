# CompostHeat

Sensor station that monitors compost pile internal temperature using a
MAX6675 K-type thermocouple amplifier, and publishes readings to the
MUTHUR MQTT broker over WiFi.

## Hardware

- Arduino Nano 33 IoT
- MAX6675 module + K-type thermocouple probe

## Wiring

The MAX6675 module is bit-banged ("software SPI") by its library, so any
digital pins work. This sketch uses:

| MAX6675 pin | Nano 33 IoT pin | Notes                         |
|-------------|-----------------|--------------------------------|
| VCC         | 3V3             | MAX6675 logic is 3.3V-5V; power it from 3V3 to match the Nano 33 IoT's 3.3V logic levels |
| GND         | GND             |                                |
| SCK         | D5              |                                |
| CS          | D6              |                                |
| SO (DO)     | D7              |                                |

Plug the K-type thermocouple probe into the MAX6675 module's screw
terminal, matching polarity (the module is marked + / -).

The onboard NINA WiFi module is not affected by this wiring: on the Nano
33 IoT it hangs off `SPI1` with its SS/reset/ack lines on internal pins
24/27/28, none of which are broken out. The broken-out hardware SPI pins
(D11/D12/D13) are also left free here for any future SPI peripheral.

## Libraries

Install via the Arduino Library Manager:

- **WiFiNINA** (Arduino) - WiFi connectivity for the Nano 33 IoT
- **PubSubClient** (Nick O'Leary) - MQTT client
- **MAX6675** (Adafruit) - thermocouple amplifier driver

## Configuration

WiFi credentials are kept out of the sketch and out of git:

1. Copy `arduino_secrets.h.example` to `arduino_secrets.h` in this same
   folder.
2. Edit `arduino_secrets.h` and fill in your real `SECRET_SSID` and
   `SECRET_PASS`.

`arduino_secrets.h` is listed in the repo's `.gitignore`, so it won't be
committed.

The MQTT broker address is set in `CompostHeat.ino`:

```cpp
const char* server = "192.168.5.110"; // MQTT server (Raspberry Pi)
```

Update this if your broker's IP changes.

## MQTT Topics

Root topic: `MUTHUR`

| Topic                        | Payload                          | Frequency |
|-------------------------------|-----------------------------------|-----------|
| `MUTHUR/NDATA/CMPST/TEMP_C`  | Compost temperature, °C (float)  | 5 min     |
| `MUTHUR/NDATA/CMPST/TEMP_F`  | Compost temperature, °F (float)  | 5 min     |
| `MUTHUR/DIAG/CMPST/FAULT`    | `1` if thermocouple open/disconnected, else `0` | 5 min |
| `MUTHUR/DIAG/CMPST/STATUS`   | JSON: `{device, rssi, uptime, thermocouple_fault}` | 30s |
| `MUTHUR/DIAG/CMPST/HB`       | Heartbeat counter                | 5 min     |

When a thermocouple fault is detected (open circuit / probe unplugged),
`TEMP_C`/`TEMP_F` are not published for that cycle, but `FAULT` and `HB`
still are, so you can alert on a stuck/faulted probe.

The thermocouple is still sampled once per second regardless; the publish
interval only controls how often the most recent reading is sent. Compost
temperature moves slowly, so 5 minutes is plenty - raise or lower
`publishInterval` in `CompostHeat.ino` to change it.

## Build

1. In the Arduino IDE, select **Board: Arduino Nano 33 IoT**.
2. Install the libraries listed above.
3. Create `arduino_secrets.h` as described in Configuration.
4. Wire the MAX6675 module per the table above.
5. Upload `CompostHeat.ino`.
6. Open the Serial Monitor at 9600 baud to confirm WiFi connects, then
   MQTT connects, then readings start printing.

On boot the sketch waits up to 5 seconds for a serial monitor to attach
before carrying on, so you get the startup banner even if you open the
monitor a moment after reset. It does not wait forever, so the board still
runs standalone on a battery or wall wart.

## Status LED

The built-in LED doubles as a link indicator, which is useful when no
serial monitor is attached:

| Blink pattern            | Meaning                                  |
|--------------------------|------------------------------------------|
| Slow (300-2000ms, scaled by signal strength) | WiFi associated |
| Fast, steady 150ms       | WiFi down, retrying                      |
| Off / not blinking       | Sketch is not running - see Troubleshooting |

## Troubleshooting

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

- The MAX6675 chip needs at least ~220-250ms between reads; this sketch
  samples once per second, well above that minimum.
- A blank/zero reading with no `FAULT` set is unusual — if you see one,
  check the thermocouple polarity and screw-terminal connection.
