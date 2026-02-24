#pragma once
// Static data tables for CW QSO Chatbot persona generation.
// Internal header — included only by cw_chatbot.cpp.

// ── Country data ────────────────────────────────────────────────────────────
struct CountryData {
    const char* const* prefixes;
    int num_prefixes;
    const char* const* names;
    int num_names;
    const char* const* cities;
    int num_cities;
};

// USA
static const char* const USA_PREFIXES[] = {
    "W", "K", "N", "AA", "AB", "AC", "AD", "AE", "AF", "AG", "AH", "AI", "AJ", "AK"};
static const char* const USA_NAMES[] = {
    "JOHN", "BOB", "MIKE", "JIM", "TOM", "BILL", "DAVE", "STEVE", "RICK", "DAN",
    "GARY", "MARK", "JEFF", "PAUL", "SCOTT", "LARRY", "DON", "ED", "FRED", "CHUCK"};
static const char* const USA_CITIES[] = {
    "BOSTON", "CHICAGO", "DENVER", "DALLAS", "SEATTLE", "PORTLAND", "PHOENIX",
    "ATLANTA", "MIAMI", "NEW YORK", "LOS ANGELES", "SAN FRANCISCO", "ST LOUIS",
    "HOUSTON", "MINNEAPOLIS", "DETROIT", "CLEVELAND", "PITTSBURGH", "TAMPA", "RALEIGH"};

// Canada
static const char* const CA_PREFIXES[] = {"VE", "VA"};
static const char* const CA_NAMES[] = {
    "PIERRE", "MARC", "DAVE", "BRIAN", "PAUL", "RICK", "JEAN", "ROBERT", "CLAUDE", "ANDRE"};
static const char* const CA_CITIES[] = {
    "TORONTO", "MONTREAL", "VANCOUVER", "OTTAWA", "CALGARY", "WINNIPEG",
    "HALIFAX", "EDMONTON", "QUEBEC CITY", "VICTORIA"};

// England
static const char* const UK_PREFIXES[] = {"G", "M", "2E"};
static const char* const UK_NAMES[] = {
    "DAVID", "PETER", "JAMES", "ALAN", "JOHN", "COLIN", "NIGEL", "GRAHAM", "BARRY", "ROGER"};
static const char* const UK_CITIES[] = {
    "LONDON", "MANCHESTER", "BRISTOL", "BIRMINGHAM", "LEEDS", "CAMBRIDGE",
    "OXFORD", "GLASGOW", "EDINBURGH", "CARDIFF"};

// Germany
static const char* const DL_PREFIXES[] = {
    "DL", "DJ", "DK", "DA", "DB", "DC", "DD", "DF", "DG", "DH"};
static const char* const DL_NAMES[] = {
    "HANS", "KARL", "PETER", "FRITZ", "KLAUS", "HEINZ", "JUERGEN", "HELMUT", "DIETER", "MANFRED"};
static const char* const DL_CITIES[] = {
    "BERLIN", "MUNICH", "HAMBURG", "FRANKFURT", "COLOGNE", "STUTTGART",
    "DRESDEN", "LEIPZIG", "NUREMBERG", "HANNOVER"};

// France
static const char* const F_PREFIXES[] = {"F"};
static const char* const F_NAMES[] = {
    "JEAN", "PIERRE", "MICHEL", "JACQUES", "ALAIN", "BERNARD", "PHILIPPE",
    "PATRICK", "CLAUDE", "CHRISTIAN"};
static const char* const F_CITIES[] = {
    "PARIS", "LYON", "MARSEILLE", "TOULOUSE", "BORDEAUX", "NICE", "NANTES",
    "STRASBOURG", "LILLE", "MONTPELLIER"};

// Italy
static const char* const I_PREFIXES[] = {"I", "IK", "IZ", "IU"};
static const char* const I_NAMES[] = {
    "MARCO", "LUIGI", "CARLO", "GIOVANNI", "ROBERTO", "PAOLO", "FRANCESCO",
    "GIUSEPPE", "ANTONIO", "ANDREA"};
static const char* const I_CITIES[] = {
    "ROME", "MILAN", "NAPLES", "TURIN", "FLORENCE", "BOLOGNA", "GENOA",
    "VENICE", "PALERMO", "VERONA"};

// Japan
static const char* const JA_PREFIXES[] = {"JA", "JH", "JR", "JE", "JF", "JG", "JI"};
static const char* const JA_NAMES[] = {
    "TARO", "HIRO", "YUKI", "MASA", "KENJI", "TAKASHI", "AKIRA", "SHIN", "NORI", "KAZU"};
static const char* const JA_CITIES[] = {
    "TOKYO", "OSAKA", "NAGOYA", "SAPPORO", "FUKUOKA", "KOBE", "KYOTO",
    "YOKOHAMA", "SENDAI", "HIROSHIMA"};

// Australia
static const char* const VK_PREFIXES[] = {"VK"};
static const char* const VK_NAMES[] = {
    "BRUCE", "STEVE", "MARK", "PETER", "DAVID", "PAUL", "CHRIS", "JOHN", "ANDREW", "MICHAEL"};
static const char* const VK_CITIES[] = {
    "SYDNEY", "MELBOURNE", "PERTH", "BRISBANE", "ADELAIDE", "HOBART",
    "DARWIN", "CANBERRA", "GOLD COAST", "NEWCASTLE"};

// ── Country table ───────────────────────────────────────────────────────────
#define COUNTRY_ENTRY(pfx, nam, cit) \
    { pfx, (int)(sizeof(pfx)/sizeof(pfx[0])), \
      nam, (int)(sizeof(nam)/sizeof(nam[0])), \
      cit, (int)(sizeof(cit)/sizeof(cit[0])) }

static const CountryData COUNTRIES[] = {
    COUNTRY_ENTRY(USA_PREFIXES, USA_NAMES, USA_CITIES),
    COUNTRY_ENTRY(CA_PREFIXES,  CA_NAMES,  CA_CITIES),
    COUNTRY_ENTRY(UK_PREFIXES,  UK_NAMES,  UK_CITIES),
    COUNTRY_ENTRY(DL_PREFIXES,  DL_NAMES,  DL_CITIES),
    COUNTRY_ENTRY(F_PREFIXES,   F_NAMES,   F_CITIES),
    COUNTRY_ENTRY(I_PREFIXES,   I_NAMES,   I_CITIES),
    COUNTRY_ENTRY(JA_PREFIXES,  JA_NAMES,  JA_CITIES),
    COUNTRY_ENTRY(VK_PREFIXES,  VK_NAMES,  VK_CITIES),
};
static constexpr int NUM_COUNTRIES = sizeof(COUNTRIES) / sizeof(COUNTRIES[0]);

#undef COUNTRY_ENTRY

// ── Rigs ────────────────────────────────────────────────────────────────────
static const char* const RIGS[] = {
    "IC7300", "IC7610", "IC7851", "FT991A", "FT710", "FTDX101",
    "TS590", "TS890", "K3S", "K4", "KX3", "KX2",
    "FLEX6600", "QCX", "QDX", "G90", "IC705", "FT818"};
static constexpr int NUM_RIGS = sizeof(RIGS) / sizeof(RIGS[0]);

static const char* const QRP_RIGS[] = {"KX2", "KX3", "FT818", "QCX", "QDX", "G90", "IC705"};
static constexpr int NUM_QRP_RIGS = sizeof(QRP_RIGS) / sizeof(QRP_RIGS[0]);

// ── Power ───────────────────────────────────────────────────────────────────
static const char* const POWERS[] = {"50W", "100W", "200W", "400W", "500W", "1KW"};
static constexpr int NUM_POWERS = sizeof(POWERS) / sizeof(POWERS[0]);
static const char* const QRP_POWERS[] = {"5W", "10W", "15W"};
static constexpr int NUM_QRP_POWERS = sizeof(QRP_POWERS) / sizeof(QRP_POWERS[0]);

// ── Antennas ────────────────────────────────────────────────────────────────
static const char* const ANTENNAS[] = {
    "DIPOLE", "INV VEE", "3 EL YAGI", "BEAM", "VERT", "GP", "EFHW",
    "LOOP", "OCF DIPOLE", "QUAD", "2 EL YAGI", "WIRE ANT", "MOBILE WHIP"};
static constexpr int NUM_ANTENNAS = sizeof(ANTENNAS) / sizeof(ANTENNAS[0]);

static const char* const HEIGHTS[] = {"UP 10M", "UP 15M", "UP 20M", "AT 5M", "UP 12M", "AT 8M"};
static constexpr int NUM_HEIGHTS = sizeof(HEIGHTS) / sizeof(HEIGHTS[0]);

// ── Weather ─────────────────────────────────────────────────────────────────
static const char* const WX_CONDITIONS[] = {
    "SUNNY", "CLDY", "RAIN", "SNOW", "WINDY", "FOG",
    "WARM", "COLD", "HOT", "MILD", "OVERCAST", "CLEAR"};
static constexpr int NUM_WX = sizeof(WX_CONDITIONS) / sizeof(WX_CONDITIONS[0]);

// ── Condition / band ratings ────────────────────────────────────────────────
static const char* const CONDX_RATINGS[] = {"GOOD", "FAIR", "POOR"};
static constexpr int NUM_CONDX = sizeof(CONDX_RATINGS) / sizeof(CONDX_RATINGS[0]);
static const char* const BAND_STATUS[] = {"OPEN", "QUIET", "BUSY"};
static constexpr int NUM_BAND = sizeof(BAND_STATUS) / sizeof(BAND_STATUS[0]);
