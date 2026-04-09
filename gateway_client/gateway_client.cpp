#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libusb.h>
#include <SDL2/SDL.h>

#include "gateway_proto.h"
#include "crypto_stream.h"
#include "transport.h"
}

struct QueuedVideoPacket {
	std::vector<uint8_t> packet;
	bool keyframe = false;
	uint32_t seq_no = 0;
};

struct SharedVideoState {
	std::mutex lock;
	std::vector<uint8_t> sps;
	std::vector<uint8_t> pps;
	std::deque<QueuedVideoPacket> queue;
	bool waiting_for_keyframe = true;
	size_t dropped_packets = 0;
};

struct PlaybackState {
	gateway_client_transport_t transport{};
	SDL_AudioDeviceID audio_dev = 0;
	std::atomic<bool> running{true};
	SharedVideoState video;
	size_t audio_drop_threshold = 3200;
	std::vector<uint8_t> usb_prefetch;
	size_t usb_prefetch_off = 0;
};

static constexpr size_t kMaxQueuedVideoPackets = 8;
static constexpr size_t kVideoCatchupThreshold = 6;
static constexpr size_t kVideoDecodeBurst = 4;
static const uint8_t kUsbCryptoSessionNonce[CRYPTO_STREAM_NONCE_SIZE] = {
	'U', 'S', 'B', 'R', 'e', 'l', 'a', 'y', 'V', '1', '!', '!'
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

struct UsbPingPacket {
	char magic[4];
	uint32_t seq_no;
	uint32_t payload_len;
	char payload[52];
};

struct UsbStartupState {
	bool have_init = false;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE]{};
	std::vector<uint8_t> leftover;
};

struct UsbUpstreamConfigPacket {
	char magic[4];
	uint32_t seq_no;
	uint16_t port;
	uint16_t reserved;
	char host[16];
	char path[36];
};
static_assert(sizeof(UsbUpstreamConfigPacket) == 64,
	      "UsbUpstreamConfigPacket must stay 64 bytes to match firmware");

struct UsbUpstreamOptions {
	bool enabled = true;
	std::string host;
	uint16_t port = 8081u;
	std::string path = "/gateway";
};

static bool audio_output_enabled()
{
#ifdef _WIN32
	return false;
#else
	return true;
#endif
}

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
			SDL_RENDERER_ACCELERATED);
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

static bool parse_u16_arg(const char *text, uint16_t &out)
{
	char *end = nullptr;
	unsigned long value;

	if (text == nullptr || *text == '\0')
		return false;
	value = strtoul(text, &end, 0);
	if (end == nullptr || *end != '\0' || value > 0xfffful)
		return false;
	out = (uint16_t)value;
	return true;
}

static bool parse_int_arg(const char *text, int &out)
{
	char *end = nullptr;
	long value;

	if (text == nullptr || *text == '\0')
		return false;
	value = strtol(text, &end, 0);
	if (end == nullptr || *end != '\0')
		return false;
	out = (int)value;
	return true;
}

static void enqueue_video_packet(SharedVideoState &state, std::vector<uint8_t> &packet,
				 bool keyframe, uint32_t seq_no)
{
	if (state.waiting_for_keyframe && !keyframe)
		return;

	if (keyframe)
		state.waiting_for_keyframe = false;

	if (state.queue.size() >= kMaxQueuedVideoPackets) {
		state.dropped_packets += state.queue.size();
		state.queue.clear();
		state.waiting_for_keyframe = true;
		if (!keyframe)
			return;
		state.waiting_for_keyframe = false;
		fprintf(stderr,
			"gateway_client: video queue overflow, dropped buffered packets and waiting for keyframe\n");
	}

	QueuedVideoPacket queued;
	queued.packet.swap(packet);
	queued.keyframe = keyframe;
	queued.seq_no = seq_no;
	state.queue.emplace_back(std::move(queued));
}

static void trim_video_queue_for_latency(SharedVideoState &state)
{
	if (state.queue.size() <= kVideoCatchupThreshold)
		return;

	size_t keep_from = state.queue.size();
	for (size_t i = state.queue.size(); i > 0; --i) {
		if (state.queue[i - 1].keyframe) {
			keep_from = i - 1;
			break;
		}
	}

	if (keep_from >= state.queue.size()) {
		state.dropped_packets += state.queue.size();
		state.queue.clear();
		state.waiting_for_keyframe = true;
		fprintf(stderr,
			"gateway_client: dropped queued video backlog, waiting for next keyframe\n");
		return;
	}

	if (keep_from > 0) {
		state.dropped_packets += keep_from;
		state.queue.erase(state.queue.begin(), state.queue.begin() + (ptrdiff_t)keep_from);
		fprintf(stderr,
			"gateway_client: trimmed video backlog, kept latest keyframe seq=%u backlog=%zu\n",
			(unsigned)state.queue.front().seq_no,
			state.queue.size());
	}
}

static bool decrypt_usb_media_payload(const gateway_proto_media_info_t &media_info,
				      std::vector<uint8_t> &payload)
{
	std::vector<uint8_t> plaintext;
	uint8_t decrypted_stream_id = 0;
	uint32_t decrypted_seq_no = 0;
	int decrypted_len;

	if (payload.empty())
		return true;

	plaintext.resize(payload.size());
	decrypted_len = crypto_stream_decrypt_packet(
		plaintext.data(), plaintext.size(),
		&decrypted_stream_id, &decrypted_seq_no,
		kUsbCryptoSessionNonce,
		payload.data(), payload.size());
	if (decrypted_len < 0) {
		fprintf(stderr,
			"gateway_client: usb payload decrypt failed stream=%u seq=%u enc_len=%zu\n",
			(unsigned)media_info.stream_id,
			(unsigned)media_info.seq_no,
			payload.size());
		return false;
	}

	if (decrypted_stream_id != media_info.stream_id ||
	    decrypted_seq_no != media_info.seq_no) {
		fprintf(stderr,
			"gateway_client: usb payload metadata mismatch outer(stream=%u seq=%u) inner(stream=%u seq=%u)\n",
			(unsigned)media_info.stream_id,
			(unsigned)media_info.seq_no,
			(unsigned)decrypted_stream_id,
			(unsigned)decrypted_seq_no);
		return false;
	}

	plaintext.resize((size_t)decrypted_len);
	payload.swap(plaintext);
	return true;
}

static bool detect_local_ipv4(std::string &out)
{
	char host_name[256];
	bool success = false;
#ifdef _WIN32
	WSADATA wsa_data;
	bool wsa_started = false;
	SOCKET sock = INVALID_SOCKET;
#else
	int sock = -1;
#endif

	out.clear();
#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0)
		wsa_started = true;
	else
		return false;
#endif

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (
#ifdef _WIN32
	    sock != INVALID_SOCKET
#else
	    sock >= 0
#endif
	) {
		sockaddr_in remote{};
		sockaddr_in local{};
#ifdef _WIN32
		int local_len = (int)sizeof(local);
#else
		socklen_t local_len = (socklen_t)sizeof(local);
#endif
		char addr_buf[INET_ADDRSTRLEN];

		remote.sin_family = AF_INET;
		remote.sin_port = htons(53);
#ifdef _WIN32
		if (InetPtonA(AF_INET, "1.1.1.1", &remote.sin_addr) == 1 &&
		    connect(sock, (const sockaddr *)&remote, sizeof(remote)) == 0 &&
		    getsockname(sock, (sockaddr *)&local, &local_len) == 0 &&
		    InetNtopA(AF_INET, &local.sin_addr, addr_buf, sizeof(addr_buf)) != nullptr &&
		    strcmp(addr_buf, "127.0.0.1") != 0) {
#else
		if (inet_pton(AF_INET, "1.1.1.1", &remote.sin_addr) == 1 &&
		    connect(sock, (const sockaddr *)&remote, sizeof(remote)) == 0 &&
		    getsockname(sock, (sockaddr *)&local, &local_len) == 0 &&
		    inet_ntop(AF_INET, &local.sin_addr, addr_buf, sizeof(addr_buf)) != nullptr &&
		    strcmp(addr_buf, "127.0.0.1") != 0) {
#endif
			out = addr_buf;
			success = true;
		}
	}

	if (!success && gethostname(host_name, sizeof(host_name)) == 0) {
		addrinfo hints{};
		addrinfo *result = nullptr;

		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host_name, nullptr, &hints, &result) == 0) {
			for (addrinfo *it = result; it != nullptr; it = it->ai_next) {
				const sockaddr_in *addr = (const sockaddr_in *)it->ai_addr;
				char addr_buf[INET_ADDRSTRLEN];

				if (addr == nullptr)
					continue;
#ifdef _WIN32
				if (InetNtopA(AF_INET, &addr->sin_addr, addr_buf, sizeof(addr_buf)) == nullptr)
#else
				if (inet_ntop(AF_INET, &addr->sin_addr, addr_buf, sizeof(addr_buf)) == nullptr)
#endif
					continue;
				if (strcmp(addr_buf, "127.0.0.1") == 0)
					continue;
				out = addr_buf;
				success = true;
				break;
			}
			freeaddrinfo(result);
		}
	}

#ifdef _WIN32
	if (sock != INVALID_SOCKET)
		closesocket(sock);
	if (wsa_started)
		WSACleanup();
#else
	if (sock >= 0)
		close(sock);
#endif

	return success;
}

static int send_usb_upstream_config(gateway_client_transport_t &transport, uint32_t seq_no,
				    const UsbUpstreamOptions &options, const char *reason)
{
	UsbUpstreamConfigPacket packet{};
	std::string host = options.host;

	if (transport.kind != GATEWAY_CLIENT_TRANSPORT_USB || !options.enabled)
		return 0;
	if (host.empty() && !detect_local_ipv4(host)) {
		fprintf(stderr,
			"gateway_client: failed to detect local IPv4 for usb upstream config (%s)\n",
			reason);
		return -1;
	}
	if (host.size() >= sizeof(packet.host) || options.path.size() >= sizeof(packet.path)) {
		fprintf(stderr,
			"gateway_client: usb upstream config too long host=%zu path=%zu (%s)\n",
			host.size(), options.path.size(), reason);
		return -1;
	}

	memcpy(packet.magic, "CFG1", sizeof(packet.magic));
	packet.seq_no = seq_no;
	packet.port = options.port;
	memcpy(packet.host, host.c_str(), host.size());
	memcpy(packet.path, options.path.c_str(), options.path.size());

	if (gateway_client_transport_write_all(&transport, &packet, sizeof(packet)) != 0) {
		fprintf(stderr, "gateway_client: usb upstream config write failed (%s)\n", reason);
		return -1;
	}

	fprintf(stderr,
		"gateway_client: usb upstream config sent (%s) host=%s port=%u path=%s\n",
		reason, host.c_str(), (unsigned)options.port, options.path.c_str());
	return 0;
}

static int perform_usb_startup_handshake(gateway_client_transport_t &transport, uint32_t seq_no,
					 const char *reason)
{
	UsbPingPacket ping{};
	UsbPingPacket pong{};
	const char *payload = "gateway-usb-open";
	const size_t payload_len = strlen(payload);

	memcpy(ping.magic, "PING", sizeof(ping.magic));
	ping.seq_no = seq_no;
	ping.payload_len = (uint32_t)payload_len;
	memcpy(ping.payload, payload, payload_len);

	if (gateway_client_transport_write_all(&transport, &ping, sizeof(ping)) != 0) {
		fprintf(stderr, "gateway_client: usb startup ping write failed (%s)\n", reason);
		return -1;
	}
	if (gateway_client_transport_read_exact(&transport, &pong, sizeof(pong)) <= 0) {
		fprintf(stderr, "gateway_client: usb startup pong read failed (%s)\n", reason);
		return -1;
	}
	if (memcmp(pong.magic, "PONG", sizeof(pong.magic)) != 0 || pong.seq_no != ping.seq_no) {
		fprintf(stderr,
			"gateway_client: invalid startup pong (%s) magic=%.4s seq=%u expected=%u\n",
			reason, pong.magic, (unsigned)pong.seq_no, (unsigned)ping.seq_no);
		return -1;
	}

	fprintf(stderr,
		"gateway_client: usb startup handshake ok (%s) seq=%u payload=\"%.*s\"\n",
		reason, (unsigned)pong.seq_no, (int)pong.payload_len, pong.payload);
	return 0;
}

static int prepare_usb_stream_startup(gateway_client_transport_t &transport, uint32_t seq_no,
				      const char *reason, UsbStartupState &startup,
				      const UsbUpstreamOptions &upstream_options)
{
	gateway_proto_init_info_t init_info{};
	UsbPingPacket ping{};
	UsbPingPacket pong{};
	uint8_t buf[512];
	const char *payload = "gateway-usb-open";
	const size_t payload_len = strlen(payload);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
	int transferred = 0;
	int rc;
	int flush_count = 0;
	bool got_pong = false;

	startup.have_init = false;
	startup.leftover.clear();
	if (transport.kind != GATEWAY_CLIENT_TRANSPORT_USB)
		return 0;

	for (;;) {
		rc = libusb_bulk_transfer(
			(libusb_device_handle *)transport.usb_handle,
			transport.usb_ep_in,
			buf,
			(int)sizeof(buf),
			&transferred,
			20);
		if (rc == LIBUSB_ERROR_TIMEOUT || (rc == 0 && transferred == 0))
			break;
		if (rc != 0)
			break;
		++flush_count;
		if (flush_count >= 16)
			break;
	}
	if (flush_count > 0) {
		fprintf(stderr, "gateway_client: usb startup flushed %d stale packets (%s)\n",
			flush_count, reason);
	}
	if (send_usb_upstream_config(transport, seq_no, upstream_options, reason) != 0)
		return -1;

	memcpy(ping.magic, "PING", sizeof(ping.magic));
	ping.seq_no = seq_no;
	ping.payload_len = (uint32_t)payload_len;
	memcpy(ping.payload, payload, payload_len);
	if (gateway_client_transport_write_all(&transport, &ping, sizeof(ping)) != 0) {
		fprintf(stderr, "gateway_client: usb startup ping write failed (%s)\n", reason);
		return -1;
	}

	while (std::chrono::steady_clock::now() < deadline) {
		transferred = 0;
		rc = libusb_bulk_transfer(
			(libusb_device_handle *)transport.usb_handle,
			transport.usb_ep_in,
			buf,
			(int)sizeof(buf),
			&transferred,
			200);
		if (rc == LIBUSB_ERROR_TIMEOUT || (rc == 0 && transferred == 0))
			continue;
		if (rc != 0) {
			fprintf(stderr,
				"gateway_client: usb startup read failed (%s) rc=%d(%s)\n",
				reason, rc, libusb_error_name(rc));
			return -1;
		}
		if (got_pong &&
		    transferred >= (int)sizeof(startup.init_buf) &&
		    gateway_proto_read_init(&init_info, buf) == 0) {
			memcpy(startup.init_buf, buf, sizeof(startup.init_buf));
			startup.have_init = true;
			if (transferred > (int)sizeof(startup.init_buf)) {
				startup.leftover.assign(buf + sizeof(startup.init_buf),
							buf + transferred);
			}
			fprintf(stderr,
				"gateway_client: usb startup found init (%s) version=%u video=%u audio=%u\n",
				reason,
				(unsigned)init_info.version,
				(unsigned)init_info.video_codec,
				(unsigned)init_info.audio_codec);
			return 0;
		}
		if (transferred >= (int)sizeof(pong)) {
			memcpy(&pong, buf, sizeof(pong));
			if (memcmp(pong.magic, "PONG", sizeof(pong.magic)) == 0 &&
			    pong.seq_no == ping.seq_no) {
				got_pong = true;
				fprintf(stderr,
					"gateway_client: usb startup handshake ok (%s) seq=%u payload=\"%.*s\"\n",
					reason,
					(unsigned)pong.seq_no,
					(int)pong.payload_len,
					pong.payload);
				continue;
			}
		}
		fprintf(stderr,
			"gateway_client: usb startup skipped stale packet (%s) len=%d first=%02x %02x %02x %02x\n",
			reason,
			transferred,
			transferred > 0 ? buf[0] : 0,
			transferred > 1 ? buf[1] : 0,
			transferred > 2 ? buf[2] : 0,
			transferred > 3 ? buf[3] : 0);
	}

	fprintf(stderr, "gateway_client: usb startup handshake timed out (%s)\n", reason);
	return -1;
}

static int read_exact_prefetched(gateway_client_transport_t &transport, std::vector<uint8_t> *prefetch,
				 size_t *prefetch_off, void *buf, size_t len)
{
	uint8_t *dst = (uint8_t *)buf;
	size_t copied = 0;

	if (prefetch != nullptr && prefetch_off != nullptr) {
		while (copied < len && *prefetch_off < prefetch->size()) {
			size_t avail = prefetch->size() - *prefetch_off;
			size_t take = len - copied;
			if (take > avail)
				take = avail;
			memcpy(dst + copied, prefetch->data() + *prefetch_off, take);
			*prefetch_off += take;
			copied += take;
		}
		if (*prefetch_off >= prefetch->size()) {
			prefetch->clear();
			*prefetch_off = 0;
		}
	}

	if (copied == len)
		return 1;

	return gateway_client_transport_read_exact(&transport, dst + copied, len - copied);
}

static uint32_t read_be32_host(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	       ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

static bool starts_gateway_magic(const uint8_t *buf, size_t len)
{
	uint32_t magic;

	if (buf == nullptr || len < 4)
		return false;

	magic = read_be32_host(buf);
	return magic == GATEWAY_PROTO_INIT_MAGIC || magic == GATEWAY_PROTO_PACKET_MAGIC;
}

static void prepend_prefetch(std::vector<uint8_t> *prefetch, size_t *prefetch_off,
			     const uint8_t *buf, size_t len)
{
	std::vector<uint8_t> merged;

	if (prefetch == nullptr || prefetch_off == nullptr || buf == nullptr || len == 0)
		return;

	merged.reserve(len + (prefetch->size() - *prefetch_off));
	merged.insert(merged.end(), buf, buf + len);
	if (*prefetch_off < prefetch->size()) {
		merged.insert(merged.end(),
			      prefetch->begin() + (ptrdiff_t)*prefetch_off,
			      prefetch->end());
	}
	prefetch->swap(merged);
	*prefetch_off = 0;
}

static int read_next_gateway_header(gateway_client_transport_t &transport,
				    std::vector<uint8_t> *prefetch, size_t *prefetch_off,
				    gateway_proto_media_info_t *media_info,
				    gateway_proto_init_info_t *init_info,
				    bool *saw_init, int *skipped_bytes)
{
	uint8_t header_buf[GATEWAY_PROTO_PACKET_HEADER_SIZE];
	int rc;
	int skipped = 0;

	if (saw_init != nullptr)
		*saw_init = false;
	if (skipped_bytes != nullptr)
		*skipped_bytes = 0;

	rc = read_exact_prefetched(transport, prefetch, prefetch_off, header_buf, sizeof(header_buf));
	if (rc <= 0)
		return rc;

	for (;;) {
		if (media_info != nullptr &&
		    gateway_proto_read_media_header(media_info, header_buf) == 0) {
			if (skipped_bytes != nullptr)
				*skipped_bytes = skipped;
			return 1;
		}

		if (init_info != nullptr &&
		    gateway_proto_read_init(init_info, header_buf) == 0) {
			if (saw_init != nullptr)
				*saw_init = true;
			if (skipped_bytes != nullptr)
				*skipped_bytes = skipped;
			return 2;
		}

		memmove(header_buf, header_buf + 1, sizeof(header_buf) - 1);
		rc = read_exact_prefetched(transport, prefetch, prefetch_off,
					   header_buf + sizeof(header_buf) - 1, 1);
		if (rc <= 0)
			return rc;
		++skipped;
	}
}

static int read_gateway_payload_resync(gateway_client_transport_t &transport,
				       std::vector<uint8_t> *prefetch, size_t *prefetch_off,
				       std::vector<uint8_t> &payload, size_t payload_len)
{
	uint8_t prefix[GATEWAY_PROTO_PACKET_HEADER_SIZE];
	size_t prefix_len;

	payload.clear();
	if (payload_len == 0)
		return 1;

	prefix_len = payload_len;
	if (prefix_len > sizeof(prefix))
		prefix_len = sizeof(prefix);

	if (read_exact_prefetched(transport, prefetch, prefetch_off, prefix, prefix_len) <= 0)
		return -1;

	if (starts_gateway_magic(prefix, prefix_len)) {
		prepend_prefetch(prefetch, prefetch_off, prefix, prefix_len);
		return 0;
	}

	payload.resize(payload_len);
	memcpy(payload.data(), prefix, prefix_len);

	if (payload_len > prefix_len &&
	    read_exact_prefetched(transport, prefetch, prefetch_off,
				  payload.data() + prefix_len,
				  payload_len - prefix_len) <= 0) {
		payload.clear();
		return -1;
	}

	return 1;
}

static int run_usb_ping_test(const gateway_client_transport_options_t &transport_options, int ping_count)
{
	gateway_client_transport_t transport{};

	if (gateway_client_transport_open(&transport, &transport_options) != 0) {
		fprintf(stderr, "gateway_client: transport open failed\n");
		return 1;
	}

	if (perform_usb_startup_handshake(transport, 0x1000u, "ping-test-open") != 0) {
		gateway_client_transport_close(&transport);
		return 1;
	}

	for (int i = 0; i < ping_count; ++i) {
		UsbPingPacket ping{};
		UsbPingPacket pong{};
		const char *payload = "gateway-usb-ping";
		const size_t payload_len = strlen(payload);

		memcpy(ping.magic, "PING", sizeof(ping.magic));
		ping.seq_no = (uint32_t)i;
		ping.payload_len = (uint32_t)payload_len;
		memcpy(ping.payload, payload, payload_len);

		if (gateway_client_transport_write_all(&transport, &ping, sizeof(ping)) != 0) {
			fprintf(stderr, "gateway_client: usb ping write failed at seq=%d\n", i);
			gateway_client_transport_close(&transport);
			return 1;
		}
		if (gateway_client_transport_read_exact(&transport, &pong, sizeof(pong)) <= 0) {
			fprintf(stderr, "gateway_client: usb pong read failed at seq=%d\n", i);
			gateway_client_transport_close(&transport);
			return 1;
		}
		if (memcmp(pong.magic, "PONG", sizeof(pong.magic)) != 0 || pong.seq_no != ping.seq_no) {
			fprintf(stderr,
				"gateway_client: invalid pong at seq=%d magic=%.4s seq=%u\n",
				i, pong.magic, (unsigned)pong.seq_no);
			gateway_client_transport_close(&transport);
			return 1;
		}

		fprintf(stderr,
			"gateway_client: usb ping/pong ok seq=%u payload=\"%.*s\"\n",
			(unsigned)pong.seq_no, (int)pong.payload_len, pong.payload);
	}

	gateway_client_transport_close(&transport);
	return 0;
}

static int run_diag_dump(const gateway_client_transport_options_t &transport_options,
			 int diag_seconds, int diag_packets,
			 const UsbUpstreamOptions &upstream_options)
{
	auto now_ms = []() -> uint64_t {
		using namespace std::chrono;
		return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	};

	gateway_client_transport_t transport{};
	UsbStartupState startup{};
	gateway_proto_init_info_t init_info{};
	std::atomic<bool> watchdog_running{true};
	std::thread watchdog;
	int packets_dumped = 0;
	size_t startup_off = 0;
	uint64_t last_diag_resync_log_ms = 0;
	unsigned diag_resync_suppressed = 0;
	uint64_t last_diag_short_payload_log_ms = 0;
	unsigned diag_short_payload_suppressed = 0;
	uint64_t last_diag_payload_desync_log_ms = 0;
	unsigned diag_payload_desync_suppressed = 0;

	if (diag_seconds <= 0)
		diag_seconds = 8;
	if (diag_packets <= 0)
		diag_packets = 8;

	if (gateway_client_transport_open(&transport, &transport_options) != 0) {
		fprintf(stderr, "gateway_client: transport open failed\n");
		return 1;
	}

	if (transport_options.kind == GATEWAY_CLIENT_TRANSPORT_USB &&
	    prepare_usb_stream_startup(transport, 0x2000u, "diag-open", startup,
				       upstream_options) != 0) {
		fprintf(stderr, "gateway_client: retrying usb startup (diag-open)\n");
		if (prepare_usb_stream_startup(transport, 0x2001u, "diag-open-retry", startup,
					       upstream_options) != 0) {
			gateway_client_transport_close(&transport);
			return 1;
		}
	}

	watchdog = std::thread([&transport, &watchdog_running, diag_seconds]() {
		std::this_thread::sleep_for(std::chrono::seconds(diag_seconds));
		if (watchdog_running.load())
			gateway_client_transport_request_stop(&transport);
	});

	if (startup.have_init) {
		if (gateway_proto_read_init(&init_info, startup.init_buf) != 0) {
			fprintf(stderr, "gateway_client: failed to read init packet during diag dump\n");
			watchdog_running.store(false);
			gateway_client_transport_request_stop(&transport);
			if (watchdog.joinable())
				watchdog.join();
			gateway_client_transport_close(&transport);
			return 1;
		}
	} else {
		gateway_proto_media_info_t dummy_media{};
		bool saw_init = false;
		int skipped = 0;
		int header_rc = read_next_gateway_header(transport, &startup.leftover, &startup_off,
							 &dummy_media, &init_info,
							 &saw_init, &skipped);
		if (header_rc <= 0 || !saw_init) {
			fprintf(stderr, "gateway_client: failed to read init packet during diag dump\n");
			watchdog_running.store(false);
			gateway_client_transport_request_stop(&transport);
			if (watchdog.joinable())
				watchdog.join();
			gateway_client_transport_close(&transport);
			return 1;
		}
		if (skipped > 0) {
			const uint64_t now = now_ms();
			if (last_diag_resync_log_ms == 0 ||
			    now - last_diag_resync_log_ms >= 5000) {
				if (diag_resync_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: resynced diag init after skipping %d bytes (suppressed=%u)\n",
						skipped, diag_resync_suppressed);
					diag_resync_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: resynced diag init after skipping %d bytes\n",
						skipped);
				}
				last_diag_resync_log_ms = now;
			} else {
				++diag_resync_suppressed;
			}
		}
	}
	if (init_info.version == 0) {
		fprintf(stderr, "gateway_client: failed to read init packet during diag dump\n");
		watchdog_running.store(false);
		gateway_client_transport_request_stop(&transport);
		if (watchdog.joinable())
			watchdog.join();
		gateway_client_transport_close(&transport);
		return 1;
	}

	fprintf(stderr,
		"gateway_client: init version=%u video=%u audio=%u channels=%u rate=%u bits=%u\n",
		(unsigned)init_info.version,
		(unsigned)init_info.video_codec,
		(unsigned)init_info.audio_codec,
		(unsigned)init_info.audio_channels,
		(unsigned)init_info.audio_sample_rate,
		(unsigned)init_info.audio_sample_bits);

	while (!transport.stop_requested && packets_dumped < diag_packets) {
		gateway_proto_media_info_t media_info{};
		gateway_proto_init_info_t resync_init{};
		std::vector<uint8_t> payload;
		bool saw_init = false;
		int skipped = 0;
		int rc = read_next_gateway_header(transport, &startup.leftover, &startup_off,
						  &media_info, &resync_init,
						  &saw_init, &skipped);
		if (rc <= 0)
			break;
		if (rc == 2 && saw_init) {
			init_info = resync_init;
			const uint64_t now = now_ms();
			if (last_diag_resync_log_ms == 0 ||
			    now - last_diag_resync_log_ms >= 5000) {
				if (diag_resync_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: resynced init during diag dump version=%u video=%u audio=%u skipped=%d (suppressed=%u)\n",
						(unsigned)init_info.version,
						(unsigned)init_info.video_codec,
						(unsigned)init_info.audio_codec,
						skipped,
						diag_resync_suppressed);
					diag_resync_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: resynced init during diag dump version=%u video=%u audio=%u skipped=%d\n",
						(unsigned)init_info.version,
						(unsigned)init_info.video_codec,
						(unsigned)init_info.audio_codec,
						skipped);
				}
				last_diag_resync_log_ms = now;
			} else {
				++diag_resync_suppressed;
			}
			continue;
		}
		if (skipped > 0) {
			const uint64_t now = now_ms();
			if (last_diag_resync_log_ms == 0 ||
			    now - last_diag_resync_log_ms >= 5000) {
				if (diag_resync_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: resynced media header after skipping %d bytes (suppressed=%u)\n",
						skipped, diag_resync_suppressed);
					diag_resync_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: resynced media header after skipping %d bytes\n",
						skipped);
				}
				last_diag_resync_log_ms = now;
			} else {
				++diag_resync_suppressed;
			}
		}

		rc = read_gateway_payload_resync(transport, &startup.leftover, &startup_off,
						 payload, media_info.payload_len);
		if (rc < 0) {
			const uint64_t now = now_ms();
			if (last_diag_short_payload_log_ms == 0 ||
			    now - last_diag_short_payload_log_ms >= 5000) {
				if (diag_short_payload_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: short payload during diag dump (suppressed=%u)\n",
						diag_short_payload_suppressed);
					diag_short_payload_suppressed = 0;
				} else {
					fprintf(stderr, "gateway_client: short payload during diag dump\n");
				}
				last_diag_short_payload_log_ms = now;
			} else {
				++diag_short_payload_suppressed;
			}
			break;
		}
		if (rc == 0) {
			const uint64_t now = now_ms();
			if (last_diag_payload_desync_log_ms == 0 ||
			    now - last_diag_payload_desync_log_ms >= 5000) {
				if (diag_payload_desync_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: payload desync detected during diag dump, retrying header scan (suppressed=%u)\n",
						diag_payload_desync_suppressed);
					diag_payload_desync_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: payload desync detected during diag dump, retrying header scan\n");
				}
				last_diag_payload_desync_log_ms = now;
			} else {
				++diag_payload_desync_suppressed;
			}
			continue;
		}
		if (!decrypt_usb_media_payload(media_info, payload)) {
			fprintf(stderr,
				"gateway_client: dropped diag packet after usb decrypt failure seq=%u\n",
				(unsigned)media_info.seq_no);
			continue;
		}

		fprintf(stderr,
			"gateway_client: pkt[%d] stream=%u flags=0x%02x seq=%u len=%u first=%02x %02x %02x %02x\n",
			packets_dumped,
			(unsigned)media_info.stream_id,
			(unsigned)media_info.flags,
			(unsigned)media_info.seq_no,
			(unsigned)payload.size(),
			payload.size() > 0 ? payload[0] : 0,
			payload.size() > 1 ? payload[1] : 0,
			payload.size() > 2 ? payload[2] : 0,
			payload.size() > 3 ? payload[3] : 0);
		++packets_dumped;
	}

	watchdog_running.store(false);
	gateway_client_transport_request_stop(&transport);
	if (watchdog.joinable())
		watchdog.join();
	gateway_client_transport_close(&transport);

	if (packets_dumped == 0) {
		fprintf(stderr, "gateway_client: diag dump saw no media packets\n");
		return 1;
	}

	fprintf(stderr, "gateway_client: diag dump complete packets=%d\n", packets_dumped);
	return 0;
}

static int run_raw_usb_probe(const gateway_client_transport_options_t &transport_options,
			     int probe_reads, int probe_timeout_ms)
{
	gateway_client_transport_t transport{};
	std::vector<uint8_t> buf(512);
	int reads_done = 0;

	if (transport_options.kind != GATEWAY_CLIENT_TRANSPORT_USB) {
		fprintf(stderr, "gateway_client: raw usb probe requires --transport usb\n");
		return 2;
	}

	if (probe_reads <= 0)
		probe_reads = 4;
	if (probe_timeout_ms <= 0)
		probe_timeout_ms = 500;

	if (gateway_client_transport_open(&transport, &transport_options) != 0) {
		fprintf(stderr, "gateway_client: transport open failed\n");
		return 1;
	}

	for (int i = 0; i < probe_reads; ++i) {
		int transferred = 0;
		int rc = libusb_bulk_transfer(
			(libusb_device_handle *)transport.usb_handle,
			transport.usb_ep_in,
			buf.data(),
			(int)buf.size(),
			&transferred,
			probe_timeout_ms);

		fprintf(stderr, "gateway_client: raw usb read[%d] rc=%d(%s) transferred=%d",
			i, rc, libusb_error_name(rc), transferred);
		if (rc == 0 && transferred > 0) {
			fprintf(stderr, " first=%02x %02x %02x %02x",
				buf[0], buf[1], buf[2], buf[3]);
		}
		fprintf(stderr, "\n");
		++reads_done;
	}

	gateway_client_transport_close(&transport);
	return reads_done > 0 ? 0 : 1;
}

static void network_reader(PlaybackState *state)
{
	auto now_ms = []() -> uint64_t {
		using namespace std::chrono;
		return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	};

	uint64_t last_payload_desync_log_ms = 0;
	unsigned payload_desync_suppressed = 0;
	uint64_t last_resync_init_log_ms = 0;
	unsigned resync_init_suppressed = 0;

	while (state->running.load()) {
		gateway_proto_media_info_t media_info;
		gateway_proto_init_info_t resync_init{};
		std::vector<uint8_t> payload;
		bool saw_init = false;
		int skipped = 0;
		int rc = read_next_gateway_header(state->transport, &state->usb_prefetch,
						  &state->usb_prefetch_off, &media_info,
						  &resync_init, &saw_init, &skipped);
		if (rc <= 0)
			break;
		if (rc == 2 && saw_init) {
			const uint64_t now = now_ms();
			if (last_resync_init_log_ms == 0 ||
			    now - last_resync_init_log_ms >= 5000) {
				if (resync_init_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: resynced init during playback version=%u skipped=%d (suppressed=%u)\n",
						(unsigned)resync_init.version, skipped,
						resync_init_suppressed);
					resync_init_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: resynced init during playback version=%u skipped=%d\n",
						(unsigned)resync_init.version, skipped);
				}
				last_resync_init_log_ms = now;
			} else {
				++resync_init_suppressed;
			}
			continue;
		}

		rc = read_gateway_payload_resync(state->transport, &state->usb_prefetch,
						 &state->usb_prefetch_off, payload,
						 media_info.payload_len);
		if (rc < 0)
			break;
		if (rc == 0) {
			const uint64_t now = now_ms();
			if (last_payload_desync_log_ms == 0 ||
			    now - last_payload_desync_log_ms >= 5000) {
				if (payload_desync_suppressed != 0) {
					fprintf(stderr,
						"gateway_client: payload desync detected during playback, resyncing (suppressed=%u)\n",
						payload_desync_suppressed);
					payload_desync_suppressed = 0;
				} else {
					fprintf(stderr,
						"gateway_client: payload desync detected during playback, resyncing\n");
				}
				last_payload_desync_log_ms = now;
			} else {
				++payload_desync_suppressed;
			}
			continue;
		}
		if (!decrypt_usb_media_payload(media_info, payload))
			continue;

		if (media_info.stream_id == GATEWAY_PROTO_STREAM_VIDEO) {
			std::lock_guard<std::mutex> guard(state->video.lock);
			uint8_t nal_type = h264_nal_type(payload);
			if ((media_info.flags & GATEWAY_PROTO_FLAG_CONFIG) != 0) {
				if (nal_type == 7)
					state->video.sps = payload;
				else if (nal_type == 8)
					state->video.pps = payload;
			} else {
				std::vector<uint8_t> packet;
				bool keyframe = (media_info.flags & GATEWAY_PROTO_FLAG_KEYFRAME) != 0;

				if (keyframe) {
					annexb_append(packet, state->video.sps);
					annexb_append(packet, state->video.pps);
				}
				annexb_append(packet, payload);
				enqueue_video_packet(state->video, packet, keyframe, media_info.seq_no);
				trim_video_queue_for_latency(state->video);
			}
		} else if (media_info.stream_id == GATEWAY_PROTO_STREAM_AUDIO) {
			if (audio_output_enabled() && state->audio_dev != 0) {
				if (SDL_GetQueuedAudioSize(state->audio_dev) > state->audio_drop_threshold)
					SDL_ClearQueuedAudio(state->audio_dev);
				SDL_QueueAudio(state->audio_dev, payload.data(), (Uint32)payload.size());
			}
		}
	}

	state->running.store(false);
	gateway_client_transport_request_stop(&state->transport);
}

int main(int argc, char **argv)
{
	gateway_proto_init_info_t init_info;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE];
	UsbStartupState startup;
	PlaybackState state;
	DecoderState decoder;
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	std::thread reader_thread;
	gateway_client_transport_options_t transport_options;
	bool usb_ping_mode = false;
	int usb_ping_count = 10;
	bool diag_dump_mode = false;
	int diag_seconds = 8;
	int diag_packets = 8;
	bool raw_usb_probe_mode = false;
	int raw_usb_probe_reads = 4;
	int raw_usb_probe_timeout_ms = 500;
	UsbUpstreamOptions usb_upstream_options;

	gateway_client_transport_options_default(&transport_options);

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--transport") == 0 && i + 1 < argc) {
			const char *value = argv[++i];
			if (strcmp(value, "usb") == 0)
				transport_options.kind = GATEWAY_CLIENT_TRANSPORT_USB;
			else if (strcmp(value, "tcp") == 0)
				transport_options.kind = GATEWAY_CLIENT_TRANSPORT_TCP;
			else {
				fprintf(stderr, "gateway_client: unsupported transport '%s'\n", value);
				return 2;
			}
		} else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
			transport_options.host = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			if (!parse_u16_arg(argv[++i], transport_options.port)) {
				fprintf(stderr, "gateway_client: invalid port\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--usb-vid") == 0 && i + 1 < argc) {
			if (!parse_u16_arg(argv[++i], transport_options.usb_vid)) {
				fprintf(stderr, "gateway_client: invalid usb vid\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--usb-pid") == 0 && i + 1 < argc) {
			if (!parse_u16_arg(argv[++i], transport_options.usb_pid)) {
				fprintf(stderr, "gateway_client: invalid usb pid\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--usb-poll-ms") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], transport_options.usb_poll_ms) ||
			    transport_options.usb_poll_ms <= 0) {
				fprintf(stderr, "gateway_client: invalid usb poll interval\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--usb-timeout-ms") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], transport_options.usb_transfer_timeout_ms) ||
			    transport_options.usb_transfer_timeout_ms <= 0) {
				fprintf(stderr, "gateway_client: invalid usb transfer timeout\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--usb-ping") == 0) {
			usb_ping_mode = true;
		} else if (strcmp(argv[i], "--usb-ping-count") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], usb_ping_count) || usb_ping_count <= 0) {
				fprintf(stderr, "gateway_client: invalid usb ping count\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--diag-dump") == 0) {
			diag_dump_mode = true;
		} else if (strcmp(argv[i], "--diag-seconds") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], diag_seconds) || diag_seconds <= 0) {
				fprintf(stderr, "gateway_client: invalid diag seconds\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--diag-packets") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], diag_packets) || diag_packets <= 0) {
				fprintf(stderr, "gateway_client: invalid diag packet count\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--raw-usb-probe") == 0) {
			raw_usb_probe_mode = true;
		} else if (strcmp(argv[i], "--raw-usb-reads") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], raw_usb_probe_reads) || raw_usb_probe_reads <= 0) {
				fprintf(stderr, "gateway_client: invalid raw usb read count\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--raw-usb-timeout-ms") == 0 && i + 1 < argc) {
			if (!parse_int_arg(argv[++i], raw_usb_probe_timeout_ms) ||
			    raw_usb_probe_timeout_ms <= 0) {
				fprintf(stderr, "gateway_client: invalid raw usb timeout\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--upstream-host") == 0 && i + 1 < argc) {
			usb_upstream_options.host = argv[++i];
		} else if (strcmp(argv[i], "--upstream-port") == 0 && i + 1 < argc) {
			if (!parse_u16_arg(argv[++i], usb_upstream_options.port) ||
			    usb_upstream_options.port == 0) {
				fprintf(stderr, "gateway_client: invalid upstream port\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--upstream-path") == 0 && i + 1 < argc) {
			usb_upstream_options.path = argv[++i];
			if (usb_upstream_options.path.empty()) {
				fprintf(stderr, "gateway_client: invalid upstream path\n");
				return 2;
			}
		} else if (strcmp(argv[i], "--no-usb-upstream-config") == 0) {
			usb_upstream_options.enabled = false;
		} else {
			fprintf(stderr,
				"usage: %s [--transport usb|tcp] [--host HOST] [--port N] "
				"[--usb-vid VID] [--usb-pid PID] [--usb-poll-ms N] "
				"[--usb-timeout-ms N] [--usb-ping] [--usb-ping-count N] "
				"[--diag-dump] [--diag-seconds N] [--diag-packets N] "
				"[--upstream-host IP] [--upstream-port N] [--upstream-path PATH] "
				"[--no-usb-upstream-config] "
				"[--raw-usb-probe] [--raw-usb-reads N] [--raw-usb-timeout-ms N]\n",
				argv[0]);
			return 2;
		}
	}

	if (usb_ping_mode) {
		if (transport_options.kind != GATEWAY_CLIENT_TRANSPORT_USB) {
			fprintf(stderr, "gateway_client: --usb-ping requires --transport usb\n");
			return 2;
		}
		fprintf(stderr, "gateway_client: opening USB ping transport %04x:%04x\n",
			(unsigned)transport_options.usb_vid, (unsigned)transport_options.usb_pid);
		return run_usb_ping_test(transport_options, usb_ping_count);
	}

	if (diag_dump_mode) {
		fprintf(stderr, "gateway_client: opening %s diag transport\n",
			transport_options.kind == GATEWAY_CLIENT_TRANSPORT_USB ? "USB" : "TCP");
		return run_diag_dump(transport_options, diag_seconds, diag_packets,
				     usb_upstream_options);
	}

	if (raw_usb_probe_mode) {
		fprintf(stderr, "gateway_client: opening raw USB probe transport %04x:%04x\n",
			(unsigned)transport_options.usb_vid, (unsigned)transport_options.usb_pid);
		return run_raw_usb_probe(transport_options, raw_usb_probe_reads, raw_usb_probe_timeout_ms);
	}

	if (transport_options.kind == GATEWAY_CLIENT_TRANSPORT_TCP) {
		fprintf(stderr, "gateway_client: connecting via TCP to %s:%u\n",
			transport_options.host, (unsigned)transport_options.port);
	} else {
		fprintf(stderr, "gateway_client: opening USB transport %04x:%04x\n",
			(unsigned)transport_options.usb_vid, (unsigned)transport_options.usb_pid);
	}

	if (gateway_client_transport_open(&state.transport, &transport_options) != 0) {
		fprintf(stderr, "gateway_client: transport open failed\n");
		return 1;
	}

	if (transport_options.kind == GATEWAY_CLIENT_TRANSPORT_USB &&
	    prepare_usb_stream_startup(state.transport, 0x3000u, "playback-open", startup,
				       usb_upstream_options) != 0) {
		fprintf(stderr, "gateway_client: retrying usb startup (playback-open)\n");
		if (prepare_usb_stream_startup(state.transport, 0x3001u, "playback-open-retry", startup,
					       usb_upstream_options) != 0) {
			gateway_client_transport_close(&state.transport);
			return 1;
		}
	}

	if (startup.have_init) {
		memcpy(init_buf, startup.init_buf, sizeof(init_buf));
		state.usb_prefetch = startup.leftover;
		state.usb_prefetch_off = 0;
	} else if (gateway_client_transport_read_exact(&state.transport, init_buf, sizeof(init_buf)) <= 0) {
		fprintf(stderr, "gateway_client: failed to read init packet\n");
		gateway_client_transport_close(&state.transport);
		return 1;
	}
	if (gateway_proto_read_init(&init_info, init_buf) != 0) {
		fprintf(stderr, "gateway_client: failed to read init packet\n");
		gateway_client_transport_close(&state.transport);
		return 1;
	}

	Uint32 sdl_init_flags = SDL_INIT_VIDEO;
	if (audio_output_enabled())
		sdl_init_flags |= SDL_INIT_AUDIO;

	if (SDL_Init(sdl_init_flags) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		gateway_client_transport_close(&state.transport);
		return 1;
	}

	if (audio_output_enabled()) {
		memset(&desired, 0, sizeof(desired));
		memset(&obtained, 0, sizeof(obtained));
		desired.freq = (int)init_info.audio_sample_rate;
		desired.format = AUDIO_S16SYS;
		desired.channels = init_info.audio_channels;
		desired.samples = 512;
		state.audio_dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
		if (state.audio_dev != 0)
			SDL_PauseAudioDevice(state.audio_dev, 0);
	} else {
		fprintf(stderr, "gateway_client: audio output disabled on Windows\n");
	}

	if (!decoder_init(decoder)) {
		fprintf(stderr, "gateway_client: decoder init failed\n");
		gateway_client_transport_close(&state.transport);
		if (state.audio_dev != 0)
			SDL_CloseAudioDevice(state.audio_dev);
		SDL_Quit();
		return 1;
	}

	reader_thread = std::thread(network_reader, &state);
	while (state.running.load()) {
		SDL_Event event;
		size_t decoded_this_round = 0;

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				state.running.store(false);
				gateway_client_transport_request_stop(&state.transport);
			}
		}

		while (state.running.load() && decoded_this_round < kVideoDecodeBurst) {
			std::vector<uint8_t> packet;
			bool keyframe = false;
			uint32_t seq_no = 0;

			{
				std::lock_guard<std::mutex> guard(state.video.lock);
				if (state.video.queue.empty())
					break;
				QueuedVideoPacket queued = std::move(state.video.queue.front());
				state.video.queue.pop_front();
				packet.swap(queued.packet);
				keyframe = queued.keyframe;
				seq_no = queued.seq_no;
			}

			if (!keyframe && !decoder.have_sync) {
				continue;
			}
			if (!decode_video_packet(decoder, packet, keyframe)) {
				fprintf(stderr,
					"gateway_client: decode dropped at seq=%u, waiting for next keyframe\n",
					(unsigned)seq_no);
				decoder.have_sync = false;
				avcodec_flush_buffers(decoder.codec_ctx);
				std::lock_guard<std::mutex> guard(state.video.lock);
				state.video.queue.clear();
				state.video.waiting_for_keyframe = true;
				break;
			}
			++decoded_this_round;
		}

		if (decoded_this_round == 0)
			SDL_Delay(1);
	}

	gateway_client_transport_request_stop(&state.transport);
	if (reader_thread.joinable())
		reader_thread.join();
	gateway_client_transport_close(&state.transport);
	if (state.audio_dev != 0)
		SDL_CloseAudioDevice(state.audio_dev);
	decoder_destroy(decoder);
	SDL_Quit();
	return 0;
}
