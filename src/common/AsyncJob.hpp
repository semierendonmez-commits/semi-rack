#pragma once
// A MetaModule AsyncThread that can be shut down safely.
//
// The SDK's AsyncThread::stop() "will not halt current execution of the thread, it merely prevents
// it from starting again" (docs/async-threads.md). A module deleted while its job is mid-run (a
// break being sliced, a stack being built) has that job keep writing into freed members: the
// closed-beta test rig, which creates, renders and deletes every module in turn, hit exactly this
// as a data abort.
//
// AsyncJob marks each run and stopAndWait() waits for a run in flight. Call it first thing in
// the module's destructor, while every member the job touches is still alive (member destructors
// run after the destructor body, in reverse declaration order, so relying on AsyncJob's own
// destructor is only safe when it is the last member).
//
// Rack: not used (jobs run in the widget's step() or a std::thread the module joins).

#ifdef METAMODULE
#include <atomic>
#include <functional>
#include <utility>

#include "threads/async_thread.hh"

namespace semi {

class AsyncJob {
public:
	template <typename M>
	AsyncJob(M* module, std::function<void()> fn)
		: job(std::move(fn)), thread(module, [this]() { tick(); }) {}

	AsyncJob(const AsyncJob&) = delete;
	AsyncJob& operator=(const AsyncJob&) = delete;

	~AsyncJob() { stopAndWait(); }

	void start() {
		stopping.store(false);
		thread.start();
	}

	// After this returns the job is not running and will not run again.
	void stopAndWait() {
		stopping.store(true);   // seq_cst pairs with tick(): either it sees `stopping`, or we see `running`
		thread.stop();
		while (running.load())
			;   // the run in flight is on the other core (or the audio core's background); it finishes
	}

private:
	void tick() {
		running.store(true);
		if (!stopping.load())
			job();
		running.store(false);
	}

	std::function<void()> job;
	std::atomic<bool> running{false};
	std::atomic<bool> stopping{false};
	MetaModule::AsyncThread thread;
};

} // namespace semi
#endif
