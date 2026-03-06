#include <stdio.h>
#include <string.h>

#include "crypto_stream.h"
#include "gateway_proto.h"

int main(void)
{
	static const uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE] = {
		0x01, 0x02, 0x03, 0x04,
		0x05, 0x06, 0x07, 0x08,
		0x09, 0x0A, 0x0B, 0x0C
	};
	static const uint8_t audio_payload[16] = {
		0x10, 0x11, 0x12, 0x13,
		0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1A, 0x1B,
		0x1C, 0x1D, 0x1E, 0x1F
	};
	uint8_t packet[CRYPTO_STREAM_HEADER_SIZE + sizeof(audio_payload)];
	uint8_t plaintext[sizeof(audio_payload)];
	uint8_t stream_id = 0;
	uint32_t seq_no = 0;
	int plain_len;
	gateway_proto_init_info_t init_info;
	gateway_proto_media_info_t media_info;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE];
	uint8_t media_buf[GATEWAY_PROTO_PACKET_HEADER_SIZE];

	if (crypto_stream_encrypt_packet(
		    packet, sizeof(packet), CRYPTO_STREAM_ID_AUDIO, 7u,
		    session_nonce, audio_payload, sizeof(audio_payload)) <= 0) {
		fprintf(stderr, "encrypt failed\n");
		return 1;
	}

	plain_len = crypto_stream_decrypt_packet(
		plaintext, sizeof(plaintext), &stream_id, &seq_no,
		session_nonce, packet, sizeof(packet));
	if (plain_len != (int)sizeof(audio_payload) ||
	    stream_id != CRYPTO_STREAM_ID_AUDIO || seq_no != 7u ||
	    memcmp(plaintext, audio_payload, sizeof(audio_payload)) != 0) {
		fprintf(stderr, "decrypt round-trip failed\n");
		return 1;
	}

	gateway_proto_default_init(&init_info);
	if (gateway_proto_write_init(init_buf, &init_info) != 0 ||
	    gateway_proto_read_init(&init_info, init_buf) != 0 ||
	    init_info.audio_sample_rate != 16000u) {
		fprintf(stderr, "init proto failed\n");
		return 1;
	}

	media_info.stream_id = GATEWAY_PROTO_STREAM_VIDEO;
	media_info.flags = GATEWAY_PROTO_FLAG_KEYFRAME;
	media_info.seq_no = 9u;
	media_info.payload_len = 1234u;
	if (gateway_proto_write_media_header(media_buf, &media_info) != 0 ||
	    gateway_proto_read_media_header(&media_info, media_buf) != 0 ||
	    media_info.stream_id != GATEWAY_PROTO_STREAM_VIDEO ||
	    media_info.flags != GATEWAY_PROTO_FLAG_KEYFRAME ||
	    media_info.seq_no != 9u || media_info.payload_len != 1234u) {
		fprintf(stderr, "media proto failed\n");
		return 1;
	}

	printf("crypto_proto_tests: ok\n");
	return 0;
}
