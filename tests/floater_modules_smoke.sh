#!/bin/sh
# Builds and runs tests/floater_modules_smoke.cpp against plugin.so (build it first). Usage: sh tests/floater_modules_smoke.sh <Rack-SDK>
set -e
R="${1:-$RACK_DIR}"
mkdir -p build
g++ -std=c++17 -O1 -I"$R/include" -I"$R/dep/include" tests/floater_modules_smoke.cpp ./plugin.so -L"$R" -lRack \
	-Wl,-rpath,"$R" -Wl,-rpath,"$(pwd)" -o build/floater_modules_smoke
build/floater_modules_smoke
