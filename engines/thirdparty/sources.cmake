# Shared third-party DSP (Mutable Instruments stmlib + Beads, artifex).
target_include_directories(SemiFamily PRIVATE ${REPO_ROOT}/engines/thirdparty ${REPO_ROOT}/engines/thirdparty/artifex)
file(GLOB TP_STMLIB ${REPO_ROOT}/engines/thirdparty/stmlib/dsp/*.cc ${REPO_ROOT}/engines/thirdparty/stmlib/utils/*.cc)
list(APPEND ENGINE_SOURCES ${TP_STMLIB}
	${REPO_ROOT}/engines/thirdparty/beads/floater_beads.cc ${REPO_ROOT}/engines/thirdparty/beads/resources.cc
	${REPO_ROOT}/engines/thirdparty/beads/dsp/processor.cc ${REPO_ROOT}/engines/thirdparty/beads/dsp/mu_law.cc
	${REPO_ROOT}/engines/thirdparty/beads/dsp/delay_clock.cc ${REPO_ROOT}/engines/thirdparty/beads/dsp/delay_controller.cc
	${REPO_ROOT}/engines/thirdparty/beads/dsp/trigger_generator.cc)
