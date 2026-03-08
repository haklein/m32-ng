#ifdef BOARD_POCKETWROOM

#include "screenshot_server.h"
#include "config_api.h"
#include <ESPAsyncWebServer.h>
#include <lvgl.h>
#include "display/lv_display_private.h"
#include <ArduinoLog.h>
#include <ArduinoJson.h>
#include <cstring>
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

void screenshot_server_update()
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
</style>
</head>
<body>
<h1>Morserino-32 NG <span id="ver" style="font-size:.5em;color:#888"></span></h1>
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
  <img id="ss-img" alt="screenshot">
</div>

<h2>Settings Slots</h2>
<div class="status" id="slot-status"></div>
<div style="display:flex;gap:8px;align-items:center;margin:8px 0">
  <input id="slot-name" placeholder="Slot name" maxlength="16">
  <button onclick="saveSlot()">Save</button>
</div>
<div class="slots" id="slot-list"></div>

<script>
let META=[], CFG={}, groups=[];

async function init(){
  const [metaR, cfgR, verR]=await Promise.all([fetch('/api/meta'),fetch('/api/config'),fetch('/api/version')]);
  META=await metaR.json(); CFG=await cfgR.json();
  try{const v=await verR.json();document.getElementById('ver').textContent=v.version;}catch(e){}
  groups=[...new Set(META.map(m=>m.group))];
  buildUI();
  loadSlots();
}

function buildUI(){
  const tabs=document.getElementById('tabs'), panels=document.getElementById('panels');
  tabs.innerHTML=''; panels.innerHTML='';
  groups.forEach((g,i)=>{
    const t=document.createElement('div');
    t.className='tab'+(i===0?' active':'');
    t.textContent=g;
    t.onclick=()=>{
      document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
      document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));
      t.classList.add('active');
      document.getElementById('p-'+i).classList.add('active');
    };
    tabs.appendChild(t);

    const p=document.createElement('div');
    p.className='panel'+(i===0?' active':'');
    p.id='p-'+i;
    META.filter(m=>m.group===g).forEach(m=>{
      const d=document.createElement('div');
      d.className='field';
      const l=document.createElement('label');
      l.textContent=m.label;
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
    lb.onclick=async()=>{
      const r=await fetch('/api/slots/load?name='+encodeURIComponent(name));
      if(r.ok){CFG=await r.json();updateUI();setSlotStatus('Loaded: '+name);}
    };
    const db=document.createElement('button');
    db.textContent='Delete';db.className='danger';
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

void screenshot_server_start()
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

    // Slots API (specific routes first)
    s_server->on("/api/slots/save", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_save(r); });
    s_server->on("/api/slots/load", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_load(r); });
    s_server->on("/api/slots/delete", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slot_delete(r); });
    s_server->on("/api/slots", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_api_slots_list(r); });

    // SPA index page
    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", INDEX_HTML);
    });

    s_server->begin();
    Log.noticeln("Web server started on port 80");
}

void screenshot_server_stop()
{
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
