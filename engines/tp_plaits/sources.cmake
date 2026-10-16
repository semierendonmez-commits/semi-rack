# Mutable Instruments Plaits (MIT), see sources.mk.
file(GLOB TP_PLAITS ${REPO_ROOT}/engines/thirdparty/plaits/*.cc ${REPO_ROOT}/engines/thirdparty/plaits/dsp/*.cc ${REPO_ROOT}/engines/thirdparty/plaits/dsp/*/*.cc)
list(APPEND ENGINE_SOURCES ${TP_PLAITS})
