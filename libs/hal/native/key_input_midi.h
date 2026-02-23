#pragma once

#ifdef NATIVE_BUILD

// Native HAL: key/paddle input via ALSA MIDI sequencer.
//
// Creates one ALSA sequencer input port ("m32-keyer:0").  Connect your MIDI
// device or MIDI keyboard with:
//
//   aconnect <device-client>:<port> m32-keyer:0
//
// Default note mapping (all configurable via constructor):
//   Note 60 (C4)  — dit paddle
//   Note 61 (C#4) — dah paddle
//   Note 62 (D4)  — straight key
//
// Note On  (velocity > 0) → _DOWN event
// Note On  (velocity = 0) → _UP   event  (running-status Note Off)
// Note Off                → _UP   event
//
// poll() and wait() are thread-safe; the MIDI thread pushes into an
// internal queue protected by a mutex and condition variable.

#include "../interfaces/i_key_input.h"

#include <alsa/asoundlib.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

class NativeKeyInputMidi : public IKeyInput
{
public:
    // note_dit/dah/straight: MIDI note numbers that map to each paddle input.
    // auto_connect: try to connect the first available MIDI output port at startup.
    NativeKeyInputMidi(int note_dit      = 60,
                       int note_dah      = 61,
                       int note_straight = 62,
                       bool auto_connect = false);
    ~NativeKeyInputMidi() override;

    // Returns true and fills `out` if an event is in the queue; non-blocking.
    bool poll(KeyEvent& out) override;

    // Blocks until an event arrives or timeout_ms elapses; returns false on timeout.
    bool wait(KeyEvent& out, uint32_t timeout_ms) override;

    // Print the ALSA sequencer client and port numbers to stderr.
    void print_port_info() const;

private:
    void midi_thread_fn();
    void process_event(const snd_seq_event_t* ev);
    void push(KeyEvent ev);

    int  note_dit_;
    int  note_dah_;
    int  note_straight_;
    bool auto_connect_;

    snd_seq_t* seq_    = nullptr;
    int        port_id_ = -1;
    int        client_id_ = -1;

    std::thread       midi_thread_;
    std::atomic<bool> running_{false};

    std::queue<KeyEvent>    queue_;
    std::mutex              queue_mutex_;
    std::condition_variable queue_cv_;
};

#endif // NATIVE_BUILD
