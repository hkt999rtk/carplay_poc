#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "wsfs_server.h"

#define TEST_PORT 19111
#define SERVER_TEXT_COUNT 120
#define SERVER_BINARY_COUNT 120
#define CLIENT_TEXT_COUNT 120
#define CLIENT_BINARY_COUNT 120
#define BINARY_PAYLOAD_SIZE 64

typedef enum {
	MODE_CONCURRENT = 0,
	MODE_SEQUENTIAL = 1,
} run_mode_t;

typedef struct {
	const char *report_path;
	size_t client_count;
	run_mode_t mode;
	unsigned stagger_ms;
} program_options_t;

typedef struct {
	size_t text_index;
	size_t binary_index;
} connection_ctx_t;

typedef struct {
	pthread_mutex_t lock;
	size_t connections_opened;
	size_t connections_closed;
	size_t initial_text_sent;
	size_t initial_binary_sent;
	size_t text_received;
	size_t binary_received;
	size_t text_echoed;
	size_t binary_echoed;
	uint16_t last_close_code;
	bool error;
	char error_message[256];
} server_metrics_t;

typedef struct {
	size_t client_id;
	size_t initial_text_received;
	size_t initial_binary_received;
	size_t text_echoes_received;
	size_t binary_echoes_received;
	size_t clientTextSent;
	size_t clientBinarySent;
	uint16_t close_code;
	bool error;
	char error_message[256];
	int exit_status;
} client_metrics_t;

typedef struct {
	size_t client_id;
	int port;
	unsigned stagger_ms;
	client_metrics_t *metrics;
} client_thread_arg_t;

typedef struct {
	uint8_t *data;
	size_t length;
	size_t offset;
} prebuffer_t;

static wsfs_server_t *g_server_handle = NULL;
static size_t g_expected_clients = 1;
static server_metrics_t g_server_metrics = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
};

static void server_metrics_add(size_t *field, size_t value)
{
	pthread_mutex_lock(&g_server_metrics.lock);
	*field += value;
	pthread_mutex_unlock(&g_server_metrics.lock);
}

static void server_metrics_record_close(uint16_t code)
{
	pthread_mutex_lock(&g_server_metrics.lock);
	g_server_metrics.last_close_code = code;
	g_server_metrics.connections_closed += 1;
	bool should_stop =
		(g_server_metrics.connections_closed >= g_expected_clients);
	pthread_mutex_unlock(&g_server_metrics.lock);
	if (should_stop && g_server_handle != NULL)
		wsfs_server_request_stop(g_server_handle);
}

static void server_metrics_record_open(void)
{
	server_metrics_add(&g_server_metrics.connections_opened, 1);
}

static void set_server_error(const char *message)
{
	pthread_mutex_lock(&g_server_metrics.lock);
	if (!g_server_metrics.error) {
		g_server_metrics.error = true;
		snprintf(g_server_metrics.error_message,
			 sizeof(g_server_metrics.error_message), "%s", message);
	}
	pthread_mutex_unlock(&g_server_metrics.lock);
}

static void make_server_text(char *buffer, size_t size, size_t index)
{
	snprintf(buffer, size, "server-text-%03zu", index);
}

static void make_client_text(char *buffer, size_t size, size_t index)
{
	snprintf(buffer, size, "client-text-%03zu", index);
}

static void fill_server_binary(uint8_t *buffer, size_t length, size_t index)
{
	for (size_t i = 0; i < length; ++i)
		buffer[i] = (uint8_t)(0xA0 ^ (uint8_t)index ^ (uint8_t)i);
}

static void fill_client_binary(uint8_t *buffer, size_t length, size_t index)
{
	for (size_t i = 0; i < length; ++i)
		buffer[i] = (uint8_t)(0x5A + (uint8_t)index + (uint8_t)i);
}

static void *server_thread_fn(void *arg)
{
	wsfs_server_t *server = (wsfs_server_t *)arg;
	wsfs_server_run(server);
	return NULL;
}

static void send_initial_payloads(wsfs_connection_t *conn, connection_ctx_t *ctx)
{
	size_t text_sent = 0;
	size_t binary_sent = 0;

	for (size_t i = 0; i < SERVER_TEXT_COUNT; ++i) {
		char text[64];
		make_server_text(text, sizeof(text), i);
		wsfs_status_t status =
			wsfs_connection_send_text(conn, text, strlen(text));
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to send server text frame");
			break;
		}
		++text_sent;
	}

	for (size_t i = 0; i < SERVER_BINARY_COUNT; ++i) {
		uint8_t payload[BINARY_PAYLOAD_SIZE];
		fill_server_binary(payload, sizeof(payload), i);
		wsfs_status_t status = wsfs_connection_send_binary(
			conn, payload, sizeof(payload));
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to send server binary frame");
			break;
		}
		++binary_sent;
	}

	if (text_sent > 0)
		server_metrics_add(&g_server_metrics.initial_text_sent,
				   text_sent);
	if (binary_sent > 0)
		server_metrics_add(&g_server_metrics.initial_binary_sent,
				   binary_sent);

	ctx->text_index = 0;
	ctx->binary_index = 0;
}

static void on_open(wsfs_connection_t *conn)
{
	server_metrics_record_open();

	connection_ctx_t *ctx =
		(connection_ctx_t *)calloc(1, sizeof(connection_ctx_t));
	if (ctx == NULL) {
		set_server_error("failed to allocate connection ctx");
		return;
	}
	wsfs_connection_set_user_data(conn, ctx);
	send_initial_payloads(conn, ctx);
}

static void on_message(wsfs_connection_t *conn,
		       const wsfs_message_view_t *msg)
{
	if (conn == NULL || msg == NULL)
		return;

	connection_ctx_t *ctx =
		(connection_ctx_t *)wsfs_connection_get_user_data(conn);
	if (ctx == NULL) {
		set_server_error("connection context missing");
		return;
	}

	if (msg->opcode == WSFS_OPCODE_TEXT) {
		if (msg->length >= 64) {
			set_server_error("client text frame too long");
			return;
		}
		char expected[64];
		make_client_text(expected, sizeof(expected),
				 ctx->text_index);
		if (msg->length != strlen(expected) ||
		    memcmp(msg->data, expected, msg->length) != 0) {
			set_server_error("client text frame content mismatch");
			return;
		}
		++ctx->text_index;
		server_metrics_add(&g_server_metrics.text_received, 1);

		wsfs_status_t status = wsfs_connection_send_text(
			conn, (const char *)msg->data, msg->length);
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to echo client text frame");
			return;
		}
		server_metrics_add(&g_server_metrics.text_echoed, 1);
	} else if (msg->opcode == WSFS_OPCODE_BINARY) {
		if (msg->length != BINARY_PAYLOAD_SIZE) {
			set_server_error("client binary frame size mismatch");
			return;
		}
		uint8_t expected[BINARY_PAYLOAD_SIZE];
		fill_client_binary(expected, sizeof(expected),
				   ctx->binary_index);
		if (memcmp(msg->data, expected, sizeof(expected)) != 0) {
			set_server_error("client binary frame content mismatch");
			return;
		}
		++ctx->binary_index;
		server_metrics_add(&g_server_metrics.binary_received, 1);

		wsfs_status_t status = wsfs_connection_send_binary(
			conn, msg->data, msg->length);
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to echo client binary frame");
			return;
		}
		server_metrics_add(&g_server_metrics.binary_echoed, 1);
	} else if (msg->opcode == WSFS_OPCODE_CLOSE) {
		wsfs_connection_close(conn, 1000);
	} else {
		set_server_error("unexpected opcode from client");
	}
}

static void on_close(wsfs_connection_t *conn, uint16_t close_code)
{
	connection_ctx_t *ctx =
		(connection_ctx_t *)wsfs_connection_get_user_data(conn);
	if (ctx != NULL) {
		free(ctx);
		wsfs_connection_set_user_data(conn, NULL);
	}

	server_metrics_record_close(close_code);
}

static void send_all(int fd, const void *buf, size_t len)
{
	const uint8_t *ptr = (const uint8_t *)buf;
	size_t total = 0;
	while (total < len) {
		ssize_t n = send(fd, ptr + total, len - total, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("send");
			exit(1);
		}
		total += (size_t)n;
	}
}

static void recv_exact(int fd, void *buf, size_t len)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t total = 0;
	while (total < len) {
		ssize_t n = recv(fd, ptr + total, len - total, 0);
		if (n <= 0) {
			perror("recv");
			exit(1);
		}
		total += (size_t)n;
	}
}

#ifndef STRINGIFY
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#endif

static int perform_handshake(int fd, int port, prebuffer_t *leftover)
{
	char request[256];
	int written = snprintf(request, sizeof(request),
				"GET /chat HTTP/1.1\r\n"
				"Host: 127.0.0.1:%d\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
				"Sec-WebSocket-Version: 13\r\n"
				"\r\n",
				port);
	if (written < 0 || (size_t)written >= sizeof(request))
		return -1;

	send_all(fd, request, (size_t)written);

	if (leftover != NULL) {
		leftover->data = NULL;
		leftover->length = 0;
		leftover->offset = 0;
	}

	char buffer[4096];
	size_t total = 0;
	while (total + 1 < sizeof(buffer)) {
		ssize_t n = recv(fd, buffer + total,
				sizeof(buffer) - 1 - total, 0);
		if (n <= 0)
			return -1;
		total += (size_t)n;
		buffer[total] = '\0';

		char *header_end = strstr(buffer, "\r\n\r\n");
		if (header_end != NULL) {
			size_t header_len = (size_t)(header_end - buffer) + 4;
			size_t extra = total - header_len;
			if (leftover != NULL) {
				leftover->data = NULL;
				leftover->length = extra;
				leftover->offset = 0;
				if (extra > 0) {
					leftover->data =
						(uint8_t *)malloc(extra);
					if (leftover->data == NULL)
						return -1;
					memcpy(leftover->data,
					       buffer + header_len,
					       extra);
				}
			}
			return strstr(buffer, "101") != NULL ? 0 : -1;
		}
	}
	return -1;
}

static void send_masked_frame(int fd, uint8_t opcode,
		      const uint8_t *payload, size_t length,
		      const uint8_t mask_key[4])
{
	uint8_t header[14];
	size_t header_len = 0;

	header[header_len++] = 0x80 | opcode;

	if (length <= 125) {
		header[header_len++] = (uint8_t)(0x80 | length);
	} else if (length <= UINT16_MAX) {
		header[header_len++] = 0x80 | 126;
		header[header_len++] = (uint8_t)((length >> 8) & 0xFF);
		header[header_len++] = (uint8_t)(length & 0xFF);
	} else {
		header[header_len++] = 0x80 | 127;
		for (int i = 7; i >= 0; --i)
			header[header_len++] = (uint8_t)((length >> (i * 8)) & 0xFF);
	}

	memcpy(header + header_len, mask_key, 4);
	header_len += 4;

	send_all(fd, header, header_len);

	uint8_t *masked = (uint8_t *)malloc(length);
	assert(masked != NULL);
	for (size_t i = 0; i < length; ++i)
		masked[i] = payload[i] ^ mask_key[i % 4];
	send_all(fd, masked, length);
	free(masked);
}

static void free_prebuffer(prebuffer_t *buffer)
{
	if (buffer == NULL)
		return;
	free(buffer->data);
	buffer->data = NULL;
	buffer->length = 0;
	buffer->offset = 0;
}

static void read_bytes(int fd, prebuffer_t *buffer,
		      uint8_t *dest, size_t len)
{
	size_t copied = 0;
	while (copied < (ssize_t)len) {
		if (buffer != NULL && buffer->offset < buffer->length) {
			size_t available = buffer->length - buffer->offset;
			size_t take = available;
			if (take > (len - (size_t)copied))
				take = len - (size_t)copied;
			memcpy(dest + copied, buffer->data + buffer->offset, take);
			buffer->offset += take;
			copied += (ssize_t)take;
			continue;
		}

		ssize_t n = recv(fd, dest + copied, len - (size_t)copied, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recv");
			exit(1);
		}
		if (n == 0) {
			fprintf(stderr, "connection closed unexpectedly\n");
			exit(1);
		}
		copied += n;
	}
}

static size_t recv_frame_with_buffer(int fd, uint8_t *opcode_out,
				    uint8_t **payload_out,
				    prebuffer_t *buffer)
{
	uint8_t header[2];
	read_bytes(fd, buffer, header, sizeof(header));

	uint8_t opcode = header[0] & 0x0F;
	size_t payload_len = (size_t)(header[1] & 0x7F);
	bool masked = (header[1] & 0x80) != 0;

	if (payload_len == 126) {
		uint8_t extended[2];
		read_bytes(fd, buffer, extended, sizeof(extended));
		payload_len =
			((size_t)extended[0] << 8) | (size_t)extended[1];
	} else if (payload_len == 127) {
		uint8_t extended[8];
		read_bytes(fd, buffer, extended, sizeof(extended));
		payload_len = 0;
		for (int i = 0; i < 8; ++i)
			payload_len =
				(payload_len << 8) | (size_t)extended[i];
	}

	uint8_t *payload = (uint8_t *)malloc(payload_len);
	assert(payload != NULL || payload_len == 0);

	uint8_t mask_key[4] = {0};
	if (masked)
		read_bytes(fd, buffer, mask_key, sizeof(mask_key));

	if (payload_len > 0)
		read_bytes(fd, buffer, payload, payload_len);

	if (masked) {
		for (size_t i = 0; i < payload_len; ++i)
			payload[i] ^= mask_key[i % 4];
	}

	*opcode_out = opcode;
	*payload_out = payload;
	return payload_len;
}

static int run_client(client_metrics_t *metrics, int port)
{
	metrics->exit_status = 0;
	int status = 0;
	int fd = -1;
	prebuffer_t leftover = {0};
	uint8_t *payload = NULL;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "failed to create socket: %s", strerror(errno));
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "connect failed: %s", strerror(errno));
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	if (perform_handshake(fd, port, &leftover) != 0) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "websocket handshake failed");
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	for (size_t i = 0; i < SERVER_TEXT_COUNT; ++i) {
		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != WSFS_OPCODE_TEXT) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "expected text frame from server");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		char expected[64];
		make_server_text(expected, sizeof(expected), i);
		if (len != strlen(expected) ||
		    memcmp(payload, expected, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "server text payload mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		free(payload);
		payload = NULL;
		++metrics->initial_text_received;
	}

	for (size_t i = 0; i < SERVER_BINARY_COUNT; ++i) {
		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != WSFS_OPCODE_BINARY) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "expected binary frame from server");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		uint8_t expected[BINARY_PAYLOAD_SIZE];
		fill_server_binary(expected, sizeof(expected), i);
		if (len != sizeof(expected) ||
		    memcmp(payload, expected, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "server binary payload mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		free(payload);
		payload = NULL;
		++metrics->initial_binary_received;
	}

	const uint8_t text_mask[4] = {0x11, 0x22, 0x33, 0x44};
	for (size_t i = 0; i < CLIENT_TEXT_COUNT; ++i) {
		char message[64];
		make_client_text(message, sizeof(message), metrics->clientTextSent);
		send_masked_frame(fd, WSFS_OPCODE_TEXT,
			  (const uint8_t *)message, strlen(message),
			  text_mask);
		metrics->clientTextSent += 1;

		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != WSFS_OPCODE_TEXT || len != strlen(message) ||
		    memcmp(payload, message, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "server text echo mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}
		free(payload);
		payload = NULL;
		++metrics->text_echoes_received;
	}

	const uint8_t binary_mask[4] = {0x21, 0x22, 0x23, 0x24};
	for (size_t i = 0; i < CLIENT_BINARY_COUNT; ++i) {
		uint8_t message[BINARY_PAYLOAD_SIZE];
		fill_client_binary(message, sizeof(message), metrics->clientBinarySent);
		send_masked_frame(fd, WSFS_OPCODE_BINARY,
			  message, sizeof(message), binary_mask);
		metrics->clientBinarySent += 1;

		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != WSFS_OPCODE_BINARY || len != sizeof(message) ||
		    memcmp(payload, message, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "server binary echo mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}
		free(payload);
		payload = NULL;
		++metrics->binary_echoes_received;
	}

	const uint8_t close_mask[4] = {0x9A, 0xBC, 0xDE, 0xF0};
	uint8_t close_payload[2];
	close_payload[0] = (uint8_t)((1000 >> 8) & 0xFF);
	close_payload[1] = (uint8_t)(1000 & 0xFF);
	send_masked_frame(fd, WSFS_OPCODE_CLOSE, close_payload,
		  sizeof(close_payload), close_mask);

	uint8_t opcode = 0;
	size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
	if (opcode != WSFS_OPCODE_CLOSE || len != 2) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "expected close frame from server");
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	uint16_t code = (uint16_t)((payload[0] << 8) | payload[1]);
	metrics->close_code = code;
	if (code != 1000) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "unexpected close code from server");
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	free(payload);
	payload = NULL;

cleanup:
	free(payload);
	free_prebuffer(&leftover);
	if (fd >= 0)
		close(fd);
	metrics->exit_status = status;
	return status;
}

static void *client_thread_fn(void *arg)
{
	client_thread_arg_t *ctx = (client_thread_arg_t *)arg;
	if (ctx->stagger_ms > 0)
		usleep(ctx->stagger_ms * 1000U * ctx->client_id);
	ctx->metrics->exit_status =
		run_client(ctx->metrics, ctx->port);
	return NULL;
}

static int parse_options(int argc, char **argv, program_options_t *opts)
{
	opts->report_path = NULL;
	opts->client_count = 1;
	opts->mode = MODE_CONCURRENT;
	opts->stagger_ms = 0;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--report") == 0) {
			if (i + 1 >= argc)
				return -1;
			opts->report_path = argv[++i];
		} else if (strcmp(argv[i], "--clients") == 0) {
			if (i + 1 >= argc)
				return -1;
			opts->client_count = (size_t)strtoul(argv[++i], NULL, 10);
			if (opts->client_count == 0)
				return -1;
		} else if (strcmp(argv[i], "--mode") == 0) {
			if (i + 1 >= argc)
				return -1;
			const char *mode = argv[++i];
			if (strcmp(mode, "concurrent") == 0)
				opts->mode = MODE_CONCURRENT;
			else if (strcmp(mode, "sequential") == 0)
				opts->mode = MODE_SEQUENTIAL;
			else
				return -1;
		} else if (strcmp(argv[i], "--stagger-ms") == 0) {
			if (i + 1 >= argc)
				return -1;
			opts->stagger_ms = (unsigned)strtoul(argv[++i], NULL, 10);
		} else {
			return -1;
		}
	}
	return 0;
}

static void write_report(const program_options_t *opts,
			 long duration_ms,
			 client_metrics_t *clients)
{
	if (opts->report_path == NULL)
		return;

	FILE *fp = fopen(opts->report_path, "w");
	if (fp == NULL) {
		fprintf(stderr, "failed to open report path %s: %s\n",
			opts->report_path, strerror(errno));
		return;
	}

	size_t aggregate_initial_text = 0;
	size_t aggregate_initial_binary = 0;
	size_t aggregate_text_echo = 0;
	size_t aggregate_binary_echo = 0;
	size_t client_failures = 0;

	for (size_t i = 0; i < opts->client_count; ++i) {
		aggregate_initial_text +=
			clients[i].initial_text_received;
		aggregate_initial_binary +=
			clients[i].initial_binary_received;
		aggregate_text_echo += clients[i].text_echoes_received;
		aggregate_binary_echo +=
			clients[i].binary_echoes_received;
		if (clients[i].error)
			++client_failures;
	}

	pthread_mutex_lock(&g_server_metrics.lock);
	bool server_error = g_server_metrics.error;
	char server_error_msg[256];
	if (server_error)
		snprintf(server_error_msg, sizeof(server_error_msg),
			 "%s", g_server_metrics.error_message);
	pthread_mutex_unlock(&g_server_metrics.lock);

	fprintf(fp,
		"{\n"
		"  \"scenario\": \"c_server_c_client\",\n"
		"  \"mode\": \"%s\",\n"
		"  \"client_count\": %zu,\n"
		"  \"stagger_ms\": %u,\n"
		"  \"status\": \"%s\",\n"
		"  \"duration_ms\": %ld,\n"
		"  \"server\": {\n"
		"    \"connections_opened\": %zu,\n"
		"    \"connections_closed\": %zu,\n"
		"    \"initial_text_sent\": %zu,\n"
		"    \"initial_binary_sent\": %zu,\n"
		"    \"text_received\": %zu,\n"
		"    \"binary_received\": %zu,\n"
		"    \"text_echoed\": %zu,\n"
		"    \"binary_echoed\": %zu,\n"
		"    \"last_close_code\": %u,\n"
		"    \"error\": %s%s%s\n"
		"  },\n"
		"  \"clients\": [\n",
		opts->mode == MODE_CONCURRENT ? "concurrent" : "sequential",
		opts->client_count,
		opts->stagger_ms,
		(server_error || client_failures > 0) ? "failed" : "passed",
		duration_ms,
		g_server_metrics.connections_opened,
		g_server_metrics.connections_closed,
		g_server_metrics.initial_text_sent,
		g_server_metrics.initial_binary_sent,
		g_server_metrics.text_received,
		g_server_metrics.binary_received,
		g_server_metrics.text_echoed,
		g_server_metrics.binary_echoed,
		g_server_metrics.last_close_code,
		server_error ? "true" : "false",
		server_error ? ", \"error_message\": \"" : "",
		server_error ? server_error_msg : "");

	if (server_error)
		fprintf(fp, "\"");

	for (size_t i = 0; i < opts->client_count; ++i) {
		const client_metrics_t *cm = &clients[i];
		bool passed = (cm->exit_status == 0 && !cm->error);
		fprintf(fp,
			"    {\n"
			"      \"client_id\": %zu,\n"
			"      \"initial_text_received\": %zu,\n"
			"      \"initial_binary_received\": %zu,\n"
			"      \"text_echoes_received\": %zu,\n"
			"      \"binary_echoes_received\": %zu,\n"
			"      \"close_code\": %u,\n"
			"      \"status\": \"%s\"%s%s%s\n"
			"    }%s\n",
			cm->client_id,
			cm->initial_text_received,
			cm->initial_binary_received,
			cm->text_echoes_received,
			cm->binary_echoes_received,
			(unsigned)cm->close_code,
			passed ? "passed" : "failed",
			passed ? "" : ", \"error_message\": \"",
			passed ? "" : cm->error_message,
			passed ? "" : "\"",
			(i + 1 == opts->client_count) ? "" : ",");
	}

	fprintf(fp,
		"  ],\n"
		"  \"client_aggregate\": {\n"
		"    \"initial_text_received\": %zu,\n"
		"    \"initial_binary_received\": %zu,\n"
		"    \"text_echoes_received\": %zu,\n"
		"    \"binary_echoes_received\": %zu\n"
		"  }\n"
		"}\n",
		aggregate_initial_text,
		aggregate_initial_binary,
		aggregate_text_echo,
		aggregate_binary_echo);

	fclose(fp);
}

static void reset_server_metrics(void)
{
	pthread_mutex_lock(&g_server_metrics.lock);
	g_server_metrics.connections_opened = 0;
	g_server_metrics.connections_closed = 0;
	g_server_metrics.initial_text_sent = 0;
	g_server_metrics.initial_binary_sent = 0;
	g_server_metrics.text_received = 0;
	g_server_metrics.binary_received = 0;
	g_server_metrics.text_echoed = 0;
	g_server_metrics.binary_echoed = 0;
	g_server_metrics.last_close_code = 0;
	g_server_metrics.error = false;
	g_server_metrics.error_message[0] = '\0';
	pthread_mutex_unlock(&g_server_metrics.lock);
}

int main(int argc, char **argv)
{
	program_options_t options;
	if (parse_options(argc, argv, &options) != 0) {
		fprintf(stderr,
			"usage: %s [--report path] [--clients N] "
			"[--mode concurrent|sequential] [--stagger-ms N]\n",
			argv[0]);
		return 2;
	}

	reset_server_metrics();

	struct timespec start_ts, end_ts;
	if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
		perror("clock_gettime");
		return 1;
	}

	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.port = TEST_PORT;
	cfg.callbacks.on_open = on_open;
	cfg.callbacks.on_message = on_message;
	cfg.callbacks.on_close = on_close;

	wsfs_server_t server = {0};
	if (wsfs_server_init(&server, &cfg) != WSFS_STATUS_OK) {
		fprintf(stderr, "failed to init server\n");
		return 1;
	}

	g_expected_clients = options.client_count;
	g_server_handle = &server;

	pthread_t server_thread;
	if (pthread_create(&server_thread, NULL, server_thread_fn, &server) != 0) {
		fprintf(stderr, "failed to start server thread\n");
		wsfs_server_deinit(&server);
		return 1;
	}

	usleep(200 * 1000);

	client_metrics_t *clients = (client_metrics_t *)calloc(
		options.client_count, sizeof(client_metrics_t));
	if (clients == NULL) {
		fprintf(stderr, "failed to allocate client metrics\n");
		wsfs_server_request_stop(&server);
		pthread_join(server_thread, NULL);
		wsfs_server_deinit(&server);
		return 1;
	}

	for (size_t i = 0; i < options.client_count; ++i)
		clients[i].client_id = i;

	int overall_status = 0;

	if (options.mode == MODE_CONCURRENT) {
		pthread_t *threads = (pthread_t *)calloc(
			options.client_count, sizeof(pthread_t));
		client_thread_arg_t *args =
			(client_thread_arg_t *)calloc(options.client_count,
						      sizeof(client_thread_arg_t));
		if (threads == NULL || args == NULL) {
			fprintf(stderr, "failed to allocate client threads\n");
			free(threads);
			free(args);
			wsfs_server_request_stop(&server);
			pthread_join(server_thread, NULL);
			wsfs_server_deinit(&server);
			free(clients);
			return 1;
		}

		for (size_t i = 0; i < options.client_count; ++i) {
			args[i].client_id = i;
			args[i].port = TEST_PORT;
			args[i].stagger_ms = options.stagger_ms;
			args[i].metrics = &clients[i];
			if (pthread_create(&threads[i], NULL,
					   client_thread_fn, &args[i]) != 0) {
				fprintf(stderr,
					"failed to create client thread %zu\n",
					i);
				overall_status = 1;
				break;
			}
			if (options.stagger_ms > 0)
				usleep(options.stagger_ms * 1000U);
		}

		for (size_t i = 0; i < options.client_count; ++i) {
			if (threads[i] != 0)
				pthread_join(threads[i], NULL);
		}

		free(threads);
		free(args);
	} else {
		for (size_t i = 0; i < options.client_count; ++i) {
			if (i > 0 && options.stagger_ms > 0)
				usleep(options.stagger_ms * 1000U);
			clients[i].exit_status =
				run_client(&clients[i], TEST_PORT);
		}
	}

	wsfs_server_request_stop(&server);
	pthread_join(server_thread, NULL);
	wsfs_server_deinit(&server);

	if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
		perror("clock_gettime");
		free(clients);
		return 1;
	}

	long duration_ms =
		(long)((end_ts.tv_sec - start_ts.tv_sec) * 1000L +
		       (end_ts.tv_nsec - start_ts.tv_nsec) / 1000000L);

	size_t expected_total =
		options.client_count * CLIENT_TEXT_COUNT;
	size_t expected_binary_total =
		options.client_count * CLIENT_BINARY_COUNT;

	pthread_mutex_lock(&g_server_metrics.lock);
	bool server_error = g_server_metrics.error;
	size_t server_text_received = g_server_metrics.text_received;
	size_t server_binary_received = g_server_metrics.binary_received;
	size_t server_initial_text = g_server_metrics.initial_text_sent;
	size_t server_initial_binary = g_server_metrics.initial_binary_sent;
	size_t server_text_echoed = g_server_metrics.text_echoed;
	size_t server_binary_echoed = g_server_metrics.binary_echoed;
	size_t server_closed = g_server_metrics.connections_closed;
	pthread_mutex_unlock(&g_server_metrics.lock);

	size_t client_failures = 0;
	for (size_t i = 0; i < options.client_count; ++i) {
		if (clients[i].exit_status != 0 || clients[i].error)
			++client_failures;
	}

	bool counts_ok =
		(server_initial_text == options.client_count * SERVER_TEXT_COUNT) &&
		(server_initial_binary ==
		 options.client_count * SERVER_BINARY_COUNT) &&
		(server_text_received == expected_total) &&
		(server_binary_received == expected_binary_total) &&
		(server_text_echoed == expected_total) &&
		(server_binary_echoed == expected_binary_total) &&
		(server_closed == options.client_count);

	if (server_error || client_failures > 0 || !counts_ok)
		overall_status = 1;

	write_report(&options, duration_ms, clients);

	if (overall_status != 0) {
		fprintf(stderr,
			"wsfs_c2c_stress: failure (server_error=%d, "
			"client_failures=%zu, counts_ok=%d)\n",
			server_error ? 1 : 0,
			client_failures,
			counts_ok ? 1 : 0);
		free(clients);
		return 1;
	}

	printf("wsfs_c2c_stress: success with %zu clients (%s) in %ld ms\n",
	       options.client_count,
	       options.mode == MODE_CONCURRENT ? "concurrent" : "sequential",
	       duration_ms);

	free(clients);
	return 0;
}
