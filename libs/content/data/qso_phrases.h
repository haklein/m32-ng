#pragma once

// QSO phrase templates with {slot} placeholders for random substitution.
// Each phrase is a realistic fragment of a ham radio CW QSO, suitable for
// generator and echo training.

// ── Templates ──────────────────────────────────────────────────────────────
static const char* const QSO_TEMPLATES[] = {
    "NAME {name} {name}",
    "NAME IS {name}",
    "RST {rst} {rst}",
    "UR RST {rst}",
    "QTH {qth}",
    "QTH IS {qth}",
    "RIG {rig} PWR {pwr}",
    "RIG IS {rig}",
    "ANT {ant}",
    "ANT IS {ant}",
    "WX {wx} TEMP {temp} C",
    "WX HR {wx}",
    "CQ CQ DE {call} K",
    "CQ CQ CQ DE {call} {call} K",
    "{call} DE {call} K",
    "TNX FER CALL",
    "TNX FER QSO 73",
    "73 {name} DE {call}",
    "GM OM TNX FER CALL",
    "GA OM TNX FER CALL",
    "GE OM TNX FER CALL",
    "HW CPY?",
    "HPE CU AGN 73",
    "GL ES 73",
    "PWR {pwr} INTO {ant}",
    "AGE {age} LIC {lic} YRS",
    "CONDX {condx} ON {band}",
    "FB OM",
    "R R TNX",
};
static constexpr int NUM_QSO_TEMPLATES =
    (int)(sizeof(QSO_TEMPLATES) / sizeof(QSO_TEMPLATES[0]));

// ── Slot pools ─────────────────────────────────────────────────────────────
static const char* const QSO_NAMES[] = {
    "TOM", "JIM", "HANS", "KARL", "PETER", "FRITZ", "MIKE", "BOB",
    "DAVE", "STEVE", "JOHN", "BILL", "MARCO", "LUIGI", "JEAN",
    "PIERRE", "DAVID", "ALAN", "TARO", "HIRO", "BRUCE", "MARK",
    "PAUL", "RICK", "SCOTT", "ED", "FRED", "KLAUS", "HEINZ"};
static constexpr int NUM_QSO_NAMES =
    (int)(sizeof(QSO_NAMES) / sizeof(QSO_NAMES[0]));

static const char* const QSO_CITIES[] = {
    "BERLIN", "MUNICH", "LONDON", "PARIS", "ROME", "TOKYO", "VIENNA",
    "BOSTON", "CHICAGO", "DENVER", "SYDNEY", "TORONTO", "OSLO",
    "MADRID", "PRAGUE", "DUBLIN", "BRUSSELS", "AMSTERDAM", "ZURICH",
    "HAMBURG", "MILAN", "OSAKA", "SEATTLE", "PORTLAND", "ATLANTA"};
static constexpr int NUM_QSO_CITIES =
    (int)(sizeof(QSO_CITIES) / sizeof(QSO_CITIES[0]));

static const char* const QSO_RIGS[] = {
    "IC7300", "IC7610", "FT991A", "FT710", "TS590", "TS890",
    "K3S", "K4", "KX3", "KX2", "FLEX6600", "IC705", "FT818",
    "QCX", "QDX", "G90"};
static constexpr int NUM_QSO_RIGS =
    (int)(sizeof(QSO_RIGS) / sizeof(QSO_RIGS[0]));

static const char* const QSO_POWERS[] = {
    "5W", "10W", "25W", "50W", "100W", "200W", "400W", "1KW"};
static constexpr int NUM_QSO_POWERS =
    (int)(sizeof(QSO_POWERS) / sizeof(QSO_POWERS[0]));

static const char* const QSO_ANTENNAS[] = {
    "DIPOLE", "INV VEE", "YAGI", "VERT", "GP", "EFHW",
    "LOOP", "OCF DIPOLE", "QUAD", "WIRE ANT", "HEXBEAM"};
static constexpr int NUM_QSO_ANTENNAS =
    (int)(sizeof(QSO_ANTENNAS) / sizeof(QSO_ANTENNAS[0]));

static const char* const QSO_WX[] = {
    "SUNNY", "CLOUDY", "RAIN", "SNOW", "WINDY", "FOG",
    "WARM", "COLD", "HOT", "MILD", "CLEAR"};
static constexpr int NUM_QSO_WX =
    (int)(sizeof(QSO_WX) / sizeof(QSO_WX[0]));

static const char* const QSO_RST[] = {
    "559", "569", "579", "589", "599"};
static constexpr int NUM_QSO_RST =
    (int)(sizeof(QSO_RST) / sizeof(QSO_RST[0]));

static const char* const QSO_CONDX[] = {"GOOD", "FAIR", "POOR"};
static constexpr int NUM_QSO_CONDX =
    (int)(sizeof(QSO_CONDX) / sizeof(QSO_CONDX[0]));

static const char* const QSO_BANDS[] = {
    "80M", "40M", "30M", "20M", "17M", "15M", "12M", "10M"};
static constexpr int NUM_QSO_BANDS =
    (int)(sizeof(QSO_BANDS) / sizeof(QSO_BANDS[0]));
