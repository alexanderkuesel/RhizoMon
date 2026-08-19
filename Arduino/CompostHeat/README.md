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

Left unconnected: the hardware SPI pins (D11/D12/D13) are intentionally
not used, so they stay free for the onboard NINA WiFi module and any
future SPI peripherals.

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
| `MUTHUR/NDATA/CMPST/TEMP_C`  | Compost temperature, °C (float)  | 5s        |
| `MUTHUR/NDATA/CMPST/TEMP_F`  | Compost temperature, °F (float)  | 5s        |
| `MUTHUR/DIAG/CMPST/FAULT`    | `1` if thermocouple open/disconnected, else `0` | 5s |
| `MUTHUR/DIAG/CMPST/STATUS`   | JSON: `{device, rssi, uptime, thermocouple_fault}` | 30s |
| `MUTHUR/DIAG/CMPST/HB`       | Heartbeat counter                | 5s        |

When a thermocouple fault is detected (open circuit / probe unplugged),
`TEMP_C`/`TEMP_F` are not published for that cycle, but `FAULT` and `HB`
still are, so you can alert on a stuck/faulted probe.

## Build

1. In the Arduino IDE, select **Board: Arduino Nano 33 IoT**.
2. Install the libraries listed above.
3. Create `arduino_secrets.h` as described in Configuration.
4. Wire the MAX6675 module per the table above.
5. Upload `CompostHeat.ino`.
6. Open the Serial Monitor at 9600 baud to confirm WiFi connects, then
   MQTT connects, then readings start printing.

## Notes

- The MAX6675 chip needs at least ~220-250ms between reads; this sketch
  samples once per second, well above that minimum.
- A blank/zero reading with no `FAULT` set is unusual — if you see one,
  check the thermocouple polarity and screw-terminal connection.
