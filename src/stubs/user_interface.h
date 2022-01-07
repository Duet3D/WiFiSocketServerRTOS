#pragma once


typedef enum {
    STATION_IDLE = 0,
    STATION_CONNECTING,
    STATION_WRONG_PASSWORD,
    STATION_NO_AP_FOUND,
    STATION_CONNECT_FAIL,
    STATION_GOT_IP
} station_status_t;

typedef enum {
	NONE_SLEEP_T	= 0,
	LIGHT_SLEEP_T,
	MODEM_SLEEP_T
} sleep_type_t;

typedef enum {
	PHY_MODE_11B	= 1,
	PHY_MODE_11G	= 2,
	PHY_MODE_11N    = 3
} phy_mode_t;

typedef unsigned int        uint32;

struct rst_info{
	uint32 reason;
	uint32 exccause;
	uint32 epc1;
	uint32 epc2;
	uint32 epc3;
	uint32 excvaddr;
	uint32 depc;
};

bool wifi_station_set_hostname(char *name);
bool wifi_set_sleep_type(sleep_type_t type);
station_status_t wifi_station_get_connect_status(void);
phy_mode_t wifi_get_phy_mode(void);
void system_soft_wdt_feed(void);

uint32 system_get_free_heap_size(void);
struct rst_info* system_get_rst_info(void);

#define STATION_IF      0x00
#define SOFTAP_IF       0x01

typedef signed char         sint8;
typedef unsigned short      uint16;
typedef unsigned char       uint8;

sint8 wifi_station_get_rssi(void);
sleep_type_t wifi_get_sleep_type(void);
uint16 system_get_vdd33(void);
bool wifi_get_macaddr(uint8 if_index, uint8 *macaddr);

void system_phy_set_max_tpw(uint8 max_tpw);
uint8 wifi_softap_get_station_num(void);