#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "wsfs_server.h"

#define DEFAULT_TEST_PORT 19113
#define TEXT_COUNT 120
#define BINARY_COUNT 120
#define BINARY_PAYLOAD_SIZE 64

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

typedef enum {
	MODE_CONCURRENT = 0,
	MODE_SEQUENTIAL = 1,
} run_mode_t;

typedef struct {
	int port;
	const char *report_path;
	size_t client_count;
	run_mode_t mode;
	unsigned stagger_ms;
} program_options_t;

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
		buffer[i] = (uint8_t)(0xA0 ^ (uint8_t)index ^
				      (uint8_t)i);
}

static void fill_client_binary(size_t index, uint8_t *buffer, size_t size)
{
	for (size_t i = 0; i < size; ++i)
		buffer[i] = (uint8_t)(0x5A + (uint8_t)index +
				      (uint8_t)i);
}

static void send_initial_payloads(wsfs_connection_t *conn)
{
	size_t text_sent = 0;
	size_t binary_sent = 0;

	for (size_t i = 0; i < TEXT_COUNT; ++i) {
		char text[64];
		make_server_text(i, text, sizeof(text));
		wsfs_status_t status =
			wsfs_connection_send_text(conn, text, strlen(text));
		if (status != WSFS_STATUS_OK) {
			set_server_error(
				"failed to send initial server text frame");
			break;
		}
		++text_sent;
	}

	for (size_t i = 0; i < BINARY_COUNT; ++i) {
		uint8_t payload[BINARY_PAYLOAD_SIZE];
		fill_server_binary(i, payload, sizeof(payload));
		wsfs_status_t status = wsfs_connection_send_binary(
			conn, payload, sizeof(payload));
		if (status != WSFS_STATUS_OK) {
			set_server_error(
				"failed to send initial server binary frame");
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
}

static void on_open(wsfs_connection_t *conn)
{
	pthread_mutex_lock(&g_server_metrics.lock);
	g_server_metrics.connections_opened += 1;
	pthread_mutex_unlock(&g_server_metrics.lock);

	connection_ctx_t *ctx =
		(connection_ctx_t *)calloc(1, sizeof(connection_ctx_t));
	if (ctx == NULL) {
		set_server_error("failed to allocate connection ctx");
		return;
	}
	wsfs_connection_set_user_data(conn, ctx);
	send_initial_payloads(conn);
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
		make_client_text(ctx->text_index, expected, sizeof(expected));
		if (msg->length != strlen(expected) ||
		    memcmp(msg->data, expected, msg->length) != 0) {
			set_server_error("client text content mismatch");
			return;
		}

		wsfs_status_t status = wsfs_connection_send_text(
			conn, (const char *)msg->data, msg->length);
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to echo client text frame");
			return;
		}

		++ctx->text_index;
		server_metrics_add(&g_server_metrics.text_received, 1);
		server_metrics_add(&g_server_metrics.text_echoed, 1);
	} else if (msg->opcode == WSFS_OPCODE_BINARY) {
		if (msg->length != BINARY_PAYLOAD_SIZE) {
			set_server_error("client binary frame size mismatch");
			return;
		}

		uint8_t expected[BINARY_PAYLOAD_SIZE];
		fill_client_binary(ctx->binary_index, expected,
				   sizeof(expected));
		if (memcmp(msg->data, expected, sizeof(expected)) != 0) {
			set_server_error("client binary content mismatch");
			return;
		}

		wsfs_status_t status = wsfs_connection_send_binary(
			conn, msg->data, msg->length);
		if (status != WSFS_STATUS_OK) {
			set_server_error("failed to echo client binary frame");
			return;
		}

		++ctx->binary_index;
		server_metrics_add(&g_server_metrics.binary_received, 1);
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

	pthread_mutex_lock(&g_server_metrics.lock);
	g_server_metrics.connections_closed += 1;
	g_server_metrics.last_close_code = close_code;
	bool should_stop =
		(g_server_metrics.connections_closed >= g_expected_clients);
	pthread_mutex_unlock(&g_server_metrics.lock);

	if (should_stop && g_server_handle != NULL)
		wsfs_server_request_stop(g_server_handle);
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
			 long duration_ms)
{
	if (opts->report_path == NULL)
		return;

	FILE *fp = fopen(opts->report_path, "w");
	if (fp == NULL) {
		fprintf(stderr, "failed to open report %s: %s\n",
			opts->report_path, strerror(errno));
		return;
	}

	pthread_mutex_lock(&g_server_metrics.lock);
	bool server_error = g_server_metrics.error;
	char server_error_msg[256];
	if (server_error)
		snprintf(server_error_msg, sizeof(server_error_msg), "%s",
			 g_server_metrics.error_message);
	size_t connections_opened = g_server_metrics.connections_opened;
	size_t connections_closed = g_server_metrics.connections_closed;
	size_t initial_text_sent = g_server_metrics.initial_text_sent;
	size_t initial_binary_sent = g_server_metrics.initial_binary_sent;
	size_t text_received = g_server_metrics.text_received;
	size_t binary_received = g_server_metrics.binary_received;
	size_t text_echoed = g_server_metrics.text_echoed;
	size_t binary_echoed = g_server_metrics.binary_echoed;
	uint16_t last_close = g_server_metrics.last_close_code;
	pthread_mutex_unlock(&g_server_metrics.lock);

	fprintf(fp,
		"{\n"
		"  \"scenario\": \"c_server_js_client\",\n"
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
		"  }\n"
		"}\n",
		opts->mode == MODE_CONCURRENT ? "concurrent" : "sequential",
		opts->client_count,
		opts->stagger_ms,
		server_error ? "failed" : "passed",
		duration_ms,
		connections_opened,
		connections_closed,
		initial_text_sent,
		initial_binary_sent,
		text_received,
		binary_received,
		text_echoed,
		binary_echoed,
		last_close,
		server_error ? "true" : "false",
		server_error ? ", \"error_message\": \"" : "",
		server_error ? server_error_msg : "");

	if (server_error)
		fprintf(fp, "\"");

	fprintf(fp, "\n}\n");
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

static void *server_thread_fn(void *arg)
{
	wsfs_server_t *server = (wsfs_server_t *)arg;
	wsfs_server_run(server);
	return NULL;
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

	reset_server_metrics();

	struct timespec start_ts;
	if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
		perror("clock_gettime");
		return 1;
	}

	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.port = (uint16_t)opts.port;
	cfg.callbacks.on_open = on_open;
	cfg.callbacks.on_message = on_message;
	cfg.callbacks.on_close = on_close;

	wsfs_server_t server = {0};
	if (wsfs_server_init(&server, &cfg) != WSFS_STATUS_OK) {
		fprintf(stderr, "failed to init server\n");
		return 1;
	}

	g_server_handle = &server;
	g_expected_clients = opts.client_count;

	pthread_t server_thread;
	if (pthread_create(&server_thread, NULL, server_thread_fn, &server) != 0) {
		fprintf(stderr, "failed to start server thread\n");
		wsfs_server_deinit(&server);
		return 1;
	}

	printf("{\"event\":\"ready\",\"port\":%d,\"clients\":%zu}\n",
	       opts.port, opts.client_count);
	fflush(stdout);

	pthread_join(server_thread, NULL);
	wsfs_server_deinit(&server);

	struct timespec end_ts;
	if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
		perror("clock_gettime");
		return 1;
	}

	long duration_ms =
		(long)((end_ts.tv_sec - start_ts.tv_sec) * 1000L +
		       (end_ts.tv_nsec - start_ts.tv_nsec) / 1000000L);

	write_report(&opts, duration_ms);

	pthread_mutex_lock(&g_server_metrics.lock);
	bool server_error = g_server_metrics.error;
	size_t connections_closed = g_server_metrics.connections_closed;
	size_t text_received = g_server_metrics.text_received;
	size_t binary_received = g_server_metrics.binary_received;
	size_t text_echoed = g_server_metrics.text_echoed;
	size_t binary_echoed = g_server_metrics.binary_echoed;
	size_t initial_text_sent = g_server_metrics.initial_text_sent;
	size_t initial_binary_sent = g_server_metrics.initial_binary_sent;
	pthread_mutex_unlock(&g_server_metrics.lock);

	size_t expected_payloads = opts.client_count * TEXT_COUNT;
	size_t expected_binary = opts.client_count * BINARY_COUNT;
	bool counts_ok =
		(initial_text_sent == opts.client_count * TEXT_COUNT) &&
		(initial_binary_sent == opts.client_count * BINARY_COUNT) &&
		(text_received == expected_payloads) &&
		(binary_received == expected_binary) &&
		(text_echoed == expected_payloads) &&
		(binary_echoed == expected_binary) &&
		(connections_closed == opts.client_count);

	if (server_error || !counts_ok) {
		fprintf(stderr,
			"wsfs_c_js_client_stress: failure (server_error=%d, counts_ok=%d)\n",
			server_error ? 1 : 0, counts_ok ? 1 : 0);
		return 1;
	}

	printf("wsfs_c_js_client_stress: success with %zu clients (%s) in %ld ms\n",
	       opts.client_count,
	       opts.mode == MODE_CONCURRENT ? "concurrent" : "sequential",
	       duration_ms);
	return 0;
}
