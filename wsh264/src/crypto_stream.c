#include "crypto_stream.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chacha.h"

static const uint8_t g_crypto_stream_key[CRYPTO_STREAM_KEY_SIZE] = {
	'C', 'a', 'r', 'p', 'l', 'a', 'y', 'P',
	'o', 'C', 'C', 'h', 'a', 'C', 'h', 'a',
	'S', 't', 'r', 'e', 'a', 'm', 'K', 'e',
	'y', '-', 'v', '1', '!', '!', '!', '!'
};

static uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	       ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t read_le32(const uint8_t *p)
{
	return ((uint32_t)p[0]) |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

void crypto_stream_default_key(uint8_t key[CRYPTO_STREAM_KEY_SIZE])
{
	memcpy(key, g_crypto_stream_key, CRYPTO_STREAM_KEY_SIZE);
}

int crypto_stream_fill_random(uint8_t *buf, size_t len)
{
	FILE *fp;

	if (buf == NULL)
		return -1;

	fp = fopen("/dev/urandom", "rb");
	if (fp != NULL) {
		if (fread(buf, 1, len, fp) == len) {
			fclose(fp);
			return 0;
		}
		fclose(fp);
	}

	srand((unsigned int)time(NULL));
	for (size_t i = 0; i < len; ++i)
		buf[i] = (uint8_t)(rand() & 0xFF);
	return 0;
}

void crypto_stream_packet_nonce(uint8_t out_nonce[CRYPTO_STREAM_NONCE_SIZE],
				const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				uint32_t seq_no)
{
	uint32_t tail;

	memcpy(out_nonce, session_nonce, CRYPTO_STREAM_NONCE_SIZE);
	tail = read_le32(out_nonce + 8);
	tail += seq_no;
	write_le32(out_nonce + 8, tail);
}

size_t crypto_stream_packet_size(size_t plaintext_len)
{
	return CRYPTO_STREAM_HEADER_SIZE + plaintext_len;
}

int crypto_stream_encrypt_packet(uint8_t *out_packet, size_t out_packet_size,
				 uint8_t stream_id, uint32_t seq_no,
				 const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				 const uint8_t *plaintext, size_t plaintext_len)
{
	uint8_t key[CRYPTO_STREAM_KEY_SIZE];
	uint8_t nonce[CRYPTO_STREAM_NONCE_SIZE];
	size_t packet_size;

	packet_size = crypto_stream_packet_size(plaintext_len);
	if (out_packet == NULL || plaintext == NULL || session_nonce == NULL)
		return -1;
	if (out_packet_size < packet_size)
		return -1;

	out_packet[0] = CRYPTO_STREAM_MAGIC0;
	out_packet[1] = CRYPTO_STREAM_MAGIC1;
	out_packet[2] = CRYPTO_STREAM_VERSION;
	out_packet[3] = stream_id;
	write_be32(out_packet + 4, seq_no);

	crypto_stream_default_key(key);
	crypto_stream_packet_nonce(nonce, session_nonce, seq_no);
	chacha20_xor(out_packet + CRYPTO_STREAM_HEADER_SIZE, plaintext, plaintext_len,
		     key, nonce, 1u);
	return (int)packet_size;
}

int crypto_stream_decrypt_packet(uint8_t *out_plaintext, size_t out_plaintext_size,
				 uint8_t *stream_id_out, uint32_t *seq_no_out,
				 const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				 const uint8_t *packet, size_t packet_size)
{
	uint8_t key[CRYPTO_STREAM_KEY_SIZE];
	uint8_t nonce[CRYPTO_STREAM_NONCE_SIZE];
	size_t ciphertext_len;
	uint32_t seq_no;

	if (out_plaintext == NULL || session_nonce == NULL || packet == NULL)
		return -1;
	if (packet_size < CRYPTO_STREAM_HEADER_SIZE)
		return -1;
	if (packet[0] != CRYPTO_STREAM_MAGIC0 || packet[1] != CRYPTO_STREAM_MAGIC1)
		return -1;
	if (packet[2] != CRYPTO_STREAM_VERSION)
		return -1;

	ciphertext_len = packet_size - CRYPTO_STREAM_HEADER_SIZE;
	if (out_plaintext_size < ciphertext_len)
		return -1;

	seq_no = read_be32(packet + 4);
	if (stream_id_out != NULL)
		*stream_id_out = packet[3];
	if (seq_no_out != NULL)
		*seq_no_out = seq_no;

	crypto_stream_default_key(key);
	crypto_stream_packet_nonce(nonce, session_nonce, seq_no);
	chacha20_xor(out_plaintext, packet + CRYPTO_STREAM_HEADER_SIZE, ciphertext_len,
		     key, nonce, 1u);
	return (int)ciphertext_len;
}
