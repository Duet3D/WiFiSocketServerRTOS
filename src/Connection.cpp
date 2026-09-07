/*
 * Socket.cpp
 *
 *  Created on: 11 Apr 2017
 *      Author: David
 */
#include <cstring> 			// memcpy
#include <cstdlib>			// for malloc / free
#include <algorithm>			// for std::min

#include "lwip/tcp.h"
#include "esp_system.h"			// for esp_get_free_heap_size

#include "Connection.h"
#include "Misc.h"				// for millis
#include "Config.h"

#if SUPPORTS_TLS
#include "TlsServer.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"	// for MBEDTLS_ERR_NET_CONN_RESET
#endif

static_assert(MaxConnections < CONFIG_LWIP_MAX_SOCKETS); // Limits the listen callback value notification

// Define to cap what SendRaw hands to the stack per call, forcing every larger write through the pending-write path for testing
//#define WRITE_STASH_TEST_CAP	512

// Public interface
Connection::Connection(uint8_t num)
	: number(num), localPort(0), remotePort(0), remoteIp(0), conn(nullptr), state(ConnState::free),
	closeTimer(0),readBuf(nullptr), readIndex(0), alreadyRead(0), pendOtherEndClosed(false),
	pendingWrite(nullptr), pendingLen(0), pendingHead(0), pendingSince(0), pendingPush(false), pendingClose(false)
#if SUPPORTS_TLS
	, ssl(nullptr), tlsBio(nullptr), tlsPlain(nullptr), tlsPlainHead(0), tlsPlainTail(0), handshakeStart(0), teardownPending(false), teardownExternal(false)
#endif
{
}

size_t Connection::Read(uint8_t *data, size_t length)
{
	size_t lengthRead = 0;
#if SUPPORTS_TLS
	if (ssl != nullptr && tlsPlain != nullptr && length != 0
		&& (state == ConnState::connected || state == ConnState::otherEndClosed))
	{
		const size_t available = tlsPlainTail - tlsPlainHead;
		const size_t toCopy = std::min<size_t>(available, length);
		if (toCopy != 0)
		{
			memcpy(data, tlsPlain + tlsPlainHead, toCopy);
			tlsPlainHead += toCopy;
			lengthRead = toCopy;
			if (tlsPlainHead == tlsPlainTail)
			{
				tlsPlainHead = tlsPlainTail = 0;
			}
		}
		if (pendOtherEndClosed && tlsPlainHead == tlsPlainTail)
		{
			pendOtherEndClosed = false;
			SetState(ConnState::otherEndClosed);
		}
		return lengthRead;
	}
#endif
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

		if (pendOtherEndClosed && !readBuf)
		{
			pendOtherEndClosed = false;
			SetState(ConnState::otherEndClosed);
		}
	}
	return lengthRead;
}

size_t Connection::CanRead() const
{
#if SUPPORTS_TLS
	if (ssl != nullptr)
	{
		return ((state == ConnState::connected || state == ConnState::otherEndClosed) && tlsPlain != nullptr)
				? (tlsPlainTail - tlsPlainHead) : 0;
	}
#endif
	return ((state == ConnState::connected || state == ConnState::otherEndClosed) && readBuf != nullptr)
			? readBuf->tot_len - readIndex : 0;
}

// Write data to the connection. The amount of data may be zero.
// The SAM is told how much we accept (see CanWrite) before the data arrives over SPI, so by the time we get here the data
// cannot be refused any more. tcp_sndbuf only tracks a byte budget; the segments and pbufs behind it come from the heap
// (MEMP_MEM_MALLOC), so a write within budget can still be refused. Whatever lwIP does not take right now is stashed in
// pendingWrite and drained from Poll(); CanWrite reports 0 until the stash is empty. Data is only lost when the connection is terminated
size_t Connection::Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending)
{
	if (!(state == ConnState::connected && !pendOtherEndClosed) || pendingLen != 0)
	{
		return 0;
	}

	const bool push = doPush || closeAfterSending;
	size_t written = 0;
	if (!SendRaw(data, length, push, written))
	{
		// A reset that lands mid-write and one that lands just after the data was buffered look the same to the client,
		// so count the data as accepted instead of reporting an incomplete write; RRF learns about the peer close from
		// the next status report
		return (state == ConnState::otherEndClosed) ? length : written;
	}
	if (written < length && !Stash(data + written, length - written, push, closeAfterSending))
	{
		debugPrintfAlways("Write stash fail len=%u\n", length - written);
		Terminate(false);
		return written;
	}
	if (closeAfterSending && pendingLen == 0)
	{
		Close();
	}
	return length;
}

// Hand data to lwIP (or mbedTLS) without blocking. 'written' receives the amount taken, which may be less than 'length'.
// Returns false if the connection is no longer usable, in which case the state has already been updated
bool Connection::SendRaw(const uint8_t *data, size_t length, bool push, size_t& written)
{
#ifdef WRITE_STASH_TEST_CAP
	length = std::min<size_t>(length, WRITE_STASH_TEST_CAP);
#endif
	written = 0;
#if SUPPORTS_TLS
	if (ssl != nullptr)
	{
		while (written < length)
		{
			// After WANT_WRITE mbedTLS keeps the encrypted record and expects the retry with the same length, which is
			// exactly what DrainPending passes because 'written' only advances by what mbedTLS reported
			const int rc = mbedtls_ssl_write(ssl, data + written, length - written);
			if (rc > 0)
			{
				written += static_cast<size_t>(rc);
			}
			else if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				break;
			}
			else if (rc == MBEDTLS_ERR_NET_CONN_RESET)
			{
				// Same mapping as the read path in Poll: a bare TCP close/reset from the peer is a peer close, not an internal error
				SetState(ConnState::otherEndClosed);
				return false;
			}
			else
			{
				debugPrintfAlways("TLS Write fail len=%u err=-0x%04x\n", written, -rc);
				Terminate(false);
				return false;
			}
		}
		return true;
	}
#endif

	const err_t rc = netconn_write_partly(conn, data, length, NETCONN_COPY | NETCONN_DONTBLOCK | (push ? 0 : NETCONN_MORE), &written);
	if (rc == ERR_OK || rc == ERR_WOULDBLOCK)
	{
		return true;
	}
	if (rc == ERR_RST || rc == ERR_CLSD || rc == ERR_ABRT || rc == ERR_CONN)
	{
		SetState(ConnState::otherEndClosed);
	}
	else
	{
		debugPrintfAlways("Write fail len=%u err=%d\n", written, (int)rc);
		Terminate(false);
	}
	return false;
}

bool Connection::Stash(const uint8_t *data, size_t length, bool push, bool closeAfterSending)
{
	if (pendingWrite == nullptr)
	{
		pendingWrite = static_cast<uint8_t *>(malloc(MaxDataLength));
		if (pendingWrite == nullptr)
		{
			return false;
		}
	}
	memcpy(pendingWrite, data, length);
	pendingLen = length;
	pendingHead = 0;
	pendingSince = millis();
	pendingPush = push;
	pendingClose = closeAfterSending;
	return true;
}

// Try to hand stashed write data to lwIP. Called from Poll, i.e. from the same task as Write
void Connection::DrainPending()
{
	if (pendingLen == 0)
	{
		return;
	}
	if (!(state == ConnState::connected && !pendOtherEndClosed))
	{
		FreePending();
		return;
	}

	size_t written = 0;
	if (!SendRaw(pendingWrite + pendingHead, pendingLen - pendingHead, pendingPush, written))
	{
		FreePending();
		return;
	}
	if (written != 0)
	{
		pendingSince = millis();		// any progress restarts the clock, so a slow link cannot trip the stall timeout
	}
	pendingHead += written;
	if (pendingHead == pendingLen)
	{
		const bool close = pendingClose;
		FreePending();
		if (close)
		{
			Close();
		}
	}
	else if (millis() - pendingSince >= MaxAckTime)
	{
		// The peer has taken nothing at all for a long time, give up on it rather than holding the socket forever
		debugPrintfAlways("Write stall len=%u\n", pendingLen - pendingHead);
		Terminate(false);
	}
}

void Connection::FreePending()
{
	if (pendingWrite != nullptr)
	{
		free(pendingWrite);
		pendingWrite = nullptr;
	}
	pendingLen = pendingHead = 0;
	pendingClose = false;
}

// Return how much write data we accept from the SAM right now. tcp_sndbuf is only a byte budget: with MEMP_MEM_MALLOC the
// segments behind it come from the heap, which the receive queues of all sockets and mbedTLS compete for, so also stop
// accepting data when the heap runs low. Data that lwIP still refuses lands in pendingWrite, which blocks further writes until drained
size_t Connection::CanWrite() const
{
	// lwIP nulls conn->pcb.tcp from its own thread when the peer resets, so read it once instead of checking
	// and dereferencing separately; a pcb freed after the snapshot only yields a stale byte count, which the clamp absorbs
	struct tcp_pcb *const pcb = (conn != nullptr) ? conn->pcb.tcp : nullptr;
	if (!(state == ConnState::connected && !pendOtherEndClosed) || pendingLen != 0 || pcb == nullptr || esp_get_free_heap_size() < MinFreeHeapForWrite)
	{
		return 0;
	}
	return std::min((size_t)tcp_sndbuf(pcb), MaxDataLength);
}

void Connection::Poll()
{
	DrainPending();
#if SUPPORTS_TLS
	if (ssl != nullptr)
	{
		if ((state == ConnState::connected && !pendOtherEndClosed) || state == ConnState::otherEndClosed)
		{
			// Top up the plaintext buffer if there's room. mbedtls_ssl_read pulls encrypted bytes
			// via our BIO callback and yields decrypted plaintext
			while (tlsPlain != nullptr && tlsPlainTail < TlsPlaintextBufSize)
			{
				const int rc = mbedtls_ssl_read(ssl, tlsPlain + tlsPlainTail, TlsPlaintextBufSize - tlsPlainTail);
				if (rc > 0)
				{
					tlsPlainTail += static_cast<size_t>(rc);
					continue;
				}
				if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
				{
					break;		// no more data right now
				}
				// A bare TCP close from the peer (FIN, or even a RST after they got what they wanted)
				// surfaces as CONN_RESET from our BIO because lwIP returns ERR_CLSD/ERR_RST and we
				// have no way to tell apart "graceful close without close_notify" from a real reset.
				// In practice this happens on virtually every HTTPS client exit (curl, browsers
				// reusing pooled sockets, `openssl s_client </dev/null`), so do not flag it as a hard error
				if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == 0 || rc == MBEDTLS_ERR_NET_CONN_RESET)
				{
					if (tlsPlainHead < tlsPlainTail)
					{
						pendOtherEndClosed = true;
					}
					else
					{
						SetState(ConnState::otherEndClosed);
					}
					break;
				}
				debugPrintfAlways("TLS read err=-0x%04x\n", -rc);
				Terminate(false);
				return;
			}
		}
		else if (state == ConnState::closeReady)
		{
			Close();
		}
		else if (state == ConnState::closePending)
		{
			struct tcp_pcb *const pcb = (conn != nullptr) ? conn->pcb.tcp : nullptr;
			if (pcb == nullptr || pcb->unacked == nullptr)
			{
				SetState(ConnState::closeReady);
			}
			else if (millis() - closeTimer >= MaxAckTime)
			{
				Terminate(true);
			}
		}
		return;
	}
#endif
	if ((state == ConnState::connected && !pendOtherEndClosed) || state == ConnState::otherEndClosed)
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
			if (rc == ERR_RST || rc == ERR_CLSD || rc == ERR_CONN || rc == ERR_ABRT)
			{
				// Pend setting the state to other end closed if there is data to be read.
				// Otherwise, set it immediately. This is to avoid a case when a socket in RRF
				// gets stuck in the peer disconnecting state, when it recieves the change of
				// the connection state to other end closed first, then polls it when in fact it
				// had data. By that time, the responder might have been already closed, leaving
				// no one to consume this data and thus the socket unable to progress in state.
				if (readBuf)
				{
					pendOtherEndClosed = true;
				}
				else
				{
					SetState(ConnState::otherEndClosed);
				}
			}
			else
			{
				Terminate(false);
			}
		}
	}
	else if (state == ConnState::closeReady)
	{
		// Deferred close, possibly outside the ISR
		Close();
	}
	else if (state == ConnState::closePending)
	{
		// The other end may have closed the connection with RST, which causes lwIP
		// to free the PCB. Detect this and close immediately instead of waiting for
		// the acknowledgement timer to expire.
		struct tcp_pcb *const pcb = (conn != nullptr) ? conn->pcb.tcp : nullptr;
		if (pcb == nullptr || pcb->unacked == nullptr)
		{
			SetState(ConnState::closeReady);
		}
		else if (millis() - closeTimer >= MaxAckTime)
		{
			// The acknowledgement timer has expired. The close was already initiated
			// by RRF, so go straight to free rather than aborted to avoid a round-trip.
			Terminate(true);
		}
	}
	else { }
}

// Close the connection.
// If 'external' is true then the Duet main processor has requested termination, so we free up the connection.
// Otherwise it has failed because of an internal error, and we set the state to 'aborted'. The Duet main processor will see this and send a termination request,
// which will free it up.
void Connection::Close()
{
#if SUPPORTS_TLS
	if (OwnedByListener())
	{
		DeferTeardown(true);
		return;
	}
#endif
	struct tcp_pcb *const pcb = (conn != nullptr) ? conn->pcb.tcp : nullptr;
	switch(state)
	{
	case ConnState::connected:						// both ends are still connected
		if (pendingLen != 0)
		{
			pendingClose = true;					// DrainPending closes once lwIP has taken the stashed data
			break;
		}
		if (pcb != nullptr && pcb->unacked != nullptr)
		{
			closeTimer = millis();
			netconn_shutdown(conn, true, false);	// shut down recieve
			SetState(ConnState::closePending);		// wait for the remaining data to be sent before closing
			break;
		}
		// fallthrough
	case ConnState::otherEndClosed:					// the other end has already closed the connection
	case ConnState::closeReady:						// the other end has closed and we were already closePending
	default:										// should not happen
#if SUPPORTS_TLS
		if (ssl != nullptr)
		{
			mbedtls_ssl_close_notify(ssl);			// graceful close, unlike Terminate
		}
		FreeTls();
#endif
		if (conn)
		{
			netconn_close(conn);
			netconn_delete(conn);
			conn = nullptr;
		}
		FreePbuf();
		FreePending();
		SetState(ConnState::free);
		if (listener)
		{
			listener->Notify();
		}
		break;

	case ConnState::closePending:					// we already asked to close
		// Should not happen, but if it does just let the close proceed when sending is complete or timeout
		break;
	}
}

void Connection::Deallocate()
{
	if (state == ConnState::allocated)
	{
		SetState(ConnState::free);
	}
}

bool Connection::Connect(uint8_t protocol, uint32_t remoteIp, uint16_t remotePort)
{
	struct netconn * conn = netconn_new_with_callback(NETCONN_TCP, ConnectCallback);

	if (conn)
	{
		netconn_set_nonblocking(conn, true);
		netconn_set_recvtimeout(conn, MaxReadWriteTime);
		netconn_set_sendtimeout(conn, MaxReadWriteTime);

		ip_set_option(conn->pcb.tcp, SOF_REUSEADDR);

		this->conn = conn;
		this->protocol = protocol;
		localPort = 0;								// clear any stale port so the SAM doesn't mistake this for an accepted connection
		SetState(ConnState::connecting);

		ip_addr_t tempIp;
		memset(&tempIp, 0, sizeof(tempIp));
		tempIp.u_addr.ip4.addr = remoteIp;
		err_t rc = netconn_connect(conn, &tempIp, remotePort);

		if (rc == ERR_OK || rc == ERR_INPROGRESS)
		{
			return true;
		}
		else
		{
			Terminate(true);
		}
	}
	else
	{
		debugPrintAlways("can't allocate connection\n");
	}

	return true;
}

void Connection::Terminate(bool external)
{
#if SUPPORTS_TLS
	if (OwnedByListener())
	{
		DeferTeardown(external);
		return;
	}
#endif
	TerminateNow(external);
}

#if SUPPORTS_TLS
// Teardown requests come from the main task, but from Allocate until the TLS handshake completes the Listener task
// owns the connection and may be inside mbedTLS for a long time. Waiting for it there stalls the SPI transaction the
// request arrived in, so hand the teardown to the Listener task instead. Once StepHandshake has set the state to
// connected the Listener task never touches the connection again, so the main task tears it down directly
void Connection::DeferTeardown(bool external)
{
	teardownExternal = external;
	teardownPending = true;
	Listener::Wake();
}
#endif

// Tear the connection down and free all its resources
void Connection::TerminateNow(bool external)
{
#if SUPPORTS_TLS
	// No graceful close_notify on termination - the peer is being dropped abruptly
	FreeTls();
#endif
	if (conn) {
		// No need to pass to ConnectionTask and do a graceful close on the connection.
		// Delete it here.
		netconn_close(conn);
		netconn_delete(conn);
		conn = nullptr;
	}
	FreePbuf();
	FreePending();
	SetState((external) ? ConnState::free : ConnState::aborted);
	if (external && listener)
	{
		listener->Notify();
	}
}

void Connection::Accept(Listener *listener, struct netconn* conn, uint8_t protocol)
{
	this->protocol = protocol;

#if SUPPORTS_TLS
	if (listener != nullptr && listener->IsTls())
	{
		debugPrintf("tls.accept[%u]: enter, heap=%u\n", (unsigned)number, (unsigned)esp_get_free_heap_size());

		// Snapshot the netconn's address fields up front. mbedtls_ssl_setup inside CreateContext
		// allocates handshake buffers and can take many ms; if lwIP frees the underlying pcb in that
		// window (peer RST, lwIP cleanup) conn->pcb.tcp would be null when InitConnection runs later
		InitConnection(listener, conn);

		auto cleanup = [&]()
		{
			FreeTls();
			netconn_close(conn);
			netconn_delete(conn);
			SetState(ConnState::free);
		};

		tlsBio = static_cast<TlsBioState *>(malloc(sizeof(TlsBioState)));
		if (tlsBio == nullptr)
		{
			debugPrintAlways("tls.accept: bio malloc failed\n");
			cleanup();
			return;
		}
		tlsBio->conn = conn;
		tlsBio->pending = nullptr;
		tlsBio->offset = 0;

		ssl = TlsServer::GetInstance()->CreateContext(tlsBio);
		if (ssl == nullptr)
		{
			debugPrintAlways("tls.accept: CreateContext returned null\n");
			cleanup();
			return;
		}
		tlsPlain = static_cast<uint8_t *>(malloc(TlsPlaintextBufSize));
		if (tlsPlain == nullptr)
		{
			debugPrintAlways("tls.accept: plaintext buf malloc failed\n");
			cleanup();
			return;
		}
		tlsPlainHead = tlsPlainTail = 0;

		// Don't run the handshake here - it can take seconds and would block the Listener task from
		// accepting other connections. Bring the connection up in the `connecting` state; the Listener
		// task drives the handshake incrementally via PollHandshakes(), interleaved with new accepts
		handshakeStart = millis();
		SetState(ConnState::connecting);
		debugPrintf("tls.accept[%u]: ready for handshake, heap=%u\n", (unsigned)number, (unsigned)esp_get_free_heap_size());
		return;
	}
#endif

	Connected(listener, conn);
}

#if SUPPORTS_TLS
// Advance the deferred TLS handshake for this connection by one step and apply any teardown the main task deferred
// to us, see DeferTeardown. Returns true if the handshake is still in progress and should be stepped again. Runs on
// the Listener task, which has the stack headroom mbedTLS needs; stepping rather than running the handshake to
// completion keeps one slow client from delaying accepts or other handshakes
bool Connection::StepHandshake()
{
	if (!teardownPending && (ssl == nullptr || state != ConnState::connecting))
	{
		return false;
	}

	// The teardown flag is checked again after the step: the main task may set it while the step runs, having seen
	// the connection as still ours, and nobody else will act on it once the state is connected
	if (!ApplyDeferredTeardown() && ssl != nullptr && state == ConnState::connecting)
	{
		const int rc = TlsServer::GetInstance()->HandshakeStep(ssl);
		if (rc == 0)
		{
			SetState(ConnState::connected);						// handshake done - the connection is now usable
		}
		else if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			TerminateNow(false);								// handshake failed - HandshakeStep already logged it
		}
		else if (millis() - handshakeStart >= MaxHandshakeTime)
		{
			debugPrintAlways("TLS handshake timed out\n");
			TerminateNow(false);
		}
		return !ApplyDeferredTeardown() && state == ConnState::connecting;
	}
	return false;
}

// Run a teardown deferred by the main task, returning true if there was one. Runs on the Listener task
bool Connection::ApplyDeferredTeardown()
{
	if (!teardownPending)
	{
		return false;
	}
	teardownPending = false;
	if (state != ConnState::free)
	{
		TerminateNow(teardownExternal);
	}
	return true;
}
#endif

// Populate the connection's fields from an established netconn. The caller assigns the state last -
// once the connection is fully ready - so the main task does not act on it before it is initialised.
// This is also safe from Connection::Allocate, since the state is not ConnState::free at that point
// (Connection::Allocate only takes connections in ConnState::free).
//
// Reads conn->pcb.tcp, which lwIP nulls out when the underlying PCB is destroyed (peer RST, etc.).
// Callers on the TLS path must invoke this BEFORE the heavy CreateContext step, otherwise the pcb
// may be gone by the time we get here - that race produced a LoadProhibited crash during testing
void Connection::InitConnection(Listener *listener, struct netconn* conn)
{
	this->conn = conn;
	this->listener = listener;
	struct tcp_pcb *const pcb = (conn != nullptr) ? conn->pcb.tcp : nullptr;
	if (pcb != nullptr)
	{
		localPort = pcb->local_port;
		remotePort = pcb->remote_port;
		remoteIp = pcb->remote_ip.u_addr.ip4.addr;
	}
	else
	{
		localPort = 0;
		remotePort = 0;
		remoteIp = 0;
	}
	readIndex = alreadyRead = closeTimer = pendOtherEndClosed = 0;
}

void Connection::Connected(Listener *listener, struct netconn* conn)
{
	InitConnection(listener, conn);
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

#if SUPPORTS_TLS
// Free this connection's TLS resources - SSL context, BIO state and plaintext buffer. Safe to call
// when none are allocated. Does not touch the netconn or the connection state
void Connection::FreeTls()
{
	if (ssl != nullptr)
	{
		TlsServer::FreeContext(ssl);
		ssl = nullptr;
	}
	if (tlsBio != nullptr)
	{
		TlsServer::DrainBio(tlsBio);
		free(tlsBio);
		tlsBio = nullptr;
	}
	if (tlsPlain != nullptr)
	{
		free(tlsPlain);
		tlsPlain = nullptr;
	}
	tlsPlainHead = tlsPlainTail = 0;
}
#endif

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
	allocateMutex = xSemaphoreCreateMutex();

	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i] = new Connection((uint8_t)i);
	}
}

/*static*/ void Connection::PollAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		Connection::Get(i).Poll();
	}
}

#if SUPPORTS_TLS
// Step every connection that has a TLS handshake in progress. Returns true if any handshake is
// still pending, so the Listener task knows to keep polling instead of blocking indefinitely
/*static*/ bool Connection::PollHandshakes()
{
	bool anyPending = false;
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (Connection::Get(i).StepHandshake())
		{
			anyPending = true;
		}
	}
	return anyPending;
}
#endif

/*static*/ void Connection::TerminateAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		Connection::Get(i).Terminate(true);
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
		else if (Connection::Get(i).GetState() == ConnState::otherEndClosed
				|| Connection::Get(i).GetState() == ConnState::aborted)
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
			if (state == ConnState::connecting || state == ConnState::connected
				|| state == ConnState::otherEndClosed || state == ConnState::closePending)
			{
				++count;
			}
		}
	}
	return count;
}

/*static*/ void Connection::ConnectCallback(struct netconn *conn, enum netconn_evt evt, u16_t len)
{
	for (Connection *connection : connectionList)
	{
		if ((connection && connection->conn == conn) && connection->state == ConnState::connecting)
		{
			switch (evt)
			{
			case NETCONN_EVT_SENDPLUS:
				connection->Connected(nullptr, conn);
				break;

			case NETCONN_EVT_ERROR:
				connection->SetState(ConnState::otherEndClosed);
				break;
			default:
				break;
			}
		}
	}
}

// Static data
SemaphoreHandle_t Connection::allocateMutex = nullptr;
Connection *Connection::connectionList[MaxConnections];

// End
