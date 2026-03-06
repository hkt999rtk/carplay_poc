#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wsfs_internal.h"

static void feed_bytes(wsfs_frame_reader_t *reader,
		       const uint8_t *data,
		       size_t length,
		       const wsfs_server_config_t *cfg)
{
	uint16_t close_code = 0;
	size_t offset = 0;
	while (offset < length) {
		if (wsfs_frame_reader_needs_header(reader)) {
			size_t need = wsfs_frame_reader_header_bytes(reader);
			size_t chunk = length - offset < need ? length - offset : need;
			memcpy(wsfs_frame_reader_header_buffer(reader), data + offset, chunk);
			reader->header_count += chunk;
			offset += chunk;
			if (!wsfs_frame_reader_needs_header(reader)) {
				wsfs_status_t st =
					wsfs_frame_reader_parse_header(reader, cfg, &close_code);
				assert(st == WSFS_STATUS_OK);
			}
			continue;
		}

		size_t consumed = 0;
		wsfs_status_t st = wsfs_frame_reader_consume_payload(
			reader, data + offset,
			length - offset, cfg, &consumed, &close_code);
		assert(st == WSFS_STATUS_OK);
		offset += consumed;
	}
}

static void test_masking_short_payload(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);

	wsfs_frame_reader_t reader;
	assert(wsfs_frame_reader_init(&reader, 0) == WSFS_STATUS_OK);

	const char *payload = "Hi";
	const size_t len = strlen(payload);
	uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};

	uint8_t *frame = (uint8_t *)malloc(2 + 4 + len);
	assert(frame != NULL);
	frame[0] = 0x81; /* FIN=1, text frame */
	frame[1] = 0x80 | (uint8_t)len;
	memcpy(frame + 2, mask, 4);
	for (size_t i = 0; i < len; ++i)
		frame[6 + i] = (uint8_t)payload[i] ^ mask[i % 4];

	feed_bytes(&reader, frame, 2 + 4 + len, &cfg);
	free(frame);

	assert(!wsfs_frame_is_control(&reader));
	assert(wsfs_frame_is_fin(&reader));
	assert(reader.opcode == WSFS_OPCODE_TEXT);
	assert(wsfs_frame_payload_length(&reader) == len);
	wsfs_buffer_t *buf = wsfs_frame_payload_buffer(&reader);
	assert(buf->length == len);
	assert(memcmp(buf->data, payload, len) == 0);

	wsfs_frame_reader_reset(&reader);
	wsfs_realloc_or_free(reader.payload.data, 0);
}

static void test_extended_length_126(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);

	wsfs_frame_reader_t reader;
	assert(wsfs_frame_reader_init(&reader, 0) == WSFS_STATUS_OK);

	const size_t len = 130;
	assert(len > 125);

	uint8_t mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	uint8_t *frame = (uint8_t *)malloc(2 + 2 + 4 + len);
	assert(frame != NULL);

	frame[0] = 0x82; /* FIN=1, binary */
	frame[1] = 0x80 | 126;
	frame[2] = (uint8_t)((len >> 8) & 0xFF);
	frame[3] = (uint8_t)(len & 0xFF);
	memcpy(frame + 4, mask, 4);

	for (size_t i = 0; i < len; ++i)
		frame[8 + i] = (uint8_t)i ^ mask[i % 4];

	feed_bytes(&reader, frame, 2 + 2 + 4 + len, &cfg);
	free(frame);

	assert(reader.opcode == WSFS_OPCODE_BINARY);
	assert(wsfs_frame_payload_length(&reader) == len);
	wsfs_buffer_t *buf = wsfs_frame_payload_buffer(&reader);
	for (size_t i = 0; i < len; ++i)
		assert(buf->data[i] == (uint8_t)i);

	wsfs_frame_reader_reset(&reader);
	wsfs_realloc_or_free(reader.payload.data, 0);
}

static void test_unmasked_rejected(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);

	wsfs_frame_reader_t reader;
	assert(wsfs_frame_reader_init(&reader, 0) == WSFS_STATUS_OK);

	uint8_t frame[2] = {0x81, 0x02}; /* MASK bit not set */
	memcpy(reader.header, frame, sizeof(frame));
	reader.header_count = sizeof(frame);

	uint16_t close_code = 0;
	wsfs_status_t status =
		wsfs_frame_reader_parse_header(&reader, &cfg, &close_code);
	assert(status == WSFS_STATUS_PROTOCOL_ERROR);
	assert(close_code == 1002);
 wsfs_frame_reader_reset(&reader);
 wsfs_realloc_or_free(reader.payload.data, 0);
}

static void test_payload_too_large(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.max_frame_size = 32;

	wsfs_frame_reader_t reader;
	assert(wsfs_frame_reader_init(&reader, 0) == WSFS_STATUS_OK);

	const size_t len = 64;
	uint8_t mask[4] = {1, 2, 3, 4};
	uint8_t *frame = (uint8_t *)malloc(2 + 2 + 4 + len);
	assert(frame != NULL);
	frame[0] = 0x82;
	frame[1] = 0x80 | 126;
	frame[2] = (uint8_t)((len >> 8) & 0xFF);
	frame[3] = (uint8_t)(len & 0xFF);
	memcpy(frame + 4, mask, 4);
	memset(frame + 8, 0xAA, len);

	uint16_t close_code = 0;
	size_t offset = 0;
	while (wsfs_frame_reader_needs_header(&reader)) {
		size_t need = wsfs_frame_reader_header_bytes(&reader);
		size_t chunk = (2 + 2 + 4) - offset;
		if (chunk > need)
			chunk = need;
		memcpy(wsfs_frame_reader_header_buffer(&reader), frame + offset, chunk);
		reader.header_count += chunk;
		offset += chunk;
		if (!wsfs_frame_reader_needs_header(&reader)) {
			wsfs_status_t st =
				wsfs_frame_reader_parse_header(&reader, &cfg, &close_code);
			assert(st == WSFS_STATUS_PROTOCOL_ERROR);
			assert(close_code == 1009);
			break;
		}
	}

	wsfs_frame_reader_reset(&reader);
	wsfs_realloc_or_free(reader.payload.data, 0);
	free(frame);
}

static void test_rsv_bits_rejected(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);

	wsfs_frame_reader_t reader;
	assert(wsfs_frame_reader_init(&reader, 0) == WSFS_STATUS_OK);

	uint8_t frame[2 + 4] = {0x91, 0x80, 0, 0, 0, 0}; /* RSV1 set */
	uint16_t close_code = 0;
	size_t offset = 0;
	while (wsfs_frame_reader_needs_header(&reader)) {
		size_t need = wsfs_frame_reader_header_bytes(&reader);
		size_t chunk = sizeof(frame) - offset;
		if (chunk > need)
			chunk = need;
		memcpy(wsfs_frame_reader_header_buffer(&reader), frame + offset, chunk);
		reader.header_count += chunk;
		offset += chunk;
		if (!wsfs_frame_reader_needs_header(&reader)) {
			wsfs_status_t st =
				wsfs_frame_reader_parse_header(&reader, &cfg, &close_code);
			assert(st == WSFS_STATUS_PROTOCOL_ERROR);
			assert(close_code == 1002);
			break;
		}
	}

	wsfs_frame_reader_reset(&reader);
}

int main(void)
{
	test_masking_short_payload();
	test_extended_length_126();
	test_unmasked_rejected();
	test_payload_too_large();
	test_rsv_bits_rejected();

	printf("wsfs_frame_reader_tests: all tests passed\n");
	return 0;
}
