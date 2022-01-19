#pragma once


typedef enum {
	NONE_SLEEP_T	= 0,
	LIGHT_SLEEP_T,
	MODEM_SLEEP_T
} sleep_type_t;


static bool wifi_set_sleep_type(sleep_type_t type) { return false; };
static sleep_type_t wifi_get_sleep_type(void) {return NONE_SLEEP_T; };