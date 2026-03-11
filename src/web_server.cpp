#ifdef BOARD_POCKETWROOM

#include "web_server.h"
#include "config_api.h"
#include <ESPAsyncWebServer.h>
#include <lvgl.h>
#include "display/lv_display_private.h"
#include <ArduinoLog.h>
#include <ArduinoJson.h>
#include <cstring>
#include <mdns.h>
#include <esp_heap_caps.h>

// ── Screen geometry (logical, after rotation) ────────────────────────────────
static constexpr int SCR_W       = 320;
static constexpr int SCR_H       = 170;
static constexpr int BPP         = 2;       // RGB565
static constexpr int ROW_BYTES   = SCR_W * BPP;
static constexpr int PIXEL_BYTES = ROW_BYTES * SCR_H;  // 108,800
static constexpr int HDR_SIZE    = 14 + 40 + 12;       // BMP file+info+masks
static constexpr int BMP_SIZE    = HDR_SIZE + PIXEL_BYTES;

// ── State ────────────────────────────────────────────────────────────────────
static AsyncWebServer* s_server      = nullptr;
static uint8_t* s_ss_pixels         = nullptr;   // PSRAM shadow framebuffer
static lv_display_flush_cb_t s_orig_flush_cb = nullptr;

// ── BMP helpers ──────────────────────────────────────────────────────────────

static void put_le16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put_le32(uint8_t* p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void build_bmp_header(uint8_t* hdr)
{
    memset(hdr, 0, HDR_SIZE);
    hdr[0] = 'B'; hdr[1] = 'M';
    put_le32(hdr + 2,  BMP_SIZE);
    put_le32(hdr + 10, HDR_SIZE);
    put_le32(hdr + 14, 40);
    put_le32(hdr + 18, SCR_W);
    put_le32(hdr + 22, SCR_H);
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 16);
    put_le32(hdr + 30, 3);              // BI_BITFIELDS
    put_le32(hdr + 34, PIXEL_BYTES);
    put_le32(hdr + 54, 0xF800);         // red mask
    put_le32(hdr + 58, 0x07E0);         // green mask
    put_le32(hdr + 62, 0x001F);         // blue mask
}

// ── Flush hook ───────────────────────────────────────────────────────────────

static void screenshot_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    if (s_ss_pixels) {
        int32_t w = lv_area_get_width(area);
        int32_t h = lv_area_get_height(area);
        for (int32_t y = 0; y < h; y++) {
            int32_t dst_y = area->y1 + y;
            int32_t dst_x = area->x1;
            if (dst_y >= 0 && dst_y < SCR_H && dst_x >= 0) {
                int32_t copy_w = (dst_x + w > SCR_W) ? (SCR_W - dst_x) : w;
                if (copy_w > 0) {
                    memcpy(s_ss_pixels + dst_y * ROW_BYTES + dst_x * BPP,
                           px_map + y * w * BPP,
                           copy_w * BPP);
                }
            }
        }
    }
    if (s_orig_flush_cb) {
        s_orig_flush_cb(disp, area, px_map);
    }
}

void web_server_update()
{
}

// ── Screenshot HTTP handler ──────────────────────────────────────────────────

static size_t screenshot_fill_cb(uint8_t* buffer, size_t maxLen, size_t index)
{
    if (index >= (size_t)BMP_SIZE) return 0;
    size_t written = 0;

    if (index < HDR_SIZE) {
        uint8_t hdr[HDR_SIZE];
        build_bmp_header(hdr);
        size_t hdr_remaining = HDR_SIZE - index;
        size_t hdr_chunk = (hdr_remaining < maxLen) ? hdr_remaining : maxLen;
        memcpy(buffer, hdr + index, hdr_chunk);
        written += hdr_chunk;
        index += hdr_chunk;
    }

    while (written < maxLen && index < (size_t)BMP_SIZE) {
        size_t pixel_offset = index - HDR_SIZE;
        int bmp_row = pixel_offset / ROW_BYTES;
        int col_offset = pixel_offset % ROW_BYTES;
        int screen_row = SCR_H - 1 - bmp_row;

        size_t row_remaining = ROW_BYTES - col_offset;
        size_t buf_remaining = maxLen - written;
        size_t chunk = (row_remaining < buf_remaining) ? row_remaining : buf_remaining;

        memcpy(buffer + written,
               s_ss_pixels + screen_row * ROW_BYTES + col_offset,
               chunk);
        written += chunk;
        index += chunk;
    }

    return written;
}

static void handle_screenshot(AsyncWebServerRequest* req)
{
    if (!s_ss_pixels) {
        req->send(503, "text/plain", "Screenshot buffer not allocated");
        return;
    }
    AsyncWebServerResponse* resp = req->beginResponse("image/bmp", BMP_SIZE, screenshot_fill_cb);
    resp->addHeader("Cache-Control", "no-cache");
    req->send(resp);
}

// ── Config API handlers ──────────────────────────────────────────────────────

static void handle_api_config_get(AsyncWebServerRequest* req)
{
    char* json = config_settings_to_json();
    if (!json) {
        req->send(500, "text/plain", "JSON serialization failed");
        return;
    }
    req->send(200, "application/json", json);
    free(json);
}

// Body handler for POST requests — accumulates body chunks
static String s_post_body;

static void handle_body(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                         size_t index, size_t total)
{
    if (index == 0) s_post_body = "";
    s_post_body += String((char*)data, len);
}

static void handle_api_config_post(AsyncWebServerRequest* req)
{
    if (s_post_body.length() == 0) {
        req->send(400, "text/plain", "Empty body");
        return;
    }
    bool ok = config_settings_from_json(s_post_body.c_str(), s_post_body.length());
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    s_post_body = "";
}

static void handle_api_version(AsyncWebServerRequest* req)
{
#ifdef GIT_VERSION
    req->send(200, "application/json", "{\"version\":\"" GIT_VERSION "\"}");
#else
    req->send(200, "application/json", "{\"version\":\"unknown\"}");
#endif
}

static void handle_api_battery(AsyncWebServerRequest* req)
{
    char buf[160];
    auto info = config_get_battery_info();
    snprintf(buf, sizeof(buf),
        "{\"percent\":%d,\"raw_mv\":%d,\"compensated_mv\":%d,"
        "\"comp_factor\":%.4f,\"charging\":%s}",
        info.percent, info.raw_mv, info.compensated_mv,
        info.comp_factor, info.charging ? "true" : "false");
    req->send(200, "application/json", buf);
}

static void handle_api_meta(AsyncWebServerRequest* req)
{
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
    req->send(200, "application/json", out);
}

static void handle_api_slots_list(AsyncWebServerRequest* req)
{
    char names[CONFIG_MAX_SLOTS][17] = {};
    int count = config_list_slots(names, CONFIG_MAX_SLOTS);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++)
        arr.add(names[i]);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

static void handle_api_slot_save(AsyncWebServerRequest* req)
{
    if (!req->hasParam("name")) {
        req->send(400, "text/plain", "Missing 'name' param");
        return;
    }
    String name = req->getParam("name")->value();
    bool ok = config_save_slot(name.c_str());
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handle_api_slot_load(AsyncWebServerRequest* req)
{
    if (!req->hasParam("name")) {
        req->send(400, "text/plain", "Missing 'name' param");
        return;
    }
    String name = req->getParam("name")->value();
    bool ok = config_load_slot(name.c_str());
    if (ok) {
        char* json = config_settings_to_json();
        if (json) {
            req->send(200, "application/json", json);
            free(json);
        } else {
            req->send(200, "application/json", "{\"ok\":true}");
        }
    } else {
        req->send(404, "application/json", "{\"ok\":false}");
    }
}

static void handle_api_slot_delete(AsyncWebServerRequest* req)
{
    if (!req->hasParam("name")) {
        req->send(400, "text/plain", "Missing 'name' param");
        return;
    }
    String name = req->getParam("name")->value();
    bool ok = config_delete_slot(name.c_str());
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ── Mode control & status API handlers ───────────────────────────────────────

static void handle_api_status(AsyncWebServerRequest* req)
{
    StatusInfo s = config_get_status();
    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"paused\":%s,\"wpm\":%d,"
        "\"decoder_signal\":%d,\"decoder_wpm\":%d}",
        s.mode, s.paused ? "true" : "false", s.wpm,
        s.decoder_signal, s.decoder_wpm);
    req->send(200, "application/json", buf);
}

static void handle_api_mode(AsyncWebServerRequest* req)
{
    if (!req->hasParam("m")) {
        req->send(400, "application/json", "{\"error\":\"missing 'm' param\"}");
        return;
    }
    String mode = req->getParam("m")->value();
    bool ok = config_request_mode(mode.c_str());
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handle_api_pause(AsyncWebServerRequest* req)
{
    bool ok = config_toggle_pause();
    req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handle_api_text(AsyncWebServerRequest* req)
{
    char* text = config_get_text();
    if (!text) {
        req->send(200, "application/json", "{\"text\":\"\"}");
        return;
    }
    // JSON-escape the text (simple: just escape quotes and backslashes)
    String json = "{\"text\":\"";
    for (const char* p = text; *p; p++) {
        if (*p == '"') json += "\\\"";
        else if (*p == '\\') json += "\\\\";
        else if (*p == '\n') json += "\\n";
        else json += *p;
    }
    json += "\"}";
    free(text);
    req->send(200, "application/json", json);
}

// ── Plain HTML page (no JS — for lynx, screen readers, curl) ─────────────────

static void handle_plain(AsyncWebServerRequest* req)
{
    // Process action if present
    if (req->hasParam("action")) {
        String action = req->getParam("action")->value();
        if (action == "pause") config_toggle_pause();
        else if (action == "clear") config_clear_text();
        else config_request_mode(action.c_str());
    }

    StatusInfo s = config_get_status();
    char* text = config_peek_text();
    bool is_gen = (strcmp(s.mode, "generator") == 0 || strcmp(s.mode, "echo") == 0 ||
                   strcmp(s.mode, "chatbot") == 0);

    String html;
    html.reserve(2048);
    html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<title>M32-NG</title></head><body>\n"
            "<h1>Morserino-32 NG</h1>\n";

    // Status
    html += "<p>Mode: <strong>";
    html += s.mode;
    html += "</strong> | ";
    html += String(s.wpm);
    html += " WPM";
    if (is_gen && s.paused) html += " | <strong>PAUSED</strong>";
    if (strcmp(s.mode, "decoder") == 0 && s.decoder_wpm > 0) {
        html += " | Signal: ";
        html += String(s.decoder_signal);
        html += "% | Decode: ";
        html += String(s.decoder_wpm);
        html += " WPM";
    }
    html += "</p>\n";

    // Mode links
    html += "<h2>Modes</h2>\n<ul>\n";
    const char* modes[] = {"home","keyer","generator","echo","decoder","chatbot"};
    const char* labels[] = {"Home","Keyer","Generator","Echo","Decoder","Chatbot"};
    for (int i = 0; i < 6; i++) {
        html += "<li>";
        if (strcmp(s.mode, modes[i]) == 0 ||
            (strcmp(modes[i], "home") == 0 && strcmp(s.mode, "none") == 0))
            html += String("[") + labels[i] + "]";
        else {
            html += "<a href=\"/plain?action=";
            html += modes[i];
            html += "\">";
            html += labels[i];
            html += "</a>";
        }
        html += "</li>\n";
    }
    html += "</ul>\n";

    // Pause/resume
    if (is_gen) {
        html += "<p><a href=\"/plain?action=pause\">[";
        html += s.paused ? "RESUME" : "PAUSE";
        html += "]</a></p>\n";
    }

    // Text output
    html += "<h2>CW Text</h2>\n<pre>\n";
    if (text) {
        // HTML-escape the text
        for (const char* p = text; *p; p++) {
            if (*p == '<') html += "&lt;";
            else if (*p == '>') html += "&gt;";
            else if (*p == '&') html += "&amp;";
            else html += *p;
        }
        free(text);
    }
    html += "\n</pre>\n";
    html += "<p><a href=\"/plain?action=clear\">[Clear text]</a>"
            " | <a href=\"/plain\">[Refresh]</a></p>\n";

    html += "</body></html>";
    req->send(200, "text/html", html);
}

// ── Embedded Single-Page App ─────────────────────────────────────────────────

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Morserino-32 NG</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#1a1a2e;color:#e0e0e0;padding:16px;max-width:800px;margin:0 auto}
h1{color:#0ff;margin-bottom:8px;font-size:1.4em}
h2{color:#7fdbca;margin:18px 0 8px;font-size:1.1em;border-bottom:1px solid #333;padding-bottom:4px}
.tabs{display:flex;gap:4px;margin:12px 0;flex-wrap:wrap}
.tab{padding:6px 14px;background:#16213e;border:1px solid #333;border-radius:4px 4px 0 0;cursor:pointer;color:#aaa;font-size:.9em}
.tab.active{background:#0f3460;color:#0ff;border-bottom-color:#0f3460}
.panel{display:none;background:#0f3460;border:1px solid #333;border-radius:0 4px 4px 4px;padding:12px}
.panel.active{display:block}
.field{display:flex;align-items:center;justify-content:space-between;padding:6px 0;border-bottom:1px solid #1a1a2e}
.field label{flex:1;font-size:.9em}
.field input[type=number],.field select,.field input[type=text]{
  background:#16213e;color:#e0e0e0;border:1px solid #555;border-radius:3px;padding:4px 8px;width:140px;font-size:.9em}
.field input[type=checkbox]{width:20px;height:20px;accent-color:#0ff}
.actions{margin:16px 0;display:flex;gap:8px;flex-wrap:wrap}
button{background:#0ff;color:#000;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-weight:bold;font-size:.85em}
button:hover{background:#00d4d4}
button.danger{background:#e74c3c;color:#fff}
button.danger:hover{background:#c0392b}
button.secondary{background:#16213e;color:#0ff;border:1px solid #0ff}
.slots{margin:12px 0}
.slot-row{display:flex;align-items:center;gap:8px;padding:4px 0}
.slot-row span{flex:1}
.screenshot{margin:12px 0}
.screenshot img{max-width:100%;border:2px solid #333;border-radius:4px}
.status{color:#7fdbca;font-size:.85em;margin:4px 0}
#slot-name{background:#16213e;color:#e0e0e0;border:1px solid #555;border-radius:3px;padding:4px 8px;width:120px}
.mode-bar{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
.mode-bar button{font-size:.8em;padding:6px 12px}
.mode-bar button.active-mode{background:#00d4d4;outline:2px solid #fff}
#status-line{font-size:.9em;margin:6px 0;color:#7fdbca}
#cw-text{background:#0a0a1a;border:1px solid #333;border-radius:4px;padding:10px;min-height:80px;max-height:200px;overflow-y:auto;font-family:monospace;font-size:.95em;white-space:pre-wrap;word-break:break-all;margin:8px 0}
</style>
</head>
<body>
<h1>Morserino-32 NG <span id="ver" style="font-size:.5em;color:#888"></span></h1>
<div id="bat-info" style="font-size:.8em;color:#7fdbca;margin-bottom:8px"></div>

<h2>Mode Control</h2>
<div id="status-line" role="status" aria-live="polite">Loading...</div>
<nav class="mode-bar" aria-label="Mode selection">
  <button onclick="setMode('home')" aria-label="Home menu">Home</button>
  <button onclick="setMode('keyer')" aria-label="CW Keyer mode">Keyer</button>
  <button onclick="setMode('generator')" aria-label="CW Generator mode">Generator</button>
  <button onclick="setMode('echo')" aria-label="Echo Trainer mode">Echo</button>
  <button onclick="setMode('decoder')" aria-label="CW Decoder mode">Decoder</button>
  <button onclick="setMode('chatbot')" aria-label="QSO Chatbot mode">Chatbot</button>
  <button onclick="togglePause()" id="pause-btn" class="secondary" aria-label="Pause or resume">Pause</button>
</nav>

<h2>CW Text</h2>
<div id="cw-text" role="log" aria-live="polite" aria-label="Decoded and generated CW text"></div>
<button onclick="clearText()" class="secondary" style="margin:4px 0">Clear</button>

<div class="tabs" id="tabs"></div>
<div id="panels"></div>

<div class="actions">
  <button onclick="downloadJSON()">Download JSON</button>
  <label class="secondary" style="background:#16213e;color:#0ff;border:1px solid #0ff;padding:8px 16px;border-radius:4px;cursor:pointer;font-weight:bold;font-size:.85em">
    Upload JSON<input type="file" accept=".json" onchange="uploadJSON(event)" style="display:none">
  </label>
  <button onclick="refreshScreenshot()" class="secondary">Screenshot</button>
</div>

<div class="screenshot" id="ss-area" style="display:none">
  <img id="ss-img" alt="Device screenshot">
</div>

<h2>Settings Slots</h2>
<div class="status" id="slot-status"></div>
<div style="display:flex;gap:8px;align-items:center;margin:8px 0">
  <input id="slot-name" placeholder="Slot name" maxlength="16" aria-label="Slot name">
  <button onclick="saveSlot()">Save</button>
</div>
<div class="slots" id="slot-list"></div>

<script>
let META=[], CFG={}, groups=[];
let cwTextBuf='';
let curStatus={mode:'none',paused:false,wpm:0};

async function init(){
  const [metaR, cfgR, verR]=await Promise.all([fetch('/api/meta'),fetch('/api/config'),fetch('/api/version')]);
  META=await metaR.json(); CFG=await cfgR.json();
  try{const v=await verR.json();document.getElementById('ver').textContent=v.version;}catch(e){}
  groups=[...new Set(META.map(m=>m.group))];
  buildUI();
  loadSlots();
  updateBattery();
  pollStatus();
  pollText();
  setInterval(updateBattery, 10000);
  setInterval(pollStatus, 1000);
  setInterval(pollText, 500);
  setInterval(refreshConfig, 5000);
}

async function pollStatus(){
  try{
    const r=await fetch('/api/status');
    const s=await r.json();
    curStatus=s;
    let line='Mode: '+s.mode+' | '+s.wpm+' WPM';
    if(s.paused) line+=' | PAUSED';
    if(s.mode==='decoder'&&s.decoder_wpm>0)
      line+=' | Signal: '+s.decoder_signal+'% | Decode: '+s.decoder_wpm+' WPM';
    document.getElementById('status-line').textContent=line;
    // Update pause button
    const pb=document.getElementById('pause-btn');
    pb.textContent=s.paused?'Resume':'Pause';
    pb.style.display=(s.mode==='generator'||s.mode==='echo'||s.mode==='chatbot')?'':'none';
    // Highlight active mode button
    document.querySelectorAll('.mode-bar button[onclick^="setMode"]').forEach(b=>{
      const m=b.getAttribute('onclick').match(/'(\w+)'/);
      if(m) b.classList.toggle('active-mode',m[1]===s.mode);
    });
  }catch(e){}
}

async function pollText(){
  try{
    const r=await fetch('/api/text');
    const d=await r.json();
    if(d.text){
      cwTextBuf+=d.text;
      const el=document.getElementById('cw-text');
      el.textContent=cwTextBuf;
      el.scrollTop=el.scrollHeight;
    }
  }catch(e){}
}

async function setMode(m){
  await fetch('/api/mode?m='+m);
}

async function togglePause(){
  await fetch('/api/pause');
}

function clearText(){
  cwTextBuf='';
  document.getElementById('cw-text').textContent='';
}

async function refreshConfig(){
  try{
    const r=await fetch('/api/config');
    const c=await r.json();
    let changed=false;
    for(const k in c){if(CFG[k]!==c[k]){changed=true;break;}}
    if(changed){CFG=c;updateUI();}
  }catch(e){}
}

async function updateBattery(){
  try{
    const r=await fetch('/api/battery');
    const b=await r.json();
    const el=document.getElementById('bat-info');
    const chg=b.charging?' &#x26A1;':'';
    el.innerHTML='Battery: '+b.percent+'%'+chg+' &middot; raw '+b.raw_mv+' mV &middot; cal '+b.compensated_mv+' mV (factor '+b.comp_factor.toFixed(4)+')';
  }catch(e){}
}

function buildUI(){
  const tabs=document.getElementById('tabs'), panels=document.getElementById('panels');
  tabs.innerHTML=''; panels.innerHTML='';
  groups.forEach((g,i)=>{
    const t=document.createElement('div');
    t.className='tab'+(i===0?' active':'');
    t.textContent=g;
    t.setAttribute('role','tab');
    t.setAttribute('tabindex','0');
    t.onclick=()=>{
      document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
      document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));
      t.classList.add('active');
      document.getElementById('p-'+i).classList.add('active');
    };
    t.onkeydown=(e)=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();t.click();}};
    tabs.appendChild(t);

    const p=document.createElement('div');
    p.className='panel'+(i===0?' active':'');
    p.id='p-'+i;
    p.setAttribute('role','tabpanel');
    META.filter(m=>m.group===g).forEach(m=>{
      const d=document.createElement('div');
      d.className='field';
      const l=document.createElement('label');
      l.textContent=m.label;
      l.id='lbl-'+m.key;
      d.appendChild(l);

      let ctrl;
      if(m.type==='bool'){
        ctrl=document.createElement('input');
        ctrl.type='checkbox';
        ctrl.checked=!!CFG[m.key];
        ctrl.onchange=()=>post({[m.key]:ctrl.checked?1:0});
      } else if(m.type==='enum'){
        ctrl=document.createElement('select');
        (m.options||'').split('|').forEach((o,idx)=>{
          const opt=document.createElement('option');
          opt.value=idx; opt.textContent=o;
          ctrl.appendChild(opt);
        });
        ctrl.value=CFG[m.key]||0;
        ctrl.onchange=()=>post({[m.key]:parseInt(ctrl.value)});
      } else if(m.type==='string'){
        ctrl=document.createElement('input');
        ctrl.type='text';
        ctrl.value=CFG[m.key]||'';
        ctrl.maxLength=m.max||15;
        ctrl.onchange=()=>post({[m.key]:ctrl.value});
      } else {
        ctrl=document.createElement('input');
        ctrl.type='number';
        ctrl.min=m.min; ctrl.max=m.max;
        ctrl.step=m.step||1;
        ctrl.value=CFG[m.key]||0;
        ctrl.onchange=()=>post({[m.key]:parseInt(ctrl.value)});
      }
      ctrl.dataset.key=m.key;
      ctrl.setAttribute('aria-labelledby','lbl-'+m.key);
      d.appendChild(ctrl);
      p.appendChild(d);
    });
    panels.appendChild(p);
  });
}

async function post(obj){
  Object.assign(CFG,obj);
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)});
}

function updateUI(){
  META.forEach(m=>{
    const el=document.querySelector('[data-key="'+m.key+'"]');
    if(!el)return;
    if(m.type==='bool') el.checked=!!CFG[m.key];
    else if(m.type==='string') el.value=CFG[m.key]||'';
    else el.value=CFG[m.key]||0;
  });
}

function downloadJSON(){
  const blob=new Blob([JSON.stringify(CFG,null,2)],{type:'application/json'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download='morserino-settings.json';
  a.click();
}

async function uploadJSON(ev){
  const file=ev.target.files[0];
  if(!file)return;
  const text=await file.text();
  try{
    const obj=JSON.parse(text);
    await post(obj);
    CFG=Object.assign(CFG,obj);
    updateUI();
  }catch(e){alert('Invalid JSON: '+e.message);}
  ev.target.value='';
}

function refreshScreenshot(){
  const area=document.getElementById('ss-area');
  const img=document.getElementById('ss-img');
  img.src='/screenshot.bmp?t='+Date.now();
  area.style.display='block';
}

async function loadSlots(){
  const r=await fetch('/api/slots');
  const slots=await r.json();
  const list=document.getElementById('slot-list');
  list.innerHTML='';
  slots.forEach(name=>{
    const row=document.createElement('div');
    row.className='slot-row';
    row.innerHTML='<span>'+name+'</span>';
    const lb=document.createElement('button');
    lb.textContent='Load';lb.className='secondary';
    lb.setAttribute('aria-label','Load slot '+name);
    lb.onclick=async()=>{
      const r=await fetch('/api/slots/load?name='+encodeURIComponent(name));
      if(r.ok){CFG=await r.json();updateUI();setSlotStatus('Loaded: '+name);}
    };
    const db=document.createElement('button');
    db.textContent='Delete';db.className='danger';
    db.setAttribute('aria-label','Delete slot '+name);
    db.onclick=async()=>{
      await fetch('/api/slots/delete?name='+encodeURIComponent(name));
      loadSlots();setSlotStatus('Deleted: '+name);
    };
    row.appendChild(lb);row.appendChild(db);
    list.appendChild(row);
  });
}

async function saveSlot(){
  const name=document.getElementById('slot-name').value.trim();
  if(!name){alert('Enter a slot name');return;}
  await fetch('/api/slots/save?name='+encodeURIComponent(name));
  document.getElementById('slot-name').value='';
  loadSlots();
  setSlotStatus('Saved: '+name);
}

function setSlotStatus(msg){
  const el=document.getElementById('slot-status');
  el.textContent=msg;
  setTimeout(()=>el.textContent='',3000);
}

init();
</script>
</body>
</html>)rawliteral";

// ── Public API ───────────────────────────────────────────────────────────────

void web_server_start()
{
    if (s_server) return;

    if (!s_ss_pixels) {
        s_ss_pixels = (uint8_t*)heap_caps_malloc(PIXEL_BYTES, MALLOC_CAP_SPIRAM);
        if (!s_ss_pixels) {
            Log.warningln("Screenshot: PSRAM malloc(%d) failed", PIXEL_BYTES);
            return;
        }
        memset(s_ss_pixels, 0, PIXEL_BYTES);
        Log.noticeln("Screenshot: allocated %d bytes in PSRAM", PIXEL_BYTES);
    }

    // Hook the display flush callback
    lv_display_t* disp = lv_display_get_default();
    if (disp && !s_orig_flush_cb) {
        s_orig_flush_cb = disp->flush_cb;
        lv_display_set_flush_cb(disp, screenshot_flush_cb);
        Log.noticeln("Screenshot: flush callback hooked");
    }

    s_server = new AsyncWebServer(80);

    // Screenshot
    s_server->on("/screenshot.bmp", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_screenshot(r); });

    // Config API
    s_server->on("/api/config", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_config_get(r); });
    s_server->on("/api/config", HTTP_POST,
                 [](AsyncWebServerRequest* r) { handle_api_config_post(r); },
                 nullptr, handle_body);
    s_server->on("/api/meta", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_meta(r); });
    s_server->on("/api/version", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_version(r); });
    s_server->on("/api/battery", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_battery(r); });

    // Mode control & status API
    s_server->on("/api/status", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_status(r); });
    s_server->on("/api/mode", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_mode(r); });
    s_server->on("/api/pause", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_pause(r); });
    s_server->on("/api/text", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_text(r); });

    // Slots API (specific routes first)
    s_server->on("/api/slots/save", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_save(r); });
    s_server->on("/api/slots/load", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_load(r); });
    s_server->on("/api/slots/delete", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_delete(r); });
    s_server->on("/api/slots", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slots_list(r); });

    // Plain HTML page (no JS — lynx, curl, screen readers)
    s_server->on("/plain", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_plain(r); });

    // SPA index page
    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", INDEX_HTML);
    });

    s_server->begin();

    // mDNS: advertise as http://morserino.local/
    mdns_init();
    mdns_hostname_set("morserino");
    mdns_instance_name_set("Morserino-32 NG");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    Log.noticeln("Web server started on port 80 (http://morserino.local/)");
}

void web_server_stop()
{
    mdns_free();

    if (s_server) {
        s_server->end();
        delete s_server;
        s_server = nullptr;
    }

    lv_display_t* disp = lv_display_get_default();
    if (disp && s_orig_flush_cb) {
        lv_display_set_flush_cb(disp, s_orig_flush_cb);
        s_orig_flush_cb = nullptr;
    }
}

#endif // BOARD_POCKETWROOM
