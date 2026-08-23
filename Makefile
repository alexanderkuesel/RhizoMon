# Wrapper around arduino-cli so the FQBN and sketch path are never retyped
# by hand - selecting the wrong board is what bricks a Nano 33 IoT.
#
#   make flash                              # CompostHeat on /dev/ttyACM0
#   make flash SKETCH=BaseStation
#   make flash PORT=/dev/ttyACM1
#   make monitor

SKETCH ?= CompostHeat
PORT   ?= /dev/ttyACM0
BAUD   ?= 9600

SKETCH_DIR := Arduino/$(SKETCH)

# Sketches targeting the UNO R4 WiFi; everything else is a Nano 33 IoT.
R4_SKETCHES := BaseStation FlowSenseR4

ifeq ($(filter $(SKETCH),$(R4_SKETCHES)),$(SKETCH))
FQBN := arduino:renesas_uno:unor4wifi
else
FQBN := arduino:samd:nano_33_iot
endif

.PHONY: compile flash monitor list

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
