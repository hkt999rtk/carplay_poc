#ifndef GATEWAY_PROTO_H
#define GATEWAY_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_PROTO_INIT_MAGIC 0x47574931u
#define GATEWAY_PROTO_PACKET_MAGIC 0x47575031u
#define GATEWAY_PROTO_VERSION 1u

#define GATEWAY_PROTO_VIDEO_CODEC_H264_ANNEXB 1u
#define GATEWAY_PROTO_AUDIO_CODEC_PCM_S16LE 1u

#define GATEWAY_PROTO_STREAM_VIDEO 1u
#define GATEWAY_PROTO_STREAM_AUDIO 2u

#define GATEWAY_PROTO_FLAG_KEYFRAME 0x01u
#define GATEWAY_PROTO_FLAG_CONFIG 0x02u

#define GATEWAY_PROTO_INIT_SIZE 16u
#define GATEWAY_PROTO_PACKET_HEADER_SIZE 16u

typedef struct gateway_proto_init_info {
	uint8_t version;
	uint8_t video_codec;
	uint8_t audio_codec;
	uint8_t audio_channels;
	uint32_t audio_sample_rate;
	uint16_t audio_sample_bits;
} gateway_proto_init_info_t;

typedef struct gateway_proto_media_info {
	uint8_t stream_id;
	uint8_t flags;
	uint32_t seq_no;
	uint32_t payload_len;
} gateway_proto_media_info_t;

void gateway_proto_default_init(gateway_proto_init_info_t *info);
int gateway_proto_write_init(uint8_t out[GATEWAY_PROTO_INIT_SIZE],
			     const gateway_proto_init_info_t *info);
int gateway_proto_read_init(gateway_proto_init_info_t *info,
			    const uint8_t in[GATEWAY_PROTO_INIT_SIZE]);

int gateway_proto_write_media_header(
	uint8_t out[GATEWAY_PROTO_PACKET_HEADER_SIZE],
	const gateway_proto_media_info_t *info);
int gateway_proto_read_media_header(gateway_proto_media_info_t *info,
				    const uint8_t in[GATEWAY_PROTO_PACKET_HEADER_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
