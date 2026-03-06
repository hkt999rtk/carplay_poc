#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "wsfs_server.h"

#define HANDSHAKE_PORT 19094

static void *server_thread(void *arg)
{
	wsfs_server_t *server = (wsfs_server_t *)arg;
	wsfs_status_t st = wsfs_server_run(server);
	if (st != WSFS_STATUS_OK)
		fprintf(stderr, "wsfs_server_run exited with %d\n", (int)st);
	return NULL;
}

static int connect_loopback(uint16_t port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(fd >= 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
	return fd;
}

static void send_all(int fd, const char *buf)
{
	size_t len = strlen(buf);
	size_t sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, buf + sent, len - sent, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("send");
			exit(1);
		}
		sent += (size_t)n;
	}
}

static void read_into(int fd, char *buffer, size_t capacity)
{
	ssize_t n = recv(fd, buffer, capacity - 1, 0);
	assert(n > 0);
	buffer[n] = '\0';
}

static void test_valid_handshake(void)
{
	int fd = connect_loopback(HANDSHAKE_PORT);
	const char request[] =
		"GET /demo HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: SGVsbG8tV29ybGQ=\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n";
	send_all(fd, request);

	char response[512];
	read_into(fd, response, sizeof(response));
	assert(strstr(response, "101 Switching Protocols") != NULL);
	close(fd);
}

static void test_missing_upgrade_header(void)
{
	int fd = connect_loopback(HANDSHAKE_PORT);
	const char request[] =
		"GET /demo HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Connection: keep-alive\r\n"
		"Sec-WebSocket-Key: SGVsbG8tV29ybGQ=\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n";
	send_all(fd, request);

	char response[256];
	read_into(fd, response, sizeof(response));
	assert(strstr(response, "400 Bad Request") != NULL);
	close(fd);
}

static void test_invalid_version(void)
{
	int fd = connect_loopback(HANDSHAKE_PORT);
	const char request[] =
		"GET /demo HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: SGVsbG8tV29ybGQ=\r\n"
		"Sec-WebSocket-Version: 12\r\n"
		"\r\n";
	send_all(fd, request);

	char response[256];
	read_into(fd, response, sizeof(response));
	assert(strstr(response, "400 Bad Request") != NULL);
	close(fd);
}

int main(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.port = HANDSHAKE_PORT;

	wsfs_server_t server = {0};
	assert(wsfs_server_init(&server, &cfg) == WSFS_STATUS_OK);

	pthread_t thread;
	assert(pthread_create(&thread, NULL, server_thread, &server) == 0);

	usleep(200 * 1000);

	test_valid_handshake();
	test_missing_upgrade_header();
	test_invalid_version();

	wsfs_server_request_stop(&server);
	pthread_join(thread, NULL);
	wsfs_server_deinit(&server);

	printf("wsfs_handshake_tests: all tests passed\n");
	return 0;
}
