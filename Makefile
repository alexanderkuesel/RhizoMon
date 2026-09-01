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
#   make flash-ota SKETCH=FlowSenseR4 OTA_IP=192.168.5.42   # upload over WiFi

SKETCH ?= CompostHeat
PORT   ?= /dev/ttyACM0
BAUD   ?= 9600

# Address of the board for an over-the-air upload. No default: there is no
# sensible guess, and guessing wrong means uploading to someone else's
# board.
OTA_IP ?=

# Read the OTA password out of the sketch's own gitignored
# arduino_secrets.h rather than keeping a second copy here. Assigned with
# ?= so the file is only read when an OTA target actually runs.
OTA_PASS ?= $(shell sed -n 's/^[[:space:]]*#define[[:space:]]\{1,\}SECRET_OTA_PASS[[:space:]]\{1,\}"\(.*\)".*/\1/p' $(SKETCH_DIR)/arduino_secrets.h 2>/dev/null)

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
LIBS_FlowSenseR4      := "PubSubClient" "TM1637" "ArduinoOTA" \
                         "DHT sensor library" "Adafruit Unified Sensor"
LIBS_BaseStation      := "ArduinoMqttClient" "ArduinoBLE"

LIBS := $(LIBS_$(SKETCH))

.PHONY: deps deps-all compile flash flash-ota monitor list fqbn

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

# Upload over WiFi instead of USB. The board must already be running a
# sketch that calls ArduinoOTA - the very first flash is always over the
# cable, and so is any recovery if a bad sketch takes the network down.
#
# Two commands rather than one: 'compile --upload' has no --upload-field,
# so the password can only be passed to 'upload'.
#
# Note the password lands in this process's command line, so it is visible
# to 'ps' for the moment the upload runs.
flash-ota:
	@test -n "$(OTA_IP)" || { echo "Set OTA_IP, e.g. make flash-ota SKETCH=FlowSenseR4 OTA_IP=192.168.5.42"; exit 1; }
	@test -n "$(OTA_PASS)" || { echo "No SECRET_OTA_PASS in $(SKETCH_DIR)/arduino_secrets.h"; exit 1; }
	arduino-cli compile --fqbn $(FQBN) $(SKETCH_DIR)
	arduino-cli upload --port $(OTA_IP) --protocol network --fqbn $(FQBN) \
	            --upload-field password=$(OTA_PASS) $(SKETCH_DIR)

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
