#ifdef BOARD_POCKETWROOM

#include "network_esp32.h"
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <ArduinoLog.h>

bool Esp32Network::wifi_connect(const char* ssid, const char* password,
                                uint32_t timeout_ms)
{
    if (ap_active_) {
        WiFi.softAPdisconnect(true);
        ap_active_ = false;
    }
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    Log.noticeln("wifi_connect: ssid='%s' pass_len=%d", ssid, strlen(password));
    WiFi.begin(ssid, password);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= timeout_ms) {
            Log.warningln("wifi_connect: timeout (status=%d)", WiFi.status());
            WiFi.disconnect(true);
            return false;
        }
        delay(250);
    }
    Log.noticeln("wifi_connect: connected, IP=%s", WiFi.localIP().toString().c_str());
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

// ── CW transport (UDP) ───────────────────────────────────────────────────────

bool Esp32Network::cw_connect(CwProto proto, const char* target,
                               uint16_t port)
{
    if (!wifi_is_connected() || !target) return false;
    (void)proto;  // all CW transport uses UDP

    // DNS resolve
    IPAddress ip;
    if (!WiFi.hostByName(target, ip)) return false;

    // Bind local UDP socket (ephemeral port)
    if (!udp_.begin(0)) return false;

    cw_remote_ip_   = ip;
    cw_remote_port_ = port;
    cw_connected_   = true;
    return true;
}

void Esp32Network::cw_disconnect()
{
    udp_.stop();
    cw_connected_ = false;
}

bool Esp32Network::cw_send(const uint8_t* data, size_t len)
{
    if (!cw_connected_) return false;
    udp_.beginPacket(cw_remote_ip_, cw_remote_port_);
    udp_.write(data, len);
    return udp_.endPacket() != 0;
}

int Esp32Network::cw_receive(uint8_t* buf, size_t len, uint32_t timeout_ms)
{
    if (!cw_connected_) return -1;

    uint32_t start = millis();
    do {
        int pkt = udp_.parsePacket();
        if (pkt > 0) {
            int n = udp_.read(buf, len);
            return n;
        }
        if (timeout_ms == 0) return 0;
        delay(1);
    } while (millis() - start < timeout_ms);

    return 0;  // timeout
}

bool Esp32Network::cw_is_connected()
{
    return cw_connected_ && wifi_is_connected();
}

#endif // BOARD_POCKETWROOM
