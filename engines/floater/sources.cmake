# FLOATER engine (see sources.mk for the changes).
foreach(f Voices.cpp Fx.cpp Engine.cpp Patch.cpp Schwung.cpp Reel.cpp Drone.cpp FloaterBeads.cc Case.cpp Presets.cpp)
	list(APPEND ENGINE_SOURCES ${REPO_ROOT}/engines/floater/${f})
endforeach()
