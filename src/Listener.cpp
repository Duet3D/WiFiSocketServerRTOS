/*
 * Listener.cpp
 *
 *  Created on: 12 Apr 2017
 *      Author: David
 */

#include "Listener.h"
#include "Connection.h"
#include "Config.h"


#include "lwip/api.h"
#include "lwip/tcp.h"

// Static member data
Listener *Listener::activeList = nullptr;
Listener *Listener::freeList = nullptr;

// Member functions
Listener::Listener()
	: next(nullptr), listeningPcb(nullptr), ip(0), port(0), maxConnections(0), protocol(0)
{
}

void Listener::Accept()
{
	if (listeningPcb != nullptr)
	{
		struct netconn *newConn;
		err_t rc = netconn_accept(listeningPcb, &newConn); // tell the listening PCB we have accepted the connectionv

		if (rc == ERR_OK) {
			// Allocate a free socket for this connection
			const uint16_t numConns = Connection::CountConnectionsOnPort(port);
			if (numConns < maxConnections)
			{
				Connection * const conn = Connection::Allocate();
				if (conn != nullptr)
				{
					netconn_set_nonblocking(newConn, 1);
					rc = conn->Accept(newConn);
					if (protocol == protocolFtpData)
					{
						debugPrintf("accept conn, stop listen on port %u\n", port);
						Stop();						// don't listen for further connections
					}

					if (rc == ERR_OK) {
						return;
					}
				}
				debugPrintfAlways("refused conn on port %u no free conn\n", port);
			}
			else
			{
				debugPrintfAlways("refused conn on port %u already %u conns\n", port, numConns);
			}

			netconn_close(newConn);
			netconn_delete(newConn);
		}
	}
	else
	{
		debugPrintfAlways("refused conn on port %u no pcb\n", port);
	}
}

void Listener::Stop()
{
	if (listeningPcb != nullptr)
	{
		netconn_close(listeningPcb);		// stop listening and free the PCB
		netconn_delete(listeningPcb);
		listeningPcb = nullptr;
	}
	Unlink(this);
	Release(this);
}

void Listener::Poll()
{
	for (Listener *p = activeList; p != nullptr; )
	{
		Listener *n = p->next;
		p->Accept();
		p = n;
	}
}

// Set up a listener on a port, returning true if successful, or stop listening of maxConnections = 0
/*static*/ bool Listener::Listen(uint32_t ip, uint16_t port, uint8_t protocol, uint16_t maxConns)
{
	// See if we are already listing for this
	for (Listener *p = activeList; p != nullptr; )
	{
		Listener *n = p->next;
		if (p->port == port)
		{
			if (maxConns != 0 && (p->ip == IPADDR_ANY || p->ip == ip))
			{
				// already listening, so nothing to do
				debugPrintf("already listening on port %u\n", port);
				return true;
			}
			if (maxConns == 0 || ip == IPADDR_ANY)
			{
				p->Stop();
				debugPrintf("stopped listening on port %u\n", port);
			}
		}
		p = n;
	}

	if (maxConns == 0)
	{
		return true;
	}


	// If we get here then we need to set up a new listener
	Listener * const p = Allocate();
	if (p == nullptr)
	{
		debugPrintAlways("can't allocate listener\n");
		return false;
	}
	p->ip = ip;
	p->port = port;
	p->protocol = protocol;
	p->maxConnections = maxConns;

	// Call LWIP to set up a listener
	struct netconn * tempPcb = netconn_new ( NETCONN_TCP );
	if (tempPcb == nullptr)
	{
		Release(p);
		debugPrintAlways("can't allocate PCB\n");
		return false;
	}
	netconn_set_nonblocking(tempPcb, 1);

	ip_addr_t tempIp;
	tempIp.u_addr.ip4.addr = ip;
	tempPcb->pcb.tcp->so_options |= SOF_REUSEADDR;			// not sure we need this, but the Arduino HTTP server does it
	err_t rc = netconn_bind(tempPcb, &tempIp, port);
	if (rc != ERR_OK)
	{
		netconn_close(tempPcb);
		netconn_delete(tempPcb);
		Release(p);
		debugPrintfAlways("can't bind PCB: %d\n", (int)rc);
		return false;
	}

	rc = netconn_listen_with_backlog(tempPcb, Backlog);
	if (rc != ERR_OK)
	{
		netconn_close(tempPcb);
		netconn_delete(tempPcb);
		Release(p);
		debugPrintAlways("tcp_listen failed\n");
		return false;
	}

	p->listeningPcb = tempPcb;
	// Don't call tcp_err in the LISTEN state because lwip gives us an assertion failure at tcp.s(1760)
	p->next = activeList;
	activeList = p;
	debugPrintf("listening on port %u\n", port);
	return true;
}

// Stop listening on the specified port, or on all ports if 'port' is zero
/*static*/ void Listener::StopListening(uint16_t port)
{
	for (Listener *p = activeList; p != nullptr; )
	{
		Listener *n = p->next;
		if (port == 0 || port == p->port)
		{
			p->Stop();
		}
		p = n;
	}
}

/*static*/ uint16_t Listener::GetPortByProtocol(uint8_t protocol)
{
	for (Listener *p = activeList; p != nullptr; p = p->next)
	{
		if (p->protocol == protocol)
		{
			return p->port;
		}
	}
	return 0;
}

/*static*/ Listener *Listener::Allocate()
{
	Listener *ret = freeList;
	if (ret != nullptr)
	{
		freeList = ret->next;
		ret->next = nullptr;
	}
	else
	{
		ret = new Listener;
	}
	return ret;
}

/*static*/ void Listener::Unlink(Listener *lst)
{
	Listener **pp = &activeList;
	while (*pp != nullptr)
	{
		if (*pp == lst)
		{
			*pp = lst->next;
			lst->next = nullptr;
			return;
		}
	}
}

/*static*/ void Listener::Release(Listener *lst)
{
	lst->next = freeList;
	freeList = lst;
}

// End
