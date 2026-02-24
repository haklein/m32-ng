#include "cw_chatbot.h"
#include "qso_data.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

// ── Phrase templates ────────────────────────────────────────────────────────

static const char* EXCHANGE_TERSE =
    "{oper_call} DE {bot_call} "
    "RST {rst} NAME {name} QTH {qth} "
    "AR {oper_call} DE {bot_call} KN";

static const char* EXCHANGE_NORMAL =
    "{oper_call} DE {bot_call} {greeting} {oper_name} TNX FER CALL = "
    "UR RST {rst} {rst} = "
    "NAME {name} {name} = "
    "QTH {qth} {qth} = "
    "HW? AR {oper_call} DE {bot_call} KN";

static const char* EXCHANGE_CHATTY =
    "{oper_call} DE {bot_call} {greeting} {greeting} DR {oper_name} = "
    "VY TNX FER FB CALL = NICE TO MEET U OM = "
    "UR RST IS {rst} {rst} = "
    "NAME HR IS {name} {name} = "
    "QTH IS NR {qth} {qth} = "
    "HW CPY? AR {oper_call} DE {bot_call} KN";

// Topic templates — RIG
static const char* TOPIC_RIG_TERSE  = "RIG {rig} PWR {pwr} AR KN";
static const char* TOPIC_RIG_NORMAL =
    "RIG HR IS {rig} {rig} = PWR ABT {pwr} = HW? AR {oper_call} DE {bot_call} KN";
static const char* TOPIC_RIG_CHATTY =
    "RIG HR IS {rig} {rig} = VY FB RIG = RUNNING ABT {pwr} = "
    "HW ABT UR RIG? AR {oper_call} DE {bot_call} KN";

// Topic templates — ANT
static const char* TOPIC_ANT_TERSE  = "ANT {ant} AR KN";
static const char* TOPIC_ANT_NORMAL =
    "ANT IS {ant} = HW? AR {oper_call} DE {bot_call} KN";
static const char* TOPIC_ANT_CHATTY =
    "ANT HR IS {ant} = WORKS FB ON THIS BAND = "
    "HW ABT UR ANT? AR {oper_call} DE {bot_call} KN";

// Topic templates — PWR (standalone, when not bundled with RIG)
static const char* TOPIC_PWR_TERSE  = "PWR {pwr} AR KN";
static const char* TOPIC_PWR_NORMAL =
    "RUNNING ABT {pwr} INTO {ant} = HW? AR {oper_call} DE {bot_call} KN";

// Topic templates — WX
static const char* TOPIC_WX_TERSE  = "WX {wx} AR KN";
static const char* TOPIC_WX_NORMAL =
    "WX HR {wx} = HW? AR {oper_call} DE {bot_call} KN";
static const char* TOPIC_WX_CHATTY =
    "WX HR IS {wx} = NICE WX FER RADIO HI = "
    "HW ABT WX UR QTH? AR {oper_call} DE {bot_call} KN";

// Topic templates — AGE
static const char* TOPIC_AGE_TERSE  = "AGE {age} YRS BEEN HAM {lic_years} YRS AR KN";
static const char* TOPIC_AGE_NORMAL =
    "AGE HR IS {age} YRS = BEEN HAM {lic_years} YRS = "
    "HW? AR {oper_call} DE {bot_call} KN";

// Topic templates — CONDX
static const char* TOPIC_CONDX_TMPL =
    "CONDX {condx} TODAY = BAND {band} = AR {oper_call} DE {bot_call} KN";

// Closing
static const char* CLOSING_TERSE =
    "{oper_call} DE {bot_call} R TNX QSO 73 SK {oper_call} DE {bot_call}";
static const char* CLOSING_NORMAL =
    "{oper_call} DE {bot_call} R TNX FER FB QSO {oper_name} = "
    "HPE CUL = 73 73 = SK {oper_call} DE {bot_call}";
static const char* CLOSING_CHATTY =
    "{oper_call} DE {bot_call} R VY TNX FER VY FB QSO DR {oper_name} = "
    "HPE TO CU AGN SN = GL ES DX = 73 73 = SK {oper_call} DE {bot_call}";


// Ack phrases by style
static const char* const ACKS_TERSE[]  = {"R", "R FB"};
static const char* const ACKS_NORMAL[] = {"R FB TNX", "R TNX INFO", "R FB"};
static const char* const ACKS_CHATTY[] = {"R VY FB TNX INFO", "R FB TNX FER INFO OM", "SOLID COPY TNX"};

// Trigger words for parsing (not content words)
static const char* const TRIGGERS[] = {
    "RST", "NAME", "OP", "QTH", "RIG", "ANT", "PWR", "WX", "AGE",
    "DE", "AR", "KN", "K", "BK", "BT", "73", "SK", "CQ", "HW", "HW?"};
static constexpr int NUM_TRIGGERS = sizeof(TRIGGERS) / sizeof(TRIGGERS[0]);

// ═════════════════════════════════════════════════════════════════════════════
// Constructor / Configuration
// ═════════════════════════════════════════════════════════════════════════════

CWChatbot::CWChatbot(chatbot_send_fn send_cb, chatbot_speed_fn speed_cb,
                      chatbot_event_fn event_cb,
                      std::function<unsigned long()> millis_cb)
    : send_cb_(std::move(send_cb))
    , speed_cb_(std::move(speed_cb))
    , event_cb_(std::move(event_cb))
    , millis_cb_(std::move(millis_cb))
{}

void CWChatbot::set_operator_call(const std::string& c) { operator_call_ = c; }
void CWChatbot::set_qso_depth(QSODepth d)               { qso_depth_ = d; }
void CWChatbot::set_speed_wpm(int w)                     { wpm_ = w; }
void CWChatbot::set_bot_initiates_probability(float p)   { bot_init_prob_ = p; }
void CWChatbot::set_rng_seed(unsigned int s)             { rng_.seed(s); }

// ═════════════════════════════════════════════════════════════════════════════
// Utility
// ═════════════════════════════════════════════════════════════════════════════

int CWChatbot::rng_range(int lo, int hi) {
    if (lo >= hi) return lo;
    return std::uniform_int_distribution<int>(lo, hi - 1)(rng_);
}

const char* CWChatbot::rng_pick(const char* const* arr, int count) {
    return arr[rng_range(0, count)];
}

// ═════════════════════════════════════════════════════════════════════════════
// Persona Generation
// ═════════════════════════════════════════════════════════════════════════════

void CWChatbot::generate_persona() {
    persona_ = Persona{};

    int ci = rng_range(0, NUM_COUNTRIES);
    const CountryData& c = COUNTRIES[ci];

    // Callsign: prefix + digit + 1-3 letter suffix
    std::string call = rng_pick(c.prefixes, c.num_prefixes);
    call += std::to_string(rng_range(0, 10));
    int slen = rng_range(1, 4);
    for (int i = 0; i < slen; ++i)
        call += static_cast<char>('A' + rng_range(0, 26));

    persona_.callsign = call;
    persona_.name = rng_pick(c.names, c.num_names);
    persona_.qth  = std::string("NR ") + rng_pick(c.cities, c.num_cities);
    persona_.rst  = generate_rst();
    persona_.rig  = rng_pick(RIGS, NUM_RIGS);

    // QRP rigs get lower power
    bool is_qrp = false;
    for (int i = 0; i < NUM_QRP_RIGS; ++i)
        if (persona_.rig == QRP_RIGS[i]) { is_qrp = true; break; }
    persona_.pwr = is_qrp ? rng_pick(QRP_POWERS, NUM_QRP_POWERS)
                          : rng_pick(POWERS, NUM_POWERS);

    persona_.ant = std::string(rng_pick(ANTENNAS, NUM_ANTENNAS)) + " " +
                   rng_pick(HEIGHTS, NUM_HEIGHTS);
    persona_.wx  = std::string(rng_pick(WX_CONDITIONS, NUM_WX)) + " TEMP " +
                   std::to_string(rng_range(-5, 40)) + "C";
    persona_.age       = rng_range(20, 86);
    persona_.lic_years = rng_range(1, std::max(2, persona_.age - 14));
    persona_.style     = static_cast<BotStyle>(rng_range(0, 3));
}

std::string CWChatbot::generate_rst() {
    return std::to_string(rng_range(4, 6)) +
           std::to_string(rng_range(5, 10)) +
           std::to_string(rng_range(8, 10));
}

// ═════════════════════════════════════════════════════════════════════════════
// Phrase Generation
// ═════════════════════════════════════════════════════════════════════════════

std::string CWChatbot::substitute(const std::string& tmpl) {
    std::string result;
    result.reserve(tmpl.size() * 2);

    for (size_t i = 0; i < tmpl.size(); ) {
        if (tmpl[i] == '{') {
            size_t end = tmpl.find('}', i);
            if (end != std::string::npos) {
                std::string var = tmpl.substr(i + 1, end - i - 1);
                const std::string& oc = oper_data_.callsign.empty()
                                        ? operator_call_ : oper_data_.callsign;
                if      (var == "bot_call")   result += persona_.callsign;
                else if (var == "oper_call")  result += oc;
                else if (var == "oper_name")  result += oper_data_.name;
                else if (var == "rst")        result += persona_.rst;
                else if (var == "name")       result += persona_.name;
                else if (var == "qth")        result += persona_.qth;
                else if (var == "rig")        result += persona_.rig;
                else if (var == "pwr")        result += persona_.pwr;
                else if (var == "ant")        result += persona_.ant;
                else if (var == "wx")         result += persona_.wx;
                else if (var == "age")        result += std::to_string(persona_.age);
                else if (var == "lic_years")  result += std::to_string(persona_.lic_years);
                else if (var == "greeting")   result += "GE";
                else if (var == "condx")      result += rng_pick(CONDX_RATINGS, NUM_CONDX);
                else if (var == "band")       result += rng_pick(BAND_STATUS, NUM_BAND);
                else                          result += "{" + var + "}";
                i = end + 1;
                continue;
            }
        }
        result += tmpl[i++];
    }
    return result;
}

std::string CWChatbot::build_cq_phrase() const {
    // CQ is simple enough to build directly (substitute is non-const)
    return "CQ CQ CQ DE " + persona_.callsign + " " + persona_.callsign + " K";
}

std::string CWChatbot::build_answer_cq_phrase() const {
    const std::string& oc = oper_data_.callsign.empty()
                            ? operator_call_ : oper_data_.callsign;
    return oc + " DE " + persona_.callsign + " " + persona_.callsign + " AR";
}

std::string CWChatbot::build_exchange_phrase() {
    const char* tmpl;
    switch (persona_.style) {
        case BotStyle::TERSE:  tmpl = EXCHANGE_TERSE; break;
        case BotStyle::CHATTY: tmpl = EXCHANGE_CHATTY; break;
        default:               tmpl = EXCHANGE_NORMAL; break;
    }
    return substitute(tmpl);
}

std::string CWChatbot::build_topic_phrase(Topic topic) {
    const char* tmpl = nullptr;
    switch (topic) {
    case Topic::RIG:
        tmpl = persona_.style == BotStyle::TERSE  ? TOPIC_RIG_TERSE :
               persona_.style == BotStyle::CHATTY ? TOPIC_RIG_CHATTY :
                                                    TOPIC_RIG_NORMAL;
        break;
    case Topic::ANT:
        tmpl = persona_.style == BotStyle::TERSE  ? TOPIC_ANT_TERSE :
               persona_.style == BotStyle::CHATTY ? TOPIC_ANT_CHATTY :
                                                    TOPIC_ANT_NORMAL;
        break;
    case Topic::PWR:
        tmpl = persona_.style == BotStyle::TERSE ? TOPIC_PWR_TERSE : TOPIC_PWR_NORMAL;
        break;
    case Topic::WX:
        tmpl = persona_.style == BotStyle::TERSE  ? TOPIC_WX_TERSE :
               persona_.style == BotStyle::CHATTY ? TOPIC_WX_CHATTY :
                                                    TOPIC_WX_NORMAL;
        break;
    case Topic::AGE:
        tmpl = persona_.style == BotStyle::TERSE ? TOPIC_AGE_TERSE : TOPIC_AGE_NORMAL;
        break;
    case Topic::CONDX:
        tmpl = TOPIC_CONDX_TMPL;
        break;
    }
    return substitute(tmpl);
}

std::string CWChatbot::build_closing_phrase() const {
    switch (persona_.style) {
        case BotStyle::TERSE:  return const_cast<CWChatbot*>(this)->substitute(CLOSING_TERSE);
        case BotStyle::CHATTY: return const_cast<CWChatbot*>(this)->substitute(CLOSING_CHATTY);
        default:               return const_cast<CWChatbot*>(this)->substitute(CLOSING_NORMAL);
    }
}

std::string CWChatbot::build_final_phrase() const {
    return "TU 73 " + oper_data_.name + " EE";
}

std::string CWChatbot::build_prompt_phrase() const {
    const std::string& oc = oper_data_.callsign.empty()
                            ? operator_call_ : oper_data_.callsign;
    return oc + "?";
}

std::string CWChatbot::build_ack_phrase() {
    std::string phrase;
    // Reactive inserts
    if (!oper_data_.pwr.empty() &&
        (oper_data_.pwr.find("5W") != std::string::npos ||
         oper_data_.pwr.find("QRP") != std::string::npos))
        phrase += "FB QRP OM = ";
    if (!oper_data_.rst.empty() && oper_data_.rst.size() >= 2 && oper_data_.rst[1] >= '7')
        phrase += "FB SIG = ";

    switch (persona_.style) {
        case BotStyle::TERSE:  phrase += rng_pick(ACKS_TERSE, 2); break;
        case BotStyle::CHATTY: phrase += rng_pick(ACKS_CHATTY, 3); break;
        default:               phrase += rng_pick(ACKS_NORMAL, 3); break;
    }
    return phrase;
}

std::string CWChatbot::build_repeat_phrase(const std::string& key) const {
    if (key.empty())  return last_bot_phrase_;
    if (key == "NAME") return "NAME " + persona_.name + " " + persona_.name;
    if (key == "QTH")  return "QTH " + persona_.qth + " " + persona_.qth;
    if (key == "RST")  return "RST " + persona_.rst + " " + persona_.rst;
    if (key == "RIG")  return "RIG " + persona_.rig + " " + persona_.rig;
    if (key == "ANT")  return "ANT " + persona_.ant;
    if (key == "WX")   return "WX " + persona_.wx;
    return last_bot_phrase_;
}

// ═════════════════════════════════════════════════════════════════════════════
// Input Parsing
// ═════════════════════════════════════════════════════════════════════════════

bool CWChatbot::fuzzy_match(const std::string& a, const std::string& b) const {
    if (a == b) return true;
    if (std::abs((int)a.size() - (int)b.size()) > 1) return false;

    // 1 substitution
    if (a.size() == b.size()) {
        int diffs = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::toupper(a[i]) != std::toupper(b[i])) ++diffs;
        return diffs <= 1;
    }

    // 1 deletion
    const std::string& longer  = (a.size() > b.size()) ? a : b;
    const std::string& shorter = (a.size() > b.size()) ? b : a;
    int skips = 0;
    size_t j = 0;
    for (size_t i = 0; i < longer.size() && j < shorter.size(); ++i) {
        if (std::toupper(longer[i]) == std::toupper(shorter[j])) ++j;
        else ++skips;
    }
    return skips <= 1 && j == shorter.size();
}

bool CWChatbot::is_trigger(const std::string& word) const {
    for (int i = 0; i < NUM_TRIGGERS; ++i)
        if (word == TRIGGERS[i]) return true;
    return false;
}

bool CWChatbot::contains_trigger(const std::string& trigger) const {
    for (const auto& w : received_words_)
        if (w == trigger) return true;
    return false;
}

std::string CWChatbot::extract_until_trigger(
    const std::vector<std::string>& words, size_t start) const
{
    std::string result;
    for (size_t i = start; i < words.size(); ++i) {
        if (is_trigger(words[i])) break;
        if (!result.empty()) result += " ";
        result += words[i];
    }
    return result;
}

bool CWChatbot::has_over_end_signal() const {
    if (received_words_.empty()) return false;
    const auto& last = received_words_.back();
    return (last == "K" || last == "KN" || last == "AR" || last == "BK");
}

bool CWChatbot::words_look_like_exchange() const {
    for (const auto& w : received_words_)
        if (w == "RST" || w == "NAME" || w == "OP" || w == "QTH") return true;
    return false;
}

void CWChatbot::extract_operator_call(const std::vector<std::string>& words) {
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i] == "DE" && i + 1 < words.size()) {
            std::string cand = words[i + 1];
            if (!fuzzy_match(cand, persona_.callsign)) {
                oper_data_.callsign = cand;
                return;
            }
            if (i + 2 < words.size() && !is_trigger(words[i + 2])) {
                oper_data_.callsign = words[i + 2];
                return;
            }
        }
    }
    // Fallback: use configured operator call
    if (oper_data_.callsign.empty())
        oper_data_.callsign = operator_call_;
}

void CWChatbot::parse_exchange(const std::vector<std::string>& words) {
    for (size_t i = 0; i < words.size(); ++i) {
        const auto& w = words[i];
        if (w == "RST" && i + 1 < words.size()) {
            const auto& val = words[i + 1];
            if (val.size() == 3 &&
                std::all_of(val.begin(), val.end(), ::isdigit)) {
                oper_data_.rst = val;
                ++i;
            }
        } else if ((w == "NAME" || w == "OP") && i + 1 < words.size()) {
            if (!is_trigger(words[i + 1])) {
                oper_data_.name = words[i + 1];
                ++i;
            }
        } else if (w == "QTH" && i + 1 < words.size()) {
            std::string v = extract_until_trigger(words, i + 1);
            if (!v.empty()) oper_data_.qth = v;
        } else if (w == "RIG" && i + 1 < words.size()) {
            std::string v = extract_until_trigger(words, i + 1);
            if (!v.empty()) oper_data_.rig = v;
        } else if (w == "ANT" && i + 1 < words.size()) {
            std::string v = extract_until_trigger(words, i + 1);
            if (!v.empty()) oper_data_.ant = v;
        } else if (w == "PWR" && i + 1 < words.size()) {
            std::string v = extract_until_trigger(words, i + 1);
            if (!v.empty()) oper_data_.pwr = v;
        } else if (w == "WX" && i + 1 < words.size()) {
            std::string v = extract_until_trigger(words, i + 1);
            if (!v.empty()) oper_data_.wx = v;
        }
    }
    if (oper_data_.callsign.empty())
        extract_operator_call(words);
}

void CWChatbot::flush_current_word() {
    if (!current_word_.empty()) {
        std::string word;
        word.reserve(current_word_.size());
        for (char c : current_word_)
            word += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        received_words_.push_back(word);
        current_word_.clear();
    }
    // Only process immediately if an over-end signal is detected.
    // Otherwise, wait for sentence timeout in tick().
    if (has_over_end_signal())
        process_received_words();
}

void CWChatbot::process_received_words() {
    if (received_words_.empty()) return;

    // ── Global: QRS / QRQ ──────────────────────────────────────────
    if (contains_trigger("QRS")) {
        handle_qrs();
        // Remove QRS from words and continue
        received_words_.erase(
            std::remove(received_words_.begin(), received_words_.end(), "QRS"),
            received_words_.end());
        received_words_.erase(
            std::remove(received_words_.begin(), received_words_.end(), "PSE"),
            received_words_.end());
        if (received_words_.empty()) return;
    }
    if (contains_trigger("QRQ")) {
        handle_qrq();
        received_words_.erase(
            std::remove(received_words_.begin(), received_words_.end(), "QRQ"),
            received_words_.end());
        if (received_words_.empty()) return;
    }

    // ── Global: 73/SK → early close (only after initial exchange) ──
    if (state_ == QSOState::TOPIC_ROUNDS ||
        state_ == QSOState::WAIT_OPER_EXCHANGE) {
        if (contains_trigger("73") || contains_trigger("SK")) {
            set_state(QSOState::CLOSING);
            closing_phase_ = 0;
            bot_send(build_closing_phrase());
            if (event_cb_) event_cb_(QSOEvent::CLOSING);
            received_words_.clear();
            return;
        }
    }

    // ── Global: AGN / repeat request ───────────────────────────────
    if (contains_trigger("AGN") || contains_trigger("?")) {
        std::string topic_key;
        for (const auto& w : received_words_) {
            if (w == "NAME" || w == "QTH" || w == "RST" ||
                w == "RIG" || w == "ANT" || w == "WX") {
                topic_key = w;
                break;
            }
        }
        bot_send(build_repeat_phrase(topic_key));
        received_words_.clear();
        return;
    }

    // ── Per-state processing ───────────────────────────────────────
    switch (state_) {
    case QSOState::BOT_CQ: {
        // Look for operator answering: should contain bot's callsign
        bool found = false;
        for (const auto& w : received_words_)
            if (fuzzy_match(w, persona_.callsign)) { found = true; break; }
        if (found) {
            extract_operator_call(received_words_);
            if (event_cb_) event_cb_(QSOEvent::CQ_ANSWERED);
            set_state(QSOState::BOT_EXCHANGE);
            bot_send(build_exchange_phrase());
            if (event_cb_) event_cb_(QSOEvent::EXCHANGE_SENT);
        }
        received_words_.clear();
        break;
    }

    case QSOState::WAIT_FOR_OPERATOR:
        if (contains_trigger("CQ")) {
            extract_operator_call(received_words_);
            if (event_cb_) event_cb_(QSOEvent::OPERATOR_CQ);
            set_state(QSOState::BOT_ANSWER_CQ);
            bot_send(build_answer_cq_phrase());
        }
        received_words_.clear();
        break;

    case QSOState::BOT_ANSWER_CQ:
        // Waiting for operator's exchange after we answered their CQ
        if (has_over_end_signal() || words_look_like_exchange()) {
            parse_exchange(received_words_);
            if (event_cb_) event_cb_(QSOEvent::EXCHANGE_RECEIVED);
            set_state(QSOState::BOT_EXCHANGE);
            bot_send(build_exchange_phrase());
            if (event_cb_) event_cb_(QSOEvent::EXCHANGE_SENT);
        }
        received_words_.clear();
        break;

    case QSOState::WAIT_OPER_EXCHANGE:
        if (has_over_end_signal() || words_look_like_exchange()) {
            parse_exchange(received_words_);
            if (event_cb_) event_cb_(QSOEvent::EXCHANGE_RECEIVED);
            proceed_after_exchange();
        }
        received_words_.clear();
        break;

    case QSOState::TOPIC_ROUNDS:
        if (waiting_for_oper_topic_) {
            if (has_over_end_signal() || received_words_.size() >= 2) {
                parse_exchange(received_words_);
                waiting_for_oper_topic_ = false;
                send_next_topic_or_close();
            }
        }
        received_words_.clear();
        break;

    case QSOState::CLOSING:
        // Any substantive response triggers final dit-dit
        if (!received_words_.empty()) {
            bot_send(build_final_phrase());
            closing_phase_ = 1;
        }
        received_words_.clear();
        break;

    default:
        received_words_.clear();
        break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Speed Management
// ═════════════════════════════════════════════════════════════════════════════

void CWChatbot::handle_qrs() {
    wpm_ = std::max(5, wpm_ - 3);
    if (speed_cb_) speed_cb_(wpm_);
    bot_send("R QRS");
}

void CWChatbot::handle_qrq() {
    wpm_ = std::min(40, wpm_ + 3);
    if (speed_cb_) speed_cb_(wpm_);
    bot_send("R QRQ");
}

// ═════════════════════════════════════════════════════════════════════════════
// Sending / State helpers
// ═════════════════════════════════════════════════════════════════════════════

void CWChatbot::bot_send(const std::string& phrase) {
    last_bot_phrase_ = phrase;
    transmitting_    = true;
    last_activity_time_ = millis_cb_();
    if (send_cb_) send_cb_(phrase);
}

void CWChatbot::set_state(QSOState s) {
    state_           = s;
    state_enter_time_ = millis_cb_();
    prompt_sent_     = false;
    received_words_.clear();
    current_word_.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Topic Management
// ═════════════════════════════════════════════════════════════════════════════

void CWChatbot::init_topic_queue() {
    topic_queue_.clear();
    std::vector<Topic> all = {
        Topic::RIG, Topic::ANT, Topic::PWR, Topic::WX, Topic::AGE, Topic::CONDX};
    std::shuffle(all.begin(), all.end(), rng_);

    int n = 0;
    switch (qso_depth_) {
        case QSODepth::MINIMAL:  n = 0; break;
        case QSODepth::STANDARD: n = rng_range(1, 3); break;
        case QSODepth::RAGCHEW:  n = rng_range(3, 7); break;
    }
    for (int i = 0; i < n && i < (int)all.size(); ++i)
        topic_queue_.push_back(all[i]);
    topic_index_ = 0;
}

void CWChatbot::proceed_after_exchange() {
    if (topic_queue_.empty()) {
        set_state(QSOState::CLOSING);
        closing_phase_ = 0;
        bot_send(build_closing_phrase());
        if (event_cb_) event_cb_(QSOEvent::CLOSING);
    } else {
        set_state(QSOState::TOPIC_ROUNDS);
        std::string phrase = build_ack_phrase() + " = " +
                             build_topic_phrase(topic_queue_[topic_index_]);
        bot_send(phrase);
        if (event_cb_) event_cb_(QSOEvent::TOPIC_SENT);
        topic_index_++;
        waiting_for_oper_topic_ = true;
    }
}

void CWChatbot::send_next_topic_or_close() {
    if (topic_index_ < (int)topic_queue_.size()) {
        std::string phrase = build_ack_phrase() + " = " +
                             build_topic_phrase(topic_queue_[topic_index_]);
        bot_send(phrase);
        if (event_cb_) event_cb_(QSOEvent::TOPIC_SENT);
        topic_index_++;
        waiting_for_oper_topic_ = true;
    } else {
        set_state(QSOState::CLOSING);
        closing_phase_ = 0;
        bot_send(build_closing_phrase());
        if (event_cb_) event_cb_(QSOEvent::CLOSING);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Runtime
// ═════════════════════════════════════════════════════════════════════════════

void CWChatbot::start() {
    generate_persona();
    oper_data_ = OperatorData{};
    oper_data_.name = "OM";
    received_words_.clear();
    current_word_.clear();
    cq_count_     = 0;
    closing_phase_ = 0;
    prompt_sent_  = false;
    topic_index_  = 0;
    waiting_for_oper_topic_ = false;
    last_activity_time_ = millis_cb_();

    init_topic_queue();

    float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_);
    if (roll < bot_init_prob_) {
        set_state(QSOState::BOT_CQ);
        bot_send(build_cq_phrase());
        if (event_cb_) event_cb_(QSOEvent::CQ_SENT);
        cq_count_    = 1;
        cq_sent_time_ = millis_cb_();
    } else {
        set_state(QSOState::WAIT_FOR_OPERATOR);
    }
}

void CWChatbot::stop() {
    set_state(QSOState::IDLE);
    transmitting_ = false;
}

void CWChatbot::symbol_received(const std::string& symbol) {
    last_activity_time_ = millis_cb_();
    last_symbol_time_   = millis_cb_();

    if (symbol == " " || symbol == "  ") {
        flush_current_word();
    } else {
        current_word_ += symbol;
    }
}

void CWChatbot::transmission_complete() {
    transmitting_ = false;
    last_activity_time_ = millis_cb_();

    switch (state_) {
    case QSOState::BOT_CQ:
        // Restart CQ retry timeout from play-end, not queue time.
        cq_sent_time_ = millis_cb_();
        break;
    case QSOState::BOT_EXCHANGE:
        set_state(QSOState::WAIT_OPER_EXCHANGE);
        break;
    case QSOState::CLOSING:
        if (closing_phase_ >= 1) {
            if (event_cb_) event_cb_(QSOEvent::QSO_COMPLETE);
            set_state(QSOState::IDLE);
        }
        break;
    default:
        break;
    }
}

void CWChatbot::tick() {
    const unsigned long now = millis_cb_();

    // Word-complete timeout: flush pending partial word
    if (!current_word_.empty() && (now - last_symbol_time_ > WORD_COMPLETE_MS))
        flush_current_word();

    // Sentence-complete timeout: process accumulated words after silence
    if (!received_words_.empty() && current_word_.empty() &&
        last_symbol_time_ > 0 && (now - last_symbol_time_ > WORD_COMPLETE_MS))
        process_received_words();

    // Global abandon
    if (state_ != QSOState::IDLE && !transmitting_ &&
        (now - last_activity_time_ > ABANDON_TIMEOUT_MS)) {
        if (event_cb_) event_cb_(QSOEvent::QSO_ABANDONED);
        set_state(QSOState::IDLE);
        return;
    }

    switch (state_) {
        case QSOState::IDLE:               tick_idle(); break;
        case QSOState::BOT_CQ:             tick_bot_cq(); break;
        case QSOState::WAIT_FOR_OPERATOR:  tick_wait_for_operator(); break;
        case QSOState::BOT_ANSWER_CQ:      tick_bot_answer_cq(); break;
        case QSOState::BOT_EXCHANGE:       tick_bot_exchange(); break;
        case QSOState::WAIT_OPER_EXCHANGE: tick_wait_oper_exchange(); break;
        case QSOState::TOPIC_ROUNDS:       tick_topic_rounds(); break;
        case QSOState::CLOSING:            tick_closing(); break;
    }
}

void CWChatbot::tick_idle() {}

void CWChatbot::tick_bot_cq() {
    if (transmitting_) return;
    const unsigned long now = millis_cb_();
    if (now - cq_sent_time_ < CQ_TIMEOUT_MS) return;

    if (cq_count_ >= MAX_CQ_REPEATS) {
        if (event_cb_) event_cb_(QSOEvent::QSO_ABANDONED);
        set_state(QSOState::IDLE);
        return;
    }
    bot_send(build_cq_phrase());
    if (event_cb_) event_cb_(QSOEvent::CQ_SENT);
    cq_count_++;
    cq_sent_time_ = now;
}

void CWChatbot::tick_wait_for_operator() {}

void CWChatbot::tick_bot_answer_cq() {
    // Waiting for transmission to complete or for operator exchange
    if (transmitting_) return;
    const unsigned long now = millis_cb_();
    if (!prompt_sent_ && (now - state_enter_time_ > OVER_TIMEOUT_MS)) {
        bot_send(build_prompt_phrase());
        prompt_sent_ = true;
    }
}

void CWChatbot::tick_bot_exchange() {
    // Transmission in progress; transmission_complete() handles transition
}

void CWChatbot::tick_wait_oper_exchange() {
    if (transmitting_) return;
    const unsigned long now = millis_cb_();
    if (!prompt_sent_ && (now - state_enter_time_ > OVER_TIMEOUT_MS)) {
        bot_send(build_prompt_phrase());
        prompt_sent_ = true;
    }
}

void CWChatbot::tick_topic_rounds() {
    if (transmitting_) return;
    const unsigned long now = millis_cb_();
    if (waiting_for_oper_topic_ && !prompt_sent_ &&
        (now - state_enter_time_ > OVER_TIMEOUT_MS)) {
        // Operator is quiet during topic round; nudge or just proceed
        waiting_for_oper_topic_ = false;
        send_next_topic_or_close();
    }
}

void CWChatbot::tick_closing() {
    if (transmitting_) return;
    const unsigned long now = millis_cb_();
    // If we sent closing and operator hasn't responded, send final after timeout
    if (closing_phase_ == 0 && (now - state_enter_time_ > OVER_TIMEOUT_MS)) {
        bot_send(build_final_phrase());
        closing_phase_ = 1;
    }
}
