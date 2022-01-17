#pragma once

class String {
    public:
        const char* c_str() const { return nullptr; }
};

class ESP8266WiFiClass {

    public:
        bool setAutoReconnect(bool autoReconnect) {return false;}

        int8_t scanNetworks(bool async = false, bool show_hidden = false) {return 0;}

        String SSID(uint8_t i) const;
        int32_t RSSI(uint8_t networkItem) {return 0;}
};

extern ESP8266WiFiClass WiFi;