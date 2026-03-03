#pragma once

#include <cstdint>

// Timing event pushed from CW task, consumed by network task.
struct TimingEvent {
    bool     key_down;      // true = key-down, false = key-up
    uint32_t timestamp_ms;  // monotonic ms
};

// Lock-free single-producer / single-consumer ring buffer.
// Producer: CW task (Core 1). Consumer: UI/network task (Core 0).
template<int N>
class TimingRingBuffer {
public:
    // Called from CW task only.
    void push(bool key_down, uint32_t ts_ms)
    {
        int next = (head_ + 1) % N;
        if (next == tail_) return;      // full — drop oldest-unread
        buf_[head_] = { key_down, ts_ms };
        head_ = next;
    }

    // Called from UI task only. Returns false if empty.
    bool pop(TimingEvent& out)
    {
        if (tail_ == head_) return false;
        out = buf_[tail_];
        tail_ = (tail_ + 1) % N;
        return true;
    }

    void clear()
    {
        head_ = 0;
        tail_ = 0;
    }

private:
    volatile int head_ = 0;
    volatile int tail_ = 0;
    TimingEvent  buf_[N] = {};
};
