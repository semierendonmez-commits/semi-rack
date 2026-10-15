#pragma once
#include <atomic>
#include <cstdint>
#include "Platform.hpp"

namespace semi {

// Lock-free single-writer / single-reader snapshot.
// The audio thread (Rack engine thread, MetaModule audio core) calls publish();
// the GUI (Rack UI thread, MetaModule GUI core) calls read(). Neither blocks.
template <typename T>
struct TripleBuffer {
	T buf[3]{};
	std::atomic<uint8_t> middle{1}; // index of the hand-over slot; bit 2 = fresh
	uint8_t back = 0;               // writer-owned
	uint8_t front = 2;              // reader-owned

	T& writeSlot() { return buf[back]; }

	void publish() {
		uint8_t prev = middle.exchange(uint8_t(back | 4), std::memory_order_acq_rel);
		back = prev & 3;
	}

	void publish(const T& v) {
		buf[back] = v;
		publish();
	}

	// Returns the newest published value (or the last one if nothing new).
	const T& read() {
		if (middle.load(std::memory_order_acquire) & 4) {
			uint8_t prev = middle.exchange(front, std::memory_order_acq_rel);
			front = prev & 3;
		}
		return buf[front];
	}
};

// Decimates the per-sample process() call down to the display rate.
struct UiDivider {
	float acc = 0.f;
	bool tick(float sampleTime) {
		acc += sampleTime;
		if (acc >= 1.f / platform::kUiRateHz) {
			acc = 0.f;
			return true;
		}
		return false;
	}
};

} // namespace semi
