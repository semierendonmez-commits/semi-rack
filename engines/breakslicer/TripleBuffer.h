// Lock-free triple buffer: one writer (the audio thread) publishes whole
// snapshots, one reader (the editor) always gets the newest complete one.
#pragma once

#include <array>
#include <atomic>

namespace gtbs
{

template <typename T>
class TripleBuffer
{
public:
    T& writeBuffer() noexcept { return bufs[(size_t) back]; }

    void publish() noexcept
    {
        // swap the back buffer with the middle one and mark it fresh
        const int prev = middle.exchange (back | kFresh, std::memory_order_acq_rel);
        back = prev & kIndex;
    }

    /** true if a newer snapshot arrived; out is then filled */
    bool read (T& out) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kFresh) == 0) return false;
        const int prev = middle.exchange (front, std::memory_order_acq_rel);
        front = prev & kIndex;
        out = bufs[(size_t) front];
        return true;
    }

private:
    static constexpr int kFresh = 4, kIndex = 3;
    std::array<T, 3> bufs {};
    int back = 0, front = 1;
    std::atomic<int> middle { 2 };
};

} // namespace gtbs
