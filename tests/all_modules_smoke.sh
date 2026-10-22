#!/bin/sh
# Builds and runs tests/all_modules_smoke.cpp against plugin.so (build the plugin first).
# Usage (repo root): sh tests/all_modules_smoke.sh <Rack-SDK> [Slug ...]
set -e
R="${1:-$RACK_DIR}"
shift || true
mkdir -p build
g++ -std=c++17 -O1 -I"$R/include" -I"$R/dep/include" tests/all_modules_smoke.cpp tests/all_modules_engine.cpp ./plugin.so -L"$R" -lRack \
	-Wl,-rpath,"$R" -Wl,-rpath,"$(pwd)" -o build/all_modules_smoke
build/all_modules_smoke "$@"
