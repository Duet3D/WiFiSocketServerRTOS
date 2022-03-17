/*
 * Socket.cpp
 *
 *  Created on: 11 Apr 2017
 *      Author: David
 */
#include <cstring> 			// memcpy
#include <algorithm>			// for std::min

#include "lwip/tcp.h"

#include "Connection.h"
#include "Misc.h"				// for millis
#include "Config.h"


// Public interface
Connection::Connection(uint8_t num)
	: number(num), state(ConnState::free), localPort(0), remotePort(0), remoteIp(0),
	readIndex(0), alreadyRead(0), ownPcb(nullptr), pb(nullptr)
{
}

void IRAM_ATTR Connection::GetStatus(ConnStatusResponse& resp) const
{
	resp.socketNumber = number;
	resp.state = state;
	resp.bytesAvailable = CanRead();
	resp.writeBufferSpace = CanWrite();
	resp.localPort = localPort;
	resp.remotePort = remotePort;
	resp.remoteIp = remoteIp;
}

// Close the connection.
// If 'external' is true then the Duet main processor has requested termination, so we free up the connection.
// Otherwise it has failed because of an internal error, and we set the state to 'aborted'. The Duet main processor will see this and send a termination request,
// which will free it up.
void Connection::Close(bool external)
{
	if (ownPcb != nullptr)
	{
		netconn_close(ownPcb);
		netconn_delete(ownPcb);
		ownPcb = nullptr;
	}
	FreePbuf();
	SetState((external) ? ConnState::free : ConnState::aborted);
}

// Perform housekeeping tasks
void IRAM_ATTR Connection::Poll()
{
	if (state == ConnState::connected || state == ConnState::otherEndClosed)
	{
		struct pbuf *data = nullptr;
		err_t rc = netconn_recv_tcp_pbuf_flags(ownPcb, &data, NETCONN_NOAUTORCVD);

		while(rc == ERR_OK) {
			if (pb == nullptr) {
				pb = data;
				readIndex = alreadyRead = 0;
			} else {
				pbuf_cat(pb, data);
			}
			data = nullptr;
			rc = netconn_recv_tcp_pbuf_flags(ownPcb, &data, NETCONN_NOAUTORCVD);
		}

		if (rc != ERR_WOULDBLOCK) {
			if (rc == ERR_RST)
			{
				SetState(ConnState::otherEndClosed);
			} else
			{
				Close(false);
			}
		}
	}
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
size_t IRAM_ATTR Connection::Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending)
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
		rc = netconn_write_partly(ownPcb, data + total, length - total, flag, &written);

		if (rc != ERR_OK && rc != ERR_WOULDBLOCK) {
			break;
		}
	}

	if (rc != ERR_OK)
	{
		// We failed to write the data. See above for possible mitigations. For now we just terminate the connection.
		debugPrintfAlways("Write fail len=%u err=%d\n", total, (int)rc);
		Close(false);		// chrishamm: Not sure if this helps with LwIP v1.4.3 but it is mandatory for proper error handling with LwIP 2.0.3
		return 0;
	}
	// Close the connection again when we're done
	if (closeAfterSending)
	{
		Close(true);
	}

	return length;
}

size_t IRAM_ATTR Connection::CanWrite() const
{
	// Return the amount of free space in the write buffer
	// Note: we cannot necessarily write this amount, because it depends on memory allocations being successful.
	return ((state == ConnState::connected) && ownPcb->pcb.tcp) ?
		std::min((size_t)tcp_sndbuf(ownPcb->pcb.tcp), MaxDataLength) : 0;
}

size_t IRAM_ATTR Connection::Read(uint8_t *data, size_t length)
{
	size_t lengthRead = 0;
	if (pb != nullptr && length != 0 && (state == ConnState::connected || state == ConnState::otherEndClosed))
	{
		do
		{
			const size_t toRead = std::min<size_t>(pb->len - readIndex, length);
			memcpy(data + lengthRead, (uint8_t *)pb->payload + readIndex, toRead);
			lengthRead += toRead;
			readIndex += toRead;
			length -= toRead;
			if (readIndex != pb->len)
			{
				break;
			}
			pbuf * const currentPb = pb;
			pb = pb->next;
			currentPb->next = nullptr;
			pbuf_free(currentPb);
			readIndex = 0;
		} while (pb != nullptr && length != 0);

		alreadyRead += lengthRead;
		if (pb == nullptr || alreadyRead >= TCP_MSS)
		{
			netconn_tcp_recvd(ownPcb, alreadyRead);
			alreadyRead = 0;
		}
	}
	return lengthRead;
}

size_t IRAM_ATTR Connection::CanRead() const
{
	return ((state == ConnState::connected || state == ConnState::otherEndClosed) && pb != nullptr)
			? pb->tot_len - readIndex : 0;
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

// Callback functions
int Connection::Accept(struct netconn *pcb)
{
	ownPcb = pcb;
	pb = nullptr;
	localPort = pcb->pcb.tcp->local_port;
	remotePort = pcb->pcb.tcp->remote_port;
	remoteIp = pcb->pcb.tcp->remote_ip.u_addr.ip4.addr;
	readIndex = alreadyRead = 0;
	SetState(ConnState::connected);

	return ERR_OK;
}

void Connection::FreePbuf()
{
	if (pb != nullptr)
	{
		pbuf_free(pb);
		pb = nullptr;
	}
}

// Static functions
/*static*/ Connection *Connection::Allocate()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->state == ConnState::free)
		{
			return connectionList[i];
		}
	}
	return nullptr;
}

/*static*/ void Connection::Init()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i] = new Connection((uint8_t)i);
	}
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

/*static*/ void Connection::TerminateAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		Connection::Get(i).Close(true);
	}
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
	}
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

// Static data
Connection *Connection::connectionList[MaxConnections] = { 0 };

// End
