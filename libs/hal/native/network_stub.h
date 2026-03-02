#pragma once

#ifdef NATIVE_BUILD

#include "../interfaces/i_network.h"

// No-op INetwork for simulator and native test targets.
class NativeNetwork : public INetwork
{
public:
    bool wifi_connect(const char*, const char*, uint32_t) override { return false; }
    bool wifi_start_ap(const char*, const char*) override          { return false; }
    void wifi_disconnect() override                                {}
    bool wifi_is_connected() override                              { return false; }
    void wifi_get_ip(char* buf, size_t len) override {
        if (buf && len) buf[0] = '\0';
    }
    int  wifi_scan(WifiNetwork*, int) override                     { return 0; }

    bool cw_connect(CwProto, const char*, uint16_t) override       { return false; }
    void cw_disconnect() override                                  {}
    bool cw_send(const uint8_t*, size_t) override                  { return false; }
    int  cw_receive(uint8_t*, size_t, uint32_t) override           { return 0; }
    bool cw_is_connected() override                                { return false; }
};

#endif // NATIVE_BUILD
