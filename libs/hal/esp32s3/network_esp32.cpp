#ifdef BOARD_POCKETWROOM

#include "network_esp32.h"
#include <WiFi.h>
#include <algorithm>
#include <cstring>

bool Esp32Network::wifi_connect(const char* ssid, const char* password,
                                uint32_t timeout_ms)
{
    if (ap_active_) {
        WiFi.softAPdisconnect(true);
        ap_active_ = false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= timeout_ms) {
            WiFi.disconnect(true);
            return false;
        }
        delay(250);
    }
    return true;
}

bool Esp32Network::wifi_start_ap(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(ssid,
                           (password && password[0]) ? password : nullptr);
    ap_active_ = ok;
    return ok;
}

void Esp32Network::wifi_disconnect()
{
    if (ap_active_) {
        WiFi.softAPdisconnect(true);
        ap_active_ = false;
    } else {
        WiFi.disconnect(true);
    }
    WiFi.mode(WIFI_OFF);
}

bool Esp32Network::wifi_is_connected()
{
    return WiFi.status() == WL_CONNECTED;
}

void Esp32Network::wifi_get_ip(char* buf, size_t len)
{
    if (!buf || len == 0) return;
    String ip = ap_active_ ? WiFi.softAPIP().toString()
                           : WiFi.localIP().toString();
    strncpy(buf, ip.c_str(), len - 1);
    buf[len - 1] = '\0';
}

int Esp32Network::wifi_scan(WifiNetwork* results, int max_results)
{
    if (!results || max_results <= 0) return 0;

    int n = WiFi.scanNetworks();
    if (n <= 0) return 0;

    int count = std::min(n, max_results);
    for (int i = 0; i < count; ++i) {
        strncpy(results[i].ssid, WiFi.SSID(i).c_str(), 32);
        results[i].ssid[32] = '\0';
        results[i].rssi = static_cast<int8_t>(WiFi.RSSI(i));
    }
    WiFi.scanDelete();
    return count;
}

#endif // BOARD_POCKETWROOM
