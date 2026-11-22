#include "querylog.h"
#include "esp_timer.h"

QueryLog qlog;

static uint32_t fnv(const char *s) {
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static uint32_t uptimeHour() {
    return (uint32_t)(esp_timer_get_time() / 3600000000LL); // 64-bit, no 49-day wrap
}

// advance the ring of hourly buckets to the current hour, zeroing skipped ones
int QueryLog::hourSlot() {
    uint32_t h = uptimeHour();
    if (h != curHour) {
        uint32_t steps = h - curHour;
        if (steps > HOURS)
            steps = HOURS;
        for (uint32_t i = 1; i <= steps; i++) {
            int s = (curHour + i) % HOURS;
            hourBlocked[s] = hourAllowed[s] = 0;
        }
        curHour = h;
    }
    return curHour % HOURS;
}

void QueryLog::tick() { hourSlot(); }

void QueryLog::allowed() { hourAllowed[hourSlot()]++; }

void QueryLog::blocked(const char *dom, uint32_t ip, uint16_t qtype, uint8_t why) {
    hourBlocked[hourSlot()]++;
    uint32_t now = millis();

    Entry &e = recent[recentHead];
    strlcpy(e.dom, dom, DLEN);
    e.ip = ip;
    e.t = now;
    e.qtype = qtype;
    e.why = why;
    recentHead = (recentHead + 1) % RECENT;
    if (recentCount < RECENT)
        recentCount++;

    if (why != LIST || !*dom)
        return; // a banned client's queries would flood the top list
    // space-saving top-k: bump if tracked, else take a free slot, else evict the min
    uint32_t h = fnv(dom);
    int minI = 0;
    for (int i = 0; i < topCount; i++) {
        if (top[i].hash == h && strncmp(top[i].dom, dom, DLEN - 1) == 0) {
            top[i].count++;
            top[i].lastT = now;
            return;
        }
        if (top[i].count < top[minI].count)
            minI = i;
    }
    int slot;
    uint32_t base = 0;
    if (topCount < TOP) {
        slot = topCount++;
    } else {
        slot = minI;
        base = top[minI].count; // inherited count keeps the estimate an upper bound
    }
    strlcpy(top[slot].dom, dom, DLEN);
    top[slot].hash = h;
    top[slot].count = base + 1;
    top[slot].lastT = now;
}

void QueryLog::clear() {
    recentHead = recentCount = 0;
    topCount = 0;
}

static void jstr(String &o, const char *s) {
    o += '"';
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\')
            o += '\\';
        if ((uint8_t)c < 0x20)
            continue;
        o += c;
    }
    o += '"';
}

String QueryLog::json() {
    hourSlot();
    uint32_t now = millis();
    String o;
    o.reserve(RECENT * 110 + 20 * 100 + HOURS * 16 + 64);
    o += "{\"recent\":[";
    // newest first
    for (int k = 0; k < recentCount; k++) {
        const Entry &e = recent[(recentHead - 1 - k + RECENT) % RECENT];
        IPAddress ip(e.ip);
        if (k)
            o += ',';
        o += "{\"d\":";
        jstr(o, e.dom);
        o += ",\"ip\":\"" + ip.toString() + "\",\"age\":" + String((now - e.t) / 1000) +
             ",\"qt\":" + String(e.qtype) + ",\"why\":" + String(e.why) + "}";
    }
    // top 20 by count (selection over a copy of the indices)
    o += "],\"top\":[";
    int idx[TOP];
    for (int i = 0; i < topCount; i++)
        idx[i] = i;
    int n = topCount < 20 ? topCount : 20;
    for (int k = 0; k < n; k++) {
        int best = k;
        for (int i = k + 1; i < topCount; i++)
            if (top[idx[i]].count > top[idx[best]].count)
                best = i;
        int t = idx[k];
        idx[k] = idx[best];
        idx[best] = t;
        const Top &e = top[idx[k]];
        if (k)
            o += ',';
        o += "{\"d\":";
        jstr(o, e.dom);
        o += ",\"n\":" + String(e.count) + ",\"age\":" + String((now - e.lastT) / 1000) + "}";
    }
    // hourly history, oldest -> newest
    o += "],\"hours\":[";
    for (int k = HOURS - 1; k >= 0; k--) {
        int s = (curHour - k + HOURS * 2) % HOURS;
        if (k != HOURS - 1)
            o += ',';
        o += "[" + String(hourBlocked[s]) + "," + String(hourAllowed[s]) + "]";
    }
    o += "],\"uph\":" + String(curHour) + "}";
    return o;
}
