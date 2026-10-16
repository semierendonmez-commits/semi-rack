# FLOATDAT engines (see sources.mk).
foreach(f IkdEngine.cpp Vtx.cpp Data.cpp Instrument.cpp)
	list(APPEND ENGINE_SOURCES ${REPO_ROOT}/engines/floatdat/${f})
endforeach()
