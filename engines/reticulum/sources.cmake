# RETICULUM engine, vendored unchanged from semierendonmez-commits/reticulum Source/engine.
list(APPEND ENGINE_SOURCES ${REPO_ROOT}/engines/reticulum/Engine.cpp)
# artifex includes <rack.hpp> -> <plugin.hpp>, which resolves to src/plugin.hpp: include it first.
set_source_files_properties(${REPO_ROOT}/engines/reticulum/Engine.cpp PROPERTIES
	COMPILE_OPTIONS "-include;${REPO_ROOT}/src/plugin.hpp")
