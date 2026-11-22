#include "wifi.h"
#include <WiFi.h>
#include <wifi_portal.h>
#include <WebServer.h>
#include "secrets.h"

extern WebServer web;

Wifi wifi;

static String esc(const String &s) {
    String o;
    for (char c : s) {
        if (c == '"' || c == '\\')
            o += '\\';
        o += c;
    }
    return o;
}

// native <select> (datalist silently fails in some in-app/captive browsers) plus
// a manual field for hidden networks
static void portalRoot() {
    const char *inp = "width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px;border:1px solid #30363d;background:#161b22;color:#c9d1d9";
    String html =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>AdBlock setup</title>"
        "<body style='font:16px system-ui,sans-serif;max-width:420px;margin:30px auto;padding:0 16px;background:#0d1117;color:#c9d1d9'>"
        "<h2>&#128737; AdBlock — WiFi setup</h2>"
        "<p style='color:#8b949e'>Pick your home network, enter its password, and tap Connect. You'll then see the box's address on your network.</p>"
        "<form method=POST action=/wifisave>"
        "<select name=s style='" + String(inp) + "'>" + wifi.portalOpts + "</select>"
        "<input name=s2 placeholder='or type the network name' style='" + String(inp) + "'>"
        "<input name=p type=password placeholder='WiFi password' style='" + String(inp) + "'>"
        "<button style='width:100%;padding:12px;margin-top:8px;border-radius:6px;border:0;background:#3fb950;color:#000;font-weight:600;cursor:pointer'>Connect</button>"
        "</form>"
        "<p style='color:#8b949e;font-size:12px'>Not in the list? Type the name (for hidden networks).</p>"
        "</body>";
    web.send(200, "text/html", html);
}

// Captive DNS. Replaces Arduino's DNSServer, which (2.0.x) answers EVERY query
// type with an IPv4 record: iOS also asks AAAA + HTTPS (type 65) for
// captive.apple.com and stalls on those malformed answers before showing the
// login sheet. Here A queries get the AP address, everything else a clean
// NOERROR with no answers, so the phone moves on immediately.
static void portalDnsHandle(WiFiUDP &udp, IPAddress apIP) {
    uint8_t b[512];
    for (int budget = 0; budget < 8; budget++) {
        int sz = udp.parsePacket();
        if (sz <= 0)
            return;
        int n = udp.read(b, sizeof(b));
        udp.flush();
        if (n < 17 || (b[2] & 0x80) || ((b[2] >> 3) & 0x0F) != 0 || b[4] != 0 || b[5] != 1)
            continue; // not a standard single-question query
        int i = 12;
        while (i < n && b[i] != 0) {
            if (b[i] & 0xC0)
                break;
            i += b[i] + 1;
        }
        if (i >= n || b[i] != 0 || i + 5 > n)
            continue;
        int qend = i + 5; // zero label + qtype + qclass
        uint16_t qtype = (b[i + 1] << 8) | b[i + 2];
        bool isA = qtype == 1 && b[i + 3] == 0 && b[i + 4] == 1;
        b[2] = 0x84 | (b[2] & 0x01); // response, authoritative, keep RD
        b[3] = 0x80;                 // RA=1 (stub resolvers expect it), NOERROR
        b[6] = 0;
        b[7] = isA ? 1 : 0;          // answers
        b[8] = b[9] = b[10] = b[11] = 0;
        int len = qend;
        if (isA && qend + 16 <= (int)sizeof(b)) {
            const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 10, 0, 4, // ptr, A, IN, TTL 10s, len 4
                                   apIP[0], apIP[1], apIP[2], apIP[3]};
            memcpy(b + qend, ans, sizeof(ans));
            len += sizeof(ans);
        }
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(b, len);
        udp.endPacket();
    }
}

static String htmlEsc(const String &s) {
    String o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else if (c == '\'') o += "&#39;";
        else o += c;
    }
    return o;
}

// Test-join from the portal: the AP stays up (AP+STA) while the STA side joins the
// chosen network, so the phone can stay on the page and be shown the IP address the
// router handed out. Creds are only persisted once the join actually succeeds.
enum JoinState { J_IDLE, J_TRYING, J_OK, J_FAIL };
static JoinState joinState = J_IDLE;
static String joinSsid, joinPass;
static uint32_t joinT0 = 0;
static const uint32_t JOIN_TIMEOUT_MS = 20000;

static const char PORTAL_HEAD[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>" DEVICE_NAME " setup</title>"
    "<style>body{font:16px system-ui,sans-serif;max-width:420px;margin:30px auto;padding:0 16px;background:#0d1117;color:#c9d1d9;text-align:center}"
    ".dim{color:#8b949e}.ip{font:600 30px ui-monospace,monospace;color:#3fb950;margin:14px 0;word-break:break-all}"
    ".btn{display:block;width:100%;box-sizing:border-box;padding:12px;margin-top:18px;border-radius:6px;border:0;background:#3fb950;color:#000;font:600 16px system-ui;text-decoration:none}</style>";

static void wifiSave() {
    String ss = web.arg("s2"); // manual field wins over the dropdown
    ss.trim();
    if (!ss.length())
        ss = web.arg("s");
    ss.trim();
    String pw = web.arg("p");
    if (!ss.length()) {
        web.send(400, "text/plain", "no network selected");
        return;
    }
    joinSsid = ss;
    joinPass = pw;
    joinState = J_TRYING;
    joinT0 = millis();
    Serial.printf("[setup] trying \"%s\"\n", ss.c_str());
    WiFi.persistent(false); // creds go to our own NVS namespace, on success only
    WiFi.setHostname(DEVICE_NAME);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ss.c_str(), pw.c_str());
    String e = htmlEsc(ss);
    web.send(200, "text/html", String(PORTAL_HEAD) +
        "<body><h2>&#128737; " DEVICE_NAME "</h2><div id=m>"
        "<p>Connecting to <b>" + e + "</b>&hellip;</p><p class=dim>This takes up to 20 seconds.</p></div>"
        "<p id=w class=dim style='display:none'>Lost contact with the setup hotspot. If your phone left "
        "<b>" DEVICE_NAME "</b>, rejoin it &mdash; this page will continue.</p>"
        "<script>const m=document.getElementById('m'),w=document.getElementById('w');"
        "function poll(){fetch('/joinstatus',{cache:'no-store'}).then(r=>r.json()).then(j=>{w.style.display='none';"
        "if(j.st=='ok'){m.innerHTML='<p>&#9989; Connected to <b>" + e + "</b></p><p class=dim>Your " DEVICE_NAME " address is</p>'"
        "+'<div class=ip>'+j.ip+'</div><p class=dim>Dashboard: http://'+j.ip+'<br>or http://" DEVICE_NAME ".local<br><br>"
        "Tip: set this as the DNS server in your router so every device gets ad blocking.</p>'"
        "+'<a class=btn href=/wifidone>OK</a>';}"
        "else if(j.st=='fail'){m.innerHTML='<p>&#10060; Could not connect to <b>" + e + "</b>.</p>"
        "<p class=dim>Check the password and that the network is in range.</p><a class=btn href=/>Try again</a>';}"
        "else setTimeout(poll,1000)}).catch(()=>{w.style.display='block';setTimeout(poll,1500)})}"
        "setTimeout(poll,1000)</script></body>");
}

static void joinStatus() {
    const char *st = joinState == J_OK ? "ok" : joinState == J_FAIL ? "fail" : joinState == J_TRYING ? "trying" : "idle";
    String ip = joinState == J_OK ? WiFi.localIP().toString() : "";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", String("{\"st\":\"") + st + "\",\"ip\":\"" + ip + "\"}");
}

static void wifiDone() {
    String ip = joinState == J_OK ? WiFi.localIP().toString() : String(DEVICE_NAME ".local");
    web.send(200, "text/html", String(PORTAL_HEAD) +
        "<body><h2>&#9989; All set</h2><p>" DEVICE_NAME " is restarting and joining your network.</p>"
        "<p class=dim>Switch your phone back to your normal WiFi, then open<br><b>http://" + ip + "</b></p></body>");
    if (joinState == J_OK) {
        delay(900);
        ESP.restart();
    }
}

// called from the portal loop: advance the test-join; true once creds were saved
static bool pollJoin() {
    if (joinState != J_TRYING)
        return false;
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
        wifi.prefs.begin("wifi", false);
        wifi.prefs.putString("ssid", joinSsid);
        wifi.prefs.putString("pass", joinPass);
        wifi.prefs.end();
        joinState = J_OK;
        Serial.printf("[setup] joined \"%s\", IP %s, saved\n", joinSsid.c_str(), WiFi.localIP().toString().c_str());
        return true;
    }
    if (s == WL_CONNECT_FAILED || millis() - joinT0 > JOIN_TIMEOUT_MS) {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        joinState = J_FAIL;
        Serial.printf("[setup] could not join \"%s\" (status %d)\n", joinSsid.c_str(), (int)s);
    }
    return false;
}

bool Wifi::hasCreds() {
    prefs.begin("wifi", true);
    bool nvs = prefs.getString("ssid", "").length() > 0;
    prefs.end();
    return nvs || (WIFI_SSID && *WIFI_SSID && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0);
}

bool Wifi::connect() {
    prefs.begin("wifi", true);
    String ss = prefs.getString("ssid", "");
    String pw = prefs.getString("pass", "");
    prefs.end();
    const char *ssid = ss.length() ? ss.c_str() : WIFI_SSID;
    const char *pass = ss.length() ? pw.c_str() : WIFI_PASS;
    if (!ssid || !*ssid || strcmp(ssid, "YOUR_WIFI_SSID") == 0)
        return false;
    Serial.printf("WiFi: connecting to \"%s\"%s\n", ssid, ss.length() ? " (provisioned)" : " (secrets.h)");
    WiFi.setHostname(DEVICE_NAME); // must precede WiFi.mode() to reach DHCP
    WiFi.mode(WIFI_STA);
    //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setSleep(false);
    WiFi.begin(ssid, pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

void Wifi::clearCreds() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
}

// blocks until creds are saved (then reboots); a configured device that just
// failed to join reboots after 3 min of nobody using the portal
void Wifi::runPortal() {
    WiFi.mode(WIFI_STA);   // scan needs station mode
    WiFi.disconnect(false); // drop the failed/pending association, else scan returns -2
    delay(200);
    int n = 0;
    for (int a = 0; a < 3 && n <= 0; a++) {
        n = WiFi.scanNetworks();
        Serial.printf("[setup] scan %d/3: %d networks\n", a + 1, n);
        if (n <= 0) {
            WiFi.scanDelete();
            delay(600);
        }
    }
    portalOpts = "<option value=''>— select network —</option>";
    for (int i = 0; i < n && i < 15; i++) {
        String ss = esc(WiFi.SSID(i));
        portalOpts += "<option value='" + ss + "'>" + ss + "</option>";
    }
    const char *ap = DEVICE_NAME;
    WiFi.mode(WIFI_AP);
    bool locked = strlen(AP_PASS) >= 8;
    if (locked)
        WiFi.softAP(ap, AP_PASS); // WPA2: only someone with the password can open setup
    else
        WiFi.softAP(ap);
    IPAddress apIP = WiFi.softAPIP();
    portalDns.begin(53);
    web.on("/", portalRoot);
    web.on("/wifisave", HTTP_POST, wifiSave);
    web.on("/joinstatus", joinStatus);
    web.on("/wifidone", wifiDone);
    // Apple's probe (captive.apple.com/hotspot-detect.html): serve the form right
    // away with 200 - any answer other than "Success" opens the login sheet, and
    // skipping the redirect saves the iPhone a round trip
    web.on("/hotspot-detect.html", []() {
        web.sendHeader("Cache-Control", "no-store");
        portalRoot();
    });
    web.on("/library/test/success.html", []() {
        web.sendHeader("Cache-Control", "no-store");
        portalRoot();
    });
    // other OS probes (Android /generate_204, Windows /ncsi.txt, ...) hit
    // onNotFound -> redirect them so the phone auto-pops the form
    web.onNotFound([]() {
        web.sendHeader("Cache-Control", "no-store");
        web.sendHeader("Location", "http://192.168.4.1/", true);
        web.send(302, "text/plain", "");
    });
    web.begin();
    Serial.printf("\n[setup] No WiFi. Join network \"%s\" (%s), a setup page pops up (or http://%s)\n",
                  ap, locked ? "password-protected" : "open", apIP.toString().c_str());
    // after a successful test-join the same timer reboots into normal mode if nobody
    // taps OK (e.g. the phone dropped off when the AP followed the router's channel)
    bool configured = hasCreds();
    uint32_t t0 = millis();
    while (true) {
        portalDnsHandle(portalDns, apIP);
        web.handleClient();
        delay(1);
        if (pollJoin()) {
            configured = true;
            t0 = millis();
        }
        if (WiFi.softAPgetStationNum() > 0)
            t0 = millis();
        if (configured && millis() - t0 > 180000UL) {
            Serial.println("[setup] retrying WiFi");
            ESP.restart();
        }
    }
}
