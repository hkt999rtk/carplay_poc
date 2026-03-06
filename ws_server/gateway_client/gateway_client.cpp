#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>

#include "gateway_proto.h"
#include "tcp_transport.h"
}

struct SharedVideoPacket {
	std::mutex lock;
	std::vector<uint8_t> sps;
	std::vector<uint8_t> pps;
	std::vector<uint8_t> packet;
	bool ready = false;
	bool keyframe = false;
	uint32_t seq_no = 0;
};

struct PlaybackState {
	int fd = -1;
	SDL_AudioDeviceID audio_dev = 0;
	std::atomic<bool> running{true};
	SharedVideoPacket latest_video;
	size_t audio_drop_threshold = 3200;
};

struct DecoderState {
	AVCodecContext *codec_ctx = nullptr;
	AVFrame *frame = nullptr;
	SwsContext *sws = nullptr;
	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	SDL_Texture *texture = nullptr;
	int width = 0;
	int height = 0;
	bool have_sync = false;
	std::vector<uint8_t> rgba;
};

static bool render_frame(DecoderState &state, AVFrame *frame)
{
	if (frame == nullptr)
		return false;

	if (state.window == nullptr) {
		state.window = SDL_CreateWindow("gateway_client",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			frame->width, frame->height,
			SDL_WINDOW_RESIZABLE);
		if (state.window == nullptr) {
			fprintf(stderr, "gateway_client: SDL_CreateWindow failed: %s\n", SDL_GetError());
			return false;
		}
		state.renderer = SDL_CreateRenderer(state.window, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (state.renderer == nullptr)
			state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_SOFTWARE);
		if (state.renderer == nullptr) {
			fprintf(stderr, "gateway_client: SDL_CreateRenderer failed: %s\n", SDL_GetError());
			return false;
		}
	}

	if (state.texture == nullptr || state.width != frame->width || state.height != frame->height) {
		if (state.texture != nullptr)
			SDL_DestroyTexture(state.texture);
		state.texture = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
		if (state.texture == nullptr) {
			fprintf(stderr, "gateway_client: SDL_CreateTexture failed: %s\n", SDL_GetError());
			return false;
		}
		state.width = frame->width;
		state.height = frame->height;
		SDL_SetWindowSize(state.window, frame->width, frame->height);
	}

	state.sws = sws_getCachedContext(
		state.sws, frame->width, frame->height, (AVPixelFormat)frame->format,
		frame->width, frame->height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR,
		nullptr, nullptr, nullptr);
	if (state.sws == nullptr)
		return false;

	state.rgba.resize((size_t)frame->width * (size_t)frame->height * 4u);
	{
		uint8_t *dst_data[4] = { state.rgba.data(), nullptr, nullptr, nullptr };
		int dst_linesize[4] = { frame->width * 4, 0, 0, 0 };
		sws_scale(state.sws, frame->data, frame->linesize, 0, frame->height,
			  dst_data, dst_linesize);
	}

	SDL_UpdateTexture(state.texture, nullptr, state.rgba.data(), state.width * 4);
	SDL_RenderClear(state.renderer);
	SDL_RenderCopy(state.renderer, state.texture, nullptr, nullptr);
	SDL_RenderPresent(state.renderer);
	return true;
}

static const char *av_error_string(int rc, char buf[AV_ERROR_MAX_STRING_SIZE])
{
	if (av_strerror(rc, buf, AV_ERROR_MAX_STRING_SIZE) < 0)
		snprintf(buf, AV_ERROR_MAX_STRING_SIZE, "ffmpeg error %d", rc);
	return buf;
}

static bool decoder_init(DecoderState &state)
{
	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (codec == nullptr)
		return false;

	av_log_set_level(AV_LOG_ERROR);

	state.codec_ctx = avcodec_alloc_context3(codec);
	if (state.codec_ctx == nullptr)
		return false;
	state.codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
	state.codec_ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;
	state.codec_ctx->thread_count = 1;
	if (avcodec_open2(state.codec_ctx, codec, nullptr) < 0)
		return false;

	state.frame = av_frame_alloc();
	return state.frame != nullptr;
}

static void decoder_destroy(DecoderState &state)
{
	if (state.texture != nullptr)
		SDL_DestroyTexture(state.texture);
	if (state.renderer != nullptr)
		SDL_DestroyRenderer(state.renderer);
	if (state.window != nullptr)
		SDL_DestroyWindow(state.window);
	if (state.sws != nullptr)
		sws_freeContext(state.sws);
	if (state.frame != nullptr)
		av_frame_free(&state.frame);
	if (state.codec_ctx != nullptr)
		avcodec_free_context(&state.codec_ctx);
}

static void annexb_append(std::vector<uint8_t> &packet, const std::vector<uint8_t> &nal)
{
	static const uint8_t start_code[4] = { 0, 0, 0, 1 };

	if (nal.empty())
		return;
	packet.insert(packet.end(), start_code, start_code + sizeof(start_code));
	packet.insert(packet.end(), nal.begin(), nal.end());
}

static bool decode_video_packet(DecoderState &state, const std::vector<uint8_t> &packet_bytes,
				bool keyframe)
{
	AVPacket *packet = nullptr;
	int rc;
	bool rendered = false;

	if (packet_bytes.empty())
		return true;

	packet = av_packet_alloc();
	if (packet == nullptr)
		return false;
	if (av_new_packet(packet, (int)packet_bytes.size()) < 0) {
		av_packet_free(&packet);
		return false;
	}

	memcpy(packet->data, packet_bytes.data(), packet_bytes.size());
	if (keyframe)
		packet->flags |= AV_PKT_FLAG_KEY;

	rc = avcodec_send_packet(state.codec_ctx, packet);
	if (rc < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		fprintf(stderr, "gateway_client: avcodec_send_packet failed: %s\n",
			av_error_string(rc, errbuf));
		av_packet_free(&packet);
		return false;
	}

	for (;;) {
		rc = avcodec_receive_frame(state.codec_ctx, state.frame);
		if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
			break;
		if (rc < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			fprintf(stderr, "gateway_client: avcodec_receive_frame failed: %s\n",
				av_error_string(rc, errbuf));
			av_packet_free(&packet);
			return false;
		}
		if (!render_frame(state, state.frame)) {
			av_packet_free(&packet);
			return false;
		}
		rendered = true;
	}

	if (rendered || keyframe)
		state.have_sync = true;
	av_packet_free(&packet);
	return true;
}

static uint8_t h264_nal_type(const std::vector<uint8_t> &payload)
{
	if (payload.empty())
		return 0;
	return (uint8_t)(payload[0] & 0x1f);
}

static void network_reader(PlaybackState *state)
{
	while (state->running.load()) {
		uint8_t header_buf[GATEWAY_PROTO_PACKET_HEADER_SIZE];
		gateway_proto_media_info_t media_info;
		std::vector<uint8_t> payload;
		int rc = tcp_transport_read_exact(state->fd, header_buf, sizeof(header_buf));
		if (rc <= 0)
			break;
		if (gateway_proto_read_media_header(&media_info, header_buf) != 0)
			break;

		payload.resize(media_info.payload_len);
		if (media_info.payload_len > 0 &&
		    tcp_transport_read_exact(state->fd, payload.data(), payload.size()) <= 0)
			break;

			if (media_info.stream_id == GATEWAY_PROTO_STREAM_VIDEO) {
				std::lock_guard<std::mutex> guard(state->latest_video.lock);
				uint8_t nal_type = h264_nal_type(payload);
				if ((media_info.flags & GATEWAY_PROTO_FLAG_CONFIG) != 0) {
					if (nal_type == 7)
						state->latest_video.sps = payload;
					else if (nal_type == 8)
						state->latest_video.pps = payload;
				} else {
					std::vector<uint8_t> packet;
					bool keyframe = (media_info.flags & GATEWAY_PROTO_FLAG_KEYFRAME) != 0;

					if (keyframe) {
						annexb_append(packet, state->latest_video.sps);
						annexb_append(packet, state->latest_video.pps);
					}
					annexb_append(packet, payload);

					if (state->latest_video.ready &&
					    state->latest_video.keyframe &&
					    !keyframe) {
						continue;
					}

					state->latest_video.packet.swap(packet);
					state->latest_video.seq_no = media_info.seq_no;
					state->latest_video.keyframe = keyframe;
					state->latest_video.ready = true;
				}
			} else if (media_info.stream_id == GATEWAY_PROTO_STREAM_AUDIO) {
			if (state->audio_dev != 0) {
				if (SDL_GetQueuedAudioSize(state->audio_dev) > state->audio_drop_threshold)
					SDL_ClearQueuedAudio(state->audio_dev);
				SDL_QueueAudio(state->audio_dev, payload.data(), (Uint32)payload.size());
			}
		}
	}

	state->running.store(false);
	tcp_transport_shutdown_close(state->fd);
}

int main(int argc, char **argv)
{
	const char *host = "127.0.0.1";
	uint16_t port = 19000u;
	gateway_proto_init_info_t init_info;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE];
	PlaybackState state;
	DecoderState decoder;
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	std::thread reader_thread;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
			host = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			port = (uint16_t)strtoul(argv[++i], nullptr, 10);
		} else {
			fprintf(stderr, "usage: %s [--host HOST] [--port N]\n", argv[0]);
			return 2;
		}
	}

	state.fd = tcp_transport_connect(host, port);
	if (state.fd < 0) {
		perror("gateway_client connect");
		return 1;
	}

	if (tcp_transport_read_exact(state.fd, init_buf, sizeof(init_buf)) <= 0 ||
	    gateway_proto_read_init(&init_info, init_buf) != 0) {
		fprintf(stderr, "gateway_client: failed to read init packet\n");
		tcp_transport_shutdown_close(state.fd);
		return 1;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		tcp_transport_shutdown_close(state.fd);
		return 1;
	}

	memset(&desired, 0, sizeof(desired));
	memset(&obtained, 0, sizeof(obtained));
	desired.freq = (int)init_info.audio_sample_rate;
	desired.format = AUDIO_S16SYS;
	desired.channels = init_info.audio_channels;
	desired.samples = 512;
	state.audio_dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
	if (state.audio_dev != 0)
		SDL_PauseAudioDevice(state.audio_dev, 0);

	if (!decoder_init(decoder)) {
		fprintf(stderr, "gateway_client: decoder init failed\n");
		tcp_transport_shutdown_close(state.fd);
		if (state.audio_dev != 0)
			SDL_CloseAudioDevice(state.audio_dev);
		SDL_Quit();
		return 1;
	}

	reader_thread = std::thread(network_reader, &state);
	while (state.running.load()) {
		SDL_Event event;
		std::vector<uint8_t> packet;
		bool keyframe = false;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				state.running.store(false);
		}

		{
			std::lock_guard<std::mutex> guard(state.latest_video.lock);
			if (state.latest_video.ready) {
				packet.swap(state.latest_video.packet);
				keyframe = state.latest_video.keyframe;
				state.latest_video.ready = false;
			}
		}

		if (!packet.empty()) {
			if (!keyframe && !decoder.have_sync) {
				SDL_Delay(1);
				continue;
			}
			if (!decode_video_packet(decoder, packet, keyframe)) {
				fprintf(stderr, "gateway_client: decode dropped, waiting for next keyframe\n");
				decoder.have_sync = false;
				avcodec_flush_buffers(decoder.codec_ctx);
			}
		} else {
			SDL_Delay(5);
		}
	}

	tcp_transport_shutdown_close(state.fd);
	if (reader_thread.joinable())
		reader_thread.join();
	if (state.audio_dev != 0)
		SDL_CloseAudioDevice(state.audio_dev);
	decoder_destroy(decoder);
	SDL_Quit();
	return 0;
}
