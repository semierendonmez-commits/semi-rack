# Open303 (MIT, Robin Schmidt) — shared third-party 303 voice, engines/thirdparty/open303 (see NOTICE-SEMI.md).
# fft4g.c is #included by rosic_FourierTransformerRadix2.cpp, so only the .cpp files are listed.
ENGINE_SOURCES += $(wildcard engines/thirdparty/open303/*.cpp)
