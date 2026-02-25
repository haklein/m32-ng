#pragma once

#ifdef BOARD_POCKETWROOM

// ESP32 NVS-backed storage via the Preferences library.
// File-system methods are stubs (LittleFS not yet wired).

#include "../interfaces/i_storage.h"
#include <Preferences.h>

class PocketStorage : public IStorage
{
public:
    bool get_int(const char* ns, const char* key, int32_t& val) override;
    void set_int(const char* ns, const char* key, int32_t val) override;

    bool get_string(const char* ns, const char* key,
                    char* buf, size_t len) override;
    void set_string(const char* ns, const char* key,
                    const char* val) override;

    bool get_blob(const char* ns, const char* key,
                  void* buf, size_t len) override;
    void set_blob(const char* ns, const char* key,
                  const void* buf, size_t len) override;

    void commit() override;

    // File system stubs — LittleFS partition not yet configured.
    bool file_open_read(const char*) override           { return false; }
    bool file_open_write(const char*) override          { return false; }
    int  file_read(uint8_t*, size_t) override           { return -1; }
    int  file_write(const uint8_t*, size_t) override    { return -1; }
    void file_close() override                          {}
    bool file_exists(const char*) override              { return false; }
    bool file_remove(const char*) override              { return false; }
    void list_files(const char*,
                    std::function<void(const char*, size_t)>) override {}

private:
    Preferences prefs_;
};

#endif // BOARD_POCKETWROOM
