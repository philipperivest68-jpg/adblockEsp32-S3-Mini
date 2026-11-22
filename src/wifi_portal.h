// WiFi: join saved/secrets creds, else run a captive portal to provision them
#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <Preferences.h>

#define DEVICE_NAME "S3MiniPihole" // setup hotspot SSID + hostname (mDNS/DHCP)

struct Wifi {
    Preferences prefs;
    WiFiUDP portalDns;      // captive DNS: every A query -> the AP address
    String portalOpts;      // <option> list of scanned networks

    bool hasCreds();
    bool connect();         // NVS creds first, then secrets.h fallback
    void runPortal();       // AP + captive portal, blocks, reboots on save
    void clearCreds();      // wipe saved WiFi, used by BOOT button and /forgetwifi
};

extern Wifi wifi;
