#pragma once

typedef enum WiFiMode 
{
    WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3
} WiFiMode_t;

typedef enum {
    WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
    WL_IDLE_STATUS      = 0,
    WL_NO_SSID_AVAIL    = 1,
    WL_SCAN_COMPLETED   = 2,
    WL_CONNECTED        = 3,
    WL_CONNECT_FAILED   = 4,
    WL_CONNECTION_LOST  = 5,
    WL_DISCONNECTED     = 6
} wl_status_t;

class IPAddress {    
    public:
        IPAddress() {};
        IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet) {};
        IPAddress(uint32_t address) {};
        IPAddress(const uint8_t *address) {};

        operator uint32_t() const {
            return 0;
        }
        IPAddress& operator=(const uint8_t *address);
        IPAddress& operator=(uint32_t address);
};

class String {
    public:
        const char* c_str() const { return nullptr; }
};

class ESP8266WiFiClass {

    public:
        bool mode(WiFiMode_t) {return false;}
        bool setAutoConnect(bool autoConnect) {return false;}
        bool setAutoReconnect(bool autoReconnect) {return false;}

        bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = (uint32_t)0x00000000, IPAddress dns2 = (uint32_t)0x00000000) {return false;}

        wl_status_t begin(const char* ssid, const char *passphrase = NULL, int32_t channel = 0, const uint8_t* bssid = NULL, bool connect = true) {return WL_IDLE_STATUS;}

        int8_t scanNetworks(bool async = false, bool show_hidden = false) {return 0;}

        String SSID(uint8_t i) const;
        int32_t RSSI(uint8_t networkItem) {return 0;}

        bool softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet) {return false;}
        bool softAP(const char* ssid, const char* passphrase = NULL, int channel = 1, int ssid_hidden = 0, int max_connection = 4) {return false;}
        bool softAPdisconnect(bool wifioff = false) {return false;}

        bool disconnect(bool wifioff = false) {return false;}
        void persistent(bool persistent) {}
};

extern ESP8266WiFiClass WiFi;