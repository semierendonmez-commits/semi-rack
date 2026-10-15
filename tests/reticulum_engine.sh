#!/bin/sh
# Builds and runs tests/reticulum_engine.cpp. Usage: sh tests/reticulum_engine.sh <Rack-SDK dir>
# (run from the repo root). Not part of the plugin build.
set -e
R="${1:-$RACK_DIR}"
mkdir -p build
g++ -std=c++17 -O2 -DTEST -Iengines -Iengines/thirdparty -Iengines/thirdparty/artifex \
	-I"$R/include" -I"$R/dep/include" -include src/plugin.hpp \
	tests/reticulum_engine.cpp engines/reticulum/Engine.cpp \
	engines/thirdparty/beads/floater_beads.cc engines/thirdparty/beads/resources.cc \
	engines/thirdparty/beads/dsp/processor.cc engines/thirdparty/beads/dsp/mu_law.cc \
	engines/thirdparty/beads/dsp/delay_clock.cc engines/thirdparty/beads/dsp/delay_controller.cc \
	engines/thirdparty/beads/dsp/trigger_generator.cc \
	engines/thirdparty/stmlib/dsp/*.cc engines/thirdparty/stmlib/utils/*.cc \
	-L"$R" -lRack -Wl,-rpath,"$R" -o build/reticulum_test
build/reticulum_test
