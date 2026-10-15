# Shared third-party DSP (Mutable Instruments stmlib + Beads, artifex). MIT / see each folder.
FLAGS += -Iengines/thirdparty -Iengines/thirdparty/artifex
ENGINE_SOURCES += $(wildcard engines/thirdparty/stmlib/dsp/*.cc) $(wildcard engines/thirdparty/stmlib/utils/*.cc)
ENGINE_SOURCES += engines/thirdparty/beads/floater_beads.cc engines/thirdparty/beads/resources.cc
ENGINE_SOURCES += engines/thirdparty/beads/dsp/processor.cc engines/thirdparty/beads/dsp/mu_law.cc
ENGINE_SOURCES += engines/thirdparty/beads/dsp/delay_clock.cc engines/thirdparty/beads/dsp/delay_controller.cc
ENGINE_SOURCES += engines/thirdparty/beads/dsp/trigger_generator.cc
