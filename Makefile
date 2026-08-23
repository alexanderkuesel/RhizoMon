# Wrapper around arduino-cli so the FQBN and sketch path are never retyped
# by hand - selecting the wrong board is what bricks a Nano 33 IoT.
#
#   make deps                               # install core + libraries for CompostHeat
#   make deps SKETCH=FlowSenseR4            # ... for a different sketch
#   make deps-all                           # both cores + every library, fresh machine
#   make compile SKETCH=FlowSense           # build without uploading
#   make flash                              # CompostHeat on /dev/ttyACM0
#   make flash SKETCH=BaseStation
#   make flash PORT=/dev/ttyACM1
#   make monitor
#   make fqbn                               # print what would be built, and with what

SKETCH ?= CompostHeat
PORT   ?= /dev/ttyACM0
BAUD   ?= 9600

SKETCH_DIR := Arduino/$(SKETCH)

# Sketches targeting the UNO R4 WiFi; everything else is a Nano 33 IoT.
R4_SKETCHES := BaseStation FlowSenseR4

ifeq ($(filter $(SKETCH),$(R4_SKETCHES)),$(SKETCH))
FQBN     := arduino:renesas_uno:unor4wifi
PLATFORM := arduino:renesas_uno
else
FQBN     := arduino:samd:nano_33_iot
PLATFORM := arduino:samd
endif

# Library Manager names, per sketch. These are the exact strings CI feeds to
# arduino/compile-sketches in .github/workflows/compile-sketches.yml - keep
# the two in step, or a sketch builds on one and not the other.
#
# Quoted because several names contain spaces: make passes the quotes
# through to the shell verbatim, which is what keeps them one argument each.
#
# Not listed, because they ship with the board package rather than the
# Library Manager: WiFiS3 and Arduino_LED_Matrix (renesas_uno).
LIBS_CompostHeat      := "WiFiNINA" "PubSubClient" "MAX6675 library" "TM1637"
LIBS_FermentationWard := "WiFiNINA" "PubSubClient" "DHT sensor library" "Adafruit Unified Sensor" "TM1637"
LIBS_FlowSense        := "WiFiNINA" "PubSubClient" "TM1637"
LIBS_FlowSenseR4      := "PubSubClient" "TM1637"
LIBS_BaseStation      := "ArduinoMqttClient" "ArduinoBLE"

LIBS := $(LIBS_$(SKETCH))

.PHONY: deps deps-all compile flash monitor list fqbn

# One-time setup for whichever sketch you are about to build.
deps:
	@test -d "$(SKETCH_DIR)" || { echo "No such sketch: $(SKETCH) (looked in $(SKETCH_DIR))"; exit 1; }
	arduino-cli core update-index
	arduino-cli core install $(PLATFORM)
	arduino-cli lib install $(LIBS)

# Everything for every sketch. Repeated libraries are a no-op, so the
# duplicates across the lists below cost nothing.
deps-all:
	arduino-cli core update-index
	arduino-cli core install arduino:samd
	arduino-cli core install arduino:renesas_uno
	arduino-cli lib install $(LIBS_CompostHeat) $(LIBS_FermentationWard) \
	                        $(LIBS_FlowSense) $(LIBS_FlowSenseR4) $(LIBS_BaseStation)

compile:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH_DIR)

# --upload compiles and uploads in one step, so a stale cached binary from
# an earlier build can never reach the board.
flash:
	arduino-cli compile --upload --port $(PORT) --fqbn $(FQBN) $(SKETCH_DIR)

monitor:
	arduino-cli monitor --port $(PORT) --config baudrate=$(BAUD)

list:
	arduino-cli board list

# Sanity check before flashing something at the wrong board.
fqbn:
	@echo "sketch   $(SKETCH_DIR)"
	@echo "fqbn     $(FQBN)"
	@echo "platform $(PLATFORM)"
	@echo "libs     $(LIBS)"
