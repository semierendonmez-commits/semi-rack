# NMS engine (JUCE-free, namespace nms; see sources.mk for the changes).
list(APPEND ENGINE_SOURCES
	${REPO_ROOT}/engines/nms/Common.cpp ${REPO_ROOT}/engines/nms/Control.cpp ${REPO_ROOT}/engines/nms/Drums.cpp
	${REPO_ROOT}/engines/nms/Fx.cpp ${REPO_ROOT}/engines/nms/Instruments2.cpp ${REPO_ROOT}/engines/nms/Patches.cpp
	${REPO_ROOT}/engines/nms/Rack.cpp ${REPO_ROOT}/engines/nms/Synths.cpp ${REPO_ROOT}/engines/nms/UpstreamStub.cpp
	${REPO_ROOT}/engines/nms/Host.cpp)
