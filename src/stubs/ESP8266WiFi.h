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

class IPAddress { //: public Printable {
    // private:
    //     union {
    //             uint8_t bytes[4];  // IPv4 address
    //             uint32_t dword;
    //     } _address;

    //     // Access the raw byte array containing the address.  Because this returns a pointer
    //     // to the internal structure rather than a copy of the address this function should only
    //     // be used when you know that the usage of the returned uint8_t* will be transient and not
    //     // stored.
    //     uint8_t* raw_address() {
    //         return _address.bytes;
    //     }

    public:
        // Constructors
        IPAddress();
        IPAddress(uint8_t first_octet, uint8_t second_octet, uint8_t third_octet, uint8_t fourth_octet);
        IPAddress(uint32_t address);
        IPAddress(const uint8_t *address);

        operator uint32_t() const {
            return 0;
        }

    //     bool fromString(const char *address);
    //     bool fromString(const String &address) { return fromString(address.c_str()); }

    //     // Overloaded cast operator to allow IPAddress objects to be used where a pointer
    //     // to a four-byte uint8_t array is expected
    //     operator uint32_t() const {
    //         return _address.dword;
    //     }
    //     bool operator==(const IPAddress& addr) const {
    //         return _address.dword == addr._address.dword;
    //     }
    //     bool operator==(uint32_t addr) const {
    //         return _address.dword == addr;
    //     }
    //     bool operator==(const uint8_t* addr) const;

    //     // Overloaded index operator to allow getting and setting individual octets of the address
    //     uint8_t operator[](int index) const {
    //         return _address.bytes[index];
    //     }
    //     uint8_t& operator[](int index) {
    //         return _address.bytes[index];
    //     }

        // Overloaded copy operators to allow initialisation of IPAddress objects from other types
        IPAddress& operator=(const uint8_t *address);
        IPAddress& operator=(uint32_t address);

    //     virtual size_t printTo(Print& p) const;
    //     String toString() const;

    //     friend class EthernetClass;
    //     friend class UDP;
    //     friend class Client;
    //     friend class Server;
    //     friend class DhcpClass;
    //     friend class DNSClient;
};

// extern const IPAddress INADDR_NONE;

class String {
    public:
        const char* c_str() const { return nullptr; }
};

class ESP8266WiFiClass {//: public ESP8266WiFiGenericClass, public ESP8266WiFiSTAClass, public ESP8266WiFiScanClass, public ESP8266WiFiAPClass {

    public:
        bool mode(WiFiMode_t);
        bool setAutoConnect(bool autoConnect);
        bool setAutoReconnect(bool autoReconnect);

        bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = (uint32_t)0x00000000, IPAddress dns2 = (uint32_t)0x00000000);

        wl_status_t begin(const char* ssid, const char *passphrase = NULL, int32_t channel = 0, const uint8_t* bssid = NULL, bool connect = true);

        int8_t scanNetworks(bool async = false, bool show_hidden = false);

        String SSID(uint8_t i) const;
        int32_t RSSI(uint8_t networkItem);

        bool softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet);
        bool softAP(const char* ssid, const char* passphrase = NULL, int channel = 1, int ssid_hidden = 0, int max_connection = 4);
        bool softAPdisconnect(bool wifioff = false);

        bool disconnect(bool wifioff = false);
        void persistent(bool persistent);
        IPAddress softAPIP();
        IPAddress localIP();

    // public:

    //     // workaround same function name with different signature
    //     using ESP8266WiFiGenericClass::channel;

    //     using ESP8266WiFiSTAClass::SSID;
    //     using ESP8266WiFiSTAClass::RSSI;
    //     using ESP8266WiFiSTAClass::BSSID;
    //     using ESP8266WiFiSTAClass::BSSIDstr;

    //     using ESP8266WiFiScanClass::SSID;
    //     using ESP8266WiFiScanClass::encryptionType;
    //     using ESP8266WiFiScanClass::RSSI;
    //     using ESP8266WiFiScanClass::BSSID;
    //     using ESP8266WiFiScanClass::BSSIDstr;
    //     using ESP8266WiFiScanClass::channel;
    //     using ESP8266WiFiScanClass::isHidden;

    //     // ----------------------------------------------------------------------------------------------
    //     // ------------------------------------------- Debug --------------------------------------------
    //     // ----------------------------------------------------------------------------------------------

    // public:

    //     void printDiag(Print& dest);

    //     friend class WiFiClient;
    //     friend class WiFiServer;

};

extern ESP8266WiFiClass WiFi;