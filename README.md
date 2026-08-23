# Intro
Named after the MU-TH-UR 6000 AI mainframe on the Nostromo (Alien), this repo contains my smart home / smart garden / eco command center project files. This is an attempt to bring forward self-contained eco-positive spores across the globe, degrowth enabled by technology.

## Building with arduino-cli

Every sketch lives in `Arduino/<Name>/` and builds with
[`arduino-cli`](https://arduino.github.io/arduino-cli/latest/installation/).
The `Makefile` wraps it so the FQBN and sketch path are never retyped by
hand — picking the wrong board is what bricks a Nano 33 IoT.

### Boards and dependencies

| Sketch | Board | Core | Libraries (Library Manager) |
|--------|-------|------|------------------------------|
| `CompostHeat` | Nano 33 IoT | `arduino:samd` | `WiFiNINA`, `PubSubClient`, `MAX6675 library`, `TM1637` |
| `FermentationWard` | Nano 33 IoT | `arduino:samd` | `WiFiNINA`, `PubSubClient`, `DHT sensor library`, `Adafruit Unified Sensor`, `TM1637` |
| `FlowSense` | Nano 33 IoT | `arduino:samd` | `WiFiNINA`, `PubSubClient`, `TM1637` |
| `FlowSenseR4` | UNO R4 WiFi | `arduino:renesas_uno` | `PubSubClient`, `TM1637` |
| `BaseStation` | UNO R4 WiFi | `arduino:renesas_uno` | `ArduinoMqttClient`, `ArduinoBLE` |

`WiFiS3` and `Arduino_LED_Matrix` are **not** in that column on purpose:
they ship with the `arduino:renesas_uno` board package, so installing them
from the Library Manager is unnecessary and can shadow the bundled copy.

### One-time setup

```sh
# install the core and libraries for one sketch
make deps SKETCH=FlowSenseR4

# or everything for every sketch, on a fresh machine
make deps-all
```

The equivalent by hand, if you would rather not use the Makefile:

```sh
arduino-cli core update-index

# cores - install whichever board(s) you have
arduino-cli core install arduino:samd           # Nano 33 IoT
arduino-cli core install arduino:renesas_uno    # UNO R4 WiFi

# libraries - quote the names that contain spaces
arduino-cli lib install "WiFiNINA" "PubSubClient" "TM1637"                         "MAX6675 library"                         "DHT sensor library" "Adafruit Unified Sensor"                         "ArduinoMqttClient" "ArduinoBLE"
```

### Credentials

Sketches that connect to WiFi read `arduino_secrets.h`, which is
gitignored. Create it before the first build or the compile fails on the
missing include:

```sh
cd Arduino/FlowSenseR4
cp arduino_secrets.h.example arduino_secrets.h
$EDITOR arduino_secrets.h        # fill in SECRET_SSID / SECRET_PASS
```

### Build, flash, monitor

```sh
make compile SKETCH=FlowSenseR4          # build only
make flash   SKETCH=FlowSenseR4          # build and upload to /dev/ttyACM0
make flash   SKETCH=FlowSenseR4 PORT=/dev/ttyACM1
make monitor PORT=/dev/ttyACM1           # serial monitor, 9600 baud
make list                                # what is plugged in, and on which port
make fqbn    SKETCH=FlowSenseR4          # print board/core/libs without building
```

`SKETCH` defaults to `CompostHeat`. `make flash` compiles and uploads in
one step, so a stale binary from an earlier build can never reach the
board.

The raw commands, if you prefer them:

```sh
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi Arduino/FlowSenseR4
arduino-cli compile --upload --port /dev/ttyACM0             --fqbn arduino:renesas_uno:unor4wifi Arduino/FlowSenseR4
arduino-cli monitor --port /dev/ttyACM0 --config baudrate=9600
```

The two FQBNs are `arduino:samd:nano_33_iot` and
`arduino:renesas_uno:unor4wifi`.

### Notes

- On Linux, if the port shows up but is not writable, add yourself to the
  `dialout` group: `sudo usermod -a -G dialout $USER`, then log out and
  back in.
- The library names above are the same strings CI feeds to
  `arduino/compile-sketches` in
  `.github/workflows/compile-sketches.yml`. If you add a library to a
  sketch, update the workflow, the `Makefile`'s `LIBS_*` list and the
  table above together, or it will build locally and fail in CI.

## Smart Garden

## Eco Command Center
1. 


### LoraWAN
https://docs.arduino.cc/learn/communication/lorawan-101/
https://docs.arduino.cc/arduino-cloud/hardware/lora/ 
