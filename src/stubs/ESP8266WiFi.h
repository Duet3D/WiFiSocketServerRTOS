#pragma once

class ESP8266WiFiClass {

    public:
        bool setAutoReconnect(bool autoReconnect) {return false;}
};

extern ESP8266WiFiClass WiFi;