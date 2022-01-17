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

static bool wifi_set_sleep_type(sleep_type_t type) { return false; };
static station_status_t wifi_station_get_connect_status(void) { return STATION_IDLE; };
static phy_mode_t wifi_get_phy_mode(void) {return PHY_MODE_11B; };

#define STATION_IF      0x00
#define SOFTAP_IF       0x01

typedef signed char         sint8;
typedef unsigned short      uint16;
typedef unsigned char       uint8;

static sleep_type_t wifi_get_sleep_type(void) {return NONE_SLEEP_T; };
static uint16 system_get_vdd33(void) {return 0;};

static void system_phy_set_max_tpw(uint8 max_tpw) {};