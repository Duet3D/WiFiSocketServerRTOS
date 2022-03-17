/*
 * SocketServer.cpp
 *
 *  Created on: 25 Mar 2017
 *      Author: David
 */

#include "ecv.h"
#undef yield
#undef array

#include <cstring>
#include <algorithm>

extern "C"
{
	#include "esp_task_wdt.h"
	#include "lwip/stats.h"			// for stats_display()
}

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_partition.h"
#include "driver/ledc.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "led_indicator.h"

#include "rom/ets_sys.h"
#include "driver/gpio.h"

#include "DNSServer.h"
#include "SocketServer.h"
#include "Config.h"
#include "PooledStrings.h"
#include "HSPI.h"

#include "include/MessageFormats.h"
#include "Connection.h"
#include "Listener.h"
#include "Misc.h"

#include "esp8266/spi.h"
#include "esp8266/gpio.h"

const gpio_num_t ONBOARD_LED_GPIO = GPIO_NUM_2;			// GPIO 2
led_indicator_handle_t ONBOARD_LED;

constexpr led_indicator_blink_type_t ONBOARD_LED_CONNECTING = BLINK_PROVISIONING;
constexpr led_indicator_blink_type_t ONBOARD_LED_CONNECTED = BLINK_CONNECTED;
constexpr led_indicator_blink_type_t ONBOARD_LED_IDLE = BLINK_PROVISIONED;

const uint32_t TransferReadyTimeout = 10;			// how many milliseconds we allow for the Duet to set TransferReady low after the end of a transaction, before we assume that we missed seeing it

const char * const MdnsServiceStrings[3] = { "_http", "_ftp", "_telnet" };
const mdns_txt_item_t MdnsTxtRecords[2] = { {"version", VERSION_MAIN}, {"product", "DuetWiFi"}, };

#define array _ecv_array

const uint32_t MaxConnectTime = 40 * 1000;		// how long we wait for WiFi to connect in milliseconds
const uint32_t StatusReportMillis = 200;

const int DefaultWiFiChannel = 6;

// Global data
static int currentSsid = -1;
char webHostName[HostNameLength + 1] = "Duet-WiFi";

DNSServer dns;

static const char* lastError = nullptr;
static const char* prevLastError = nullptr;
static WiFiState currentState = WiFiState::idle,
				lastReportedState = WiFiState::disabled;

static HSPIClass hspi;
static uint32_t transferBuffer[NumDwords(MaxDataLength + 1)];


static TaskHandle_t main_taskhdl;

#define CONNECT_EVT 0x01
#define CONNECT_EXPIRE 0x02

typedef enum {
	WIFI_IDLE = 0,
	STATION_CONNECTING,
	STATION_WRONG_PASSWORD,
	STATION_NO_AP_FOUND,
	STATION_CONNECT_TIMEOUT,
	STATION_CONNECT_FAIL,
	STATION_GOT_IP,
	AP_STARTED,
} wifi_status_t;

typedef enum {
	PHY_MODE_11B	= 1,
	PHY_MODE_11G	= 2,
	PHY_MODE_11N	= 3
} phy_mode_t;

#define TFR_REQUEST           0x01
#define TFR_REQUEST_TIMEOUT   0x02
#define SAM_TFR_READY         0x04

static TaskHandle_t connect_poll_taskhdl;

static nvs_handle_t ssids;
static const char* ssidsNs = "ssids";

bool GetSsidDataByIndex(int idx, WirelessConfigurationData& data)
{
	if (idx <= MaxRememberedNetworks) {
		size_t sz = sizeof(data);
		esp_err_t res = nvs_get_blob(ssids, std::to_string(idx).c_str(), &data, &sz);
		return (res == ESP_OK) && (sz == sizeof(data));
	}

	return false;
}

// Look up a SSID in our remembered network list, return pointer to it if found
int GetSsidDataByName(const char* ssid, WirelessConfigurationData& data)
{
	for (int i = MaxRememberedNetworks; i >= 0; i--)
	{
		WirelessConfigurationData d;
		if (GetSsidDataByIndex(i, d) && strncmp(ssid, d.ssid, sizeof(d.ssid)) == 0)
		{
			data = d;
			return i;
		}
	}
	return -1;
}

int FindEmptySsidEntry()
{
	for (int i = MaxRememberedNetworks; i >= 0; i--)
	{
		WirelessConfigurationData d;
		if (GetSsidDataByIndex(i, d) && d.ssid[0] == 0xFF)
		{
			return i;
		}
	}

	return -1;
}

bool SetSsidData(int idx, const WirelessConfigurationData& data, bool commit=true)
{
	if (idx <= MaxRememberedNetworks) {
		esp_err_t res = nvs_set_blob(ssids, std::to_string(idx).c_str(), &data, sizeof(data));

		if (res == ESP_OK && commit) {
			res = nvs_commit(ssids);
		}

		return res == ESP_OK;
	}

	return false;
}

bool EraseSsidData(int idx, bool commit = true)
{
	uint8_t clean[sizeof(WirelessConfigurationData)];
	memset(clean, 0xFF, sizeof(clean));
	const WirelessConfigurationData& d = *(reinterpret_cast<const WirelessConfigurationData*>(clean));
	return SetSsidData(idx, d, commit);
}

// Check socket number in range, returning true if yes. Otherwise, set lastError and return false;
bool ValidSocketNumber(uint8_t num)
{
	if (num < MaxConnections)
	{
		return true;
	}
	lastError = "socket number out of range";
	return false;
}

// Reset to default settings
void FactoryReset()
{
	for (int i = MaxRememberedNetworks; i >= 0; i--)
	{
		EraseSsidData(i, false);
	}

	nvs_commit(ssids);
}

static void wifi_evt_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	wifi_status_t wifiEvt = WIFI_IDLE;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		wifiEvt = STATION_CONNECTING;
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
		switch (disconnected->reason) {
			case WIFI_REASON_AUTH_EXPIRE:
			case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
			case WIFI_REASON_AUTH_FAIL:
			case WIFI_REASON_ASSOC_FAIL:
			case WIFI_REASON_HANDSHAKE_TIMEOUT:
				wifiEvt = STATION_WRONG_PASSWORD;
				break;
			case WIFI_REASON_NO_AP_FOUND:
			case WIFI_REASON_BEACON_TIMEOUT:
				wifiEvt = STATION_NO_AP_FOUND;
				break;
			case WIFI_REASON_ASSOC_LEAVE:
				wifiEvt = WIFI_IDLE;
				break;
			default:
				wifiEvt = STATION_CONNECT_FAIL;
				break;
		}
	} else if (event_base == WIFI_EVENT && (event_id == WIFI_EVENT_STA_STOP || event_id == WIFI_EVENT_AP_STOP)) {
		wifiEvt = WIFI_IDLE; 
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		wifiEvt = STATION_GOT_IP;
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
		wifiEvt = AP_STARTED;
	}

	xTaskNotify(connect_poll_taskhdl, wifiEvt, eSetValueWithOverwrite);
}

// Rebuild the mDNS services
void RebuildServices()
{
	mdns_service_remove_all();
	mdns_hostname_set(webHostName);
	for (size_t protocol = 0; protocol < 3; protocol++)
	{
		const uint16_t port = Listener::GetPortByProtocol(protocol);
		if (port != 0)
		{
			mdns_service_add(webHostName, MdnsServiceStrings[protocol], "_tcp", port,
							(protocol == 0/*HttpProtocol*/) ? (mdns_txt_item_t*)MdnsTxtRecords : nullptr,
							(protocol == 0/*HttpProtocol*/) ? 2 : 0);
		}
	}
}

void RemoveMdnsServices()
{
	mdns_service_remove_all();
	mdns_free();
}

// Try to connect using the specified SSID and password
void ConnectToAccessPoint(const WirelessConfigurationData& apData)
pre(currentState == NetworkState::idle)
{
	wifi_config_t wifi_config;
	SafeStrncpy((char*)wifi_config.sta.ssid, (char*)apData.ssid,
		std::min(sizeof(wifi_config.sta.ssid), sizeof(apData.ssid)));
	SafeStrncpy((char*)wifi_config.sta.password, (char*)apData.password,
		std::min(sizeof(wifi_config.sta.password), sizeof(apData.password)));
	esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);

	if (apData.ip != 0) {
		tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		tcpip_adapter_ip_info_t ip_info;
		ip_info.ip.addr = apData.ip;
		ip_info.gw.addr = apData.gateway;
		ip_info.netmask.addr = apData.netmask;
		tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
	}

	tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, webHostName);

	debugPrintf("Trying to connect to ssid \"%s\" with password \"%s\"\n", apData.ssid, apData.password);
	esp_wifi_connect();
}


void ConnectPoll(void* data)
{
	TimerHandle_t connect_expire_timer = xTimerCreate("connTmr", MaxConnectTime, pdFALSE, NULL, 
			[](void* data) { xTaskNotify(connect_poll_taskhdl, STATION_CONNECT_TIMEOUT, eSetBits); });

	static char lastConnectError[100];

	while(true)
	{
		uint32_t flags = 0;
		xTaskNotifyWait(0, UINT_MAX, &flags, portMAX_DELAY);

		WiFiState prevCurrentState = currentState;
		bool connectErrorChanged = false;

		// The Arduino WiFi.status() call is fairly useless here because it discards too much information, so use the SDK API call instead
		const wifi_status_t status = static_cast<wifi_status_t>(flags);
		const char *error = nullptr;
		bool retry = false;

		switch (currentState)
		{
		case WiFiState::connecting:
		case WiFiState::reconnecting:
			// We are trying to connect or reconnect, so check for success or failure
			switch (status)
			{
			case WIFI_IDLE:
				currentState = WiFiState::idle;	// cancelled connection/reconnection
				break;

			case STATION_CONNECT_TIMEOUT:
				error = "Timed out";
				break;

			case STATION_WRONG_PASSWORD:
				error = "Wrong password";
				break;

			case STATION_NO_AP_FOUND:
				error = "Didn't find access point";
				retry = (currentState == WiFiState::reconnecting);
				break;

			case STATION_CONNECT_FAIL:
				error = "Failed";
				retry = (currentState == WiFiState::reconnecting);
				break;

			case STATION_GOT_IP:
				xTimerStop(connect_expire_timer, portMAX_DELAY);
				if (currentState == WiFiState::reconnecting)
				{
					lastError = "Reconnect succeeded";
				}

				debugPrint("Connected to AP\n");
				currentState = WiFiState::connected;
				break;

			default:
				error = "Unknown WiFi state";
				break;
			}

			if (error != nullptr)
			{
				strcpy(lastConnectError, error);
				SafeStrncat(lastConnectError, " while trying to connect to ", ARRAY_SIZE(lastConnectError));
				WirelessConfigurationData wp;
				GetSsidDataByIndex(currentSsid, wp);
				SafeStrncat(lastConnectError, wp.ssid, ARRAY_SIZE(lastConnectError));
				lastError = lastConnectError;
				connectErrorChanged = true;
				debugPrint("Failed to connect to AP\n");

				if (!retry) {
					esp_wifi_stop();
				}
			}
			break;

		case WiFiState::connected:
			if (status == WIFI_IDLE) {
				currentState = WiFiState::idle;							// disconnected/stopped Wi-Fi
			} else if (status == STATION_WRONG_PASSWORD ||
						status == STATION_NO_AP_FOUND ||
						status == STATION_CONNECT_FAIL)
			{
				currentState = WiFiState::autoReconnecting;
				xTimerReset(connect_expire_timer, portMAX_DELAY);		// start the auto reconnect timer
				esp_wifi_connect();
				lastError = "Lost connection, auto reconnecting";
				debugPrint("Lost connection to AP\n");
				break;
			}
			break;

		case WiFiState::autoReconnecting:
			if (status == WIFI_IDLE) {
				currentState = WiFiState::idle;							// disconnected/stopped Wi-Fi
			} else if (status == STATION_GOT_IP) {
				xTimerStop(connect_expire_timer, portMAX_DELAY);
				lastError = "Auto reconnect succeeded";
				currentState = WiFiState::connected;
			} else if (status != STATION_CONNECTING) {
				if (status == STATION_CONNECT_TIMEOUT) {
					lastError = "Timed out trying to auto-reconnect";
				} else {
					lastError = "Auto reconnect failed, trying manual reconnect";
				}
				xTimerReset(connect_expire_timer, portMAX_DELAY);		// start the reconnect timer
				retry = true;
			}
			break;
		
		case WiFiState::idle:
			if (status == AP_STARTED) {
				currentState = WiFiState::runningAsAccessPoint;
			} else if (status == STATION_CONNECTING) {
				currentState = WiFiState::connecting;
				xTimerReset(connect_expire_timer, portMAX_DELAY);		// start the econnect timer
			}
			break;

		case WiFiState::runningAsAccessPoint:
			if (status == WIFI_IDLE) {
				currentState = WiFiState::idle;
			}

			break;
		default:
			break;
		}

		if (retry)
		{
			WirelessConfigurationData wp;
			GetSsidDataByIndex(currentSsid, wp);
			currentState = WiFiState::reconnecting;
			ConnectToAccessPoint(wp);
		}

		if (currentState != prevCurrentState) {
			led_indicator_blink_type_t new_blink;

			if (currentState == WiFiState::autoReconnecting ||
				currentState == WiFiState::connecting ||
				currentState == WiFiState::reconnecting)
			{
				new_blink = ONBOARD_LED_CONNECTING;
			} else if (currentState == WiFiState::connected || 
				currentState == WiFiState::runningAsAccessPoint) {
				new_blink = ONBOARD_LED_CONNECTED;
			} else {
				new_blink = ONBOARD_LED_IDLE;
			}

			led_indicator_stop(ONBOARD_LED, ONBOARD_LED_IDLE);
			led_indicator_stop(ONBOARD_LED, ONBOARD_LED_CONNECTING);
			led_indicator_stop(ONBOARD_LED, ONBOARD_LED_CONNECTED);
			led_indicator_start(ONBOARD_LED, new_blink);
		}

		if (lastError != prevLastError || currentState != prevCurrentState || 
			connectErrorChanged)
		{
			xTaskNotify(main_taskhdl, TFR_REQUEST, eSetBits);
		}
	}
}

void StartClient(const char * array ssid)
pre(currentState == WiFiState::idle)
{
	mdns_init();

	WirelessConfigurationData wp;

	esp_wifi_restore();
	esp_wifi_set_mode(WIFI_MODE_STA);
	esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
#if NO_WIFI_SLEEP
	esp_wifi_set_ps(WIFI_PS_NONE);
#else
	esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#endif

	if (ssid == nullptr || ssid[0] == 0)
	{
		esp_wifi_start();

		wifi_scan_config_t cfg;
		cfg.ssid = NULL;
		cfg.bssid = NULL;
		cfg.show_hidden = true;

		esp_err_t res = esp_wifi_scan_start(&cfg, true);

		if (res != ESP_OK) {
			lastError = "network scan failed";
			esp_wifi_stop();
			return;
		}

		uint16_t num_ssids = 0;
		esp_wifi_scan_get_ap_num(&num_ssids);

		wifi_ap_record_t *ap_records = (wifi_ap_record_t*) calloc(num_ssids, sizeof(wifi_ap_record_t));

		esp_wifi_scan_get_ap_records(&num_ssids, ap_records);

		// Find the strongest network that we know about
		int8_t strongestNetwork = -1;
		for (int8_t i = 0; i < num_ssids; ++i)
		{
			debugPrintfAlways("found network %s\n", ap_records[i].ssid);
			if (strongestNetwork < 0 || ap_records[i].rssi > ap_records[strongestNetwork].rssi)
			{
				WirelessConfigurationData temp;
				if (GetSsidDataByName((const char*)ap_records[i].ssid, temp) > 0)
				{
					strongestNetwork = i;
				}
			}
		}

		char ssid[SsidLength + 1] = { 0 };

		if (strongestNetwork >= 0) {
			SafeStrncpy(ssid, (const char*)ap_records[strongestNetwork].ssid,
						std::min(sizeof(ssid), sizeof(ap_records[strongestNetwork].ssid)));
		}

		free(ap_records);

		if (strongestNetwork < 0)
		{
			lastError = "no known networks found";
			esp_wifi_stop();
			return;
		}

		currentSsid = GetSsidDataByName(ssid, wp);
	}
	else
	{
		int idx = GetSsidDataByName(ssid, wp);
		if (idx <= 0)
		{
			lastError = "no data found for requested SSID";
			return;
		}

		esp_wifi_start();
		currentSsid = idx;
	}

	// ssidData contains the details of the strongest known access point
	ConnectToAccessPoint(wp);
}

bool CheckValidSSID(const char * array s)
{
	size_t len = 0;
	while (*s != 0)
	{
		if (*s < 0x20 || *s == 0x7F)
		{
			return false;					// bad character
		}
		++s;
		++len;
		if (len == SsidLength)
		{
			return false;					// ESP8266 core requires strlen(ssid) <= 31
		}
	}
	return len != 0;
}

bool CheckValidPassword(const char * array s)
{
	size_t len = 0;
	while (*s != 0)
	{
		if (*s < 0x20 || *s == 0x7F)
		{
			return false;					// bad character
		}
		++s;
		++len;
		if (len == PasswordLength)
		{
			return false;					// ESP8266 core requires strlen(password) <= 63
		}
	}
	return len == 0 || len >= 8;			// password must be empty or at least 8 characters (WPA2 restriction)
}

// Check that the access point data is valid
bool ValidApData(const WirelessConfigurationData &apData)
{
	// Check the IP address
	if (apData.ip == 0 || apData.ip == 0xFFFFFFFF)
	{
		return false;
	}

	// Check the channel. 0 means auto so it OK.
	if (apData.channel > 13)
	{
		return false;
	}

	return CheckValidSSID(apData.ssid) && CheckValidPassword(apData.password);
}

void StartAccessPoint()
{
	WirelessConfigurationData apData;
	if (GetSsidDataByIndex(0, apData) && ValidApData(apData))
	{
		esp_wifi_restore();
		esp_err_t res = esp_wifi_set_mode(WIFI_MODE_AP);

		if (res == ESP_OK)
		{
			wifi_config_t wifi_config;
			SafeStrncpy((char*)wifi_config.sta.ssid, apData.ssid,
				std::min(sizeof(wifi_config.sta.ssid), sizeof(apData.ssid)));
			SafeStrncpy((char*)wifi_config.sta.password, (char*)apData.password,
				std::min(sizeof(wifi_config.sta.password), sizeof(apData.password)));
			wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
			wifi_config.ap.channel = (apData.channel == 0) ? DefaultWiFiChannel : apData.channel;
			wifi_config.ap.max_connection = 4;

			res = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);

			if (res == ESP_OK)
			{
				tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);

				tcpip_adapter_ip_info_t ip_info;
				ip_info.ip.addr = apData.ip;
				ip_info.gw.addr = apData.gateway;
				IP4_ADDR(&ip_info.netmask, 255,255,255,0);
				res = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);

				tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);

				if (res == ESP_OK) {
					debugPrintf("Starting AP %s with password \"%s\"\n", apData.ssid, apData.password);
					res = esp_wifi_start();
				}

				if (res != ESP_OK)
				{
					debugPrintAlways("Failed to start AP\n");
				}
			}
			else
			{
				debugPrintAlways("Failed to set AP config\n");
			}
		}
		else
		{
			debugPrintAlways("Failed to set AP mode\n");
		}

		if (res == ESP_OK)
		{
			debugPrintAlways("AP started\n");
			dns.setErrorReplyCode(DNSReplyCode::NoError);
			ip_addr_t addr;
			addr.u_addr.ip4.addr = apData.ip;
			if (!dns.start(53, "*", addr))
			{
				lastError = "Failed to start DNS\n";
				debugPrintf("%s\n", lastError);
			}
			mdns_init();
		}
		else
		{
			lastError = "Failed to start access point";
			debugPrintf("%s\n", lastError);
		}
	}
	else
	{
		lastError = "invalid access point configuration";
		debugPrintf("%s\n", lastError);
	}
}

static union
{
	MessageHeaderSamToEsp hdr;			// the actual header
	uint32_t asDwords[headerDwords];	// to force alignment
} messageHeaderIn;

static union
{
	MessageHeaderEspToSam hdr;
	uint32_t asDwords[headerDwords];	// to force alignment
} messageHeaderOut;


// Send a response.
// 'response' is the number of byes of response if positive, or the error code if negative.
// Use only to respond to commands which don't include a data block, or when we don't want to read the data block.
void IRAM_ATTR SendResponse(int32_t response)
{
	(void)hspi.transfer32(response);
	if (response > 0)
	{
		hspi.transferDwords(transferBuffer, nullptr, NumDwords((size_t)response));
	}
}

// This is called when the SAM is asking to transfer data
void IRAM_ATTR ProcessRequest()
{
	// Set up our own headers
	messageHeaderIn.hdr.formatVersion = InvalidFormatVersion;
	messageHeaderOut.hdr.formatVersion = MyFormatVersion;
	messageHeaderOut.hdr.state = currentState;
	bool deferCommand = false;

	// Begin the transaction
	gpio_set_level(SamSSPin, 0);		// assert CS to SAM
	hspi.beginTransaction();

	// Exchange headers, except for the last dword which will contain our response
	hspi.transferDwords(messageHeaderOut.asDwords, messageHeaderIn.asDwords, headerDwords - 1);

	if (messageHeaderIn.hdr.formatVersion != MyFormatVersion)
	{
		SendResponse(ResponseBadRequestFormatVersion);
	}
	else if (messageHeaderIn.hdr.dataLength > MaxDataLength)
	{
		SendResponse(ResponseBadDataLength);
	}
	else
	{
		const size_t dataBufferAvailable = std::min<size_t>(messageHeaderIn.hdr.dataBufferAvailable, MaxDataLength);

		// See what command we have received and take appropriate action
		switch (messageHeaderIn.hdr.command)
		{
		case NetworkCommand::nullCommand:					// no command being sent, SAM just wants the network status
			SendResponse(ResponseEmpty);
			break;

		case NetworkCommand::networkStartClient:			// connect to an access point
			if (currentState == WiFiState::idle)
			{
				deferCommand = true;
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				if (messageHeaderIn.hdr.dataLength != 0 && messageHeaderIn.hdr.dataLength <= SsidLength + 1)
				{
					hspi.transferDwords(nullptr, transferBuffer, NumDwords(messageHeaderIn.hdr.dataLength));
					reinterpret_cast<char *>(transferBuffer)[messageHeaderIn.hdr.dataLength] = 0;
				}
			}
			else
			{
				SendResponse(ResponseWrongState);
			}
			break;

		case NetworkCommand::networkStartAccessPoint:		// run as an access point
			if (currentState == WiFiState::idle)
			{
				deferCommand = true;
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			}
			else
			{
				SendResponse(ResponseWrongState);
			}
			break;

		case NetworkCommand::networkFactoryReset:			// clear remembered list, reset factory defaults
			deferCommand = true;
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			break;

		case NetworkCommand::networkStop:					// disconnect from an access point, or close down our own access point
			deferCommand = true;
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			break;

		case NetworkCommand::networkGetStatus:				// get the network connection status
			{
				const bool runningAsAp = (currentState == WiFiState::runningAsAccessPoint);
				const bool runningAsStation = (currentState == WiFiState::connected);
				NetworkStatusResponse * const response = reinterpret_cast<NetworkStatusResponse*>(transferBuffer);
				response->freeHeap = esp_get_free_heap_size();

				switch (esp_reset_reason())
				{
				case ESP_RST_POWERON:
					response->resetReason = 0; // Power-on
					break;
				case ESP_RST_WDT:
					response->resetReason = 1; // Hardware watchdog
					break;
				case ESP_RST_PANIC:
					response->resetReason = 2; // Exception
					break;
				case ESP_RST_TASK_WDT:
				case ESP_RST_INT_WDT:
					response->resetReason = 3; // Software watchdog
					break;
				case ESP_RST_SW:
				case ESP_RST_FAST_SW:
					response->resetReason = 4; // Software-initiated reset
					break;
				case ESP_RST_DEEPSLEEP:
					response->resetReason = 5; // Wake from deep-sleep
					break;
				case ESP_RST_EXT:
					response->resetReason = 6; // External reset
					break;
				case ESP_RST_BROWNOUT:
					response->resetReason = 7; // Brownout
					break;
				case ESP_RST_SDIO:
					response->resetReason = 8; // SDIO
					break;
				case ESP_RST_UNKNOWN:
				default:
					response->resetReason = 9; // Out-of-range, translates to 'Unknown' in RRF
					break;
				}

				if (runningAsStation) {
					wifi_ap_record_t ap_info;
					esp_wifi_sta_get_ap_info(&ap_info);
					response->rssi = ap_info.rssi; 
					response->numClients = 0;
					esp_wifi_get_mac(WIFI_IF_STA, response->macAddress);
					tcpip_adapter_ip_info_t ip_info;
					tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
					response->ipAddress = ip_info.ip.addr;
				} else if (runningAsAp) {
					wifi_sta_list_t sta_list;
					esp_wifi_ap_get_sta_list(&sta_list);

					response->numClients = sta_list.num; 
					response->rssi = 0;
					esp_wifi_get_mac(WIFI_IF_AP, response->macAddress);
					tcpip_adapter_ip_info_t ip_info;
					tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
					response->ipAddress = ip_info.ip.addr;
				} else {
					response->ipAddress = 0;
				}

				response->flashSize = spi_flash_get_chip_size();

				wifi_ps_type_t ps = WIFI_PS_NONE;
				esp_wifi_get_ps(&ps);

				switch (ps)
				{
				case WIFI_PS_NONE:
					response->sleepMode = 1;
					break;
				case WIFI_PS_MIN_MODEM:
					response->sleepMode = 3;
					break;
				default:	
					// sleepMode = 2 (light sleep) is not set by firmware.
					break;
				}

				uint8_t phyMode;	
				esp_wifi_get_protocol(ESP_IF_WIFI_STA, &phyMode);


				if (phyMode | WIFI_PROTOCOL_11N) {
					response->phyMode = PHY_MODE_11N;
				} else if (phyMode | WIFI_PROTOCOL_11G) {
					response->phyMode = PHY_MODE_11G;
				} else if (phyMode | WIFI_PROTOCOL_11B) {
					response->phyMode = PHY_MODE_11B;
				}

				response->zero1 = 0;
				response->zero2 = 0;
				response->vcc = esp_wifi_get_vdd33();
				WirelessConfigurationData wp;
				GetSsidDataByIndex(currentSsid, wp);
				SafeStrncpy(response->versionText, firmwareVersion, sizeof(response->versionText));
				SafeStrncpy(response->hostName, webHostName, sizeof(response->hostName));
				SafeStrncpy(response->ssid, wp.ssid, sizeof(response->ssid));
				response->clockReg = REG(SPI_CLOCK(HSPI));
				SendResponse(sizeof(NetworkStatusResponse));
			}
			break;

		case NetworkCommand::networkAddSsid:				// add to our known access point list
		case NetworkCommand::networkConfigureAccessPoint:	// configure our own access point details
			if (messageHeaderIn.hdr.dataLength == sizeof(WirelessConfigurationData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(sizeof(WirelessConfigurationData)));
				const WirelessConfigurationData * const receivedClientData = reinterpret_cast<const WirelessConfigurationData *>(transferBuffer);
				int index;
				if (messageHeaderIn.hdr.command == NetworkCommand::networkConfigureAccessPoint)
				{
					index = 0;
				}
				else
				{
					WirelessConfigurationData d;
					index = GetSsidDataByName(receivedClientData->ssid, d);
					if (index < 0)
					{
						index = FindEmptySsidEntry();
						if (index == 0) { // reserved for AP details
							index = -1;
						}
					}
				}

				if (index >= 0)
				{
					SetSsidData(index, *receivedClientData);
				}
				else
				{
					lastError = "SSID table full";
				}
			}
			else
			{
				SendResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkDeleteSsid:				// delete a network from our access point list
			if (messageHeaderIn.hdr.dataLength == SsidLength)
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(SsidLength));

				WirelessConfigurationData d;
				int index = GetSsidDataByName(reinterpret_cast<char*>(transferBuffer), d);

				if (index >= 0)
				{
					EraseSsidData(index);
				}
				else
				{
					lastError = "SSID not found";
				}
			}
			else
			{
				SendResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkRetrieveSsidData:	// list the access points we know about, including our own access point details
			if (dataBufferAvailable < ReducedWirelessConfigurationDataSize)
			{
				SendResponse(ResponseBufferTooSmall);
			}
			else
			{
				char *p = reinterpret_cast<char*>(transferBuffer);
				for (size_t i = 0; i <= MaxRememberedNetworks && (i + 1) * ReducedWirelessConfigurationDataSize <= dataBufferAvailable; ++i)
				{

					WirelessConfigurationData tempData;
					GetSsidDataByIndex(i, tempData);
					if (tempData.ssid[0] != 0xFF)
					{
						memcpy(p, &tempData, ReducedWirelessConfigurationDataSize);
						p += ReducedWirelessConfigurationDataSize;
					}
					else if (i == 0)
					{
						memset(p, 0, ReducedWirelessConfigurationDataSize);
						p += ReducedWirelessConfigurationDataSize;
					}
				}
				const size_t numBytes = p - reinterpret_cast<char*>(transferBuffer);
				SendResponse(numBytes);
			}
			break;

		case NetworkCommand::networkListSsids_deprecated:	// list the access points we know about, plus our own access point details
			{
				char *p = reinterpret_cast<char*>(transferBuffer);
				for (size_t i = 0; i <= MaxRememberedNetworks; ++i)
				{
					WirelessConfigurationData tempData;
					GetSsidDataByIndex(i, tempData);
					if (tempData.ssid[0] != 0xFF)
					{
						for (size_t j = 0; j < SsidLength && tempData.ssid[j] != 0; ++j)
						{
							*p++ = tempData.ssid[j];
						}
						*p++ = '\n';
					}
					else if (i == 0)
					{
						// Include an empty entry for our own access point SSID
						*p++ = '\n';
					}
				}
				*p++ = 0;
				const size_t numBytes = p - reinterpret_cast<char*>(transferBuffer);
				if (numBytes <= dataBufferAvailable)
				{
					SendResponse(numBytes);
				}
				else
				{
					SendResponse(ResponseBufferTooSmall);
				}
			}
			break;

		case NetworkCommand::networkSetHostName:			// set the host name
			if (messageHeaderIn.hdr.dataLength == HostNameLength)
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(HostNameLength));
				memcpy(webHostName, transferBuffer, HostNameLength);
				webHostName[HostNameLength] = 0;			// ensure null terminator
			}
			else
			{
				SendResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkGetLastError:
			if (lastError == nullptr)
			{
				SendResponse(0);
			}
			else
			{
				const size_t len = strlen(lastError) + 1;
				if (dataBufferAvailable >= len)
				{
					strcpy(reinterpret_cast<char*>(transferBuffer), lastError);		// copy to 32-bit aligned buffer
					SendResponse(len);
				}
				else
				{
					SendResponse(ResponseBufferTooSmall);
				}
				lastError = nullptr;
			}
			lastReportedState = currentState;
			break;

		case NetworkCommand::networkListen:				// listen for incoming connections
			if (messageHeaderIn.hdr.dataLength == sizeof(ListenOrConnectData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				ListenOrConnectData lcData;
				hspi.transferDwords(nullptr, reinterpret_cast<uint32_t*>(&lcData), NumDwords(sizeof(lcData)));
				const bool ok = Listener::Listen(lcData.remoteIp, lcData.port, lcData.protocol, lcData.maxConnections);
				if (ok)
				{
					if (lcData.protocol < 3)			// if it's FTP, HTTP or Telnet protocol
					{
						RebuildServices();				// update the MDNS services
					}
					debugPrintf("%sListening on port %u\n", (lcData.maxConnections == 0) ? "Stopped " : "", lcData.port);
				}
				else
				{
					lastError = "Listen failed";
					debugPrint("Listen failed\n");
				}
			}
			break;

#if 0	// We don't use the following command, instead we use networkListen with maxConnections = 0
		case NetworkCommand::unused_networkStopListening:
			if (messageHeaderIn.hdr.dataLength == sizeof(ListenOrConnectData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				ListenOrConnectData lcData;
				hspi.transferDwords(nullptr, reinterpret_cast<uint32_t*>(&lcData), NumDwords(sizeof(lcData)));
				Listener::StopListening(lcData.port);
				RebuildServices();						// update the MDNS services
				debugPrintf("Stopped listening on port %u\n", lcData.port);
			}
			break;
#endif

		case NetworkCommand::connAbort:					// terminate a socket rudely
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				Connection::Get(messageHeaderIn.hdr.socketNumber).Close(true);
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connClose:					// close a socket gracefully
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				Connection::Get(messageHeaderIn.hdr.socketNumber).Close(true);
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connRead:					// read data from a connection
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				const size_t amount = conn.Read(reinterpret_cast<uint8_t *>(transferBuffer), std::min<size_t>(messageHeaderIn.hdr.dataBufferAvailable, MaxDataLength));
				messageHeaderIn.hdr.param32 = hspi.transfer32(amount);
				hspi.transferDwords(transferBuffer, nullptr, NumDwords(amount));
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connWrite:					// write data to a connection
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				const size_t requestedlength = messageHeaderIn.hdr.dataLength;
				const size_t acceptedLength = std::min<size_t>(conn.CanWrite(), std::min<size_t>(requestedlength, MaxDataLength));
				const bool closeAfterSending = (acceptedLength == requestedlength) && (messageHeaderIn.hdr.flags & MessageHeaderSamToEsp::FlagCloseAfterWrite) != 0;
				const bool push = (acceptedLength == requestedlength) && (messageHeaderIn.hdr.flags & MessageHeaderSamToEsp::FlagPush) != 0;
				messageHeaderIn.hdr.param32 = hspi.transfer32(acceptedLength);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(acceptedLength));
				const size_t written = conn.Write(reinterpret_cast<uint8_t *>(transferBuffer), acceptedLength, push, closeAfterSending);
				if (written != acceptedLength)
				{
					lastError = "incomplete write";
				}
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connGetStatus:				// get the status of a socket, and summary status for all sockets
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(sizeof(ConnStatusResponse));
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				ConnStatusResponse resp;
				conn.Poll();
				conn.GetStatus(resp);
				Connection::GetSummarySocketStatus(resp.connectedSockets, resp.otherEndClosedSockets);
				hspi.transferDwords(reinterpret_cast<const uint32_t *>(&resp), nullptr, NumDwords(sizeof(resp)));
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::diagnostics:					// print some debug info over the UART line
			SendResponse(ResponseEmpty);
			deferCommand = true;							// we need to send the diagnostics after we have sent the response, so the SAM is ready to receive them
			break;

		case NetworkCommand::networkSetTxPower:
			{
				const uint8_t txPower = messageHeaderIn.hdr.flags;
				if (txPower <= 82)
				{
					esp_wifi_set_max_tx_power(txPower);
					SendResponse(ResponseEmpty);
				}
				else
				{
					SendResponse(ResponseBadParameter);
				}
			}
			break;

		case NetworkCommand::networkSetClockControl:
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			deferCommand = true;
			break;

		case NetworkCommand::connCreate:					// create a connection
			// Not implemented yet
		default:
			SendResponse(ResponseUnknownCommand);
			break;
		}
	}

	gpio_set_level(SamSSPin, 1);			// de-assert CS to SAM to end the transaction and tell SAM the transfer is complete
	hspi.endTransaction();

	// If we deferred the command until after sending the response (e.g. because it may take some time to execute), complete it now
	if (deferCommand)
	{
		// The following functions must set up lastError if an error occurs
		lastError = nullptr;								// assume no error
		switch (messageHeaderIn.hdr.command)
		{
		case NetworkCommand::networkStartClient:			// connect to an access point
			if (messageHeaderIn.hdr.dataLength == 0 || reinterpret_cast<const char*>(transferBuffer)[0] == 0)
			{
				StartClient(nullptr);						// connect to strongest known access point
			}
			else
			{
				StartClient(reinterpret_cast<const char*>(transferBuffer));		// connect to specified access point
			}
			break;

		case NetworkCommand::networkStartAccessPoint:		// run as an access point
			StartAccessPoint();
			break;

		case NetworkCommand::networkStop:					// disconnect from an access point, or close down our own access point
			Connection::TerminateAll();						// terminate all connections
			Listener::StopListening(0);						// stop listening on all ports
			RebuildServices();								// remove the MDNS services
			switch (currentState)
			{
			case WiFiState::connected:
			case WiFiState::connecting:
			case WiFiState::reconnecting:
				RemoveMdnsServices();
				delay(20);									// try to give lwip time to recover from stopping everything
				esp_wifi_stop();
				break;

			case WiFiState::runningAsAccessPoint:
				dns.stop();
				delay(20);									// try to give lwip time to recover from stopping everything
				esp_wifi_stop();
				break;

			default:
				break;
			}
			delay(100);
			break;

		case NetworkCommand::networkFactoryReset:			// clear remembered list, reset factory defaults
			FactoryReset();
			break;

		case NetworkCommand::diagnostics:
			Connection::ReportConnections();
			delay(20);										// give the Duet main processor time to digest that
			stats_display();
			break;

		case NetworkCommand::networkSetClockControl:
			hspi.setClockDivider(messageHeaderIn.hdr.param32);
			break;

		default:
			lastError = "bad deferred command";
			break;
		}
	}

	if (lastError != prevLastError) {
		xTaskNotify(main_taskhdl, TFR_REQUEST, eSetBits);
	}
}

static TimerHandle_t tfrReqExpTmr;

void IRAM_ATTR TransferReadyIsr(void* p)
{
	BaseType_t wake = pdFALSE;
	xTaskNotifyFromISR(main_taskhdl, SAM_TFR_READY, eSetBits, &wake);
	if( wake == pdTRUE )
	{
		portYIELD_FROM_ISR();
	}
}

void setup()
{
	main_taskhdl = xTaskGetCurrentTaskHandle();

	tcpip_adapter_init();

	nvs_flash_init();
	nvs_open(ssidsNs, NVS_READWRITE, &ssids);
	nvs_iterator_t savedSsids = nvs_entry_find("nvs", ssidsNs, NVS_TYPE_ANY);
	if (!savedSsids) {
		FactoryReset();
	}
	nvs_release_iterator(savedSsids);

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_evt_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_evt_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP, &wifi_evt_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_evt_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &wifi_evt_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &wifi_evt_handler, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	cfg.nvs_enable = false;
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_log_level_set("wifi", ESP_LOG_NONE);


	// Set up the SPI subsystem
	gpio_reset_pin(SamTfrReadyPin);
	gpio_set_direction(SamTfrReadyPin, GPIO_MODE_INPUT);

	gpio_reset_pin(EspReqTransferPin);
	gpio_set_direction(EspReqTransferPin, GPIO_MODE_OUTPUT);
	gpio_set_level(EspReqTransferPin, 0);

	gpio_reset_pin(SamSSPin);
	gpio_set_direction(SamSSPin, GPIO_MODE_OUTPUT);
	gpio_set_level(SamSSPin, 1);

	// Set up the fast SPI channel
	hspi.InitMaster(SPI_MODE1, defaultClockControl, true);

	Connection::Init();
	Listener::Init();
	lastError = nullptr;
	debugPrint("Init completed\n");

	led_indicator_config_t onboard_led_cfg;
	onboard_led_cfg.off_level = 1;	// active low
	onboard_led_cfg.mode = LED_GPIO_MODE;

	ONBOARD_LED = led_indicator_create(ONBOARD_LED_GPIO, &onboard_led_cfg);
	led_indicator_start(ONBOARD_LED, ONBOARD_LED_IDLE);

	xTaskCreate(ConnectPoll, "connPoll", 1536, NULL, CONN_POLL_PRIO, &connect_poll_taskhdl);

	tfrReqExpTmr = xTimerCreate("tfrReqTmr", StatusReportMillis, pdFALSE, NULL, 
			[](void* data) { xTaskNotify(main_taskhdl, TFR_REQUEST_TIMEOUT, eSetBits); });
	xTimerStart(tfrReqExpTmr, portMAX_DELAY);

	gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
	gpio_isr_handler_add(SamTfrReadyPin, TransferReadyIsr, nullptr);
	gpio_set_intr_type(SamTfrReadyPin, GPIO_INTR_POSEDGE);
	gpio_set_level(EspReqTransferPin, 1);					// tell the SAM we are ready to receive a command
}

void IRAM_ATTR loop()
{
	// See whether there is a request from the SAM.
	// Duet WiFi 1.04 and earlier have hardware to ensure that TransferReady goes low when a transaction starts.
	// Duet 3 Mini doesn't, so we need to see TransferReady go low and then high again. In case that happens so fast that we dn't get the interrupt, we have a timeout.
	uint32_t flags = 0;
	xTaskNotifyWait(0, UINT_MAX, &flags, TransferReadyTimeout);

	if ((flags & TFR_REQUEST) || ((flags & TFR_REQUEST_TIMEOUT) &&
		(lastError != nullptr || currentState != lastReportedState) ))
	{
		ets_delay_us(2);									// make sure the pin stays high for long enough for the SAM to see it
		gpio_set_level(EspReqTransferPin, 0);			// force a low to high transition to signal that an error message is available
		ets_delay_us(2);									// make sure it is low enough to create an interrupt when it goes high
		gpio_set_level(EspReqTransferPin, 1);			// tell the SAM we are ready to receive a command
		prevLastError = lastError;
		xTimerReset(tfrReqExpTmr, portMAX_DELAY);
	}

	if (gpio_get_level(SamTfrReadyPin) == 1 && 
		(flags == 0 || (flags & SAM_TFR_READY))) {
		ProcessRequest();
	}
}

// End
