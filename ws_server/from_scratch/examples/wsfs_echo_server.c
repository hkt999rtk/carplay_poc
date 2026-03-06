#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wsfs_server.h"

static void on_open(wsfs_connection_t *conn)
{
	(void)conn;
	printf("[wsfs_echo] connection opened\n");
}

static void on_message(wsfs_connection_t *conn,
	       const wsfs_message_view_t *msg)
{
	(void)conn;
	printf("[wsfs_echo] received message (%zu bytes, opcode=%u)\n",
	       msg != NULL ? msg->length : 0,
	       msg != NULL ? (unsigned)msg->opcode : 0);
	if (msg == NULL)
		return;

	if (msg->opcode == WSFS_OPCODE_TEXT) {
		wsfs_connection_send_text(conn,
			(const char *)msg->data,
			msg->length);
	} else if (msg->opcode == WSFS_OPCODE_BINARY) {
		wsfs_connection_send_binary(conn, msg->data, msg->length);
	}
}

static void on_close(wsfs_connection_t *conn, uint16_t close_code)
{
	(void)conn;
	printf("[wsfs_echo] connection closed (code=%u)\n",
	       (unsigned)close_code);
}

static int parse_args(int argc, char **argv,
		      const char **host_override,
		      uint16_t *port_override)
{
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (strcmp(arg, "--host") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "--host requires a value\n");
				return -1;
			}
			*host_override = argv[++i];
		} else if (strcmp(arg, "--port") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "--port requires a value\n");
				return -1;
			}
			char *endptr = NULL;
			unsigned long value = strtoul(argv[++i], &endptr, 10);
			if (endptr == NULL || *endptr != '\0' ||
			    value == 0 || value > 65535) {
				fprintf(stderr, "invalid port value: %s\n",
				        argv[i]);
				return -1;
			}
			*port_override = (uint16_t)value;
		} else {
			fprintf(stderr,
				"Usage: %s [--host HOST] [--port PORT]\n",
				argv[0]);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *host_override = NULL;
	uint16_t port_override = 0;

	if (parse_args(argc, argv, &host_override, &port_override) != 0)
		return EXIT_FAILURE;

	wsfs_server_config_t config;
	wsfs_server_config_defaults(&config);
	if (host_override != NULL)
		config.host = host_override;
	if (port_override != 0)
		config.port = port_override;
	config.callbacks.on_open = on_open;
	config.callbacks.on_message = on_message;
	config.callbacks.on_close = on_close;

	wsfs_server_t server = {0};
	wsfs_status_t status = wsfs_server_init(&server, &config);
	if (status != WSFS_STATUS_OK) {
		fprintf(stderr, "Failed to initialize server (status=%d)\n",
			(int)status);
		return EXIT_FAILURE;
	}

	printf("{\"event\":\"ready\",\"host\":\"%s\",\"port\":%u}\n",
	       config.host != NULL ? config.host : "(null)",
	       (unsigned)config.port);
	fflush(stdout);

	status = wsfs_server_run(&server);
	if (status != WSFS_STATUS_OK) {
		fprintf(stderr, "wsfs_server_run returned status=%d\n",
			(int)status);
	}

	wsfs_server_deinit(&server);
	return status == WSFS_STATUS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
