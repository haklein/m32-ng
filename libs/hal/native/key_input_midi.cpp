#ifdef NATIVE_BUILD

#include "key_input_midi.h"
#include <chrono>
#include <cstdio>
#include <vector>

NativeKeyInputMidi::NativeKeyInputMidi(int note_dit, int note_dah,
                                       int note_straight, bool auto_connect)
    : note_dit_(note_dit)
    , note_dah_(note_dah)
    , note_straight_(note_straight)
    , auto_connect_(auto_connect)
{
    int err;

    err = snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0) {
        fprintf(stderr, "MIDI: cannot open sequencer: %s\n", snd_strerror(err));
        return;
    }

    snd_seq_set_client_name(seq_, "m32-keyer");
    client_id_ = snd_seq_client_id(seq_);

    port_id_ = snd_seq_create_simple_port(seq_, "keys",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_id_ < 0) {
        fprintf(stderr, "MIDI: cannot create port: %s\n", snd_strerror(port_id_));
        snd_seq_close(seq_);
        seq_ = nullptr;
        return;
    }

    print_port_info();

    if (auto_connect_) {
        // Scan all sequencer ports and print them so the user can debug.
        // Connect to the first *hardware* MIDI output port we find.
        // Hardware ports must have SND_SEQ_PORT_TYPE_HARDWARE set — this
        // filters out "Midi Through" and other software loopback ports that
        // also advertise READ|SUBS_READ but carry no real device data.
        snd_seq_client_info_t* cinfo;
        snd_seq_port_info_t*   pinfo;
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_port_info_alloca(&pinfo);

        fprintf(stderr, "MIDI: scanning sequencer ports...\n");

        snd_seq_client_info_set_client(cinfo, -1);
        while (snd_seq_query_next_client(seq_, cinfo) >= 0) {
            int cid = snd_seq_client_info_get_client(cinfo);
            if (cid == client_id_) continue;

            snd_seq_port_info_set_client(pinfo, cid);
            snd_seq_port_info_set_port(pinfo, -1);
            while (snd_seq_query_next_port(seq_, pinfo) >= 0) {
                unsigned int caps = snd_seq_port_info_get_capability(pinfo);
                unsigned int type = snd_seq_port_info_get_type(pinfo);
                int pid = snd_seq_port_info_get_port(pinfo);
                const char* pname = snd_seq_port_info_get_name(pinfo);
                const char* cname = snd_seq_client_info_get_name(cinfo);

                bool readable = (caps & SND_SEQ_PORT_CAP_READ) &&
                                (caps & SND_SEQ_PORT_CAP_SUBS_READ);
                bool hardware = (type & SND_SEQ_PORT_TYPE_HARDWARE) != 0;

                fprintf(stderr, "MIDI:   %d:%d  %-30s [%s%s]\n",
                        cid, pid, pname,
                        readable  ? "readable " : "",
                        hardware  ? "hardware" : "software");

                if (readable && hardware) {
                    err = snd_seq_connect_from(seq_, port_id_, cid, pid);
                    if (err == 0) {
                        fprintf(stderr, "MIDI: auto-connected from %d:%d '%s / %s'\n",
                                cid, pid, cname, pname);
                        goto connected;
                    }
                    fprintf(stderr, "MIDI: connect failed: %s\n", snd_strerror(err));
                }
            }
        }
        fprintf(stderr, "MIDI: no hardware port found; "
                        "connect manually with:  aconnect <device>:<port> %d:%d\n",
                        client_id_, port_id_);
        connected:;
    }

    running_ = true;
    midi_thread_ = std::thread(&NativeKeyInputMidi::midi_thread_fn, this);
}

NativeKeyInputMidi::~NativeKeyInputMidi()
{
    running_ = false;
    if (midi_thread_.joinable())
        midi_thread_.join();
    if (seq_)
        snd_seq_close(seq_);
}

void NativeKeyInputMidi::print_port_info() const
{
    fprintf(stderr, "MIDI: listening on client %d port %d  "
                    "(connect with: aconnect <device> %d:%d)\n",
            client_id_, port_id_, client_id_, port_id_);
}

// ── IKeyInput ──────────────────────────────────────────────────────────────────

bool NativeKeyInputMidi::poll(KeyEvent& out)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop();
    return true;
}

bool NativeKeyInputMidi::wait(KeyEvent& out, uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);
    bool got = queue_cv_.wait_for(lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return !queue_.empty(); });
    if (!got) return false;
    out = queue_.front();
    queue_.pop();
    return true;
}

// ── MIDI thread ───────────────────────────────────────────────────────────────

void NativeKeyInputMidi::push(KeyEvent ev)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(ev);
    }
    queue_cv_.notify_one();
}

void NativeKeyInputMidi::process_event(const snd_seq_event_t* ev)
{
    bool is_on  = (ev->type == SND_SEQ_EVENT_NOTEON  && ev->data.note.velocity > 0);
    bool is_off = (ev->type == SND_SEQ_EVENT_NOTEOFF) ||
                  (ev->type == SND_SEQ_EVENT_NOTEON  && ev->data.note.velocity == 0);
    if (!is_on && !is_off) return;

    int note = ev->data.note.note;
    if (note == note_dit_) {
        push(is_on ? KeyEvent::PADDLE_DIT_DOWN : KeyEvent::PADDLE_DIT_UP);
    } else if (note == note_dah_) {
        push(is_on ? KeyEvent::PADDLE_DAH_DOWN : KeyEvent::PADDLE_DAH_UP);
    } else if (note == note_straight_) {
        push(is_on ? KeyEvent::STRAIGHT_DOWN : KeyEvent::STRAIGHT_UP);
    }
    // All other notes are silently ignored.
}

void NativeKeyInputMidi::midi_thread_fn()
{
    if (!seq_) return;

    // Set non-blocking so that snd_seq_event_input won't block indefinitely.
    snd_seq_nonblock(seq_, 1);

    int npfds = snd_seq_poll_descriptors_count(seq_, POLLIN);
    std::vector<pollfd> pfds(npfds);

    while (running_) {
        snd_seq_poll_descriptors(seq_, pfds.data(), npfds, POLLIN);
        // 100 ms timeout so we wake up periodically to check running_.
        int ret = ::poll(pfds.data(), npfds, 100);
        if (ret <= 0) continue;

        snd_seq_event_t* event = nullptr;
        int rc;
        while ((rc = snd_seq_event_input(seq_, &event)) > 0 && event) {
            process_event(event);
        }
        // rc == -EAGAIN means no more events waiting — normal in non-blocking mode.
    }
}

#endif // NATIVE_BUILD
