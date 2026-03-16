#ifndef CRYPTO_STREAM_H
#define CRYPTO_STREAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_STREAM_MAGIC0 'C'
#define CRYPTO_STREAM_MAGIC1 'H'
#define CRYPTO_STREAM_VERSION 1u
#define CRYPTO_STREAM_HEADER_SIZE 8u
#define CRYPTO_STREAM_NONCE_SIZE 12u
#define CRYPTO_STREAM_KEY_SIZE 32u

#define CRYPTO_STREAM_ID_VIDEO 1u
#define CRYPTO_STREAM_ID_AUDIO 2u

void crypto_stream_default_key(uint8_t key[CRYPTO_STREAM_KEY_SIZE]);
int crypto_stream_fill_random(uint8_t *buf, size_t len);
void crypto_stream_packet_nonce(uint8_t out_nonce[CRYPTO_STREAM_NONCE_SIZE],
				const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				uint32_t seq_no);

size_t crypto_stream_packet_size(size_t plaintext_len);
int crypto_stream_encrypt_packet(uint8_t *out_packet, size_t out_packet_size,
				 uint8_t stream_id, uint32_t seq_no,
				 const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				 const uint8_t *plaintext, size_t plaintext_len);
int crypto_stream_decrypt_packet(uint8_t *out_plaintext, size_t out_plaintext_size,
				 uint8_t *stream_id_out, uint32_t *seq_no_out,
				 const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				 const uint8_t *packet, size_t packet_size);

#ifdef __cplusplus
}
#endif

#endif
