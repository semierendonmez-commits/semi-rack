# MODPORT engine, vendored from semierendonmez-commits/modport Source/engine (JUCE-free).
# Module.cpp/.h = .mod loader, Player.cpp/.h = ProTracker 2.3D replayer after pt2-clone (BSD-3,
# see NOTICE.md). One change: Player.h + setSpeedTempo () (NEXT jumps keep speed / tempo).
# Driver.h is new: the Rack transport (free tempo / one row per clock). Export.* is not vendored.
# Namespace modport.
ENGINE_SOURCES += engines/modport/Module.cpp engines/modport/Player.cpp
