// Configuration for RepRapWiFi

#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#define NO_WIFI_SLEEP	0

#define VERSION_MAIN	"2.1beta7"

#if NO_WIFI_SLEEP
#define VERSION_SLEEP	"-nosleep"
#else
#define VERSION_SLEEP	""
#endif

#ifdef DEBUG
#define VERSION_DEBUG	"-D"
#else
#define VERSION_DEBUG	""
#endif

#include "driver/gpio.h"

const char* const firmwareVersion = VERSION_MAIN VERSION_DEBUG VERSION_SLEEP;

// Define the maximum length (bytes) of file upload data per SPI packet. Use a multiple of the SD card sector or cluster size for efficiency.
// ************ This must be kept in step with the corresponding value in RepRapFirmware *************
const uint32_t maxSpiFileData = 2048;

// Define the SPI clock register
// Useful values of the register are:
// 0x1001	40MHz 1:1
// 0x2001	26.7MHz 1:2
// 0x2402	26.7MHz 1:2
// 0x2002	26.7MHz 2:1
// 0x3043	20MHz 2:2

// The SAM occasionally transmits incorrect data at 40MHz, so we now use 26.7MHz.
// Due to the 15ns SCLK to MISO delay of the SAMD51, 2:1 is preferred over 1:2
const uint32_t defaultClockControl = 0x2002;		// 80MHz/3, mark:space 2:1

// Pin numbers
// SamSSPin - output to SAM, SS pin for SPI transfer
// EspReqTransfer - output, indicates to the SAM that we want to send something
// SamTfrReadyPin4 - input, indicates that SAM is ready to execute an SPI transaction
// OnboardLedPin - output, wifi connection indicator
#ifdef ESP8266
const gpio_num_t SamSSPin = GPIO_NUM_15;
const gpio_num_t EspReqTransferPin = GPIO_NUM_0;
const gpio_num_t SamTfrReadyPin = GPIO_NUM_4;
const gpio_num_t OnboardLedPin = GPIO_NUM_2;
#else

#if CONFIG_IDF_TARGET_ESP32C3
const gpio_num_t SamSSPin = GPIO_NUM_7;
const gpio_num_t EspReqTransferPin = GPIO_NUM_9;
const gpio_num_t SamTfrReadyPin = GPIO_NUM_10;
const gpio_num_t OnboardLedPin = GPIO_NUM_8;
#elif CONFIG_IDF_TARGET_ESP32S3
const gpio_num_t SamSSPin = GPIO_NUM_10;
const gpio_num_t EspReqTransferPin = GPIO_NUM_0;
const gpio_num_t SamTfrReadyPin = GPIO_NUM_8;
const gpio_num_t OnboardLedPin = GPIO_NUM_6;
#elif CONFIG_IDF_TARGET_ESP32
const gpio_num_t SamSSPin = GPIO_NUM_5;
const gpio_num_t EspReqTransferPin = GPIO_NUM_0;
const gpio_num_t SamTfrReadyPin = GPIO_NUM_4;
const gpio_num_t OnboardLedPin = GPIO_NUM_32;
#else
#error "pins not specifed for target chip"
#endif

#endif

const uint8_t Backlog = 8;

#define ARRAY_SIZE(_x) (sizeof(_x)/sizeof((_x)[0]))

#ifdef DEBUG
#include "rom/ets_sys.h"
#define debugPrint(_str)			ets_printf("%s(%d): %s", __FILE__, __LINE__, _str)
#define debugPrintf(_format, ...)	ets_printf("%s(%d): ", __FILE__, __LINE__); ets_printf(_format, __VA_ARGS__)
#else
#define debugPrint(_format)			do {} while(false)
#define debugPrintf(_format, ...)	do {} while(false)
#endif

#define debugPrintAlways(_str)			ets_printf("%s(%d): %s", __FILE__, __LINE__, _str)
#define debugPrintfAlways(_format, ...)	ets_printf("%s(%d): ", __FILE__, __LINE__); ets_printf(_format, __VA_ARGS__)


#define MAIN_PRIO								(ESP_TASK_TCPIP_PRIO + 1)
#define CONN_POLL_PRIO							(ESP_TASKD_EVENT_PRIO - 1)
#define CONNECTION_PRIO							(MAIN_PRIO + 1)
#define DNS_SERVER_PRIO							(ESP_TASK_MAIN_PRIO)


#ifdef ESP8266
#define CONN_POLL_STACK							(1492)
#define CONNECTION_TASK  						(742)
#define DNS_SERVER_STACK						(592)
#else
#define CONN_POLL_STACK							(2260)
#define CONNECTION_TASK	 						(1560)
#define DNS_SERVER_STACK						(1360)
#endif

#endif
