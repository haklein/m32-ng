#pragma once

#ifdef BOARD_EMBEDDED

#include "../interfaces/i_network.h"
#include <WiFiUdp.h>

class Esp32Network : public INetwork
{
public:
    // ── WiFi ──────────────────────────────────────────────────────────────────
    bool wifi_connect(const char* ssid, const char* password,
                      uint32_t timeout_ms) override;
    bool wifi_start_ap(const char* ssid, const char* password) override;
    void wifi_disconnect() override;
    bool wifi_is_connected() override;
    void wifi_get_ip(char* buf, size_t len) override;
    int  wifi_scan(WifiNetwork* results, int max_results) override;

    // ── CW transport (UDP) ───────────────────────────────────────────────────
    bool cw_connect(CwProto proto, const char* target,
                    uint16_t port) override;
    void cw_disconnect() override;
    bool cw_send(const uint8_t* data, size_t len) override;
    int  cw_receive(uint8_t* buf, size_t len,
                    uint32_t timeout_ms) override;
    bool cw_is_connected() override;

private:
    bool      ap_active_      = false;
    WiFiUDP   udp_;
    bool      cw_connected_   = false;
    IPAddress cw_remote_ip_;
    uint16_t  cw_remote_port_ = 0;
};

#endif // BOARD_EMBEDDED
