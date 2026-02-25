#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

// Persistent storage: key-value preferences + file system.
//
// Pocket / M5Core2 target: ESP32 NVS (Preferences library) for KV;
//                          LittleFS for files.
// Native target: in-memory map for KV; std::fstream for files.
class IStorage
{
public:
    virtual ~IStorage() = default;

    // ── Key-value (NVS / Preferences) ────────────────────────────────────────

    // Returns false if the key does not exist.
    virtual bool get_int(const char* ns, const char* key, int32_t& val) = 0;
    virtual void set_int(const char* ns, const char* key, int32_t val)  = 0;

    virtual bool get_string(const char* ns, const char* key,
                            char* buf, size_t len) = 0;
    virtual void set_string(const char* ns, const char* key,
                            const char* val) = 0;

    // Binary blob — for serializing structs (e.g. AppSettings).
    // Returns false / no-op by default; ESP32 implementation uses putBytes/getBytes.
    virtual bool get_blob(const char* ns, const char* key,
                          void* buf, size_t len) { (void)ns; (void)key; (void)buf; (void)len; return false; }
    virtual void set_blob(const char* ns, const char* key,
                          const void* buf, size_t len) { (void)ns; (void)key; (void)buf; (void)len; }

    // Flush pending KV writes to non-volatile storage.
    virtual void commit() = 0;

    // ── File system (LittleFS) ────────────────────────────────────────────────

    virtual bool file_open_read(const char* path) = 0;
    virtual bool file_open_write(const char* path) = 0;

    // Returns bytes read/written, or -1 on error.
    virtual int  file_read(uint8_t* buf, size_t len)        = 0;
    virtual int  file_write(const uint8_t* buf, size_t len) = 0;

    virtual void file_close()                       = 0;
    virtual bool file_exists(const char* path)      = 0;
    virtual bool file_remove(const char* path)      = 0;

    // Enumerate files in directory; cb receives (name, size_bytes).
    virtual void list_files(const char* dir,
                            std::function<void(const char* name,
                                               size_t size)> cb) = 0;
};
