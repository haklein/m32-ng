#ifdef BOARD_POCKETWROOM

#include "storage_nvs.h"

// Each method opens the NVS namespace, performs the operation, then closes it.
// This avoids holding the namespace open across calls and is safe for the
// low-frequency access pattern of user settings.

bool PocketStorage::get_int(const char* ns, const char* key, int32_t& val)
{
    if (!prefs_.begin(ns, /*readOnly=*/true)) return false;
    bool found = prefs_.isKey(key);
    if (found) val = prefs_.getInt(key, 0);
    prefs_.end();
    return found;
}

void PocketStorage::set_int(const char* ns, const char* key, int32_t val)
{
    if (!prefs_.begin(ns, /*readOnly=*/false)) return;
    prefs_.putInt(key, val);
    prefs_.end();
}

bool PocketStorage::get_string(const char* ns, const char* key,
                               char* buf, size_t len)
{
    if (!prefs_.begin(ns, /*readOnly=*/true)) return false;
    bool found = prefs_.isKey(key);
    if (found) {
        String s = prefs_.getString(key, "");
        size_t copy = s.length() < len ? s.length() : len - 1;
        memcpy(buf, s.c_str(), copy);
        buf[copy] = '\0';
    }
    prefs_.end();
    return found;
}

void PocketStorage::set_string(const char* ns, const char* key,
                               const char* val)
{
    if (!prefs_.begin(ns, /*readOnly=*/false)) return;
    prefs_.putString(key, val);
    prefs_.end();
}

size_t PocketStorage::get_blob(const char* ns, const char* key,
                               void* buf, size_t len)
{
    if (!prefs_.begin(ns, /*readOnly=*/true)) return 0;
    size_t bytes_read = 0;
    if (prefs_.isKey(key)) {
        size_t stored = prefs_.getBytesLength(key);
        size_t to_read = stored < len ? stored : len;
        bytes_read = prefs_.getBytes(key, buf, to_read);
    }
    prefs_.end();
    return bytes_read;
}

void PocketStorage::set_blob(const char* ns, const char* key,
                             const void* buf, size_t len)
{
    if (!prefs_.begin(ns, /*readOnly=*/false)) return;
    prefs_.putBytes(key, buf, len);
    prefs_.end();
}

void PocketStorage::commit()
{
    // ESP32 Preferences writes are committed on each putXxx() call.
    // This method exists for interfaces that batch writes.
}

#endif // BOARD_POCKETWROOM
