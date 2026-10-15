# SEMI Family — VCV Rack plugin
# Build: make RACK_DIR=/path/to/Rack-SDK  (dist: make dist)
RACK_DIR ?= ../..

# No -Isrc: artifex includes <rack.hpp>, which includes <plugin.hpp>; src/plugin.hpp must not shadow it.
FLAGS += -Iengines
# stmlib: use its portable sqrt instead of ARM inline asm on desktop (the plugins' CMake does the same).
FLAGS += -DTEST
# Rack's compile.mk appends -std=c++11; EXTRA_CXXFLAGS is added after it, so C++17 wins.
EXTRA_CXXFLAGS += -std=c++17

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/modules/*.cpp)

# Each engine lists its own sources (paths relative to the repo root).
ENGINE_SOURCES :=
-include $(wildcard engines/*/sources.mk)
SOURCES += $(ENGINE_SOURCES)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*) README.md

include $(RACK_DIR)/plugin.mk
