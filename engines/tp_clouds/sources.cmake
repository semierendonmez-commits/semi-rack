# Mutable Instruments Clouds (MIT), see sources.mk.
file(GLOB TP_CLOUDS ${REPO_ROOT}/engines/thirdparty/clouds/*.cc ${REPO_ROOT}/engines/thirdparty/clouds/dsp/*.cc ${REPO_ROOT}/engines/thirdparty/clouds/dsp/pvoc/*.cc)
list(APPEND ENGINE_SOURCES ${TP_CLOUDS})
