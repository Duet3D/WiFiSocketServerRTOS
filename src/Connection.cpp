/*
 * Socket.cpp
 *
 *  Created on: 11 Apr 2017
 *      Author: David
 */
#include <cstring> 			// memcpy
#include <algorithm>			// for std::min

#include "lwip/tcp.h"

#include "Listener.h"
#include "Connection.h"
#include "Misc.h"				// for millis
#include "Config.h"


typedef enum
{
	Accept,
	Close,
	Terminate,
} ConnectionEventType;

typedef struct
{
	ConnectionEventType type;
	union {
		int i;
		void* ptr;
	} data;
} ConnectionEvent;

static const uint32_t MaxAckTime = 4000;		// how long we wait for a connection to acknowledge the remaining data before it is closed

// Public interface
Connection::Connection(uint8_t num)
	: number(num), localPort(0), remotePort(0), remoteIp(0), conn(nullptr), state(ConnState::free),
	readBuf(nullptr), readIndex(0), alreadyRead(0)
{
}

size_t Connection::Read(uint8_t *data, size_t length)
{
	size_t lengthRead = 0;
	if (readBuf != nullptr && length != 0 && (state == ConnState::connected || state == ConnState::otherEndClosed))
	{
		do
		{
			const size_t toRead = std::min<size_t>(readBuf->len - readIndex, length);
			memcpy(data + lengthRead, (uint8_t *)readBuf->payload + readIndex, toRead);
			lengthRead += toRead;
			readIndex += toRead;
			length -= toRead;
			if (readIndex != readBuf->len)
			{
				break;
			}
			pbuf * const currentPb = readBuf;
			readBuf = readBuf->next;
			currentPb->next = nullptr;
			pbuf_free(currentPb);
			readIndex = 0;
		} while (readBuf != nullptr && length != 0);

		alreadyRead += lengthRead;
		if (readBuf == nullptr || alreadyRead >= TCP_MSS)
		{
			netconn_tcp_recvd(conn, alreadyRead);
			alreadyRead = 0;
		}
	}
	return lengthRead;
}

size_t Connection::CanRead() const
{
	return ((state == ConnState::connected || state == ConnState::otherEndClosed) && readBuf != nullptr)
			? readBuf->tot_len - readIndex : 0;
}

// Write data to the connection. The amount of data may be zero.
// A note about writing:
// - LWIP is compiled with option LWIP_NETIF_TX_SINGLE_PBUF set. A comment says this is mandatory for the ESP8266.
// - A side effect of this is that when we call tcp_write, the data is always copied even if we don't set the TCP_WRITE_FLAG_COPY flag.
// - The PBUFs used to copy the outgoing data into are always large enough to accommodate the MSS. The total allocation size per PBUF is 1560 bytes.
// - Sending a full 2K of data may require 2 of these PBUFs to be allocated.
// - Due to memory fragmentation and other pending packets, this allocation is sometimes fails if we are serving more than 2 files at a time.
// - The result returned by tcp_sndbuf doesn't take account of the possibility that this allocation may fail.
// - When it receives a write request from the Duet main processor, our socket server has to say how much data it can accept before accepting it.
// - So in version 1.21 it sometimes happened that we accept some data based on the amount that tcp_sndbuf say we can, but we can't actually send it.
// - We then terminate the connection, and the client request fails.
// To mitigate this we could:
// - Have one overflow write buffer, shared between all connections
// - Only accept write data from the Duet main processor if the overflow buffer is free
// - If after accepting data from the Duet main processor we find that we can't send it, we send some of it if we can and store the rest in the overflow buffer
// - Then we push any pending data that we already have, and in Poll() we try to send the data in overflow buffer
// - When the overflow buffer is empty again, we can start accepting write data from the Duet main processor again.
// A further mitigation would be to restrict the amount of data we accept so some amount that will fit in the MSS, then tcp_write will need to allocate at most one PBUF.
// However, another reason why tcp_write can fail is because MEMP_NUM_TCP_SEG is set too low in Lwip. It now appears that this is the maoin cause of files tcp_write
// call in version 1.21. So I have increased it from 10 to 16, which seems to have fixed the problem..
size_t Connection::Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending)
{
	if (state != ConnState::connected)
	{
		return 0;
	}

	// Try to send all the data
	const bool push = doPush || closeAfterSending;

	u8_t flag = NETCONN_COPY | (push ? NETCONN_MORE : 0);

	size_t total = 0;
	size_t written = 0;
	err_t rc = ERR_OK;

	for( ; total < length; total += written) {
		written = 0;
		rc = netconn_write_partly(conn, data + total, length - total, flag, &written);

		if (rc != ERR_OK && rc != ERR_WOULDBLOCK) {
			break;
		}
	}

	if (rc != ERR_OK)
	{
		if (rc == ERR_RST || rc == ERR_CLSD)
		{
			SetState(ConnState::otherEndClosed);
		}
		else
		{
			// We failed to write the data. See above for possible mitigations. For now we just terminate the connection.
			debugPrintfAlways("Write fail len=%u err=%d\n", total, (int)rc);
			Terminate(false);		// chrishamm: Not sure if this helps with LwIP v1.4.3 but it is mandatory for proper error handling with LwIP 2.0.3
			return 0;
		}
	}

	// Close the connection again when we're done
	if (closeAfterSending)
	{
		Close();
	}

	return length;
}

size_t Connection::CanWrite() const
{
	// Return the amount of free space in the write buffer
	// Note: we cannot necessarily write this amount, because it depends on memory allocations being successful.
	return ((state == ConnState::connected) && conn->pcb.tcp) ?
		std::min((size_t)tcp_sndbuf(conn->pcb.tcp), MaxDataLength) : 0;
}

void Connection::Poll()
{
	if (state == ConnState::connected || state == ConnState::otherEndClosed)
	{
		struct pbuf *data = nullptr;
		err_t rc = netconn_recv_tcp_pbuf_flags(conn, &data, NETCONN_NOAUTORCVD);

		while(rc == ERR_OK) {
			if (readBuf == nullptr) {
				readBuf = data;
				readIndex = alreadyRead = 0;
			} else {
				pbuf_cat(readBuf, data);
			}
			data = nullptr;
			rc = netconn_recv_tcp_pbuf_flags(conn, &data, NETCONN_NOAUTORCVD);
		}

		if (rc != ERR_WOULDBLOCK)
		{
			if (rc == ERR_RST || rc == ERR_CLSD || rc == ERR_CONN)
			{
				SetState(ConnState::otherEndClosed);
			}
			else
			{
				Terminate(false);
			}
		}
	}
	else if (state == ConnState::closePending)
	{
		// Retry closing this connection.
		Close();
	}
	else { }
}

// Close the connection.
// If 'external' is true then the Duet main processor has requested termination, so we free up the connection.
// Otherwise it has failed because of an internal error, and we set the state to 'aborted'. The Duet main processor will see this and send a termination request,
// which will free it up.
void Connection::Close()
{
	if (state == ConnState::otherEndClosed ||  state == ConnState::connected)
	{
		SetState(ConnState::closePending);
		FreePbuf();
	}

	ConnectionEvent evt;
	evt.type = ConnectionEventType::Close;
	evt.data.ptr = conn;
	if (xQueueSend(connectionQueue, &evt, 0) == pdTRUE)
	{
		SetState(ConnState::free);
		conn = nullptr;
	}
}

bool Connection::Connect(uint8_t protocol, uint32_t remoteIp, uint16_t remotePort)
{
	struct netconn * tempPcb = netconn_new_with_callback(NETCONN_TCP, ConnectCallback);
	if (tempPcb == nullptr)
	{
		debugPrintAlways("can't allocate connection\n");
		return false;
	}
	netconn_set_nonblocking(tempPcb, 1);

	conn = tempPcb;

	// Since the member 'socket' is not used, use it to store
	// reference to the owning Connection of the netconn.
	static_assert(sizeof(conn->socket) == sizeof(this));
	conn->socket = reinterpret_cast<int>(this);

	ip_set_option(tempPcb->pcb.tcp, SOF_REUSEADDR);

	ip_addr_t tempIp;
	memset(&tempIp, 0, sizeof(tempIp));
	tempIp.u_addr.ip4.addr = remoteIp;
	err_t rc = netconn_connect(conn, &tempIp, remotePort);

	if (!(rc == ERR_OK || rc == ERR_INPROGRESS))
	{
		Terminate(true);
		debugPrintfAlways("can't connect: %d\n", (int)rc);
		return false;
	}

	this->protocol = protocol;
	SetState(ConnState::connecting);
	return true;
}

void Connection::Terminate(bool external)
{
	if (conn) {
		// No need to pass to ConnectionTask and do a graceful close on the connection.
		// Delete it here.
		netconn_close(conn);
		netconn_delete(conn);
		conn = nullptr;
	}
	FreePbuf();
	SetState((external) ? ConnState::free : ConnState::aborted);
}

void Connection::Accept(struct netconn* conn, uint8_t protocol)
{
	this->protocol = protocol;
	Connected(conn);
}

void Connection::Connected(struct netconn* conn)
{
	this->conn = conn;
	localPort = conn->pcb.tcp->local_port;
	remotePort = conn->pcb.tcp->remote_port;
	remoteIp = conn->pcb.tcp->remote_ip.u_addr.ip4.addr;
	readIndex = alreadyRead = 0;

	// This function is used in lower priority tasks than the main task.
	// Mark the connection ready last, so the main task does not use it when it's not ready.
	// This should also be free from being taken by Connection::Allocate, since the previous
	// state is not ConnState::free (Connection::Allocate sets the state to ConnState::allocated.).
	SetState(ConnState::connected);
}

void Connection::GetStatus(ConnStatusResponse& resp) const
{
	resp.socketNumber = number;
	resp.protocol = protocol;
	resp.state = state;
	resp.bytesAvailable = CanRead();
	resp.writeBufferSpace = CanWrite();
	resp.localPort = localPort;
	resp.remotePort = remotePort;
	resp.remoteIp = remoteIp;
}

void Connection::FreePbuf()
{
	if (readBuf != nullptr)
	{
		pbuf_free(readBuf);
		readBuf = nullptr;
	}
}

void Connection::Report()
{
	// The following must be kept in the same order as the declarations in class ConnState
	static const char* const connStateText[] =
	{
		"free",
		"connecting",			// socket is trying to connect
		"connected",			// socket is connected
		"remoteClosed",			// the other end has closed the connection

		"aborted",				// an error has occurred
		"closePending",			// close this socket when sending is complete
		"closeReady"			// about to be closed
	};

	const unsigned int st = (int)state;
	ets_printf("%s", (st < ARRAY_SIZE(connStateText)) ? connStateText[st]: "unknown");
	if (state != ConnState::free)
	{
		ets_printf(" %u, %u, %u.%u.%u.%u", localPort, remotePort, remoteIp & 255, (remoteIp >> 8) & 255, (remoteIp >> 16) & 255, (remoteIp >> 24) & 255);
	}
}

// Static functions

/*static*/ void Connection::Init()
{
	connectionQueue = xQueueCreate(MaxConnections * 3, sizeof(ConnectionEvent));
	allocateMutex = xSemaphoreCreateMutex();
	xTaskCreate(ConnectionTask, "conn", CONNECTION_TASK, NULL, CONNECTION_PRIO, NULL);

	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i] = new Connection((uint8_t)i);
	}
}

bool Connection::Listen(uint16_t port, uint32_t ip, uint8_t protocol, uint16_t maxConns)
{
	// See if we are already listing for this
	for (Listener *p = Listener::List(); p != nullptr; )
	{
		Listener *n = p->GetNext();
		if (p->GetPort() == port)
		{
			if (maxConns != 0 && (p->GetIp() == IPADDR_ANY || p->GetIp() == ip))
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

	// Setup LWIP listening connection.
	struct netconn * tempPcb = netconn_new_with_callback(NETCONN_TCP, ListenCallback);
	if (tempPcb == nullptr)
	{
		debugPrintAlways("can't allocate PCB\n");
		return false;
	}
	netconn_set_nonblocking(tempPcb, 1);

	ip_addr_t tempIp;
	memset(&tempIp, 0, sizeof(tempIp));
	tempIp.u_addr.ip4.addr = ip;
	ip_set_option(tempPcb->pcb.tcp, SOF_REUSEADDR); // seems to be needed for avoiding ERR_USE error when switching from client to AP

	err_t rc = netconn_bind(tempPcb, &tempIp, port);
	if (rc != ERR_OK)
	{
		netconn_close(tempPcb);
		netconn_delete(tempPcb);
		debugPrintfAlways("can't bind PCB: %d\n", (int)rc);
		return false;
	}

	return Listener::Start(port, ip, protocol, maxConns, tempPcb);
}

void Connection::StopListen(uint16_t port)
{
	for (Listener *p = Listener::List(); p != nullptr; )
	{
		Listener *n = p->GetNext();
		if (port == 0 || port == p->GetPort())
		{
			p->Stop();
		}
		p = n;
	}
}

/*static*/ void Connection::PollAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		Connection::Get(i).Poll();
	}
}

/*static*/ void Connection::TerminateAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		Connection::Get(i).Terminate(true);
	}
}

/*static*/ uint16_t Connection::GetPortByProtocol(uint8_t protocol)
{
	for (Listener *p = Listener::List(); p != nullptr; p = p->GetNext())
	{
		if (p->GetProtocol() == protocol)
		{
			return p->GetPort();
		}
	}
	return 0;
}

/*static*/ void Connection::ReportConnections()
{
	ets_printf("Conns");
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		ets_printf("%c %u:", (i == 0) ? ':' : ',', i);
		connectionList[i]->Report();
	}
	ets_printf("\n");
}

/*static*/ void Connection::GetSummarySocketStatus(uint16_t& connectedSockets, uint16_t& otherEndClosedSockets)
{
	connectedSockets = 0;
	otherEndClosedSockets = 0;
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (Connection::Get(i).GetState() == ConnState::connected)
		{
			connectedSockets |= (1 << i);
		}
		else if (Connection::Get(i).GetState() == ConnState::otherEndClosed)
		{
			otherEndClosedSockets |= (1 << i);
		}
		else { }
	}
}

/*static*/ Connection *Connection::Allocate()
{
	Connection *conn = nullptr;

	// This sequence must be protected with a mutex, since it happens on
	// both ConnectionTask and the main task, the latter having lower
	// priority. If for example, this is executing on main task
	// specifically after the state == free check, at which point is
	// pre-empted by the ConnectionTask executing the same code, the allocated
	// Connection will have been already spent.
	xSemaphoreTake(allocateMutex, portMAX_DELAY);
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->state == ConnState::free)
		{
			conn = connectionList[i];
			conn->SetState(ConnState::allocated);
			break;
		}
	}
	xSemaphoreGive(allocateMutex);
	return conn;
}

/*static*/ uint16_t Connection::CountConnectionsOnPort(uint16_t port)
{
	uint16_t count = 0;
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->localPort == port)
		{
			const ConnState state = connectionList[i]->state;
			if (state == ConnState::connected || state == ConnState::otherEndClosed || state == ConnState::closePending)
			{
				++count;
			}
		}
	}
	return count;
}

/*static*/ void Connection::ConnectionTask(void* p)
{
	static int closeTimer[MaxConnections];

	for (int i = 0; i < MaxConnections; i++)
	{
		closeTimer[i] = MaxAckTime;
	}

	int nextClose = -1;
	int nextWait = MaxAckTime;
	unsigned long last = millis();

	while (true)
	{
		ConnectionEvent evt;

		// If no connection in the pending list, wait indefinitely.
		uint32_t waitTime = (nextClose < 0) ? portMAX_DELAY : pdMS_TO_TICKS(nextWait);
		BaseType_t res = xQueueReceive(connectionQueue, &evt, waitTime);

		unsigned long now = millis();

		if (res == pdTRUE)
		{
			nextWait -= (nextClose < 0) ? 0 : (now - last);

			if (evt.type == ConnectionEventType::Accept)
			{
				struct netconn *conn, *newConn;
				conn = static_cast<struct netconn*>(evt.data.ptr);
				err_t rc = netconn_accept(conn, &newConn);
				if (rc == ERR_OK)
				{
					Listener* p = reinterpret_cast<Listener*>(conn->socket);
					const uint16_t numConns = Connection::CountConnectionsOnPort(p->GetPort());
					if (numConns < p->GetMaxConnections())
					{
						Connection * const c = Connection::Allocate();
						if (c != nullptr)
						{
							netconn_set_nonblocking(newConn, 1);
							c->Accept(newConn, p->GetProtocol());
							if (p->GetProtocol() == protocolFtpData)
							{
								debugPrintf("accept conn, stop listen on port %u\n", p->GetPort());
								p->Stop();	// don't listen for further connections
							}
						}
						else
						{
							netconn_close(newConn);
							netconn_delete(newConn);
							debugPrintfAlways("refused conn on port %u no free conn\n", p->GetPort());
						}
					}
					else
					{
						netconn_close(newConn);
						netconn_delete(newConn);
						debugPrintfAlways("refused conn on port %u already %u conns\n", p->GetPort(), numConns);
					}
				}
			}
			else if (evt.type == ConnectionEventType::Close)
			{
				struct netconn *conn = static_cast<struct netconn*>(evt.data.ptr);
				for (int i = 0; i < MaxConnections; i++)
				{
					if (closePending[i] == nullptr)
					{
						conn->socket = i;
						break;
					}
				}

				closePending[conn->socket] = conn;
				closeTimer[conn->socket] = MaxAckTime;
				// Send a FIN packet, which triggers an event, after the conditions
				// for sending ConnectionEventType::Terminate in the callbacks has been set above.
				netconn_shutdown(closePending[conn->socket], false, true);

				if (nextClose < 0)
				{
					// This is the first connection to add to the close pending list after some time.
					// Since it is the only element, set it as the connection with the nearest expiry.
					nextClose = conn->socket;
					nextWait = MaxAckTime;
					last = now;
				}
			}
			else if (evt.type == ConnectionEventType::Terminate)
			{
				int idx = evt.data.i;
				struct netconn *conn = closePending[idx];

				// This connection might have been closed in a previous iteration.
				// Since there is no way to cancel a ConnectionEventType::Terminate command in the queue,
				// re-check the connection still exists here.
				if (conn)
				{
					assert(idx == conn->socket);
					struct pbuf* buf = nullptr;

					err_t rc = netconn_recv_tcp_pbuf(conn, &buf);

					if (rc != ERR_OK || !buf || buf->tot_len == 0)
					{
						if (idx == nextClose)
						{
							// Closing the connection ahead of the timeout. Since the full timeout
							// was not used, compute and store the time elapsed.
							closeTimer[idx] -= (nextWait < 0) ? 0 : nextWait;
							nextWait = 0;
						}

						netconn_close(conn);
						netconn_delete(conn);
						closePending[idx] = nullptr;
					}
				}
			}
			else { }

		}
		else
		{
			// The full timeout was spent, and so closeTimer[nextClose] is not changed.
			nextWait = 0;
		}

		if (nextWait <= 0)
		{
			int elapsed = closeTimer[nextClose];

			// Update the other timeouts in the pending array.
			for (int i = 0; i < MaxConnections; i++)
			{
				if (closePending[i])
				{
					closeTimer[i] -= elapsed;
				}
			}

			// Find the next timeout, closing other connections that have the same expiry.
			nextClose = -1;
			nextWait = MaxAckTime;

			for (int i = 0; i < MaxConnections; i++)
			{
				if (closePending[i])
				{
					if (closeTimer[i] <= 0)
					{
						netconn_close(closePending[i]);
						netconn_delete(closePending[i]);
						closePending[i] = nullptr;
					}
					else
					{
						if (closeTimer[i] < nextWait)
						{
							nextClose = i;
							nextWait = closeTimer[i];
						}
					}
				}
			}
		}

		last = now;
	}
}

/*static*/ void Connection::ListenCallback(struct netconn *conn, enum netconn_evt evt, u16_t len)
{
	if ((conn->socket >= 0 && conn->socket < MaxConnections)
			 && closePending[conn->socket] == conn)
	{
		ConnectionEvent evt;
		evt.type = ConnectionEventType::Terminate;
		evt.data.i = conn->socket;
		xQueueSend(connectionQueue, &evt, portMAX_DELAY);
	}
	else
	{
		if (evt == NETCONN_EVT_RCVPLUS && len == 0)
		{
			if (conn->socket > 0)
			{
				ConnectionEvent evt;
				evt.type = ConnectionEventType::Accept;
				evt.data.ptr = conn;
				xQueueSend(connectionQueue, &evt, portMAX_DELAY);
			}
		}
	}
}

/*static*/ void Connection::ConnectCallback(struct netconn *conn, enum netconn_evt evt, u16_t len)
{
	if ((conn->socket >= 0 && conn->socket < MaxConnections)
			 && closePending[conn->socket] == conn)
	{
		ConnectionEvent evt;
		evt.type = ConnectionEventType::Terminate;
		evt.data.i = conn->socket;
		xQueueSend(connectionQueue, &evt, portMAX_DELAY);
	}
	else
	{
		if (conn->socket > 0)
		{
			Connection *c = reinterpret_cast<Connection*>(conn->socket);
			if (c->state == ConnState::connecting)
			{
				if (evt == NETCONN_EVT_SENDPLUS)
				{
					c->Connected(conn);
				}
				else if (evt == NETCONN_EVT_ERROR)
				{
					c->SetState(ConnState::otherEndClosed);
				}
				else { }
			}
		}
	}
}

// Static data
QueueHandle_t Connection::connectionQueue = nullptr;
SemaphoreHandle_t Connection::allocateMutex = nullptr;
netconn * Connection::closePending[MaxConnections];
Connection *Connection::connectionList[MaxConnections];

// End
