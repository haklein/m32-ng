#pragma once

#ifdef BOARD_POCKETWROOM

#include "../interfaces/i_network.h"

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

    // ── CW transport (stubs — filled in when transceiver is implemented) ─────
    bool cw_connect(CwProto, const char*, uint16_t) override { return false; }
    void cw_disconnect() override {}
    bool cw_send(const uint8_t*, size_t) override { return false; }
    int  cw_receive(uint8_t*, size_t, uint32_t) override { return 0; }
    bool cw_is_connected() override { return false; }

private:
    bool ap_active_ = false;
};

#endif // BOARD_POCKETWROOM
