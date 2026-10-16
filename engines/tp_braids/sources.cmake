# Mutable Instruments Braids (MIT), see sources.mk.
file(GLOB TP_BRAIDS ${REPO_ROOT}/engines/thirdparty/braids/*.cc)
list(APPEND ENGINE_SOURCES ${TP_BRAIDS})
