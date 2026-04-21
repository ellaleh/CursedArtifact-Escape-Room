#include "room_net.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "room_session.h"

#include <string.h>

static const char *TAG = "room_net";

/* SoftAP — laptop joins this Wi‑Fi to open the room page. */
#define ROOM_AP_SSID "CursedRoom"
#define ROOM_AP_PASS "escape42"

static httpd_handle_t s_srv;

static esp_err_t root_get(httpd_req_t *req) {
  static const char html[] =
      "<!DOCTYPE html><html lang=en><head><meta charset=utf-8><meta "
      "name=viewport content=\"width=device-width,initial-scale=1\">"
      "<title>Welcome to the Ruins of Ancient City</title><style>"
      ":root{--sand1:#f7ead8;--sand2:#edd9b8;--sand3:#dcc9a4;--ink:#2a1f14;"
      "--gold:#a67c2d;--ochre:#7a5c32;--muted:#5c4a38;--lapis:#2c4a52}"
      "*{box-sizing:border-box}"
      "body{font-family:Georgia,Palatino,serif;margin:0;min-height:100vh;"
      "color:var(--ink);background:linear-gradient(180deg,var(--sand1) 0%,"
      "var(--sand2) 35%,var(--sand3) 100%);background-attachment:fixed;"
      "padding:20px 16px 28px}"
      ".wrap{max-width:36rem;margin:0 auto}"
      ".glyph{opacity:.45;font-size:.72rem;letter-spacing:.25em;text-align:center;"
      "color:var(--muted);margin-bottom:.5rem;font-variant:small-caps}"
      "h1{font-size:1.25rem;font-weight:600;margin:.2rem 0 .4rem;"
      "letter-spacing:.03em;color:var(--lapis);text-align:center;line-height:1.35;"
      "text-shadow:0 1px 0 rgba(255,255,255,.5)}"
      "h2{font-size:1.05rem;margin:1.1rem 0 .55rem;color:var(--ochre);"
      "border-bottom:2px solid var(--gold);padding-bottom:.3rem}"
      ".sub{font-size:.82rem;color:var(--muted);text-align:center;margin:0 0 "
      "1rem;font-style:italic;line-height:1.4}"
      ".row{display:flex;justify-content:space-between;align-items:center;"
      "gap:10px;flex-wrap:wrap;margin:.45rem 0;font-size:.92rem}"
      ".row span:first-child{color:var(--muted);font-variant:small-caps}"
      "#cd{font-size:2rem;font-variant-numeric:tabular-nums;color:var(--lapis);"
      "font-weight:700;text-shadow:0 1px 0 rgba(255,255,255,.45)}"
      ".timer-panel{background:rgba(255,255,255,.28);border:1px solid "
      "rgba(122,92,50,.35);border-radius:4px;padding:.65rem .8rem;margin:.55rem "
      "0 .4rem;box-shadow:inset 0 1px 2px rgba(255,255,255,.4)}"
      "table{width:100%;border-collapse:collapse;margin-top:.4rem;font-size:"
      ".88rem}"
      "th,td{padding:8px 6px;border-bottom:1px solid rgba(122,92,50,.3);"
      "text-align:left}"
      "th{color:var(--ochre);font-weight:600;font-variant:small-caps;font-size:"
      ".75rem}"
      "tr:nth-child(even) td{background:rgba(255,255,255,.18)}"
      ".warn{color:#7a3018;font-size:.85rem;min-height:1.2em;margin:.45rem 0}"
      "button{background:linear-gradient(180deg,#c9a227,#9a7619);color:#fff;"
      "border:2px solid #6b4f12;padding:10px 18px;border-radius:3px;cursor:"
      "pointer;font-size:.95rem;font-family:inherit;letter-spacing:.03em;"
      "box-shadow:0 2px 4px rgba(0,0,0,.15)}"
      "button:hover{filter:brightness(1.05)}"
      "button.danger{background:linear-gradient(180deg,#9a4a38,#6b3028);"
      "border-color:#5a2820}"
      "input[type=text]{width:100%;padding:10px;border-radius:3px;border:2px "
      "solid rgba(122,92,50,.45);background:rgba(255,255,255,.45);color:var(--ink);"
      "font-family:inherit}"
      "#nameBox{margin-top:.85rem;padding:.9rem;background:rgba(255,255,255,.22);"
      "border:2px dashed rgba(122,92,50,.5);border-radius:4px}"
      "#nameBox h2{border:none;margin-top:0}"
      ".footer{margin-top:1.15rem;text-align:center;font-size:.68rem;color:"
      "var(--muted);letter-spacing:.12em;opacity:.85}"
      "</style></head><body><div class=wrap>"
      "<div class=glyph>&#9765; &middot; &middot; &middot; &#9765;</div>"
      "<h1>Welcome to the Ruins of Ancient City</h1>"
      "<p class=sub>Exhibition log-- Escape The Cursed Artifact</p>"
      "<div class=timer-panel><p class=row><span>Sand remaining</span>"
      "<span id=cd>—:—</span></p>"
      "<p class=row><span>Current trial</span><span id=ph>—</span></p></div>"
      "<p id=st class=warn></p>"
      "<p class=row><button type=button class=danger id=reset>Reset the Site</"
      "button></p>"
      "<h2>Stele of Victors</h2>"
      "<table><thead><tr><th>Rank</th><th>Scribe</th><th>Duration</th></tr>"
      "</thead><tbody id=lb></tbody></table>"
      "<div id=nameBox style=display:none><h2>Inscribe your name</h2>"
      "<p class=sub>A new record awaits the cartouche.</p>"
      "<form id=nf><input type=text name=name maxlength=31 "
      "placeholder=\"Name of the explorer\" required><button type=submit>"
      "Carve into stone</button></form></div>"
      "<p class=footer>MA'AT · WISDOM · BALANCE</p></div><script>"
      "function fmt(ms){if(ms===undefined||ms===null)return'—';"
      "var s=Math.floor(ms/1000),m=Math.floor(s/60);s%=60;"
      "return m+':'+(s<10?'0':'')+s;}"
      "async function poll(){try{"
      "var r=await fetch('/api/state');var j=await r.json();"
      "document.getElementById('cd').textContent=fmt(j.remaining_ms);"
      "document.getElementById('ph').textContent=j.phase||'—';"
      "var st=document.getElementById('st');"
      "if(j.expired)st.textContent="
      "'The hour has turned — the sands are still. Reset the site to try again.';"
      "else st.textContent='';"
      "var tb=document.getElementById('lb');tb.innerHTML='';"
      "(j.leaderboard||[]).forEach(function(e,i){"
      "var tr=document.createElement('tr');"
      "tr.innerHTML='<td>'+(i+1)+'</td><td>'+(e.name||'—')+'</td><td>'+"
      "fmt(e.time_ms)+'</td>';tb.appendChild(tr);});"
      "var nb=document.getElementById('nameBox');"
      "if(j.name_pending){nb.style.display='block';}else{nb.style.display="
      "'none';}"
      "}catch(e){document.getElementById('cd').textContent='?';}}"
      "document.getElementById('reset').onclick=async function(){"
      "if(!confirm('Reset the site? The timer will restart from the compass "
      "trial.'))return;"
      "await fetch('/api/reset',{method:'POST'});poll();};"
      "document.getElementById('nf').onsubmit=async function(ev){"
      "ev.preventDefault();var fd=new FormData(ev.target);"
      "var n=fd.get('name')||'';"
      "await fetch('/api/name?name='+encodeURIComponent(n),{method:'POST'});"
      "poll();};setInterval(poll,500);poll();"
      "</script></body></html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t state_get(httpd_req_t *req) {
  char buf[1536];
  if (room_state_json(buf, sizeof(buf)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t reset_post(httpd_req_t *req) {
  room_request_reset();
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t name_post(httpd_req_t *req) {
  char name[ROOM_NAME_MAX_LEN + 4];
  char buf[192];

  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK &&
      httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
    goto apply;
  }
  size_t len = req->content_len;
  if (len > 0 && len < sizeof(buf)) {
    int r = httpd_req_recv(req, buf, len);
    if (r > 0 && (size_t)r < sizeof(buf)) {
      buf[r] = '\0';
      if (httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
        goto apply;
      }
    }
  }
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name");
  return ESP_FAIL;

apply:
  esp_err_t e = room_submit_name(name);
  if (e != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "state");
    return ESP_FAIL;
  }
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static httpd_uri_t uris[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_get},
    {.uri = "/api/state", .method = HTTP_GET, .handler = state_get},
    {.uri = "/api/reset", .method = HTTP_POST, .handler = reset_post},
    {.uri = "/api/name", .method = HTTP_POST, .handler = name_post},
};

esp_err_t room_net_start(void) {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

  wifi_config_t ap = {0};
  strncpy((char *)ap.ap.ssid, ROOM_AP_SSID, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = (uint8_t)strlen(ROOM_AP_SSID);
  strncpy((char *)ap.ap.password, ROOM_AP_PASS, sizeof(ap.ap.password));
  ap.ap.channel = 1;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "SoftAP SSID=%s password=%s (IP is usually 192.168.4.1)",
           ROOM_AP_SSID, ROOM_AP_PASS);

  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.stack_size = 8192;
  hc.server_port = 80;
  hc.lru_purge_enable = true;
  if (httpd_start(&s_srv, &hc) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return ESP_FAIL;
  }
  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
    httpd_register_uri_handler(s_srv, &uris[i]);
  }

  return ESP_OK;
}
