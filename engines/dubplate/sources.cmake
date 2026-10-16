# DUBPLATE arrangement core (JUCE-free port, namespace dubplate; see sources.mk).
list(APPEND ENGINE_SOURCES
	${REPO_ROOT}/engines/dubplate/Roles.cpp ${REPO_ROOT}/engines/dubplate/Arrangement.cpp
	${REPO_ROOT}/engines/dubplate/GenreTemplates.cpp ${REPO_ROOT}/engines/dubplate/PatternGenerator.cpp
	${REPO_ROOT}/engines/dubplate/Sequencer.cpp)
