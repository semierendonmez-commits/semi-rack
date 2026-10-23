# Port fixes in IngpEngine.cpp render(): the oscillator phase wraps fully (negative or
# above-Nyquist frequencies from CV used to index past the sine table) and the table index is clamped.
ENGINE_SOURCES += engines/tstingp/IngpEngine.cpp
