#!/bin/sh
# Builds and runs tests/floater_engine.cpp against the FLOATER engine and the shared Mutable libraries.
#   sh tests/floater_engine.sh [objdir]      (from the repo root)
set -e
O=${1:-build/test-floater}
mkdir -p "$O"
CXX="ccache g++"; CC="ccache gcc"
F="-O2 -DTEST -Iengines -Iengines/thirdparty -w"
objs=""
for s in $(ls engines/thirdparty/stmlib/dsp/*.cc engines/thirdparty/stmlib/utils/*.cc \
  engines/thirdparty/beads/resources.cc engines/thirdparty/beads/dsp/processor.cc engines/thirdparty/beads/dsp/mu_law.cc \
  engines/thirdparty/beads/dsp/delay_clock.cc engines/thirdparty/beads/dsp/delay_controller.cc engines/thirdparty/beads/dsp/trigger_generator.cc \
  engines/thirdparty/plaits/*.cc engines/thirdparty/plaits/dsp/*.cc engines/thirdparty/plaits/dsp/*/*.cc \
  engines/thirdparty/braids/*.cc engines/thirdparty/clouds/*.cc engines/thirdparty/clouds/dsp/*.cc engines/thirdparty/clouds/dsp/pvoc/*.cc \
  engines/floater/*.cpp engines/floater/*.cc); do
  o="$O/$(echo $s | tr / _).o"; objs="$objs $o"
  [ "$o" -nt "$s" ] || $CXX -std=c++17 $F -c "$s" -o "$o" &
  while [ $(jobs -p | wc -l) -ge 2 ]; do sleep 0.2; done
done
for s in engines/thirdparty/parasites/floater_parasites.cc engines/thirdparty/parasites/floater_parasites_resources.cc; do
  o="$O/$(echo $s | tr / _).o"; objs="$objs $o"
  [ "$o" -nt "$s" ] || $CXX -std=c++17 $F -iquote engines/thirdparty/parasites -c "$s" -o "$o"
done
o="$O/cloudseed.o"; objs="$objs $o"
[ "$o" -nt engines/thirdparty/cloudseed/floater_cloudseed.c ] || $CC -O2 -w -c engines/thirdparty/cloudseed/floater_cloudseed.c -o "$o"
wait
$CXX -std=c++17 -O2 -DTEST -Iengines -Iengines/thirdparty tests/floater_engine.cpp $objs -o "$O/floater_engine"
"$O/floater_engine"
