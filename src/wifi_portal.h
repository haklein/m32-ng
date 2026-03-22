#pragma once

#ifdef BOARD_EMBEDDED

#include <functional>
#include "i_network.h"

// Called when the user submits WiFi credentials via the portal form.
using PortalCredentialsCb = std::function<void(const char* ssid,
                                               const char* password)>;

class WifiPortal
{
public:
    // ap_ssid: open AP name (e.g. "Morserino-XXXX").
    // network: used for scan and AP control.
    // on_credentials: fired (from async TCP task!) when form is submitted.
    WifiPortal(const char* ap_ssid, INetwork& network,
               PortalCredentialsCb on_credentials);
    ~WifiPortal();

    // Scan networks, start AP, start DNS redirect + HTTP server.
    void begin();

    // Stop HTTP, DNS, and disconnect AP.
    void end();

    // Pump DNS server — call from Arduino loop.
    void loop();

    int scanned_count() const { return scan_count_; }

private:
    void handle_root(void* request);
    void handle_connect(void* request);
    std::string build_page() const;

    char                ap_ssid_[33];
    INetwork&           network_;
    PortalCredentialsCb on_credentials_;

    int                 scan_count_ = 0;
    WifiNetwork         networks_[20];

    void*               server_ = nullptr;   // ESPAsyncWebServer*
    void*               dns_    = nullptr;   // DNSServer*
    bool                running_ = false;
};

#endif // BOARD_EMBEDDED
