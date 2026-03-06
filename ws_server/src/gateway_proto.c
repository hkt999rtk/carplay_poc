#include "gateway_proto.h"

#include <string.h>

static void write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint16_t read_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	       ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

void gateway_proto_default_init(gateway_proto_init_info_t *info)
{
	if (info == NULL)
		return;
	info->version = GATEWAY_PROTO_VERSION;
	info->video_codec = GATEWAY_PROTO_VIDEO_CODEC_H264_ANNEXB;
	info->audio_codec = GATEWAY_PROTO_AUDIO_CODEC_PCM_S16LE;
	info->audio_channels = 1u;
	info->audio_sample_rate = 16000u;
	info->audio_sample_bits = 16u;
}

int gateway_proto_write_init(uint8_t out[GATEWAY_PROTO_INIT_SIZE],
			     const gateway_proto_init_info_t *info)
{
	if (out == NULL || info == NULL)
		return -1;
	write_be32(out + 0, GATEWAY_PROTO_INIT_MAGIC);
	out[4] = info->version;
	out[5] = info->video_codec;
	out[6] = info->audio_codec;
	out[7] = info->audio_channels;
	write_be32(out + 8, info->audio_sample_rate);
	write_be16(out + 12, info->audio_sample_bits);
	write_be16(out + 14, 0u);
	return 0;
}

int gateway_proto_read_init(gateway_proto_init_info_t *info,
			    const uint8_t in[GATEWAY_PROTO_INIT_SIZE])
{
	if (info == NULL || in == NULL)
		return -1;
	if (read_be32(in + 0) != GATEWAY_PROTO_INIT_MAGIC)
		return -1;
	info->version = in[4];
	info->video_codec = in[5];
	info->audio_codec = in[6];
	info->audio_channels = in[7];
	info->audio_sample_rate = read_be32(in + 8);
	info->audio_sample_bits = read_be16(in + 12);
	return 0;
}

int gateway_proto_write_media_header(
	uint8_t out[GATEWAY_PROTO_PACKET_HEADER_SIZE],
	const gateway_proto_media_info_t *info)
{
	if (out == NULL || info == NULL)
		return -1;
	write_be32(out + 0, GATEWAY_PROTO_PACKET_MAGIC);
	out[4] = info->stream_id;
	out[5] = info->flags;
	write_be16(out + 6, 0u);
	write_be32(out + 8, info->seq_no);
	write_be32(out + 12, info->payload_len);
	return 0;
}

int gateway_proto_read_media_header(gateway_proto_media_info_t *info,
				    const uint8_t in[GATEWAY_PROTO_PACKET_HEADER_SIZE])
{
	if (info == NULL || in == NULL)
		return -1;
	if (read_be32(in + 0) != GATEWAY_PROTO_PACKET_MAGIC)
		return -1;
	info->stream_id = in[4];
	info->flags = in[5];
	info->seq_no = read_be32(in + 8);
	info->payload_len = read_be32(in + 12);
	return 0;
}
