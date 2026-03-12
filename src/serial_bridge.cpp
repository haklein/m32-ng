#ifdef BOARD_POCKETWROOM

#include "serial_bridge.h"
#include "config_api.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>

static String s_line_buf;

void serial_bridge_init()
{
    s_line_buf.reserve(256);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

// Extract query parameter value: "/api/mode?m=keyer" → "keyer"
static String query_param(const String& path, const char* key)
{
    String prefix = String(key) + "=";
    int qpos = path.indexOf('?');
    if (qpos < 0) return String();
    String qs = path.substring(qpos + 1);
    int kpos = qs.indexOf(prefix);
    if (kpos < 0) return String();
    int vstart = kpos + prefix.length();
    int vend = qs.indexOf('&', vstart);
    return (vend < 0) ? qs.substring(vstart) : qs.substring(vstart, vend);
}

// URL-decode in place (minimal: just %20 → space, %25 → %)
static String url_decode(const String& s)
{
    String out;
    out.reserve(s.length());
    for (unsigned i = 0; i < s.length(); i++) {
        if (s[i] == '%' && i + 2 < s.length()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

static void reply(const char* json)
{
    Serial.println(json);
}

static void reply_ok(bool ok)
{
    reply(ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void reply_free(char* json)
{
    if (json) { Serial.println(json); free(json); }
    else      { reply("null"); }
}

// ── Dispatch ─────────────────────────────────────────────────────────────────

static void dispatch(const String& method, const String& path, const String& body)
{
    // Strip query string for path matching
    String base = path;
    int qpos = path.indexOf('?');
    if (qpos >= 0) base = path.substring(0, qpos);

    // --- GET endpoints ---
    if (method == "GET") {
        if (base == "/api/status") {
            StatusInfo s = config_get_status();
            char buf[200];
            snprintf(buf, sizeof(buf),
                "{\"mode\":\"%s\",\"paused\":%s,\"wpm\":%d,"
                "\"decoder_signal\":%d,\"decoder_wpm\":%d}",
                s.mode, s.paused ? "true" : "false", s.wpm,
                s.decoder_signal, s.decoder_wpm);
            reply(buf);
        }
        else if (base == "/api/config") {
            reply_free(config_settings_to_json());
        }
        else if (base == "/api/meta") {
            const FieldMeta* meta;
            int count = config_get_field_meta(&meta);
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < count; i++) {
                JsonObject f = arr.add<JsonObject>();
                f["key"] = meta[i].key;
                f["label"] = meta[i].label;
                f["group"] = meta[i].group;
                f["type"] = (meta[i].type == FieldType::INT)    ? "int" :
                            (meta[i].type == FieldType::BOOL)   ? "bool" :
                            (meta[i].type == FieldType::ENUM)   ? "enum" :
                            (meta[i].type == FieldType::STRING) ? "string" : "?";
                f["min"] = meta[i].min_val;
                f["max"] = meta[i].max_val;
                if (meta[i].step > 0) f["step"] = meta[i].step;
                if (meta[i].options) f["options"] = meta[i].options;
            }
            String out;
            serializeJson(doc, out);
            Serial.println(out);
        }
        else if (base == "/api/version") {
#ifdef GIT_VERSION
            reply("{\"version\":\"" GIT_VERSION "\"}");
#else
            reply("{\"version\":\"unknown\"}");
#endif
        }
        else if (base == "/api/battery") {
            auto info = config_get_battery_info();
            char buf[160];
            snprintf(buf, sizeof(buf),
                "{\"percent\":%d,\"raw_mv\":%d,\"compensated_mv\":%d,"
                "\"comp_factor\":%.4f,\"charging\":%s}",
                info.percent, info.raw_mv, info.compensated_mv,
                info.comp_factor, info.charging ? "true" : "false");
            reply(buf);
        }
        else if (base == "/api/mode") {
            String m = query_param(path, "m");
            reply_ok(!m.isEmpty() && config_request_mode(m.c_str()));
        }
        else if (base == "/api/pause") {
            reply_ok(config_toggle_pause());
        }
        else if (base == "/api/text") {
            char* text = config_get_text();
            if (!text) { reply("{\"text\":\"\"}"); return; }
            // JSON-escape
            String json = "{\"text\":\"";
            for (const char* p = text; *p; p++) {
                if (*p == '"') json += "\\\"";
                else if (*p == '\\') json += "\\\\";
                else if (*p == '\n') json += "\\n";
                else json += *p;
            }
            json += "\"}";
            free(text);
            Serial.println(json);
        }
        else if (base == "/api/slots") {
            char names[CONFIG_MAX_SLOTS][17] = {};
            int count = config_list_slots(names, CONFIG_MAX_SLOTS);
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < count; i++) arr.add(names[i]);
            String out;
            serializeJson(doc, out);
            Serial.println(out);
        }
        else if (base == "/api/slots/save") {
            String name = url_decode(query_param(path, "name"));
            reply_ok(!name.isEmpty() && config_save_slot(name.c_str()));
        }
        else if (base == "/api/slots/load") {
            String name = url_decode(query_param(path, "name"));
            if (name.isEmpty()) { reply_ok(false); return; }
            bool ok = config_load_slot(name.c_str());
            if (ok) { reply_free(config_settings_to_json()); }
            else    { reply_ok(false); }
        }
        else if (base == "/api/slots/delete") {
            String name = url_decode(query_param(path, "name"));
            reply_ok(!name.isEmpty() && config_delete_slot(name.c_str()));
        }
        else if (base == "/api/scan") {
            int n = WiFi.scanNetworks();
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < n; i++) {
                JsonObject net = arr.add<JsonObject>();
                net["ssid"] = WiFi.SSID(i);
                net["rssi"] = WiFi.RSSI(i);
            }
            WiFi.scanDelete();
            String out;
            serializeJson(doc, out);
            Serial.println(out);
        }
        else {
            reply("{\"error\":\"unknown endpoint\"}");
        }
    }
    // --- POST endpoints ---
    else if (method == "POST") {
        if (base == "/api/config") {
            if (body.length() == 0) { reply_ok(false); return; }
            bool ok = config_settings_from_json(body.c_str(), body.length());
            reply_ok(ok);
        }
        else if (base == "/api/send") {
            if (body.length() == 0) { reply_ok(false); return; }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err || !doc["text"].is<const char*>()) {
                reply("{\"error\":\"missing 'text' field\"}");
                return;
            }
            reply_ok(config_send_text(doc["text"].as<const char*>()));
        }
        else if (base == "/api/wifi") {
            if (body.length() == 0) { reply_ok(false); return; }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err || !doc["ssid"].is<const char*>()) {
                reply("{\"error\":\"missing 'ssid' field\"}");
                return;
            }
            const char* ssid = doc["ssid"].as<const char*>();
            const char* pass = doc["pass"].is<const char*>()
                             ? doc["pass"].as<const char*>() : "";
            reply_ok(config_set_wifi(ssid, pass));
        }
        else {
            reply("{\"error\":\"unknown endpoint\"}");
        }
    }
    else {
        reply("{\"error\":\"unknown method\"}");
    }
}

// ── Poll ─────────────────────────────────────────────────────────────────────

void serial_bridge_poll()
{
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_line_buf.length() == 0) continue;
            s_line_buf.trim();

            // Only process lines starting with GET or POST
            if (s_line_buf.startsWith("GET ") || s_line_buf.startsWith("POST ")) {
                // Parse: "METHOD /path [body]"
                int first_sp = s_line_buf.indexOf(' ');
                String method = s_line_buf.substring(0, first_sp);
                String rest = s_line_buf.substring(first_sp + 1);
                rest.trim();

                String path, body;
                // For POST, body is everything after the path (separated by space before '{')
                if (method == "POST") {
                    int brace = rest.indexOf('{');
                    if (brace > 0) {
                        path = rest.substring(0, brace);
                        path.trim();
                        body = rest.substring(brace);
                    } else {
                        path = rest;
                    }
                } else {
                    path = rest;
                }

                dispatch(method, path, body);
            }
            // else: ignore (ArduinoLog output, garbage, etc.)

            s_line_buf = "";
        } else {
            if (s_line_buf.length() < 1024)  // safety limit
                s_line_buf += c;
        }
    }
}

#endif // BOARD_POCKETWROOM
