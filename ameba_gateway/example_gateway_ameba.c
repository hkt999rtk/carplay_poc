#include "FreeRTOS.h"
#include "task.h"

#include <platform/platform_stdlib.h>
#include <string.h>

#include "core/inc/usb_composite.h"
#include "diag.h"
#include "example_gateway_ameba.h"
#include "gateway_proto.h"
#include "crypto_stream.h"
#include "base64.h"
#include "hal_cache.h"
#include "log_service.h"
#include "lwip_netconf.h"
#include "lwip/errno.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "usb.h"
#include "usb_gadget.h"
#include "wifi_conf.h"
#include "wifi_constants.h"
#include "wifi_ind.h"
#include "cJSON.h"
#include "gateway_wifi_local_config.h"

#define GATEWAY_AMEBA_DIAG_STAGE_MINIMAL 0

#ifndef GATEWAY_AMEBA_ASSUME_WIFI_SAMPLE
#define GATEWAY_AMEBA_ASSUME_WIFI_SAMPLE 0
#endif

#ifndef GATEWAY_AMEBA_ENABLE_USB
#define GATEWAY_AMEBA_ENABLE_USB 1
#endif

#define GATEWAY_AMEBA_TASK_STACK_SIZE 3072
#define GATEWAY_AMEBA_TASK_PRIORITY   (tskIDLE_PRIORITY + 1)
#define GATEWAY_AMEBA_HEARTBEAT_MS    1000
#define GATEWAY_AMEBA_USB_OUT_PACKET_SIZE 64
#define GATEWAY_AMEBA_USB_IN_CHUNK_SIZE 512
#define GATEWAY_AMEBA_USB_TX_RING_SIZE (128 * 1024)
#define GATEWAY_AMEBA_UPSTREAM_RETRY_MS 5000
#define GATEWAY_AMEBA_USB_STALL_TIMEOUT_MS 2000
#define GATEWAY_AMEBA_UPSTREAM_HOST_MAX 16
#define GATEWAY_AMEBA_UPSTREAM_PATH_MAX 36

#ifndef GATEWAY_AMEBA_UPSTREAM_HOST
#define GATEWAY_AMEBA_UPSTREAM_HOST "192.168.1.54"
#endif

#ifndef GATEWAY_AMEBA_UPSTREAM_PORT
#define GATEWAY_AMEBA_UPSTREAM_PORT 8081
#endif

#ifndef GATEWAY_AMEBA_UPSTREAM_PATH
#define GATEWAY_AMEBA_UPSTREAM_PATH "/gateway"
#endif

#define GATEWAY_USB_STRING_MANUFACTURER 1
#define GATEWAY_USB_STRING_PRODUCT      2
#define GATEWAY_USB_STRING_SERIAL       3
#define GATEWAY_USB_STRING_CONFIG       4
#define GATEWAY_USB_STRING_INTERFACE    5

static const uint8_t g_gateway_usb_crypto_nonce[CRYPTO_STREAM_NONCE_SIZE] = {
	'U', 'S', 'B', 'R', 'e', 'l', 'a', 'y', 'V', '1', '!', '!'
};

typedef struct gateway_usb_ping_packet {
	char magic[4];
	u32 seq_no;
	u32 payload_len;
	char payload[52];
} gateway_usb_ping_packet_t;

typedef struct gateway_usb_upstream_config_packet {
	char magic[4];
	u32 seq_no;
	u16 port;
	u16 reserved;
	char host[GATEWAY_AMEBA_UPSTREAM_HOST_MAX];
	char path[GATEWAY_AMEBA_UPSTREAM_PATH_MAX];
} gateway_usb_upstream_config_packet_t;
typedef char gateway_usb_upstream_config_packet_size_check[
	(sizeof(gateway_usb_upstream_config_packet_t) == GATEWAY_AMEBA_USB_OUT_PACKET_SIZE) ? 1 : -1];

typedef struct gateway_usb_function_ctx {
	struct usb_function function;
	struct usb_ep *in_ep;
	struct usb_ep *out_ep;
	struct usb_request *in_req;
	struct usb_request *out_req;
	u8 *in_buf;
	u8 *out_buf;
	int configured;
	int in_busy;
	size_t in_flight_len;
} gateway_usb_function_ctx_t;

typedef struct gateway_upstream_state {
	int fd;
	uint8_t *prebuffer;
	size_t prebuffer_len;
	size_t prebuffer_off;
	int connected;
	int have_nonce;
	uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE];
} gateway_upstream_state_t;

typedef struct gateway_upstream_config {
	char host[GATEWAY_AMEBA_UPSTREAM_HOST_MAX];
	char path[GATEWAY_AMEBA_UPSTREAM_PATH_MAX];
	u16 port;
	int valid;
	u32 seq_no;
} gateway_upstream_config_t;

static volatile int gateway_usb_started = 0;
static volatile int gateway_usb_configured = 0;
static volatile unsigned long gateway_usb_rx_packets = 0;
static volatile unsigned long gateway_usb_tx_packets = 0;
static volatile unsigned long gateway_usb_last_actual = 0;
static volatile unsigned long gateway_usb_last_seq = 0;
static volatile unsigned long gateway_usb_last_payload_len = 0;
static volatile unsigned long gateway_usb_last_req_buf_addr = 0;
static volatile unsigned long gateway_usb_last_ctx_buf_addr = 0;
static volatile u8 gateway_usb_last_magic[4] = {0};
static volatile u8 gateway_usb_last_bytes[4] = {0};
static volatile u8 gateway_usb_last_req_dump[16] = {0};
static volatile u8 gateway_usb_last_ctx_dump[16] = {0};
static volatile int gateway_usb_last_ping_valid = 0;
static volatile int gateway_wifi_connect_started = 0;
static volatile int gateway_wifi_connected = 0;
static volatile int gateway_wifi_dhcp_ready = 0;
static volatile int gateway_wifi_task_finished = 0;
static volatile int gateway_wifi_last_connect_rc = RTW_ERROR;
static volatile int gateway_task_create_attempted = 0;
static volatile int gateway_task_created = 0;
static volatile int gateway_task_running = 0;
static volatile int gateway_task_create_rc = -999;
static volatile int gateway_usb_start_attempted = 0;
static volatile int gateway_usb_start_rc = -999;
static volatile unsigned long gateway_wifi_connect_attempts = 0;
static volatile unsigned long gateway_wifi_disconnect_count = 0;
static volatile int gateway_upstream_connect_started = 0;
static volatile int gateway_upstream_connected = 0;
static volatile int gateway_upstream_last_rc = -999;
static volatile unsigned long gateway_upstream_connect_attempts = 0;
static volatile unsigned long gateway_upstream_text_frames = 0;
static volatile unsigned long gateway_upstream_binary_frames = 0;
static volatile unsigned long gateway_upstream_close_frames = 0;
static volatile unsigned long gateway_upstream_last_opcode = 0;
static volatile unsigned long gateway_upstream_last_payload_len = 0;
static volatile unsigned long gateway_upstream_last_attempt_tick = 0;
static volatile int gateway_upstream_last_errno = 0;
static volatile int gateway_upstream_config_valid = 0;
static volatile unsigned long gateway_upstream_config_seq = 0;
static volatile int gateway_upstream_reconnect_requested = 0;
static volatile unsigned long gateway_usb_stream_bytes_enqueued = 0;
static volatile unsigned long gateway_usb_stream_bytes_sent = 0;
static volatile unsigned long gateway_usb_stream_drop_count = 0;
static volatile int gateway_usb_host_ready = 0;
static volatile int gateway_usb_proto_init_sent = 0;
static volatile int gateway_usb_stream_resetting = 0;
static volatile int gateway_usb_last_kick_rc = 0;
static volatile size_t gateway_usb_last_ring_count = 0;
static volatile unsigned long gateway_usb_stream_kick_count = 0;
static volatile unsigned long gateway_usb_in_complete_count = 0;
static volatile unsigned long gateway_usb_in_error_count = 0;
static volatile unsigned long gateway_usb_fifo_flush_count = 0;
static volatile unsigned long gateway_usb_watchdog_reset_count = 0;
static volatile unsigned long gateway_usb_last_queue_len = 0;
static int gateway_atcmd_registered = 0;
static volatile unsigned long gateway_cpu_busy_pct = 0;
static volatile unsigned long gateway_cpu_idle_pct = 0;
static volatile unsigned long gateway_cpu_sample_ticks = 0;
static unsigned long gateway_cpu_busy_history[10] = {0};
static unsigned long gateway_cpu_history_sum = 0;
static unsigned int gateway_cpu_history_index = 0;
static unsigned int gateway_cpu_history_count = 0;

extern struct netif xnetif[NET_IF_NUM];
extern volatile uint32_t g_gateway_cpu_tick_total;
extern volatile uint32_t g_gateway_cpu_tick_busy;
static gateway_upstream_state_t gateway_upstream = { -1, NULL, 0, 0, 0 };
static gateway_upstream_config_t gateway_upstream_config = {
	GATEWAY_AMEBA_UPSTREAM_HOST,
	GATEWAY_AMEBA_UPSTREAM_PATH,
	GATEWAY_AMEBA_UPSTREAM_PORT,
	0,
	0u
};
static gateway_usb_function_ctx_t gateway_usb_ctx;
static u8 gateway_usb_in_buf[GATEWAY_AMEBA_USB_IN_CHUNK_SIZE] __attribute__((aligned(32)));
static u8 gateway_usb_out_buf[GATEWAY_AMEBA_USB_OUT_PACKET_SIZE] __attribute__((aligned(32)));
static uint8_t gateway_usb_tx_ring[GATEWAY_AMEBA_USB_TX_RING_SIZE];
static size_t gateway_usb_tx_head = 0;
static size_t gateway_usb_tx_tail = 0;
static size_t gateway_usb_tx_count = 0;

static int gateway_parse_crypto_init(const uint8_t *payload, size_t payload_len,
				     uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				     int *have_nonce);
static int gateway_usb_enqueue_init_packet(void);
static int gateway_usb_enqueue_media_packet(uint8_t stream_id, uint32_t seq_no,
					    const uint8_t *payload, size_t payload_len);
static int gateway_usb_kick_tx(gateway_usb_function_ctx_t *ctx);
static void gateway_usb_retry_init_if_needed(void);
static void gateway_usb_reset_stream_session(gateway_usb_function_ctx_t *ctx, const char *reason);
static int gateway_upstream_config_snapshot(gateway_upstream_config_t *config);
void gateway_ameba_register_atcmd(void);
extern void sys_reset(void);

static void gateway_cpu_update_sample(void)
{
	static uint32_t last_total = 0;
	static uint32_t last_busy = 0;
	uint32_t total = g_gateway_cpu_tick_total;
	uint32_t busy = g_gateway_cpu_tick_busy;
	uint32_t delta_total = total - last_total;
	uint32_t delta_busy = busy - last_busy;
	unsigned long busy_pct = 0;

	last_total = total;
	last_busy = busy;
	gateway_cpu_sample_ticks = delta_total;
	if (delta_total == 0) {
		gateway_cpu_busy_pct = 0;
		gateway_cpu_idle_pct = 0;
		return;
	}
	if (delta_busy > delta_total) {
		delta_busy = delta_total;
	}
	busy_pct = ((unsigned long)delta_busy * 100UL + (unsigned long)(delta_total / 2U)) / (unsigned long)delta_total;
	if (busy_pct > 100UL) {
		busy_pct = 100UL;
	}
	gateway_cpu_history_sum -= gateway_cpu_busy_history[gateway_cpu_history_index];
	gateway_cpu_busy_history[gateway_cpu_history_index] = busy_pct;
	gateway_cpu_history_sum += busy_pct;
	gateway_cpu_history_index = (gateway_cpu_history_index + 1u) % 10u;
	if (gateway_cpu_history_count < 10u) {
		gateway_cpu_history_count++;
	}
	gateway_cpu_busy_pct = gateway_cpu_history_sum / gateway_cpu_history_count;
	gateway_cpu_idle_pct = 100UL - gateway_cpu_busy_pct;
}

static void gateway_upstream_close(gateway_upstream_state_t *state)
{
	if (state == NULL) {
		return;
	}
	if (state->fd >= 0) {
		lwip_close(state->fd);
	}
	if (state->prebuffer != NULL) {
		free(state->prebuffer);
	}
	state->fd = -1;
	state->prebuffer = NULL;
	state->prebuffer_len = 0;
	state->prebuffer_off = 0;
	state->connected = 0;
	state->have_nonce = 0;
	memset(state->session_nonce, 0, sizeof(state->session_nonce));
	gateway_upstream_connected = 0;
}

static int gateway_upstream_copy_field(char *dst, size_t dst_size,
				       const char *src, size_t src_size)
{
	size_t len = 0u;

	if (dst == NULL || dst_size == 0u || src == NULL || src_size == 0u)
		return -1;

	while (len < src_size && src[len] != '\0')
		++len;
	if (len == 0u || len >= src_size || len >= dst_size)
		return -1;

	memcpy(dst, src, len);
	dst[len] = '\0';
	return 0;
}

static int gateway_upstream_apply_usb_config(const gateway_usb_upstream_config_packet_t *packet)
{
	gateway_upstream_config_t next;
	ip_addr_t addr;

	if (packet == NULL)
		return -1;
	if (packet->port == 0u)
		return -1;
	if (gateway_upstream_copy_field(next.host, sizeof(next.host),
					packet->host, sizeof(packet->host)) != 0)
		return -1;
	if (gateway_upstream_copy_field(next.path, sizeof(next.path),
					packet->path, sizeof(packet->path)) != 0)
		return -1;
	if (inet_aton(next.host, &addr) == 0)
		return -1;

	next.port = packet->port;
	next.valid = 1;
	next.seq_no = packet->seq_no;

	taskENTER_CRITICAL();
	gateway_upstream_config = next;
	gateway_upstream_config_valid = 1;
	gateway_upstream_config_seq = next.seq_no;
	gateway_upstream_reconnect_requested = 1;
	taskEXIT_CRITICAL();

	printf("[gateway][usb] upstream cfg seq=%lu host=%s port=%u path=%s\r\n",
	       (unsigned long)next.seq_no,
	       next.host,
	       (unsigned)next.port,
	       next.path);
	return 0;
}

static int gateway_upstream_config_snapshot(gateway_upstream_config_t *config)
{
	if (config == NULL)
		return 0;

	taskENTER_CRITICAL();
	*config = gateway_upstream_config;
	taskEXIT_CRITICAL();
	return config->valid;
}

static int gateway_upstream_write_all(int fd, const uint8_t *buf, size_t len)
{
	size_t written = 0;

	while (written < len) {
		int rc = lwip_write(fd, buf + written, len - written);
		if (rc <= 0) {
			return -1;
		}
		written += (size_t)rc;
	}

	return 0;
}

static int gateway_upstream_send_masked_frame(int fd, uint8_t opcode,
					      const uint8_t *payload, size_t length)
{
	uint8_t header[14];
	uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
	uint8_t *masked = NULL;
	size_t header_len = 0;
	size_t i;
	int rc;

	header[header_len++] = (uint8_t)(0x80u | opcode);
	if (length <= 125u) {
		header[header_len++] = (uint8_t)(0x80u | (uint8_t)length);
	} else if (length <= 0xFFFFu) {
		header[header_len++] = 0x80u | 126u;
		header[header_len++] = (uint8_t)(length >> 8);
		header[header_len++] = (uint8_t)length;
	} else {
		header[header_len++] = 0x80u | 127u;
		for (i = 0; i < 8u; ++i) {
			header[header_len++] = (uint8_t)(length >> ((7u - i) * 8u));
		}
	}

	memcpy(header + header_len, mask, sizeof(mask));
	header_len += sizeof(mask);
	if (gateway_upstream_write_all(fd, header, header_len) != 0) {
		return -1;
	}

	if (length == 0u) {
		return 0;
	}

	masked = (uint8_t *)malloc(length);
	if (masked == NULL) {
		return -1;
	}
	for (i = 0; i < length; ++i) {
		masked[i] = payload[i] ^ mask[i % 4u];
	}
	rc = gateway_upstream_write_all(fd, masked, length);
	free(masked);
	return rc;
}

static int gateway_upstream_send_text(int fd, const char *text)
{
	return gateway_upstream_send_masked_frame(fd, 0x1u, (const uint8_t *)text, strlen(text));
}

static int gateway_upstream_read_some(gateway_upstream_state_t *state, uint8_t *dest, size_t len)
{
	size_t copied = 0;

	while (copied < len) {
		if (state->prebuffer != NULL && state->prebuffer_off < state->prebuffer_len) {
			size_t avail = state->prebuffer_len - state->prebuffer_off;
			size_t take = avail;
			if (take > (len - copied)) {
				take = len - copied;
			}
			memcpy(dest + copied, state->prebuffer + state->prebuffer_off, take);
			state->prebuffer_off += take;
			copied += take;
			if (state->prebuffer_off == state->prebuffer_len) {
				free(state->prebuffer);
				state->prebuffer = NULL;
				state->prebuffer_len = 0;
				state->prebuffer_off = 0;
			}
			continue;
		}

		{
			int rc = lwip_read(state->fd, dest + copied, len - copied);
			if (rc <= 0) {
				return -1;
			}
			copied += (size_t)rc;
		}
	}

	return 0;
}

static int gateway_upstream_recv_frame(gateway_upstream_state_t *state, uint8_t *opcode_out,
				       uint8_t **payload_out, size_t *payload_len_out)
{
	uint8_t header[2];
	uint8_t mask[4] = {0};
	uint8_t *payload = NULL;
	size_t payload_len;

	if (gateway_upstream_read_some(state, header, sizeof(header)) != 0) {
		return -1;
	}

	*opcode_out = (uint8_t)(header[0] & 0x0Fu);
	payload_len = (size_t)(header[1] & 0x7Fu);
	if (payload_len == 126u) {
		uint8_t ext[2];
		if (gateway_upstream_read_some(state, ext, sizeof(ext)) != 0) {
			return -1;
		}
		payload_len = ((size_t)ext[0] << 8) | (size_t)ext[1];
	} else if (payload_len == 127u) {
		uint8_t ext[8];
		size_t i;
		payload_len = 0u;
		if (gateway_upstream_read_some(state, ext, sizeof(ext)) != 0) {
			return -1;
		}
		for (i = 0; i < 8u; ++i) {
			payload_len = (payload_len << 8) | (size_t)ext[i];
		}
	}

	if ((header[1] & 0x80u) != 0u) {
		if (gateway_upstream_read_some(state, mask, sizeof(mask)) != 0) {
			return -1;
		}
	}

	if (payload_len > 0u) {
		size_t i;
		payload = (uint8_t *)malloc(payload_len);
		if (payload == NULL) {
			return -1;
		}
		if (gateway_upstream_read_some(state, payload, payload_len) != 0) {
			free(payload);
			return -1;
		}
		if ((header[1] & 0x80u) != 0u) {
			for (i = 0; i < payload_len; ++i) {
				payload[i] ^= mask[i % 4u];
			}
		}
	}

	*payload_out = payload;
	*payload_len_out = payload_len;
	return 0;
}

static int gateway_upstream_connect(gateway_upstream_state_t *state)
{
	gateway_upstream_config_t config;
	struct sockaddr_in addr;
	char request[512];
	uint8_t response[2048];
	size_t total = 0;
	int fd;
	int written;

	gateway_upstream_connect_started = 1;
	gateway_upstream_last_attempt_tick = (unsigned long)xTaskGetTickCount();
	++gateway_upstream_connect_attempts;
	gateway_upstream_last_errno = 0;
	state->have_nonce = 0;
	memset(state->session_nonce, 0, sizeof(state->session_nonce));
	if (!gateway_upstream_config_snapshot(&config)) {
		gateway_upstream_last_rc = -12;
		return -1;
	}

	fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		gateway_upstream_last_errno = errno;
		gateway_upstream_last_rc = -1;
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = lwip_htons(config.port);
	addr.sin_addr.s_addr = inet_addr(config.host);
	if (addr.sin_addr.s_addr == IPADDR_NONE) {
		lwip_close(fd);
		gateway_upstream_last_errno = errno;
		gateway_upstream_last_rc = -2;
		return -1;
	}

	if (lwip_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		gateway_upstream_last_errno = errno;
		lwip_close(fd);
		gateway_upstream_last_rc = -3;
		return -1;
	}

	written = snprintf(request, sizeof(request),
			   "GET %s HTTP/1.1\r\n"
			   "Host: %s:%u\r\n"
			   "Upgrade: websocket\r\n"
			   "Connection: Upgrade\r\n"
			   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
			   "Sec-WebSocket-Version: 13\r\n"
			   "\r\n",
			   config.path,
			   config.host,
			   (unsigned)config.port);
	if (written <= 0 || (size_t)written >= sizeof(request)) {
		lwip_close(fd);
		gateway_upstream_last_errno = errno;
		gateway_upstream_last_rc = -4;
		return -1;
	}

	if (gateway_upstream_write_all(fd, (const uint8_t *)request, (size_t)written) != 0) {
		lwip_close(fd);
		gateway_upstream_last_errno = errno;
		gateway_upstream_last_rc = -5;
		return -1;
	}

	while (total + 1u < sizeof(response)) {
		char *header_end;
		int rc = lwip_read(fd, response + total, sizeof(response) - total - 1u);
		if (rc <= 0) {
			lwip_close(fd);
			gateway_upstream_last_errno = errno;
			gateway_upstream_last_rc = -6;
			return -1;
		}
		total += (size_t)rc;
		response[total] = '\0';
		header_end = strstr((const char *)response, "\r\n\r\n");
		if (header_end != NULL) {
			size_t header_len = (size_t)(header_end - (char *)response) + 4u;
			size_t extra = total - header_len;
			if (strstr((const char *)response, "101") == NULL) {
				lwip_close(fd);
				gateway_upstream_last_errno = errno;
				gateway_upstream_last_rc = -7;
				return -1;
			}
			if (extra > 0u) {
				state->prebuffer = (uint8_t *)malloc(extra);
				if (state->prebuffer == NULL) {
					lwip_close(fd);
					gateway_upstream_last_errno = errno;
					gateway_upstream_last_rc = -8;
					return -1;
				}
				memcpy(state->prebuffer, response + header_len, extra);
				state->prebuffer_len = extra;
				state->prebuffer_off = 0;
			}
			state->fd = fd;
			state->connected = 1;
			gateway_upstream_connected = 1;
			gateway_upstream_last_rc = 0;
			if (gateway_upstream_send_text(fd, "{\"cmd\":\"start_stream\",\"param\":{\"mirror\":1}}") != 0 ||
			    gateway_upstream_send_text(fd, "{\"cmd\":\"start_audio_stream\"}") != 0) {
				gateway_upstream_close(state);
				gateway_upstream_last_errno = errno;
				gateway_upstream_last_rc = -9;
				return -1;
			}
			printf("[gateway][printf] upstream connected host=%s port=%u\r\n",
			       config.host,
			       (unsigned)config.port);
			dbg_printf("[gateway][upstream] connected host=%s port=%u path=%s\r\n",
				   config.host,
				   (unsigned)config.port,
				   config.path);
			return 0;
		}
	}

	lwip_close(fd);
	gateway_upstream_last_errno = errno;
	gateway_upstream_last_rc = -10;
	return -1;
}

static void gateway_upstream_poll(gateway_upstream_state_t *state)
{
	fd_set rfds;
	struct timeval tv;
	uint8_t opcode = 0;
	uint8_t *payload = NULL;
	size_t payload_len = 0;
	int rc;

	if (state == NULL || !state->connected || state->fd < 0) {
		return;
	}

	FD_ZERO(&rfds);
	FD_SET(state->fd, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	rc = lwip_select(state->fd + 1, &rfds, NULL, NULL, &tv);
	if (rc <= 0 || !FD_ISSET(state->fd, &rfds)) {
		return;
	}

	if (gateway_upstream_recv_frame(state, &opcode, &payload, &payload_len) != 0) {
		dbg_printf("[gateway][upstream] recv failed, closing connection\r\n");
		gateway_upstream_last_errno = errno;
		gateway_upstream_last_rc = -11;
		gateway_upstream_close(state);
		return;
	}

	gateway_upstream_last_opcode = opcode;
	gateway_upstream_last_payload_len = payload_len;
	if (opcode == 0x1u) {
		++gateway_upstream_text_frames;
		(void)gateway_parse_crypto_init(payload, payload_len, state->session_nonce, &state->have_nonce);
	} else if (opcode == 0x2u) {
		uint8_t stream_id = 0;
		uint32_t seq_no = 0;
		uint8_t *plaintext = NULL;
		int plain_len;
		++gateway_upstream_binary_frames;
		if (!state->have_nonce) {
			free(payload);
			return;
		}
		plaintext = (uint8_t *)malloc(payload_len);
		if (plaintext == NULL) {
			free(payload);
			return;
		}
		plain_len = crypto_stream_decrypt_packet(plaintext, payload_len, &stream_id, &seq_no,
							 state->session_nonce, payload, payload_len);
		if (plain_len > 0) {
			if (gateway_usb_started && gateway_usb_configured && gateway_usb_host_ready &&
			    gateway_usb_enqueue_init_packet() == 0 &&
			    gateway_usb_enqueue_media_packet(stream_id, seq_no, plaintext, (size_t)plain_len) == 0) {
				(void)gateway_usb_kick_tx(&gateway_usb_ctx);
			}
		}
		free(plaintext);
	} else if (opcode == 0x8u) {
		++gateway_upstream_close_frames;
		gateway_upstream_close(state);
	}

	free(payload);
}

static int gateway_usb_function_set_alt(struct usb_function *function,
					unsigned interface, unsigned alt);
static int gateway_usb_function_setup(struct usb_function *function,
				      const struct usb_ctrlrequest *ctrl);

static struct usb_device_descriptor gateway_usb_device_desc = {
	.bLength = sizeof(gateway_usb_device_desc),
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0x00,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0BDA,
	.idProduct = 0x8195,
	.iManufacturer = GATEWAY_USB_STRING_MANUFACTURER,
	.iProduct = GATEWAY_USB_STRING_PRODUCT,
	.iSerialNumber = GATEWAY_USB_STRING_SERIAL,
	.bNumConfigurations = 0x01,
};

static struct usb_string gateway_usb_strings_tab[] = {
	{ GATEWAY_USB_STRING_MANUFACTURER, "Realtek Singapore Semiconductor" },
	{ GATEWAY_USB_STRING_PRODUCT, "AmebaPro Gateway" },
	{ GATEWAY_USB_STRING_SERIAL, "0123456789" },
	{ GATEWAY_USB_STRING_CONFIG, "AmebaPro Gateway Config" },
	{ GATEWAY_USB_STRING_INTERFACE, "AmebaPro Gateway Interface" },
	{ 0, NULL },
};

static struct usb_gadget_strings gateway_usb_stringtab = {
	.language = 0x0409,
	.strings = gateway_usb_strings_tab,
};

static struct usb_gadget_strings *gateway_usb_dev_strings[] = {
	&gateway_usb_stringtab,
	NULL,
};

static struct usb_interface_descriptor gateway_usb_intf_desc = {
	.bLength = sizeof(gateway_usb_intf_desc),
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.iInterface = GATEWAY_USB_STRING_INTERFACE,
};

static struct usb_endpoint_descriptor gateway_usb_ep_in_fs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_out_fs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_in_hs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 512,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_out_hs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 512,
	.bInterval = 0,
};

static struct usb_descriptor_header *gateway_usb_fs_descs[] = {
	(struct usb_descriptor_header *)&gateway_usb_intf_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_in_fs_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_out_fs_desc,
	NULL,
};

static struct usb_descriptor_header *gateway_usb_hs_descs[] = {
	(struct usb_descriptor_header *)&gateway_usb_intf_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_in_hs_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_out_hs_desc,
	NULL,
};

static uint8_t gateway_video_flags(const uint8_t *payload, size_t payload_len)
{
	uint8_t flags = 0;
	uint8_t nal_type;

	if (payload == NULL || payload_len == 0u)
		return 0;
	if (payload_len >= 16u && memcmp(payload, "VFG1", 4) == 0)
		return payload[4];
	nal_type = (uint8_t)(payload[0] & 0x1Fu);
	if (nal_type == 5u)
		flags |= GATEWAY_PROTO_FLAG_KEYFRAME;
	if (nal_type == 7u || nal_type == 8u)
		flags |= GATEWAY_PROTO_FLAG_CONFIG;
	return flags;
}

static int gateway_parse_crypto_init(const uint8_t *payload, size_t payload_len,
				     uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				     int *have_nonce)
{
	cJSON *root;
	cJSON *type;
	cJSON *result;
	cJSON *session_nonce_json;
	unsigned char *decoded;
	size_t decoded_len;
	char *text;

	text = (char *)malloc(payload_len + 1u);
	if (text == NULL)
		return -1;
	memcpy(text, payload, payload_len);
	text[payload_len] = '\0';

	root = cJSON_Parse(text);
	free(text);
	if (root == NULL)
		return 0;

	type = cJSON_GetObjectItem(root, "type");
	if (type == NULL || type->valuestring == NULL ||
	    strcmp(type->valuestring, "crypto_init") != 0) {
		cJSON_Delete(root);
		return 0;
	}

	result = cJSON_GetObjectItem(root, "result");
	session_nonce_json = result != NULL ? cJSON_GetObjectItem(result, "session_nonce") : NULL;
	if (session_nonce_json == NULL || session_nonce_json->valuestring == NULL) {
		cJSON_Delete(root);
		return -1;
	}

	decoded = base64_decode((const unsigned char *)session_nonce_json->valuestring,
				strlen(session_nonce_json->valuestring), &decoded_len);
	if (decoded == NULL || decoded_len != CRYPTO_STREAM_NONCE_SIZE) {
		free(decoded);
		cJSON_Delete(root);
		return -1;
	}

	memcpy(session_nonce, decoded, CRYPTO_STREAM_NONCE_SIZE);
	free(decoded);
	*have_nonce = 1;
	cJSON_Delete(root);
	return 0;
}

static void gateway_usb_tx_reset(void)
{
	taskENTER_CRITICAL();
	gateway_usb_tx_head = 0;
	gateway_usb_tx_tail = 0;
	gateway_usb_tx_count = 0;
	taskEXIT_CRITICAL();
	gateway_usb_proto_init_sent = 0;
	gateway_usb_last_ring_count = 0;
	gateway_usb_last_queue_len = 0;
}

static size_t gateway_usb_tx_free_space(void)
{
	return GATEWAY_AMEBA_USB_TX_RING_SIZE - gateway_usb_tx_count;
}

static void gateway_usb_tx_ring_write(size_t pos, const uint8_t *data, size_t len)
{
	size_t first_part;
	size_t second_part;

	if (data == NULL || len == 0u)
		return;

	first_part = len;
	if (first_part > (GATEWAY_AMEBA_USB_TX_RING_SIZE - pos))
		first_part = GATEWAY_AMEBA_USB_TX_RING_SIZE - pos;
	second_part = len - first_part;

	memcpy(gateway_usb_tx_ring + pos, data, first_part);
	if (second_part > 0u)
		memcpy(gateway_usb_tx_ring, data + first_part, second_part);
}

static int gateway_usb_tx_enqueue_packet(const uint8_t *prefix, size_t prefix_len,
					 const uint8_t *payload, size_t payload_len)
{
	size_t total_len = prefix_len + payload_len;
	size_t head;

	if ((prefix == NULL && prefix_len > 0u) || (payload == NULL && payload_len > 0u))
		return -1;
	if (total_len == 0u)
		return 0;

	taskENTER_CRITICAL();
	if (total_len > gateway_usb_tx_free_space()) {
		taskEXIT_CRITICAL();
		++gateway_usb_stream_drop_count;
		return -1;
	}

	head = gateway_usb_tx_head;
	if (prefix_len > 0u) {
		gateway_usb_tx_ring_write(head, prefix, prefix_len);
		head = (head + prefix_len) % GATEWAY_AMEBA_USB_TX_RING_SIZE;
	}
	if (payload_len > 0u) {
		gateway_usb_tx_ring_write(head, payload, payload_len);
		head = (head + payload_len) % GATEWAY_AMEBA_USB_TX_RING_SIZE;
	}

	gateway_usb_tx_head = head;
	gateway_usb_tx_count += total_len;
	gateway_usb_last_ring_count = gateway_usb_tx_count;
	gateway_usb_stream_bytes_enqueued += total_len;
	taskEXIT_CRITICAL();
	return 0;
}

static size_t gateway_usb_tx_peek_chunk(uint8_t *dest, size_t max_len)
{
	size_t len;
	size_t first_part;
	size_t second_part;

	if (dest == NULL || max_len == 0u)
		return 0u;

	taskENTER_CRITICAL();
	len = gateway_usb_tx_count;
	if (len > max_len)
		len = max_len;
	if (len == 0u) {
		taskEXIT_CRITICAL();
		return 0u;
	}

	first_part = len;
	if (first_part > (GATEWAY_AMEBA_USB_TX_RING_SIZE - gateway_usb_tx_tail))
		first_part = GATEWAY_AMEBA_USB_TX_RING_SIZE - gateway_usb_tx_tail;
	second_part = len - first_part;

	memcpy(dest, gateway_usb_tx_ring + gateway_usb_tx_tail, first_part);
	if (second_part > 0u)
		memcpy(dest + first_part, gateway_usb_tx_ring, second_part);

	taskEXIT_CRITICAL();
	return len;
}

static void gateway_usb_tx_consume_chunk(size_t len)
{
	if (len == 0u)
		return;

	taskENTER_CRITICAL();
	if (len > gateway_usb_tx_count)
		len = gateway_usb_tx_count;
	gateway_usb_tx_tail = (gateway_usb_tx_tail + len) % GATEWAY_AMEBA_USB_TX_RING_SIZE;
	gateway_usb_tx_count -= len;
	gateway_usb_last_ring_count = gateway_usb_tx_count;
	gateway_usb_stream_bytes_sent += len;
	taskEXIT_CRITICAL();
}

static int gateway_usb_enqueue_init_packet(void)
{
	gateway_proto_init_info_t init_info;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE];

	if (gateway_usb_proto_init_sent)
		return 0;

	gateway_proto_default_init(&init_info);
	if (gateway_proto_write_init(init_buf, &init_info) != 0)
		return -1;
	if (gateway_usb_tx_enqueue_packet(init_buf, sizeof(init_buf), NULL, 0u) != 0)
		return -1;
	gateway_usb_proto_init_sent = 1;
	return 0;
}

static int gateway_usb_enqueue_media_packet(uint8_t stream_id, uint32_t seq_no,
					    const uint8_t *payload, size_t payload_len)
{
	gateway_proto_media_info_t media_info;
	uint8_t header[GATEWAY_PROTO_PACKET_HEADER_SIZE];
	uint8_t *encrypted_payload = NULL;
	size_t encrypted_payload_len = 0u;
	int encrypted_len = 0;

	media_info.stream_id = stream_id;
	media_info.flags = stream_id == GATEWAY_PROTO_STREAM_VIDEO ?
		gateway_video_flags(payload, payload_len) : 0u;
	media_info.seq_no = seq_no;

	if (payload_len > 0u) {
		encrypted_payload_len = crypto_stream_packet_size(payload_len);
		encrypted_payload = (uint8_t *)malloc(encrypted_payload_len);
		if (encrypted_payload == NULL)
			return -1;
		encrypted_len = crypto_stream_encrypt_packet(
			encrypted_payload, encrypted_payload_len, stream_id, seq_no,
			g_gateway_usb_crypto_nonce, payload, payload_len);
		if (encrypted_len < 0) {
			free(encrypted_payload);
			return -1;
		}
		encrypted_payload_len = (size_t)encrypted_len;
	}

	media_info.payload_len = (uint32_t)encrypted_payload_len;

	if (gateway_proto_write_media_header(header, &media_info) != 0)
		goto fail;
	if (gateway_usb_tx_enqueue_packet(header, sizeof(header),
					  encrypted_payload, encrypted_payload_len) != 0)
		goto fail;
	free(encrypted_payload);
	return 0;

fail:
	free(encrypted_payload);
	return -1;
}

static void gateway_usb_try_force_configure(void)
{
	enum usb_device_speed speed;

	if (!gateway_usb_started || gateway_usb_ctx.configured) {
		return;
	}
	if (gateway_usb_ctx.function.config == NULL ||
	    gateway_usb_ctx.function.config->cdev == NULL ||
	    gateway_usb_ctx.function.config->cdev->gadget == NULL) {
		return;
	}

	speed = gateway_usb_ctx.function.config->cdev->gadget->speed;
	if (speed != USB_SPEED_FULL && speed != USB_SPEED_HIGH) {
		return;
	}

	printf("[gateway][usb] forcing alt0 configure, speed=%d\r\n", speed);
	(void)gateway_usb_function_set_alt(&gateway_usb_ctx.function,
					   gateway_usb_intf_desc.bInterfaceNumber,
					   0);
}

static int gateway_usb_queue_out_request(gateway_usb_function_ctx_t *ctx);
static int gateway_usb_kick_tx(gateway_usb_function_ctx_t *ctx);

static void gateway_usb_fifo_flush(struct usb_ep *ep, const char *reason)
{
	if (ep == NULL || ep->ops == NULL || ep->ops->fifo_flush == NULL)
		return;

	ep->ops->fifo_flush(ep);
	++gateway_usb_fifo_flush_count;
	printf("[gateway][usb] fifo_flush ep=%s reason=%s count=%lu\r\n",
	       ep->name != NULL ? ep->name : "<na>",
	       reason != NULL ? reason : "<na>",
	       gateway_usb_fifo_flush_count);
}

static void gateway_usb_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	size_t consume_len = 0u;

	(void)ep;

	++gateway_usb_in_complete_count;
	gateway_usb_ctx.in_busy = 0;
	if (req != NULL && req->status != 0) {
		gateway_usb_ctx.in_flight_len = 0u;
		if (gateway_usb_stream_resetting) {
			gateway_usb_stream_resetting = 0;
			printf("[gateway][usb] bulk IN reset completion status=%d actual=%u\r\n",
			       req->status, req->actual);
			return;
		}
		++gateway_usb_in_error_count;
		printf("[gateway][usb] bulk IN complete status=%d actual=%u\r\n",
		       req->status, req->actual);
		gateway_usb_reset_stream_session(&gateway_usb_ctx, "in-complete-error");
		return;
	}
	if (gateway_usb_ctx.in_flight_len > 0u) {
		consume_len = gateway_usb_ctx.in_flight_len;
		if (req != NULL && req->actual > 0u && (size_t)req->actual < consume_len)
			consume_len = (size_t)req->actual;
		gateway_usb_tx_consume_chunk(consume_len);
		gateway_usb_ctx.in_flight_len = 0u;
	}
	(void)gateway_usb_kick_tx(&gateway_usb_ctx);
}

static int gateway_usb_send_pong(gateway_usb_function_ctx_t *ctx, u32 seq_no)
{
	gateway_usb_ping_packet_t *pong;
	const char *payload = "gateway-usb-pong";
	unsigned int payload_len = (unsigned int)strlen(payload);
	int rc;

	if (ctx == NULL || ctx->in_ep == NULL || ctx->in_req == NULL || ctx->in_busy) {
		return -1;
	}

	memset(ctx->in_buf, 0, GATEWAY_AMEBA_USB_IN_CHUNK_SIZE);
	pong = (gateway_usb_ping_packet_t *)ctx->in_buf;
	memcpy(pong->magic, "PONG", sizeof(pong->magic));
	pong->seq_no = seq_no;
	pong->payload_len = payload_len;
	memcpy(pong->payload, payload, payload_len);

	ctx->in_req->buf = ctx->in_buf;
	ctx->in_req->dma = (dma_addr_t)ctx->in_buf;
	ctx->in_req->length = sizeof(*pong);
	ctx->in_req->actual = 0;
	ctx->in_req->status = 0;
	ctx->in_req->zero = 0;
	ctx->in_busy = 1;
	ctx->in_flight_len = 0u;
	dcache_clean_by_addr((uint32_t *)ctx->in_buf, (int32_t)ctx->in_req->length);

	rc = usb_ep_queue(ctx->in_ep, ctx->in_req, 0);
	if (rc != 0) {
		ctx->in_busy = 0;
		printf("[gateway][usb] failed to queue bulk IN rc=%d\r\n", rc);
		return -1;
	}

	++gateway_usb_tx_packets;
	printf("[gateway][usb] pong seq=%lu tx_packets=%lu\r\n",
	       (unsigned long)seq_no,
	       gateway_usb_tx_packets);
	return 0;
}

static void gateway_usb_reset_stream_session(gateway_usb_function_ctx_t *ctx, const char *reason)
{
	int rc = 0;

	if (ctx == NULL || ctx->in_ep == NULL || ctx->in_req == NULL)
		return;

	if (ctx->in_busy) {
		gateway_usb_stream_resetting = 1;
		rc = usb_ep_dequeue(ctx->in_ep, ctx->in_req);
		printf("[gateway][usb] dequeue IN rc=%d reason=%s\r\n", rc,
		       reason != NULL ? reason : "<na>");
		if (rc != 0)
			gateway_usb_stream_resetting = 0;
	} else {
		gateway_usb_stream_resetting = 0;
	}
	ctx->in_busy = 0;
	ctx->in_flight_len = 0u;
	gateway_usb_fifo_flush(ctx->in_ep, reason);
	gateway_usb_tx_reset();
	gateway_usb_last_kick_rc = rc;
}

static int gateway_usb_kick_tx(gateway_usb_function_ctx_t *ctx)
{
	size_t chunk_len;
	int rc;

	if (ctx == NULL || ctx->in_ep == NULL || ctx->in_req == NULL ||
	    !ctx->configured || ctx->in_busy)
		return 0;

	chunk_len = gateway_usb_tx_peek_chunk(ctx->in_buf, GATEWAY_AMEBA_USB_IN_CHUNK_SIZE);
	if (chunk_len == 0u)
		return 0;

	ctx->in_req->buf = ctx->in_buf;
	ctx->in_req->dma = (dma_addr_t)ctx->in_buf;
	ctx->in_req->length = chunk_len;
	ctx->in_req->actual = 0;
	ctx->in_req->status = 0;
	ctx->in_req->zero = 0;
	ctx->in_busy = 1;
	ctx->in_flight_len = chunk_len;
	gateway_usb_last_queue_len = chunk_len;
	++gateway_usb_stream_kick_count;
	dcache_clean_by_addr((uint32_t *)ctx->in_buf, (int32_t)chunk_len);

	rc = usb_ep_queue(ctx->in_ep, ctx->in_req, 0);
	if (rc != 0) {
		ctx->in_busy = 0;
		ctx->in_flight_len = 0u;
		printf("[gateway][usb] failed to queue stream IN rc=%d\r\n", rc);
		gateway_usb_last_kick_rc = rc;
		return -1;
	}

	gateway_usb_last_kick_rc = 0;
	return 0;
}

static void gateway_usb_retry_init_if_needed(void)
{
	if (!gateway_usb_started || !gateway_usb_configured)
		return;
	if (!gateway_usb_host_ready)
		return;

	if (gateway_usb_enqueue_init_packet() == 0)
		(void)gateway_usb_kick_tx(&gateway_usb_ctx);
}

static void gateway_usb_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)req->context;
	const gateway_usb_upstream_config_packet_t *config_pkt;
	const gateway_usb_ping_packet_t *ping;
	const u8 *raw;
	unsigned int i;

	(void)ep;

	if (ctx == NULL || req == NULL) {
		return;
	}

	if (req->status != 0) {
		printf("[gateway][usb] bulk OUT complete status=%d actual=%u\r\n",
		       req->status, req->actual);
		return;
	}

	++gateway_usb_rx_packets;
	gateway_usb_host_ready = 1;
	gateway_usb_last_actual = req->actual;
	gateway_usb_last_req_buf_addr = (unsigned long)req->buf;
	gateway_usb_last_ctx_buf_addr = (unsigned long)ctx->out_buf;
	dcache_invalidate_by_addr((uint32_t *)ctx->out_buf, GATEWAY_AMEBA_USB_OUT_PACKET_SIZE);

	for (i = 0; i < sizeof(gateway_usb_last_req_dump); ++i) {
		if (req->buf != NULL && i < req->actual) {
			gateway_usb_last_req_dump[i] = ((const u8 *)req->buf)[i];
		} else {
			gateway_usb_last_req_dump[i] = 0;
		}
		gateway_usb_last_ctx_dump[i] = ctx->out_buf[i];
	}

	if (req->actual < 12) {
		gateway_usb_last_ping_valid = -1;
		(void)gateway_usb_queue_out_request(ctx);
		return;
	}

	raw = (const u8 *)req->buf;
	if (raw == NULL) {
		raw = ctx->out_buf;
	}
	ping = (const gateway_usb_ping_packet_t *)raw;
	config_pkt = (const gateway_usb_upstream_config_packet_t *)raw;
	gateway_usb_last_magic[0] = raw[0];
	gateway_usb_last_magic[1] = raw[1];
	gateway_usb_last_magic[2] = raw[2];
	gateway_usb_last_magic[3] = raw[3];
	gateway_usb_last_bytes[0] = raw[0];
	gateway_usb_last_bytes[1] = raw[1];
	gateway_usb_last_bytes[2] = raw[2];
	gateway_usb_last_bytes[3] = raw[3];
	gateway_usb_last_seq = ping->seq_no;
	gateway_usb_last_payload_len = ping->payload_len;
	if (memcmp(config_pkt->magic, "CFG1", sizeof(config_pkt->magic)) == 0) {
		if (gateway_upstream_apply_usb_config(config_pkt) == 0) {
			gateway_usb_last_ping_valid = 3;
		} else {
			gateway_usb_last_ping_valid = -2;
		}
		(void)gateway_usb_queue_out_request(ctx);
		return;
	}
	if (memcmp(ping->magic, "PING", sizeof(ping->magic)) == 0) {
		gateway_usb_last_ping_valid = 1;
		gateway_usb_reset_stream_session(ctx, "startup-ping");
	} else {
		/* For bring-up, prove bulk IN works even if the received payload is not decoded yet. */
		gateway_usb_last_ping_valid = 2;
	}
	(void)gateway_usb_send_pong(ctx, ping->seq_no);
	if (gateway_usb_enqueue_init_packet() == 0)
		(void)gateway_usb_kick_tx(ctx);

	(void)gateway_usb_queue_out_request(ctx);
}

static int gateway_usb_queue_out_request(gateway_usb_function_ctx_t *ctx)
{
	int rc;

	if (ctx == NULL || ctx->out_ep == NULL || ctx->out_req == NULL || !ctx->configured) {
		return -1;
	}

	memset(ctx->out_buf, 0, GATEWAY_AMEBA_USB_OUT_PACKET_SIZE);
	ctx->out_req->buf = ctx->out_buf;
	ctx->out_req->dma = (dma_addr_t)ctx->out_buf;
	ctx->out_req->length = GATEWAY_AMEBA_USB_OUT_PACKET_SIZE;
	ctx->out_req->actual = 0;
	ctx->out_req->status = 0;
	ctx->out_req->short_not_ok = 0;

	rc = usb_ep_queue(ctx->out_ep, ctx->out_req, 0);
	if (rc != 0) {
		printf("[gateway][usb] failed to queue bulk OUT rc=%d\r\n", rc);
		return -1;
	}

	return 0;
}

static int gateway_usb_function_bind(struct usb_configuration *config,
				     struct usb_function *function)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;
	int interface_id;

	interface_id = usb_interface_id(config, function);
	if (interface_id < 0) {
		return interface_id;
	}

	gateway_usb_intf_desc.bInterfaceNumber = (u8)interface_id;

	usb_ep_autoconfig_reset(config->cdev->gadget);
	ctx->in_ep = usb_ep_autoconfig(config->cdev->gadget, &gateway_usb_ep_in_fs_desc);
	ctx->out_ep = usb_ep_autoconfig(config->cdev->gadget, &gateway_usb_ep_out_fs_desc);
	if (ctx->in_ep == NULL || ctx->out_ep == NULL) {
		printf("[gateway][usb] usb_ep_autoconfig failed\r\n");
		return -1;
	}

	gateway_usb_ep_in_hs_desc.bEndpointAddress = gateway_usb_ep_in_fs_desc.bEndpointAddress;
	gateway_usb_ep_out_hs_desc.bEndpointAddress = gateway_usb_ep_out_fs_desc.bEndpointAddress;

	function->fs_descriptors = gateway_usb_fs_descs;
	function->hs_descriptors = gateway_usb_hs_descs;

	printf("[gateway][usb] function bind interface=%d ep_in=0x%02x ep_out=0x%02x\r\n",
	       interface_id,
	       gateway_usb_ep_in_fs_desc.bEndpointAddress,
	       gateway_usb_ep_out_fs_desc.bEndpointAddress);
	return 0;
}

static void gateway_usb_function_disable(struct usb_function *function)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;

	gateway_usb_configured = 0;
	ctx->configured = 0;
	ctx->in_busy = 0;
	gateway_usb_tx_reset();
	if (ctx->in_ep != NULL) {
		usb_ep_disable(ctx->in_ep);
	}
	if (ctx->out_ep != NULL) {
		usb_ep_disable(ctx->out_ep);
	}
	printf("[gateway][usb] disabled\r\n");
}

static int gateway_usb_function_setup(struct usb_function *function,
				      const struct usb_ctrlrequest *ctrl)
{
	(void)function;
	printf("[gateway][usb] setup reqType=0x%02x req=0x%02x value=0x%04x index=0x%04x len=%u\r\n",
	       ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
	return -1;
}

static int gateway_usb_function_set_alt(struct usb_function *function,
					unsigned interface, unsigned alt)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;
	const struct usb_endpoint_descriptor *in_desc;
	const struct usb_endpoint_descriptor *out_desc;
	enum usb_device_speed speed;
	int rc;

	(void)interface;

	speed = function->config->cdev->gadget->speed;
	in_desc = (speed == USB_SPEED_HIGH) ? &gateway_usb_ep_in_hs_desc : &gateway_usb_ep_in_fs_desc;
	out_desc = (speed == USB_SPEED_HIGH) ? &gateway_usb_ep_out_hs_desc : &gateway_usb_ep_out_fs_desc;

	if (ctx->configured) {
		gateway_usb_function_disable(function);
	}

	rc = usb_ep_enable(ctx->in_ep, in_desc);
	if (rc != 0) {
		printf("[gateway][usb] enable IN endpoint failed rc=%d\r\n", rc);
		return rc;
	}

	rc = usb_ep_enable(ctx->out_ep, out_desc);
	if (rc != 0) {
		printf("[gateway][usb] enable OUT endpoint failed rc=%d\r\n", rc);
		usb_ep_disable(ctx->in_ep);
		return rc;
	}

	if (ctx->in_req == NULL) {
		ctx->in_req = usb_ep_alloc_request(ctx->in_ep, 0);
	}
	if (ctx->out_req == NULL) {
		ctx->out_req = usb_ep_alloc_request(ctx->out_ep, 0);
	}
	if (ctx->in_req == NULL || ctx->out_req == NULL) {
		printf("[gateway][usb] request allocation failed\r\n");
		gateway_usb_function_disable(function);
		return -1;
	}

	ctx->in_req->complete = gateway_usb_in_complete;
	ctx->in_req->context = ctx;
	ctx->in_req->buf = ctx->in_buf;
	ctx->in_req->dma = (dma_addr_t)ctx->in_buf;
	ctx->out_req->complete = gateway_usb_out_complete;
	ctx->out_req->context = ctx;
	ctx->out_req->buf = ctx->out_buf;
	ctx->out_req->dma = (dma_addr_t)ctx->out_buf;
	ctx->configured = 1;
	ctx->in_busy = 0;
	gateway_usb_host_ready = 0;
	gateway_usb_configured = 1;
	gateway_usb_tx_reset();

	printf("[gateway][usb] configured interface=%u alt=%u speed=%d\r\n",
	       interface, alt, speed);
	rc = gateway_usb_queue_out_request(ctx);
	if (rc != 0)
		return rc;

	return 0;
}

static int gateway_usb_function_get_alt(struct usb_function *function, unsigned interface)
{
	(void)function;
	(void)interface;
	return 0;
}

static int gateway_usb_config_bind(struct usb_configuration *config)
{
	memset(&gateway_usb_ctx, 0, sizeof(gateway_usb_ctx));
	gateway_usb_ctx.in_buf = gateway_usb_in_buf;
	gateway_usb_ctx.out_buf = gateway_usb_out_buf;
	gateway_usb_ctx.function.name = "gateway-bulk";
	gateway_usb_ctx.function.bind = gateway_usb_function_bind;
	gateway_usb_ctx.function.set_alt = gateway_usb_function_set_alt;
	gateway_usb_ctx.function.get_alt = gateway_usb_function_get_alt;
	gateway_usb_ctx.function.disable = gateway_usb_function_disable;
	gateway_usb_ctx.function.setup = gateway_usb_function_setup;
	return usb_add_function(config, &gateway_usb_ctx.function);
}

static struct usb_configuration gateway_usb_config_driver = {
	.label = "gateway-bulk",
	.iConfiguration = GATEWAY_USB_STRING_CONFIG,
	.bConfigurationValue = 1,
	.bmAttributes = 0x80,
	.MaxPower = 100,
};

static int gateway_usb_bind(struct usb_composite_dev *cdev)
{
	int rc;

	rc = usb_add_config(cdev, &gateway_usb_config_driver, gateway_usb_config_bind);
	if (rc != 0) {
		printf("[gateway][usb] usb_add_config failed rc=%d\r\n", rc);
		return rc;
	}

	(void)usb_gadget_vbus_draw(cdev->gadget, 100);
	printf("[gateway][usb] composite bind done\r\n");
	return 0;
}

static void gateway_usb_disconnect(struct usb_composite_dev *cdev)
{
	(void)cdev;
	gateway_usb_configured = 0;
	gateway_usb_ctx.configured = 0;
	gateway_usb_ctx.in_busy = 0;
	gateway_usb_tx_reset();
	printf("[gateway][usb] disconnected\r\n");
}

static void gateway_usb_suspend(struct usb_composite_dev *cdev)
{
	(void)cdev;
	gateway_usb_configured = 0;
	gateway_usb_ctx.configured = 0;
	gateway_usb_ctx.in_busy = 0;
	gateway_usb_tx_reset();
	printf("[gateway][usb] suspended\r\n");
}

static void gateway_usb_resume(struct usb_composite_dev *cdev)
{
	(void)cdev;
	printf("[gateway][usb] resumed\r\n");
}

static struct usb_composite_driver gateway_usb_driver = {
	.name = "gateway-usb-ping",
	.dev = &gateway_usb_device_desc,
	.strings = gateway_usb_dev_strings,
	.max_speed = USB_SPEED_HIGH,
	.bind = gateway_usb_bind,
	.disconnect = gateway_usb_disconnect,
	.suspend = gateway_usb_suspend,
	.resume = gateway_usb_resume,
};

static int gateway_usb_start(void)
{
	int status;

	gateway_usb_start_attempted = 1;
	if (gateway_usb_started) {
		gateway_usb_start_rc = 0;
		return 0;
	}

	printf("[gateway][usb] starting vendor bulk device\r\n");
	_usb_init();
	status = wait_usb_ready();
	if (status != USB_INIT_OK) {
		if (status == USB_NOT_ATTACHED) {
			printf("[gateway][usb] no USB host attached\r\n");
		} else {
			printf("[gateway][usb] USB init failed status=%d\r\n", status);
		}
		gateway_usb_start_rc = -1;
		return -1;
	}

	status = usb_composite_probe(&gateway_usb_driver);
	if (status != 0) {
		printf("[gateway][usb] usb_composite_probe failed status=%d\r\n", status);
		gateway_usb_start_rc = status;
		return -1;
	}

	gateway_usb_started = 1;
	gateway_usb_start_rc = 0;
	printf("[gateway][usb] vendor bulk device initialized vid=0x0BDA pid=0x8195\r\n");
	return 0;
}

static int gateway_wifi_has_ip(void)
{
	unsigned char *ip = LwIP_GetIP(&xnetif[0]);

	if (ip == NULL) {
		return 0;
	}

	return (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
}

static int gateway_wifi_link_ready(void)
{
	rtw_wifi_setting_t setting;

	memset(&setting, 0, sizeof(setting));
	if (wifi_get_setting(WLAN0_NAME, &setting) != RTW_SUCCESS) {
		return 0;
	}

	return setting.ssid[0] != '\0';
}

static void gateway_wifi_log_ip(const char *tag)
{
	unsigned char *ip = LwIP_GetIP(&xnetif[0]);

	if (ip == NULL) {
		dbg_printf("[gateway][wifi] %s ip unavailable\r\n", tag);
		return;
	}

	dbg_printf("[gateway][wifi] %s ip=%u.%u.%u.%u\r\n",
		   tag, ip[0], ip[1], ip[2], ip[3]);
}

static void gateway_wifi_connect_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	(void)buf;
	(void)buf_len;
	(void)flags;
	(void)userdata;
	gateway_wifi_connected = 1;
	dbg_printf("[gateway][wifi] WIFI_EVENT_CONNECT\r\n");
}

static void gateway_wifi_disconnect_hdl(char *buf, int buf_len, int flags, void *userdata)
{
	(void)buf;
	(void)buf_len;
	(void)flags;
	(void)userdata;
	gateway_wifi_connected = 0;
	gateway_wifi_dhcp_ready = 0;
	++gateway_wifi_disconnect_count;
	dbg_printf("[gateway][wifi] WIFI_EVENT_DISCONNECT count=%lu\r\n",
		   gateway_wifi_disconnect_count);
}

static int gateway_wifi_bring_up_sta(void)
{
	TickType_t deadline;
	int dhcp_rc;
	int wifi_on_rc;

	gateway_wifi_connect_started = 1;
	gateway_wifi_task_finished = 0;
	++gateway_wifi_connect_attempts;

	dbg_printf("[gateway][wifi] enabling STA for \"%s\"\r\n",
		   GATEWAY_WIFI_STA_SSID);
	wifi_on_rc = wifi_on(RTW_MODE_STA);
	dbg_printf("[gateway][wifi] wifi_on rc=%d\r\n", wifi_on_rc);
	if (wifi_on_rc != RTW_SUCCESS) {
		gateway_wifi_last_connect_rc = wifi_on_rc;
		gateway_wifi_task_finished = 1;
		return -1;
	}

	wifi_reg_event_handler(WIFI_EVENT_CONNECT, gateway_wifi_connect_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_DISCONNECT, gateway_wifi_disconnect_hdl, NULL);

	dbg_printf("[gateway][wifi] connecting to \"%s\"\r\n",
		   GATEWAY_WIFI_STA_SSID);
	gateway_wifi_last_connect_rc = wifi_connect((char *)GATEWAY_WIFI_STA_SSID,
						      GATEWAY_WIFI_STA_SECURITY,
						      (char *)GATEWAY_WIFI_STA_PASSWORD,
						      (int)strlen(GATEWAY_WIFI_STA_SSID),
						      (int)strlen(GATEWAY_WIFI_STA_PASSWORD),
						      -1,
						      NULL);
	dbg_printf("[gateway][wifi] wifi_connect rc=%d attempts=%lu\r\n",
		   gateway_wifi_last_connect_rc,
		   gateway_wifi_connect_attempts);
	if (gateway_wifi_last_connect_rc != RTW_SUCCESS) {
		gateway_wifi_task_finished = 1;
		return -1;
	}

	dbg_printf("[gateway][wifi] connected, starting DHCP\r\n");
	dhcp_rc = LwIP_DHCP(0, DHCP_START);
	dbg_printf("[gateway][wifi] LwIP_DHCP rc=%d\r\n", dhcp_rc);
	if (dhcp_rc != 0) {
		gateway_wifi_task_finished = 1;
		return -1;
	}

	deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
	while (!gateway_wifi_has_ip()) {
		if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
			dbg_printf("[gateway][wifi] DHCP timeout connected=%d\r\n",
				   gateway_wifi_connected);
			gateway_wifi_task_finished = 1;
			return -1;
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}

	gateway_wifi_dhcp_ready = 1;
	gateway_wifi_task_finished = 1;
	gateway_wifi_log_ip("DHCP ready");
	return 0;
}

#if GATEWAY_AMEBA_DIAG_STAGE_MINIMAL
static void gateway_diag_task(void *param)
{
	unsigned long heartbeat_counter = 0;
	TickType_t last_heartbeat_tick;

	(void)param;

	dbg_printf("\r\n[gateway][diag] minimal task booted\r\n");
	dbg_printf("[gateway][diag] scheduler is alive tick=%lu heap=%lu\r\n",
		   (unsigned long)xTaskGetTickCount(),
		   (unsigned long)xPortGetFreeHeapSize());
	dbg_printf("[gateway][diag] minimal Wi-Fi validation image: no USB, no gateway task\r\n");

	last_heartbeat_tick = xTaskGetTickCount();
	for (;;) {
		if ((xTaskGetTickCount() - last_heartbeat_tick) >= pdMS_TO_TICKS(GATEWAY_AMEBA_HEARTBEAT_MS)) {
			last_heartbeat_tick = xTaskGetTickCount();
			dbg_printf("[gateway][diag] heartbeat counter=%lu tick=%lu heap=%lu wifi_task=%d wifi_conn=%d wifi_ip=%d wifi_rc=%d\r\n",
				   heartbeat_counter++,
				   (unsigned long)xTaskGetTickCount(),
				   (unsigned long)xPortGetFreeHeapSize(),
				   gateway_wifi_connect_started,
				   gateway_wifi_connected,
				   gateway_wifi_dhcp_ready,
				   gateway_wifi_last_connect_rc);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
#endif

static void gateway_ameba_task(void *param)
{
	unsigned long heartbeat_counter = 0;
	TickType_t last_heartbeat_tick;
	TickType_t next_upstream_retry_tick = 0;
	TickType_t usb_progress_tick = 0;
	int wifi_rc = 0;
	unsigned long last_sent_observed = 0;
	unsigned long last_in_done_observed = 0;
	gateway_upstream_config_t heartbeat_config;
	int heartbeat_cfg_valid = 0;
	const char *heartbeat_host = "<unset>";

	(void)param;
	gateway_task_running = 1;
	gateway_upstream_close(&gateway_upstream);
	gateway_upstream_last_rc = -999;
	gateway_upstream_connect_started = 0;
	gateway_upstream_connected = 0;
	gateway_upstream_connect_attempts = 0;
	gateway_upstream_text_frames = 0;
	gateway_upstream_binary_frames = 0;
	gateway_upstream_close_frames = 0;
	gateway_upstream_last_opcode = 0;
	gateway_upstream_last_payload_len = 0;
	gateway_upstream_last_attempt_tick = 0;
	gateway_upstream_config_valid = 0;
	gateway_upstream_config_seq = 0;
	gateway_upstream_reconnect_requested = 0;
	gateway_upstream_config.valid = 0;
	gateway_upstream_config.seq_no = 0u;

	dbg_printf("\r\n[gateway] AmebaPro gateway firmware booted\r\n");
	dbg_printf("[gateway] UART log is alive\r\n");
	dbg_printf("[gateway] FreeRTOS tick=%lu heap=%lu\r\n",
		   (unsigned long)xTaskGetTickCount(),
		   (unsigned long)xPortGetFreeHeapSize());
	printf("\r\n[gateway][printf] task booted tick=%lu heap=%lu\r\n",
	       (unsigned long)xTaskGetTickCount(),
	       (unsigned long)xPortGetFreeHeapSize());

#if GATEWAY_AMEBA_ASSUME_WIFI_SAMPLE
	dbg_printf("[gateway] assuming SDK sample already started Wi-Fi/DHCP\r\n");
	printf("[gateway][printf] assuming SDK sample already started Wi-Fi/DHCP\r\n");
	gateway_wifi_connect_started = 1;
	gateway_wifi_last_connect_rc = RTW_SUCCESS;
	if (gateway_wifi_link_ready()) {
		gateway_wifi_connected = 1;
		if (gateway_wifi_has_ip()) {
			gateway_wifi_dhcp_ready = 1;
			gateway_wifi_log_ip("sample baseline");
		}
	}
	gateway_wifi_task_finished = 1;
#else
	wifi_rc = gateway_wifi_bring_up_sta();
	if (wifi_rc == 0) {
		dbg_printf("[gateway] Wi-Fi ready, starting USB gateway transport\r\n");
	} else {
		dbg_printf("[gateway] Wi-Fi bring-up incomplete rc=%d, starting USB diagnostics\r\n",
			   wifi_rc);
	}
#endif

#if GATEWAY_AMEBA_ENABLE_USB
	wifi_rc = gateway_usb_start();
	printf("[gateway][printf] gateway_usb_start rc=%d usb_started=%d\r\n",
	       wifi_rc,
	       gateway_usb_started);
#else
	dbg_printf("[gateway] USB bring-up disabled for pristine Wi-Fi integration stage\r\n");
	printf("[gateway][printf] USB disabled for pristine Wi-Fi integration stage\r\n");
#endif
	last_heartbeat_tick = xTaskGetTickCount();
	next_upstream_retry_tick = last_heartbeat_tick;
	usb_progress_tick = last_heartbeat_tick;
	last_sent_observed = gateway_usb_stream_bytes_sent;
	last_in_done_observed = gateway_usb_in_complete_count;

	for (;;) {
#if GATEWAY_AMEBA_ASSUME_WIFI_SAMPLE
		if (!gateway_wifi_connected && gateway_wifi_link_ready()) {
			gateway_wifi_connected = 1;
		}
		if (gateway_wifi_connected && !gateway_wifi_dhcp_ready && gateway_wifi_has_ip()) {
			gateway_wifi_dhcp_ready = 1;
			gateway_wifi_log_ip("sample DHCP ready");
		}
#endif
#if GATEWAY_AMEBA_ENABLE_USB
		gateway_usb_try_force_configure();
		gateway_usb_retry_init_if_needed();
		if (gateway_usb_ctx.in_busy) {
			if (gateway_usb_stream_bytes_sent != last_sent_observed ||
			    gateway_usb_in_complete_count != last_in_done_observed) {
				last_sent_observed = gateway_usb_stream_bytes_sent;
				last_in_done_observed = gateway_usb_in_complete_count;
				usb_progress_tick = xTaskGetTickCount();
			} else if ((xTaskGetTickCount() - usb_progress_tick) >=
				   pdMS_TO_TICKS(GATEWAY_AMEBA_USB_STALL_TIMEOUT_MS)) {
				++gateway_usb_watchdog_reset_count;
				printf("[gateway][usb] watchdog reset count=%lu sent=%lu in_done=%lu ring=%lu\r\n",
				       gateway_usb_watchdog_reset_count,
				       gateway_usb_stream_bytes_sent,
				       gateway_usb_in_complete_count,
				       (unsigned long)gateway_usb_last_ring_count);
				gateway_usb_reset_stream_session(&gateway_usb_ctx, "in-stall-watchdog");
				usb_progress_tick = xTaskGetTickCount();
				last_sent_observed = gateway_usb_stream_bytes_sent;
				last_in_done_observed = gateway_usb_in_complete_count;
			}
		} else {
			usb_progress_tick = xTaskGetTickCount();
			last_sent_observed = gateway_usb_stream_bytes_sent;
			last_in_done_observed = gateway_usb_in_complete_count;
		}
#endif
		if (gateway_upstream_reconnect_requested) {
			gateway_upstream_close(&gateway_upstream);
			gateway_upstream_reconnect_requested = 0;
			next_upstream_retry_tick = xTaskGetTickCount();
		}
		if (gateway_wifi_connected && gateway_wifi_dhcp_ready) {
			TickType_t now = xTaskGetTickCount();

			if (gateway_upstream_config_valid &&
			    !gateway_upstream.connected &&
			    (int32_t)(now - next_upstream_retry_tick) >= 0) {
				gateway_upstream_last_rc = gateway_upstream_connect(&gateway_upstream);
				next_upstream_retry_tick = now + pdMS_TO_TICKS(GATEWAY_AMEBA_UPSTREAM_RETRY_MS);
			}
			gateway_upstream_poll(&gateway_upstream);
		}
		if ((xTaskGetTickCount() - last_heartbeat_tick) >= pdMS_TO_TICKS(GATEWAY_AMEBA_HEARTBEAT_MS)) {
			last_heartbeat_tick = xTaskGetTickCount();
			gateway_cpu_update_sample();
			memset(&heartbeat_config, 0, sizeof(heartbeat_config));
			heartbeat_cfg_valid = gateway_upstream_config_snapshot(&heartbeat_config);
			if (heartbeat_cfg_valid) {
				if (heartbeat_config.host[0] != '\0')
					heartbeat_host = heartbeat_config.host;
			}
			printf("[gateway] heartbeat counter=%lu cpu_busy=%lu cpu_idle=%lu cpu_ticks=%lu wifi_conn=%d wifi_ip=%d usb_cfg=%d host_ready=%d upstream_cfg=%d cfg_host=%s cfg_port=%u upstream_conn=%d proto_init=%d enq=%lu sent=%lu drop=%lu ring=%lu in_done=%lu wd=%lu\r\n",
			       heartbeat_counter,
			       gateway_cpu_busy_pct,
			       gateway_cpu_idle_pct,
			       gateway_cpu_sample_ticks,
			       gateway_wifi_connected,
			       gateway_wifi_dhcp_ready,
			       gateway_usb_configured,
			       gateway_usb_host_ready,
			       heartbeat_cfg_valid,
			       heartbeat_host,
			       (unsigned)heartbeat_config.port,
			       gateway_upstream_connected,
			       gateway_usb_proto_init_sent,
			       gateway_usb_stream_bytes_enqueued,
			       gateway_usb_stream_bytes_sent,
			       gateway_usb_stream_drop_count,
			       (unsigned long)gateway_usb_last_ring_count,
			       gateway_usb_in_complete_count,
			       gateway_usb_watchdog_reset_count);
			++heartbeat_counter;
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void example_gateway_ameba(void)
{
	gateway_ameba_register_atcmd();
#if GATEWAY_AMEBA_DIAG_STAGE_MINIMAL
	dbg_printf("[gateway][diag] staging minimal task only\r\n");
	gateway_task_create_attempted = 1;
	gateway_task_create_rc = xTaskCreate(gateway_diag_task,
					     "gw_diag",
					     GATEWAY_AMEBA_TASK_STACK_SIZE,
					     NULL,
					     GATEWAY_AMEBA_TASK_PRIORITY,
					     NULL);
	if (gateway_task_create_rc != pdPASS) {
		dbg_printf("[gateway][diag] failed to create minimal task\r\n");
	} else {
		gateway_task_created = 1;
	}
#else
	gateway_task_create_attempted = 1;
	printf("[gateway][printf] example_gateway_ameba() creating task heap=%lu\r\n",
	       (unsigned long)xPortGetFreeHeapSize());
	gateway_task_create_rc = xTaskCreate(gateway_ameba_task,
					     "gateway_ameba",
					     GATEWAY_AMEBA_TASK_STACK_SIZE,
					     NULL,
					     GATEWAY_AMEBA_TASK_PRIORITY,
					     NULL);
	if (gateway_task_create_rc != pdPASS) {
		dbg_printf("[gateway] failed to create gateway task\r\n");
		printf("[gateway][printf] xTaskCreate failed rc=%d heap=%lu\r\n",
		       gateway_task_create_rc,
		       (unsigned long)xPortGetFreeHeapSize());
	} else {
		gateway_task_created = 1;
		printf("[gateway][printf] xTaskCreate ok rc=%d heap=%lu\r\n",
		       gateway_task_create_rc,
		       (unsigned long)xPortGetFreeHeapSize());
	}
#endif
}

void gateway_ameba_log_status(void)
{
	gateway_upstream_config_t status_config;
	int status_cfg_valid = 0;
	const char *status_host = "<unset>";

	memset(&status_config, 0, sizeof(status_config));
	status_cfg_valid = gateway_upstream_config_snapshot(&status_config);
	if (status_cfg_valid) {
		if (status_config.host[0] != '\0')
			status_host = status_config.host;
	}

	printf("[gateway] status tick=%lu heap=%lu cpu_busy=%lu cpu_idle=%lu cpu_ticks=%lu wifi_conn=%d wifi_ip=%d usb_cfg=%d host_ready=%d upstream_cfg=%d cfg_host=%s cfg_port=%u upstream_conn=%d upstream_attempts=%lu proto_init=%d enq=%lu sent=%lu drop=%lu ring=%lu in_done=%lu wd=%lu\r\n",
	       (unsigned long)xTaskGetTickCount(),
	       (unsigned long)xPortGetFreeHeapSize(),
	       gateway_cpu_busy_pct,
	       gateway_cpu_idle_pct,
	       gateway_cpu_sample_ticks,
	       gateway_wifi_connected,
	       gateway_wifi_dhcp_ready,
	       gateway_usb_configured,
	       gateway_usb_host_ready,
	       status_cfg_valid,
	       status_host,
	       (unsigned)status_config.port,
	       gateway_upstream_connected,
	       gateway_upstream_connect_attempts,
	       gateway_usb_proto_init_sent,
	       gateway_usb_stream_bytes_enqueued,
	       gateway_usb_stream_bytes_sent,
	       gateway_usb_stream_drop_count,
	       (unsigned long)gateway_usb_last_ring_count,
	       gateway_usb_in_complete_count,
	       gateway_usb_watchdog_reset_count);
}

static void fATGS(void *arg)
{
	(void)arg;
	AT_PRINTK("[ATGS] gateway status");
	gateway_ameba_log_status();
	AT_PRINTK("[ATGS] OK");
}

static void fATGSTAT(void *arg)
{
	(void)arg;
	AT_PRINTK("[ATGSTAT] gateway status");
	gateway_ameba_log_status();
	AT_PRINTK("[ATGSTAT] OK");
}

static void fATGR(void *arg)
{
	(void)arg;
	AT_PRINTK("[ATGR] OK");
	sys_reset();
}

static void fATGX(void *arg)
{
	(void)arg;
	AT_PRINTK("[ATGX] OK");
	sys_reset();
}

static void fATGH(void *arg)
{
	(void)arg;
	AT_PRINTK("[ATGH] gateway commands: ATGS, ATGSTAT, ATGI, ATGR, ATGX");
	AT_PRINTK("[ATGH] OK");
}

static log_item_t gateway_at_items[] = {
	{"ATGS", fATGS,},
	{"ATGSTAT", fATGSTAT,},
	{"ATGI", fATGS,},
	{"ATGR", fATGR,},
	{"ATGX", fATGX,},
	{"ATGH", fATGH,},
	{"\nATGS", fATGS,},
	{"\nATGSTAT", fATGSTAT,},
	{"\nATGI", fATGS,},
	{"\nATGR", fATGR,},
	{"\nATGX", fATGX,},
	{"\nATGH", fATGH,},
};

void gateway_ameba_register_atcmd(void)
{
	if (gateway_atcmd_registered)
		return;

	log_service_add_table(gateway_at_items, sizeof(gateway_at_items) / sizeof(gateway_at_items[0]));
	gateway_atcmd_registered = 1;
	printf("[gateway][printf] UART AT commands registered: ATGS ATGSTAT ATGI ATGR ATGX ATGH\r\n");
}
