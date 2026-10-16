# CloudSeed (Ghost Note Audio, MIT), the C port charlesvestal/schwung-cloudseed, with FLOATER's float bridge
# (floater_cloudseed.c includes cloudseed.c). Rack only: one instance allocates ~60 MB of delay lines, so the
# MetaModule build leaves it out (see sources.cmake and engines/floater/Schwung.cpp).
ENGINE_SOURCES += engines/thirdparty/cloudseed/floater_cloudseed.c
