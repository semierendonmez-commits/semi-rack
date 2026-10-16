# CONDUIT pin matrix (vendored from semierendonmez-commits/conduit Source/engine: Patch.h, Router.*).
# Changes: Patch.h kRows 24 -> 15, kCols 16 -> 8 (the cable module's rows / columns);
# Router.h kHistory 4096 -> 256 with a tempo-scaled history stride in Router.cpp (RAM on MetaModule).
# Patchbay / Catalog (shared-memory FamilyBus) are not vendored: the module feeds the router from cables.
ENGINE_SOURCES += engines/conduit/Router.cpp
