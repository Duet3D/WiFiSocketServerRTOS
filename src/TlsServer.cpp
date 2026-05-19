/*
 * TlsServer.cpp
 */

#include "Config.h"					// for SUPPORTS_TLS

#if SUPPORTS_TLS

#include "TlsServer.h"

#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "lwip/err.h"

#include "mbedtls/net_sockets.h"		// for MBEDTLS_ERR_NET_*
#include "mbedtls/error.h"

#include "WirelessConfigurationMgr.h"

TlsServer* TlsServer::instance = nullptr;

TlsServer::TlsServer()
	: enabled(false)
{
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&cert);
	mbedtls_pk_init(&key);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctrDrbg);

	static const char personalisation[] = "duet-wifi-tls";
	mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
		reinterpret_cast<const unsigned char *>(personalisation), sizeof(personalisation) - 1);
}

bool TlsServer::Enable()
{
	if (enabled)
	{
		return true;
	}

	debugPrint("tls.enable: load cert/key\n");

	uint8_t *certBuf = nullptr;
	uint8_t *keyBuf = nullptr;
	size_t certLen = 0, keyLen = 0;

	if (!WirelessConfigurationMgr::GetInstance()->LoadTlsCertAndKey(&certBuf, &certLen, &keyBuf, &keyLen))
	{
		debugPrintAlways("tls.enable: no cert/key in flash\n");
		return false;
	}

	debugPrintf("tls.enable: cert=%u key=%u bytes\n", (unsigned)certLen, (unsigned)keyLen);

	bool ok = true;

	int rc = mbedtls_x509_crt_parse(&cert, certBuf, certLen);
	if (rc != 0)
	{
		debugPrintfAlways("cert parse failed: -0x%04x\n", -rc);
		ok = false;
	}

	if (ok)
	{
		// ESP-IDF v4.x mbedTLS uses the older 5-argument signature (no RNG callback)
		rc = mbedtls_pk_parse_key(&key, keyBuf, keyLen, nullptr, 0);
		if (rc != 0)
		{
			debugPrintfAlways("key parse failed: -0x%04x\n", -rc);
			ok = false;
		}
	}

	if (ok)
	{
		rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
			MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
		if (rc != 0)
		{
			debugPrintfAlways("config_defaults failed: -0x%04x\n", -rc);
			ok = false;
		}
	}

	if (ok)
	{
		mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctrDrbg);
		rc = mbedtls_ssl_conf_own_cert(&conf, &cert, &key);
		if (rc != 0)
		{
			debugPrintfAlways("conf_own_cert failed: -0x%04x\n", -rc);
			ok = false;
		}
	}

	if (ok)
	{
		// Allow only P-256 and P-384 ECDHE. Mirrors the Cortex-M (MB6HC / MB6XD) Ethernet path's
		// accepted curves so a single cert/key pair works across interfaces. P-521 is excluded
		// because handshakes can take seconds even with hardware MPI, causing browser timeouts
		// under the parallel-connection load DWC opens
		static const mbedtls_ecp_group_id ecdheCurves[] = {
			MBEDTLS_ECP_DP_SECP256R1,
			MBEDTLS_ECP_DP_SECP384R1,
			MBEDTLS_ECP_DP_NONE,	// sentinel
		};
		mbedtls_ssl_conf_curves(&conf, ecdheCurves);
	}

	// PEM buffers are no longer needed after parsing - the x509_crt and pk_context own the parsed data
	free(certBuf);
	free(keyBuf);

	if (!ok)
	{
		Disable();
		return false;
	}

	debugPrint("tls.enable: ok\n");
	enabled = true;
	return true;
}

// Free and re-initialise the shared config/cert/key. Unconditional (not gated on `enabled`) so that
// a *failed* Enable() - which may have partially populated cert/key/conf before erroring out - also
// leaves the structures clean for a later retry. mbedtls_*_free + _init are safe on init-only structures
void TlsServer::Disable()
{
	mbedtls_ssl_config_free(&conf);
	mbedtls_x509_crt_free(&cert);
	mbedtls_pk_free(&key);
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&cert);
	mbedtls_pk_init(&key);
	enabled = false;
}

mbedtls_ssl_context *TlsServer::CreateContext(TlsBioState *bio)
{
	if (!enabled || bio == nullptr || bio->conn == nullptr)
	{
		return nullptr;
	}

	mbedtls_ssl_context *ctx = static_cast<mbedtls_ssl_context *>(malloc(sizeof(mbedtls_ssl_context)));
	if (!ctx)
	{
		return nullptr;
	}
	mbedtls_ssl_init(ctx);

	const int rc = mbedtls_ssl_setup(ctx, &conf);
	if (rc != 0)
	{
		debugPrintfAlways("ssl_setup failed: -0x%04x\n", -rc);
		mbedtls_ssl_free(ctx);
		free(ctx);
		return nullptr;
	}

	mbedtls_ssl_set_bio(ctx, bio, &TlsServer::BioSend, &TlsServer::BioRecv, nullptr);
	return ctx;
}

int TlsServer::HandshakeStep(mbedtls_ssl_context *ctx)
{
	if (!ctx)
	{
		return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
	}
	const int rc = mbedtls_ssl_handshake(ctx);
	if (rc != 0 && rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
	{
		// Peer dropping the connection mid-handshake is common and expected (port scanners,
		// SIGKILL'd clients, etc.) - silent under DEBUG only. Real failures still print
		if (rc == MBEDTLS_ERR_NET_CONN_RESET)
		{
			debugPrint("handshake aborted: peer closed before completion\n");
		}
		else
		{
			debugPrintfAlways("handshake failed: -0x%04x\n", -rc);
		}
	}
	return rc;
}

void TlsServer::FreeContext(mbedtls_ssl_context *ctx)
{
	if (ctx)
	{
		mbedtls_ssl_free(ctx);
		free(ctx);
	}
}

void TlsServer::DrainBio(TlsBioState *bio)
{
	if (bio != nullptr && bio->pending != nullptr)
	{
		netbuf_delete(bio->pending);
		bio->pending = nullptr;
		bio->offset = 0;
	}
}

// Bridge mbedTLS BIO to lwIP netconn. The netconn is set non-blocking with short timeouts in Listener;
// translate would-block into the mbedTLS WANT_* sentinels so the SSL layer can retry.
// Map every lwIP "connection no longer usable" code (CLSD/RST/CONN/ABRT/RTE/IF) to CONN_RESET so
// Connection::Poll treats them all as a peer close instead of flagging a hard error
int TlsServer::BioSend(void *ctx, const unsigned char *buf, size_t len)
{
	TlsBioState *bio = static_cast<TlsBioState *>(ctx);
	size_t written = 0;
	const err_t rc = netconn_write_partly(bio->conn, buf, len, NETCONN_COPY, &written);
	if (rc == ERR_OK)
	{
		return static_cast<int>(written);
	}
	if (rc == ERR_WOULDBLOCK || rc == ERR_TIMEOUT)
	{
		return (written > 0) ? static_cast<int>(written) : MBEDTLS_ERR_SSL_WANT_WRITE;
	}
	if (rc == ERR_RST || rc == ERR_CLSD || rc == ERR_CONN || rc == ERR_ABRT || rc == ERR_RTE || rc == ERR_IF)
	{
		return MBEDTLS_ERR_NET_CONN_RESET;
	}
	return MBEDTLS_ERR_NET_SEND_FAILED;
}

// Read up to `len` bytes of ciphertext from the underlying netconn. mbedTLS issues small reads
// (e.g. 5 bytes for a record header), so we keep a pending netbuf with an offset cursor and only
// fetch the next netbuf once the previous one is fully drained
int TlsServer::BioRecv(void *ctx, unsigned char *buf, size_t len)
{
	TlsBioState *bio = static_cast<TlsBioState *>(ctx);

	if (bio->pending == nullptr)
	{
		const err_t rc = netconn_recv(bio->conn, &bio->pending);
		if (rc != ERR_OK)
		{
			bio->pending = nullptr;
			if (rc == ERR_TIMEOUT || rc == ERR_WOULDBLOCK)
			{
				return MBEDTLS_ERR_SSL_WANT_READ;
			}
			if (rc == ERR_RST || rc == ERR_CLSD || rc == ERR_CONN || rc == ERR_ABRT || rc == ERR_RTE || rc == ERR_IF)
			{
				return MBEDTLS_ERR_NET_CONN_RESET;
			}
			return MBEDTLS_ERR_NET_RECV_FAILED;
		}
		bio->offset = 0;
	}

	const u16_t got = netbuf_copy_partial(bio->pending, buf, len, bio->offset);
	bio->offset += got;

	if (bio->offset >= netbuf_len(bio->pending))
	{
		netbuf_delete(bio->pending);
		bio->pending = nullptr;
		bio->offset = 0;
	}

	return static_cast<int>(got);
}

#endif // SUPPORTS_TLS
