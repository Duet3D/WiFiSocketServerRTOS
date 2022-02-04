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

#include "lwip/api.h"

class Listener
{
public:
	Listener();
	void Accept();

	static void Init() { }
	static void Poll();
	static bool Listen(uint32_t ip, uint16_t port, uint8_t protocol, uint16_t maxConns);
	static void StopListening(uint16_t port);
	static uint16_t GetPortByProtocol(uint8_t protocol);

private:
	void Stop();

	static Listener *Allocate();
	static void Unlink(Listener *lst);
	static void Release(Listener *lst);

	Listener *next;
	netconn *listeningPcb;
	uint32_t ip;
	uint16_t port;
	uint16_t maxConnections;
	uint8_t protocol;

	static Listener *activeList;
	static Listener *freeList;
};

#endif /* SRC_LISTENER_H_ */
