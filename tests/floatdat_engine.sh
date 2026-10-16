#!/bin/sh
# Builds and runs tests/floatdat_engine.cpp (reuses the objects of tests/floater_engine.sh).
#   sh tests/floatdat_engine.sh [objdir]      (from the repo root)
set -e
O=${1:-build/test-floater}
sh tests/floater_engine.sh "$O" > /dev/null || true
F="-std=c++17 -O2 -DTEST -Iengines -Iengines/thirdparty -w"
for s in engines/floatdat/*.cpp; do
  o="$O/$(echo $s | tr / _).o"
  [ "$o" -nt "$s" ] || ccache g++ $F -c "$s" -o "$o"
done
ccache g++ $F tests/floatdat_engine.cpp $(ls "$O"/*.o) -o "$O/floatdat_engine"
"$O/floatdat_engine"
