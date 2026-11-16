# Daisy Seed benchmark build (Electrosmith Daisy Seed 65MB). A separate build from the desktop CMake
# project, because it needs libDaisy and an Arm cross-compiler. Why this board: docs/HARDWARE_PLAN.md.
#
# One-time setup
#   1. Install the Arm toolchain (arm-none-eabi-gcc) and make.
#        macOS:  brew install --cask gcc-arm-embedded      (or an xPack arm-none-eabi-gcc on PATH)
#   2. Get libDaisy (with submodules) and build it once:
#        git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git ~/src/libDaisy
#        make -C ~/src/libDaisy
#
# Build (LIBDAISY_DIR defaults to ../../libDaisy, next to this repository)
#   make -C daisy LIBDAISY_DIR=~/src/libDaisy        -> daisy/build/vocal_daisy_bench.bin
#
# Flash and read the results
#   Plug the Seed in over micro-USB, hold BOOT, press and release RESET, release BOOT. Then:
#     make -C daisy LIBDAISY_DIR=~/src/libDaisy program-dfu      (needs dfu-util: brew install dfu-util)
#   Open the serial port that appears (macOS: ls /dev/tty.usbmodem*), e.g.  screen /dev/tty.usbmodem* 115200
#   The program waits for the port to be opened before it starts.

TARGET = vocal_daisy_bench

CPP_SOURCES = main_daisy.cpp ../src/demonic_engine.cpp

LIBDAISY_DIR ?= ../../libDaisy

# Same as the desktop and Pico builds: C++17, optimised.
CPP_STANDARD = -std=gnu++17
OPT = -O3

# VFX_VERSION, with the same rules as cmake/version.cmake.
VFX_VERSION := $(shell v=$$(git describe --tags --match 'v[0-9]*' --dirty 2>/dev/null) && echo $${v\#v} || \
                 (g=$$(git describe --always --dirty 2>/dev/null) && echo 0.0.0-g$$g) || echo unknown)
C_INCLUDES = -I../include
C_DEFS = -DVFX_VERSION=\"$(VFX_VERSION)\"

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
