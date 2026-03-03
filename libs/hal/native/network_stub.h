#pragma once

#ifdef NATIVE_BUILD

#include "../interfaces/i_network.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <poll.h>

// NativeNetwork: WiFi stubs + real POSIX UDP for CW transport.
// Allows the simulator to connect to real CWCom/MOPP servers.
class NativeNetwork : public INetwork
{
public:
    ~NativeNetwork() override { cw_disconnect(); }

    // WiFi stubs — simulator has no WiFi; pretend connected for CW to work.
    bool wifi_connect(const char*, const char*, uint32_t) override { return true; }
    bool wifi_start_ap(const char*, const char*) override          { return false; }
    void wifi_disconnect() override                                {}
    bool wifi_is_connected() override                              { return true; }
    void wifi_get_ip(char* buf, size_t len) override {
        if (buf && len) strncpy(buf, "127.0.0.1", len);
    }
    int  wifi_scan(WifiNetwork*, int) override                     { return 0; }

    // CW transport — real POSIX UDP
    bool cw_connect(CwProto, const char* target, uint16_t port) override
    {
        if (!target) return false;

        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);
        if (getaddrinfo(target, port_str, &hints, &res) != 0) return false;

        sock_ = socket(res->ai_family, SOCK_DGRAM, 0);
        if (sock_ < 0) { freeaddrinfo(res); return false; }

        // Store remote address
        memcpy(&remote_addr_, res->ai_addr, res->ai_addrlen);
        remote_len_ = res->ai_addrlen;
        freeaddrinfo(res);

        // Set non-blocking
        int flags = fcntl(sock_, F_GETFL, 0);
        fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

        // Bind to any local port
        struct sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = 0;
        bind(sock_, (struct sockaddr*)&local, sizeof(local));

        connected_ = true;
        return true;
    }

    void cw_disconnect() override
    {
        if (sock_ >= 0) { close(sock_); sock_ = -1; }
        connected_ = false;
    }

    bool cw_send(const uint8_t* data, size_t len) override
    {
        if (sock_ < 0) return false;
        ssize_t n = sendto(sock_, data, len, 0,
                           (struct sockaddr*)&remote_addr_, remote_len_);
        return n >= 0;
    }

    int cw_receive(uint8_t* buf, size_t len, uint32_t timeout_ms) override
    {
        if (sock_ < 0) return -1;

        struct pollfd pfd = { sock_, POLLIN, 0 };
        int ret = poll(&pfd, 1, (int)timeout_ms);
        if (ret <= 0) return 0;

        ssize_t n = recvfrom(sock_, buf, len, 0, nullptr, nullptr);
        return (n >= 0) ? (int)n : -1;
    }

    bool cw_is_connected() override { return connected_; }

private:
    int                sock_       = -1;
    bool               connected_  = false;
    struct sockaddr_in remote_addr_ = {};
    socklen_t          remote_len_  = 0;
};

#endif // NATIVE_BUILD
