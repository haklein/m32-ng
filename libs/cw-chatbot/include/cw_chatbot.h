#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <random>

// ── Callback types ──────────────────────────────────────────────────────────

// Chatbot has a phrase to send.  The host feeds it into a Morse player.
using chatbot_send_fn  = std::function<void(const std::string& text)>;

// Chatbot wants to change sending speed.
using chatbot_speed_fn = std::function<void(int wpm)>;

// QSO lifecycle events for UI status display.
enum class QSOEvent {
    CQ_SENT,
    CQ_ANSWERED,
    OPERATOR_CQ,
    EXCHANGE_SENT,
    EXCHANGE_RECEIVED,
    TOPIC_SENT,
    CLOSING,
    QSO_COMPLETE,
    QSO_ABANDONED,
};
using chatbot_event_fn = std::function<void(QSOEvent event)>;

// ── Configuration enums ─────────────────────────────────────────────────────

enum class QSODepth { MINIMAL, STANDARD, RAGCHEW };

enum class QSOState {
    IDLE,
    BOT_CQ,
    WAIT_FOR_OPERATOR,
    BOT_ANSWER_CQ,
    BOT_EXCHANGE,
    WAIT_OPER_EXCHANGE,
    TOPIC_ROUNDS,
    CLOSING,
};

enum class BotStyle { TERSE, NORMAL, CHATTY };

enum class Topic { RIG, ANT, PWR, WX, AGE, CONDX };

// ── Persona (bot's identity for one QSO) ────────────────────────────────────

struct Persona {
    std::string callsign;
    std::string name;
    std::string qth;
    std::string rst;      // e.g. "579"
    std::string rig;
    std::string pwr;      // e.g. "100W"
    std::string ant;      // e.g. "3 EL YAGI UP 15M"
    std::string wx;       // e.g. "SUNNY TEMP 22C"
    int         age       = 0;
    int         lic_years = 0;
    BotStyle    style     = BotStyle::NORMAL;
};

// ── Parsed operator data ────────────────────────────────────────────────────

struct OperatorData {
    std::string callsign;
    std::string name  = "OM";   // fallback if never parsed
    std::string qth;
    std::string rst;
    std::string rig;
    std::string ant;
    std::string pwr;
    std::string wx;
};

// ── CW QSO Chatbot ─────────────────────────────────────────────────────────

class CWChatbot {
public:
    CWChatbot(chatbot_send_fn  send_cb,
              chatbot_speed_fn speed_cb,
              chatbot_event_fn event_cb,
              std::function<unsigned long()> millis_cb);

    // ── Configuration (call before start) ───────────────────────────────
    void set_operator_call(const std::string& callsign);
    void set_qso_depth(QSODepth depth);
    void set_speed_wpm(int wpm);
    void set_bot_initiates_probability(float p);
    void set_rng_seed(unsigned int seed);

    // ── Runtime ─────────────────────────────────────────────────────────
    void start();
    void stop();
    void tick();
    void symbol_received(const std::string& symbol);
    void transmission_complete();

    // ── Queries ─────────────────────────────────────────────────────────
    QSOState             state()         const { return state_; }
    std::string          bot_callsign()  const { return persona_.callsign; }
    int                  current_wpm()   const { return wpm_; }
    bool                 is_transmitting() const { return transmitting_; }
    const Persona&       persona()       const { return persona_; }
    const OperatorData&  operator_data() const { return oper_data_; }

private:
    // ── Callbacks ───────────────────────────────────────────────────────
    chatbot_send_fn                send_cb_;
    chatbot_speed_fn               speed_cb_;
    chatbot_event_fn               event_cb_;
    std::function<unsigned long()> millis_cb_;

    // ── RNG ─────────────────────────────────────────────────────────────
    std::mt19937 rng_{42};

    // ── Configuration ───────────────────────────────────────────────────
    std::string operator_call_ = "W1TEST";
    QSODepth    qso_depth_     = QSODepth::STANDARD;
    int         wpm_           = 15;
    float       bot_init_prob_ = 0.5f;

    // ── State ───────────────────────────────────────────────────────────
    QSOState     state_        = QSOState::IDLE;
    Persona      persona_;
    OperatorData oper_data_;
    bool         transmitting_ = false;

    // ── Input accumulation ──────────────────────────────────────────────
    std::string              current_word_;
    std::vector<std::string> received_words_;
    unsigned long            last_symbol_time_ = 0;

    // ── CQ management ───────────────────────────────────────────────────
    int           cq_count_     = 0;
    unsigned long cq_sent_time_ = 0;

    // ── Topic round management ──────────────────────────────────────────
    std::vector<Topic> topic_queue_;
    int                topic_index_          = 0;
    bool               waiting_for_oper_topic_ = false;

    // ── Closing phases ──────────────────────────────────────────────────
    int closing_phase_ = 0;   // 0 = first closing, 1 = final dit-dit sent

    // ── Timing ──────────────────────────────────────────────────────────
    unsigned long last_activity_time_ = 0;
    unsigned long state_enter_time_   = 0;
    bool          prompt_sent_        = false;

    // ── Last bot phrase (for AGN repeat) ────────────────────────────────
    std::string last_bot_phrase_;

    // ── Timeouts ────────────────────────────────────────────────────────
    static constexpr unsigned long CQ_TIMEOUT_MS       = 6000;
    static constexpr unsigned long CQ_REPEAT_DELAY_MS  = 4000;
    static constexpr unsigned long EXCHANGE_TIMEOUT_MS  = 30000;
    static constexpr unsigned long OVER_TIMEOUT_MS      = 15000;
    static constexpr unsigned long ABANDON_TIMEOUT_MS   = 60000;
    static constexpr unsigned long INTER_OVER_DELAY_MS  = 1500;
    static constexpr unsigned long WORD_COMPLETE_MS     = 1500;
    static constexpr int           MAX_CQ_REPEATS       = 4;

    // ── Internal methods ────────────────────────────────────────────────
    // Persona generation
    void        generate_persona();
    std::string generate_rst();

    // State machine
    void set_state(QSOState new_state);
    void tick_idle();
    void tick_bot_cq();
    void tick_wait_for_operator();
    void tick_bot_answer_cq();
    void tick_bot_exchange();
    void tick_wait_oper_exchange();
    void tick_topic_rounds();
    void tick_closing();

    // Input parsing
    void flush_current_word();
    void process_received_words();
    bool fuzzy_match(const std::string& a, const std::string& b) const;
    void extract_operator_call(const std::vector<std::string>& words);
    void parse_exchange(const std::vector<std::string>& words);
    bool contains_trigger(const std::string& trigger) const;
    bool is_trigger(const std::string& word) const;
    std::string extract_until_trigger(const std::vector<std::string>& words, size_t start) const;
    bool has_over_end_signal() const;
    bool words_look_like_exchange() const;

    // Phrase generation
    std::string build_cq_phrase() const;
    std::string build_answer_cq_phrase() const;
    std::string build_exchange_phrase();
    std::string build_topic_phrase(Topic topic);
    std::string build_closing_phrase() const;
    std::string build_final_phrase() const;
    std::string build_prompt_phrase() const;
    std::string build_ack_phrase();
    std::string build_repeat_phrase(const std::string& topic_key) const;
    std::string substitute(const std::string& tmpl);

    // Speed management
    void handle_qrs();
    void handle_qrq();

    // Sending helper
    void bot_send(const std::string& phrase);

    // Topic management
    void init_topic_queue();
    void proceed_after_exchange();
    void send_next_topic_or_close();

    // Utility
    int         rng_range(int lo, int hi);
    const char* rng_pick(const char* const* arr, int count);
};
