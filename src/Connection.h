/*
 * Socket.h
 *
 *  Created on: 11 Apr 2017
 *      Author: David
 *
 * Simplified socket class to run in ESP8266 in Duet WiFi
 */

#ifndef SRC_CONNECTION_H_
#define SRC_CONNECTION_H_

#include <cstdint>
#include <cstddef>

#include "Config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/api.h"

#include "include/MessageFormats.h"			// for ConnState
#include "Listener.h"

#if SUPPORTS_TLS
struct mbedtls_ssl_context;
struct TlsBioState;
#endif

constexpr uint32_t MaxReadWriteTime = 2000;		// how long we wait for a write operation to complete before it is cancelled
constexpr uint32_t MaxAckTime = 4000;			// how long we wait for a connection to acknowledge the remaining data before it is closed
#if SUPPORTS_TLS
constexpr size_t TlsPlaintextBufSize = 2048;	// per-connection plaintext staging buffer for decrypted TLS data
constexpr uint32_t MaxHandshakeTime = 8000;		// upper bound on a deferred TLS handshake before the connection is dropped
#endif

class Connection
{
	friend Listener;

public:
	Connection(uint8_t num);

	// Public interface
	size_t Read(uint8_t *data, size_t length);
	size_t CanRead() const;
	size_t Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending);
	size_t CanWrite() const;

	bool Connect(uint8_t protocol, uint32_t remoteIp, uint16_t remotePort);
	void Close();
	void Terminate(bool external);
	void Deallocate();
	void GetStatus(ConnStatusResponse& resp) const;
	uint8_t GetNum() { return number; }

	// Static functions
	static Connection *Allocate();

	static void Init();
	static void PollAll();
	static void TerminateAll();

	static Connection& Get(uint8_t num) { return *connectionList[num]; }
	static void GetSummarySocketStatus(uint16_t& connectedSockets, uint16_t& otherEndClosedSockets);
	static void ReportConnections();

protected:
	void Accept(Listener *listener, struct netconn *conn, uint8_t protocol);

	static uint16_t CountConnectionsOnPort(uint16_t port);

private:
	uint8_t number;
	uint8_t protocol;
	uint16_t localPort;
	uint16_t remotePort;
	uint32_t remoteIp;
	struct netconn *conn;		// the pcb that corresponds to this connection
	Listener *listener;
	volatile ConnState state;

	uint32_t closeTimer;

	struct pbuf *readBuf;		// the buffers holding data we have received that has not yet been taken
	size_t readIndex;			// how much data we have already read from the current pbuf
	size_t alreadyRead;			// how much data we read from previous pbufs and didn't tell LWIP about yet
	bool pendOtherEndClosed;	// indicates that the other end has closed the connection, but changing the state
								// should wait after the data from this connection has all been read

#if SUPPORTS_TLS
	mbedtls_ssl_context *ssl;	// non-null when this connection is TLS-wrapped; replaces the pbuf chain
	TlsBioState *tlsBio;		// BIO state (netconn + pending pbuf cursor) - referenced by ssl via mbedtls_ssl_set_bio
	uint8_t *tlsPlain;			// decrypted plaintext staging buffer (TlsPlaintextBufSize bytes)
	size_t tlsPlainHead;		// next byte to read from tlsPlain
	size_t tlsPlainTail;		// next byte to fill in tlsPlain
	uint32_t handshakeStart;	// millis() when the deferred TLS handshake began - see MaxHandshakeTime
#endif

	void Poll();
	void SetState(ConnState st) { state = st; }
	void InitConnection(Listener *listener, struct netconn *conn);
	void Connected(Listener *listener, struct netconn *conn);
	void TerminateLocked(bool external);
	ConnState GetState() const { return state; }

#if SUPPORTS_TLS
	void FreeTls();
	bool StepHandshake();
	static bool PollHandshakes();
#endif

	static SemaphoreHandle_t allocateMutex;
#if SUPPORTS_TLS
	static SemaphoreHandle_t tlsHandshakeMutex;
#endif
	static Connection *connectionList[MaxConnections];

	void FreePbuf();
	void Report();

	static void ConnectCallback(struct netconn *conn, enum netconn_evt evt, u16_t len);

};

#endif /* SRC_CONNECTION_H_ */
