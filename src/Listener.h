/*
 * Listener.h
 *
 *  Created on: 12 Apr 2017
 *      Author: David
 */

#ifndef SRC_LISTENER_H_
#define SRC_LISTENER_H_

#include <cstdint>
#include <cstddef>

#include "include/MessageFormats.h"
#include "lwip/api.h"

class Listener
{
public:
	void Notify();

	static void Init();
	static bool Start(uint16_t port, uint32_t ip, int protocol, int maxConns, bool tls = false);
	static void Stop(uint16_t port);

	static uint16_t GetPortByProtocol(uint8_t protocol);
	static uint8_t Find(uint8_t port);

	bool IsTls() const { return tls; }

	static TaskHandle_t GetTaskHandle() { return listenTaskHandle; }

private:
	struct netconn *conn;

	uint32_t ip;
	uint16_t port;
	uint16_t maxConnections;
	uint8_t protocol;
	bool tls;

	void Stop();

	static TaskHandle_t listenTaskHandle;
	static Listener *listeners[MaxConnections];

	static void ListenerTask(void* data);
	static void ListenCallback(struct netconn *conn, enum netconn_evt evt, u16_t len);
};

#endif /* SRC_LISTENER_H_ */
