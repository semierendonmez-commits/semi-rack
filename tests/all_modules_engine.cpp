// Creates a bare Rack context + engine for tests/all_modules_smoke.cpp. Includes Rack's headers directly
// (not rack.hpp) so the PRIVATE engine constructor is reachable; test-only.
#include <context.hpp>
#include <engine/Engine.hpp>

void smokeMakeEngine() {
	rack::contextSet(new rack::Context);
	APP->engine = new rack::engine::Engine;
}
