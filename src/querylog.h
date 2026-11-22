// blocked-query insights: last N blocked queries, top blocked domains,
// and a rolling 24h per-hour blocked/allowed history (all in RAM, reset on reboot)
#pragma once
#include <Arduino.h>

struct QueryLog {
    static const int RECENT = 50;  // last blocked queries kept
    static const int TOP = 64;     // tracked domains for the top list (space-saving)
    static const int HOURS = 24;   // hourly history depth
    static const int DLEN = 72;    // stored domain length (longer names truncated)

    enum Why : uint8_t { LIST = 0, CLIENT_BANNED = 1 };

    struct Entry {
        char dom[DLEN];
        uint32_t ip;
        uint32_t t; // millis() when blocked
        uint16_t qtype;
        uint8_t why;
    };
    struct Top {
        char dom[DLEN];
        uint32_t hash; // quick compare before strcmp
        uint32_t count;
        uint32_t lastT;
    };

    Entry recent[RECENT];
    int recentHead = 0, recentCount = 0; // ring buffer
    Top top[TOP];
    int topCount = 0;
    uint32_t hourBlocked[HOURS] = {0}, hourAllowed[HOURS] = {0};
    uint32_t curHour = 0; // uptime hour the newest bucket belongs to

    void blocked(const char *dom, uint32_t ip, uint16_t qtype, uint8_t why);
    void allowed();
    void tick();  // roll the hourly buckets forward even when idle
    void clear(); // wipe recent + top (hourly history kept)
    String json(); // {"recent":[...],"top":[...],"hours":[[b,a],...]}

  private:
    int hourSlot();
};

extern QueryLog qlog;
