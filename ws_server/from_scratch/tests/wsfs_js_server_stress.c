#include <arpa/inet.h>
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

#define DEFAULT_TEST_PORT 19112
#define TEXT_COUNT 120
#define BINARY_COUNT 120
#define BINARY_PAYLOAD_SIZE 64

typedef enum {
	MODE_CONCURRENT = 0,
	MODE_SEQUENTIAL = 1,
} run_mode_t;

typedef struct {
	size_t client_id;
	size_t initial_text_received;
	size_t initial_binary_received;
	size_t text_echoes_received;
	size_t binary_echoes_received;
	uint16_t close_code;
	bool error;
	char error_message[256];
	int exit_status;
} client_metrics_t;

typedef struct {
	int port;
	const char *report_path;
	size_t client_count;
	run_mode_t mode;
	unsigned stagger_ms;
} program_options_t;

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

static void pad_index(size_t index, char *buffer, size_t size)
{
	snprintf(buffer, size, "%03zu", index);
}

static void make_server_text(size_t index, char *buffer, size_t size)
{
	char suffix[8];
	pad_index(index, suffix, sizeof(suffix));
	snprintf(buffer, size, "server-text-%s", suffix);
}

static void make_client_text(size_t index, char *buffer, size_t size)
{
	char suffix[8];
	pad_index(index, suffix, sizeof(suffix));
	snprintf(buffer, size, "client-text-%s", suffix);
}

static void fill_server_binary(size_t index, uint8_t *buffer, size_t size)
{
	for (size_t i = 0; i < size; ++i)
		buffer[i] = (uint8_t)(0xA0 ^ (uint8_t)index ^ (uint8_t)i);
}

static void fill_client_binary(size_t index, uint8_t *buffer, size_t size)
{
	for (size_t i = 0; i < size; ++i)
		buffer[i] = (uint8_t)(0x5A + (uint8_t)index + (uint8_t)i);
}

static int perform_handshake(int fd, int port, prebuffer_t *leftover)
{
	char request[256];
	int written = snprintf(request, sizeof(request),
				"GET /js-server HTTP/1.1\r\n"
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
					leftover->data = (uint8_t *)malloc(extra);
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
			header[header_len++] =
				(uint8_t)((length >> (i * 8)) & 0xFF);
	}

	memcpy(header + header_len, mask_key, 4);
	header_len += 4;

	send_all(fd, header, header_len);

	uint8_t *masked = (uint8_t *)malloc(length);
	if (masked == NULL)
		exit(1);
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
		uint8_t ext[2];
		read_bytes(fd, buffer, ext, sizeof(ext));
		payload_len = ((size_t)ext[0] << 8) | (size_t)ext[1];
	} else if (payload_len == 127) {
		uint8_t ext[8];
		read_bytes(fd, buffer, ext, sizeof(ext));
		payload_len = 0;
		for (int i = 0; i < 8; ++i)
			payload_len =
				(payload_len << 8) | (size_t)ext[i];
	}

	uint8_t mask_key[4] = {0};
	if (masked)
		read_bytes(fd, buffer, mask_key, sizeof(mask_key));

	uint8_t *payload = NULL;
	if (payload_len > 0) {
		payload = (uint8_t *)malloc(payload_len);
		if (payload == NULL)
			exit(1);
		read_bytes(fd, buffer, payload, payload_len);
		if (masked) {
			for (size_t i = 0; i < payload_len; ++i)
				payload[i] ^= mask_key[i % 4];
		}
	}

	*opcode_out = opcode;
	*payload_out = payload;
	return payload_len;
}

static int run_single_client(client_metrics_t *metrics, int port)
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
			 "socket: %s", strerror(errno));
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "connect: %s", strerror(errno));
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	if (perform_handshake(fd, port, &leftover) != 0) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "handshake failed");
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	for (size_t i = 0; i < TEXT_COUNT; ++i) {
		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != 0x1) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "expected text frame");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		char expected[64];
		make_server_text(i, expected, sizeof(expected));
		if (len != strlen(expected) ||
		    memcmp(payload, expected, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "server text mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		free(payload);
		payload = NULL;
		++metrics->initial_text_received;
	}

	for (size_t i = 0; i < BINARY_COUNT; ++i) {
		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != 0x2) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "expected binary frame");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		uint8_t expected[BINARY_PAYLOAD_SIZE];
		fill_server_binary(i, expected, sizeof(expected));
		if (len != sizeof(expected) ||
		    memcmp(payload, expected, len) != 0) {
			snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "server binary mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}

		free(payload);
		payload = NULL;
		++metrics->initial_binary_received;
	}

	const uint8_t text_mask[4] = {0x11, 0x22, 0x33, 0x44};
	for (size_t i = 0; i < TEXT_COUNT; ++i) {
		char message[64];
		make_client_text(i, message, sizeof(message));
		send_masked_frame(fd, 0x1, (const uint8_t *)message,
			  strlen(message), text_mask);

		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != 0x1 || len != strlen(message) ||
		    memcmp(payload, message, len) != 0) {
			snprintf(metrics->error_message,
				 sizeof(metrics->error_message),
				 "text echo mismatch");
			metrics->error = true;
			status = -1;
			goto cleanup;
		}
		free(payload);
		payload = NULL;
		++metrics->text_echoes_received;
	}

	const uint8_t binary_mask[4] = {0x21, 0x22, 0x23, 0x24};
	for (size_t i = 0; i < BINARY_COUNT; ++i) {
		uint8_t message[BINARY_PAYLOAD_SIZE];
		fill_client_binary(i, message, sizeof(message));
		send_masked_frame(fd, 0x2, message,
			  sizeof(message), binary_mask);

		uint8_t opcode = 0;
		size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
		if (opcode != 0x2 || len != sizeof(message) ||
		    memcmp(payload, message, len) != 0) {
			snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "binary echo mismatch");
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
	send_masked_frame(fd, 0x8, close_payload,
		  sizeof(close_payload), close_mask);

	uint8_t opcode = 0;
	size_t len = recv_frame_with_buffer(fd, &opcode, &payload, &leftover);
	if (opcode != 0x8 || len != 2) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "expected close echo");
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	uint16_t code = (uint16_t)((payload[0] << 8) | payload[1]);
	if (code != 1000) {
		snprintf(metrics->error_message,
			 sizeof(metrics->error_message),
			 "unexpected close code %u", code);
		metrics->error = true;
		status = -1;
		goto cleanup;
	}

	free(payload);
	payload = NULL;
	metrics->close_code = code;

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
		run_single_client(ctx->metrics, ctx->port);
	return NULL;
}

static int parse_options(int argc, char **argv, program_options_t *opts)
{
	opts->port = DEFAULT_TEST_PORT;
	opts->report_path = NULL;
	opts->client_count = 1;
	opts->mode = MODE_CONCURRENT;
	opts->stagger_ms = 0;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--port") == 0) {
			if (i + 1 >= argc)
				return -1;
			opts->port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--report") == 0) {
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
		fprintf(stderr, "failed to open report %s: %s\n",
			opts->report_path, strerror(errno));
		return;
	}

	size_t aggregate_initial_text = 0;
	size_t aggregate_initial_binary = 0;
	size_t aggregate_text_echo = 0;
	size_t aggregate_binary_echo = 0;
	size_t failures = 0;

	for (size_t i = 0; i < opts->client_count; ++i) {
		aggregate_initial_text += clients[i].initial_text_received;
		aggregate_initial_binary +=
			clients[i].initial_binary_received;
		aggregate_text_echo += clients[i].text_echoes_received;
		aggregate_binary_echo += clients[i].binary_echoes_received;
		if (clients[i].exit_status != 0 || clients[i].error)
			++failures;
	}

	fprintf(fp,
		"{\n"
		"  \"scenario\": \"js_server_c_client\",\n"
		"  \"mode\": \"%s\",\n"
		"  \"client_count\": %zu,\n"
		"  \"stagger_ms\": %u,\n"
		"  \"status\": \"%s\",\n"
		"  \"duration_ms\": %ld,\n"
		"  \"clients\": [\n",
		opts->mode == MODE_CONCURRENT ? "concurrent" : "sequential",
		opts->client_count,
		opts->stagger_ms,
		failures > 0 ? "failed" : "passed",
		duration_ms);

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

int main(int argc, char **argv)
{
	program_options_t opts;
	if (parse_options(argc, argv, &opts) != 0) {
		fprintf(stderr,
			"usage: %s [--port N] [--clients N] "
			"[--mode concurrent|sequential] [--stagger-ms N] "
			"[--report path]\n",
			argv[0]);
		return 2;
	}

	client_metrics_t *clients = (client_metrics_t *)calloc(
		opts.client_count, sizeof(client_metrics_t));
	if (clients == NULL) {
		fprintf(stderr, "failed to allocate client metrics\n");
		return 1;
	}
	for (size_t i = 0; i < opts.client_count; ++i)
		clients[i].client_id = i;

	struct timespec start_ts, end_ts;
	if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
		perror("clock_gettime");
		free(clients);
		return 1;
	}

	int overall_status = 0;

	if (opts.mode == MODE_CONCURRENT) {
		pthread_t *threads = (pthread_t *)calloc(
			opts.client_count, sizeof(pthread_t));
		client_thread_arg_t *args =
			(client_thread_arg_t *)calloc(opts.client_count,
						      sizeof(client_thread_arg_t));
		if (threads == NULL || args == NULL) {
			fprintf(stderr, "failed to allocate client threads\n");
			free(threads);
			free(args);
			free(clients);
			return 1;
		}

		for (size_t i = 0; i < opts.client_count; ++i) {
			args[i].client_id = i;
			args[i].port = opts.port;
			args[i].stagger_ms = opts.stagger_ms;
			args[i].metrics = &clients[i];
			if (pthread_create(&threads[i], NULL,
					   client_thread_fn, &args[i]) != 0) {
				fprintf(stderr,
					"failed to launch client thread %zu\n",
					i);
				overall_status = 1;
				break;
			}
			if (opts.stagger_ms > 0)
				usleep(opts.stagger_ms * 1000U);
		}

		for (size_t i = 0; i < opts.client_count; ++i) {
			if (threads[i] != 0)
				pthread_join(threads[i], NULL);
		}

		free(threads);
		free(args);
	} else {
		for (size_t i = 0; i < opts.client_count; ++i) {
			if (i > 0 && opts.stagger_ms > 0)
				usleep(opts.stagger_ms * 1000U);
			clients[i].exit_status =
				run_single_client(&clients[i], opts.port);
		}
	}

	if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
		perror("clock_gettime");
		free(clients);
		return 1;
	}

	long duration_ms =
		(long)((end_ts.tv_sec - start_ts.tv_sec) * 1000L +
		       (end_ts.tv_nsec - start_ts.tv_nsec) / 1000000L);

	size_t failures = 0;
	for (size_t i = 0; i < opts.client_count; ++i) {
		if (clients[i].exit_status != 0 || clients[i].error)
			++failures;
	}

	if (failures > 0)
		overall_status = 1;

	write_report(&opts, duration_ms, clients);

	if (overall_status != 0) {
		fprintf(stderr,
			"wsfs_js_server_stress: failure (%zu client failures)\n",
			failures);
		free(clients);
		return 1;
	}

	printf("wsfs_js_server_stress: success with %zu clients (%s) in %ld ms\n",
	       opts.client_count,
	       opts.mode == MODE_CONCURRENT ? "concurrent" : "sequential",
	       duration_ms);

	free(clients);
	return 0;
}
