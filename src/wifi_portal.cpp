#ifdef BOARD_EMBEDDED

#include "wifi_portal.h"

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <ArduinoLog.h>
#include <cstring>
#include <string>

// ── Embedded HTML ────────────────────────────────────────────────────────────

static const char HTML_HEAD[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Morserino WiFi</title>
<style>
body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}
h2{color:#333}
label{display:block;margin-top:12px;font-weight:bold}
input,select{width:100%;box-sizing:border-box;padding:10px;margin:6px 0;
  font-size:1em;border:1px solid #ccc;border-radius:4px}
button{width:100%;padding:14px;margin-top:16px;font-size:1.1em;
  background:#2a7;color:#fff;border:none;border-radius:4px;cursor:pointer}
button:hover{background:#196}
.rssi{color:#888;font-size:0.85em}
</style></head><body>
<h2>Morserino WiFi Setup</h2>
<form method="POST" action="/connect">
<label>Network</label>
<select name="ssid">)raw";

static const char HTML_TAIL[] PROGMEM = R"raw(</select>
<label>Password</label>
<input type="password" name="pass" placeholder="leave blank for open networks">
<button type="submit">Connect</button>
</form></body></html>)raw";

static const char HTML_OK[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Morserino WiFi</title></head><body>
<h2>Connecting...</h2>
<p>Check the device screen for status.</p>
</body></html>)raw";

// ── WifiPortal implementation ────────────────────────────────────────────────

WifiPortal::WifiPortal(const char* ap_ssid, INetwork& network,
                       PortalCredentialsCb on_credentials)
    : network_(network)
    , on_credentials_(std::move(on_credentials))
{
    strncpy(ap_ssid_, ap_ssid, 32);
    ap_ssid_[32] = '\0';
}

WifiPortal::~WifiPortal()
{
    if (running_) end();
}

std::string WifiPortal::build_page() const
{
    std::string page = HTML_HEAD;
    for (int i = 0; i < scan_count_; ++i) {
        page += "<option value=\"";
        page += networks_[i].ssid;
        page += "\">";
        page += networks_[i].ssid;
        page += " <span class=\"rssi\">(";
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", (int)networks_[i].rssi);
        page += rssi;
        page += " dBm)</span></option>\n";
    }
    page += HTML_TAIL;
    return page;
}

void WifiPortal::handle_root(void* req)
{
    auto* r = static_cast<AsyncWebServerRequest*>(req);
    std::string page = build_page();
    r->send(200, "text/html", page.c_str());
}

void WifiPortal::handle_connect(void* req)
{
    auto* r = static_cast<AsyncWebServerRequest*>(req);

    if (!r->hasParam("ssid", true)) {
        r->send(400, "text/plain", "Missing SSID");
        return;
    }

    std::string ssid = r->getParam("ssid", true)->value().c_str();
    std::string pass;
    if (r->hasParam("pass", true))
        pass = r->getParam("pass", true)->value().c_str();

    r->send(200, "text/html", HTML_OK);

    Log.noticeln("Portal: credentials received for '%s'", ssid.c_str());

    if (on_credentials_)
        on_credentials_(ssid.c_str(), pass.c_str());
}

void WifiPortal::begin()
{
    // 1. Scan before starting AP (better results in STA mode)
    scan_count_ = network_.wifi_scan(networks_, 20);
    Log.noticeln("Portal: found %d networks", scan_count_);

    // 2. Start SoftAP (open, no password)
    network_.wifi_start_ap(ap_ssid_, "");
    Log.noticeln("Portal: AP '%s' started, IP %s",
                 ap_ssid_, WiFi.softAPIP().toString().c_str());

    // 3. DNS — redirect all domains to our AP IP
    auto* dns = new DNSServer();
    dns->start(53, "*", WiFi.softAPIP());
    dns_ = dns;

    // 4. HTTP server
    auto* srv = new AsyncWebServer(80);

    // Captive portal detection paths (Apple, Android, Windows)
    auto root_handler = [this](AsyncWebServerRequest* r) { handle_root(r); };
    srv->on("/", HTTP_GET, root_handler);
    srv->on("/hotspot-detect.html", HTTP_GET, root_handler);
    srv->on("/generate_204", HTTP_GET, root_handler);
    srv->on("/ncsi.txt", HTTP_GET, root_handler);
    srv->on("/connecttest.txt", HTTP_GET, root_handler);

    srv->on("/connect", HTTP_POST,
            [this](AsyncWebServerRequest* r) { handle_connect(r); });

    srv->onNotFound(root_handler);

    srv->begin();
    server_ = srv;
    running_ = true;
}

void WifiPortal::end()
{
    if (!running_) return;

    if (server_) {
        auto* srv = static_cast<AsyncWebServer*>(server_);
        srv->end();
        delete srv;
        server_ = nullptr;
    }
    if (dns_) {
        auto* dns = static_cast<DNSServer*>(dns_);
        dns->stop();
        delete dns;
        dns_ = nullptr;
    }
    network_.wifi_disconnect();
    running_ = false;
}

void WifiPortal::loop()
{
    if (dns_)
        static_cast<DNSServer*>(dns_)->processNextRequest();
}

#endif // BOARD_EMBEDDED
