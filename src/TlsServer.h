/*
 * TlsServer.h
 *
 * Server-side TLS support for the WiFi socket server.
 * Built only when SUPPORTS_TLS is set (see Config.h) - currently every ESP32-family target. The
 * ESP8266 cannot host a TLS server in practice (heap ~40 KiB vs handshake peak ~40-50 KiB), so it is
 * excluded there. The whole body is guarded, so this header is harmless when included from a build
 * with SUPPORTS_TLS == 0.
 */

#ifndef SRC_TLSSERVER_H_
#define SRC_TLSSERVER_H_

#include "Config.h"					// for SUPPORTS_TLS

#if SUPPORTS_TLS

#include <cstdint>
#include <cstddef>

#include "lwip/api.h"

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

struct netbuf;

// BIO state for one TLS connection. Holds the netconn and a "pending pbuf" cursor so the BIO can
// satisfy small reads (which mbedTLS does when parsing record headers) without dropping data
struct TlsBioState
{
	struct netconn *conn;
	struct netbuf *pending;		// fetched but not fully consumed
	size_t offset;				// bytes already consumed from pending
};

class TlsServer
{
public:
	static TlsServer* GetInstance()
	{
		if (!instance)
		{
			instance = new TlsServer();
		}
		return instance;
	}

	// Load cert+key from WirelessConfigurationMgr and build the mbedTLS server config
	// Returns true if a valid cert+key is in place (freshly loaded or already cached), false if
	// no cert/key is provisioned or parsing fails
	//
	// Concurrency: Enable() and Disable() mutate the shared conf/cert/key that live SSL contexts
	// point at, so they must only run when no TLS connection or listener is active. The SocketServer
	// command flow guarantees this - networkEnableTls / networkSetTls* / networkClearTls only arrive
	// during interface bring-up from a disabled state, before any TLS listener has been created
	bool Enable();

	// Free and re-initialise the shared config, leaving it clean for a later Enable()
	// Safe to call when already disabled or after a failed Enable(); see the concurrency note above
	void Disable();

	bool IsEnabled() const { return enabled; }

	// Set up an SSL context for one accepted connection, wired via BIO callbacks to the bio state
	// The bio.conn must already be populated by the caller. On success returns a heap-allocated
	// mbedtls_ssl_context that the caller owns and must FreeContext() when done
	mbedtls_ssl_context *CreateContext(TlsBioState *bio);

	// Perform one step of the TLS handshake on a context from CreateContext. Returns 0 when the
	// handshake is complete, MBEDTLS_ERR_SSL_WANT_READ / MBEDTLS_ERR_SSL_WANT_WRITE when it must be
	// called again (the BIO would block), or another negative mbedTLS error code on failure.
	// Driven incrementally from the Listener task so the handshake never blocks accepts
	int HandshakeStep(mbedtls_ssl_context *ctx);

	// Free an SSL context. Does NOT close the netconn or free the bio state
	static void FreeContext(mbedtls_ssl_context *ctx);

	// Free any pending netbuf held by a bio state. Safe to call when bio.pending is null
	static void DrainBio(TlsBioState *bio);

private:
	TlsServer();

	static int BioSend(void *ctx, const unsigned char *buf, size_t len);
	static int BioRecv(void *ctx, unsigned char *buf, size_t len);

	static TlsServer *instance;

	bool enabled;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cert;
	mbedtls_pk_context key;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctrDrbg;
};

#endif // SUPPORTS_TLS

#endif /* SRC_TLSSERVER_H_ */
