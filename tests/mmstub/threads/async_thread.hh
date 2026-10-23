#pragma once
// Test stub of the MetaModule SDK's AsyncThread with the documented semantics
// (docs/async-threads.md): the action is called about every 0.5 ms on another core and runs to
// completion; stop() does not halt a run in progress, it only prevents the next one.
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace MetaModule {
using Callback = std::function<void()>;
class AsyncThread {
	struct State {
		Callback action;
		std::atomic<bool> enabled{false}, quit{false};
	};
	std::shared_ptr<State> st = std::make_shared<State>();   // the runner keeps its own reference

public:
	template <typename M> AsyncThread(M*, Callback&& a) { st->action = std::move(a); }
	void start() {
		if (st->enabled.exchange(true)) return;
		std::thread([s = st]() {
			while (!s->quit) {
				if (s->enabled) s->action();
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
		}).detach();
	}
	void stop() { st->enabled = false; }   // as documented: a run in progress continues
	~AsyncThread() { st->enabled = false; st->quit = true; }
};
} // namespace MetaModule
