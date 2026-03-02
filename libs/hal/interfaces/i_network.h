#pragma once

#include <cstdint>
#include <cstddef>

struct WifiNetwork {
    char   ssid[33];   // null-terminated, max 32 chars
    int8_t rssi;       // signal strength in dBm
};

enum class CwProto {
    ESP_NOW,  // low-latency local peer-to-peer
    UDP,      // IP UDP unicast
    TCP,      // IP TCP stream (iCW, VBand)
};

// WiFi and CW transport abstraction.
//
// Both Pocket and M5Core2 use the ESP32 WiFi stack.
// Native target stub: wifi_is_connected() always returns false.
class INetwork
{
public:
    virtual ~INetwork() = default;

    // ── WiFi ─────────────────────────────────────────────────────────────────

    // Connect to an access point; blocks until connected or timeout.
    virtual bool wifi_connect(const char* ssid, const char* password,
                              uint32_t timeout_ms) = 0;

    // Start a SoftAP.
    virtual bool wifi_start_ap(const char* ssid, const char* password) = 0;

    virtual void wifi_disconnect() = 0;
    virtual bool wifi_is_connected() = 0;

    // Writes null-terminated IP string into buf.
    virtual void wifi_get_ip(char* buf, size_t len) = 0;

    // Scan for visible networks.  Populates results[0..max_results-1].
    // Returns count found (may be 0).  Blocks until scan completes.
    virtual int wifi_scan(WifiNetwork* results, int max_results) = 0;

    // ── CW transport ──────────────────────────────────────────────────────────

    virtual bool cw_connect(CwProto proto,
                            const char* target, uint16_t port) = 0;
    virtual void cw_disconnect() = 0;
    virtual bool cw_send(const uint8_t* data, size_t len) = 0;

    // Returns bytes received, 0 on timeout, -1 on error.
    virtual int  cw_receive(uint8_t* buf, size_t len,
                            uint32_t timeout_ms) = 0;

    virtual bool cw_is_connected() = 0;
};
