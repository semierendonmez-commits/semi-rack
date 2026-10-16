# Open303 (MIT, Robin Schmidt) — shared third-party 303 voice (see engines/thirdparty/open303/NOTICE-SEMI.md).
file(GLOB TP_OPEN303 ${REPO_ROOT}/engines/thirdparty/open303/*.cpp)
list(APPEND ENGINE_SOURCES ${TP_OPEN303})
