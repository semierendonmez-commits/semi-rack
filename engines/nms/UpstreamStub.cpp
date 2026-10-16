// SEMI Family port: replaces Source/engine/Upstream.cpp. The original Schwung modules
// (SIGNAL, ESSAIM, BREAKBEAT, STRUCTOR, DISSOLVER) are not part of the Rack / MetaModule
// build: SIGNAL's and DISSOLVER's sources are not in the plugin repository, BREAKBEAT loads
// its breaks on a POSIX thread with mmap, the host glue needs std::mutex and a resampler
// per instance, and SIGNAL / DISSOLVER are GPL-3.0. The registry simply has no upstream
// types; Patches.cpp puts native modules in their places.
#include "Modules.h"

namespace nms
{
void setUpstreamDataDir (const std::string&) {}
std::unique_ptr<Module> createUpstream (const std::string&) { return nullptr; }
void upstreamSpecs (std::vector<const TypeSpec*>&) {}
} // namespace nms
