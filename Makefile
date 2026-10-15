# SEMI Family — VCV Rack plugin
# Build: make RACK_DIR=/path/to/Rack-SDK  (dist: make dist)
RACK_DIR ?= ../..

FLAGS += -Isrc -Iengines
CFLAGS +=
CXXFLAGS += -std=c++17

SOURCES += $(wildcard src/*.cpp)
SOURCES += $(wildcard src/modules/*.cpp)

# Each engine lists its own sources (paths relative to the repo root).
ENGINE_SOURCES :=
-include $(wildcard engines/*/sources.mk)
SOURCES += $(ENGINE_SOURCES)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*) README.md

include $(RACK_DIR)/plugin.mk
