// AsyncJob against a stub AsyncThread with the SDK's documented semantics: delete the owner while
// a long job is in flight. Without the wait the job writes into freed memory (ASan reports it);
// with stopAndWait() in the destructor it must finish first.
//   g++ -std=c++17 -g -fsanitize=address -DMETAMODULE -Itests/mmstub -Isrc/common tests/async_job_test.cpp -lpthread
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include "AsyncJob.hpp"

struct Owner {
	std::atomic<bool> want{true};
	std::vector<float> data = std::vector<float>(1 << 16);
	semi::AsyncJob job{this, [this]() { work(); }};
	Owner() { job.start(); }
	~Owner() { job.stopAndWait(); }
	void work() {
		if (!want.exchange(false)) return;
		for (int pass = 0; pass < 200; pass++)          // ~ a long build
			for (auto& v : data) v = v * 0.5f + 1.f;
	}
};

int main() {
	for (int i = 0; i < 200; i++) {
		auto* o = new Owner;
		std::this_thread::sleep_for(std::chrono::microseconds(700 + (i % 7) * 300));   // mid-build
		delete o;
	}
	std::printf("async_job_test: 200 deletes mid-build, no use after free\n");
	return 0;
}
