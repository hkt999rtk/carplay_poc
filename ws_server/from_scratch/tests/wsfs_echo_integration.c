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

#define TEST_PORT 19093

#ifndef STRINGIFY
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#endif

static void on_open(wsfs_connection_t *conn)
{
    (void)conn;
}

static void on_message(wsfs_connection_t *conn,
		       const wsfs_message_view_t *msg)
{
	if (msg == NULL)
		return;

	if (msg->opcode == WSFS_OPCODE_TEXT)
		wsfs_connection_send_text(conn, (const char *)msg->data,
					  msg->length);
	else if (msg->opcode == WSFS_OPCODE_BINARY)
		wsfs_connection_send_binary(conn, msg->data, msg->length);
}

static void on_close(wsfs_connection_t *conn, uint16_t close_code)
{
	(void)conn;
	(void)close_code;
}

static void *server_thread_fn(void *arg)
{
	wsfs_server_t *server = (wsfs_server_t *)arg;
	wsfs_server_run(server);
	return NULL;
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

static int perform_handshake(int fd)
{
	const char request[] =
		"GET /chat HTTP/1.1\r\n"
		"Host: 127.0.0.1:" STRINGIFY(TEST_PORT) "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n";

	send_all(fd, request, sizeof(request) - 1);

	char response[512];
	ssize_t n = recv(fd, response, sizeof(response) - 1, 0);
	if (n <= 0)
		return -1;
	response[n] = '\0';
	return strstr(response, "101") != NULL ? 0 : -1;
}

static void send_text_message(int fd, const char *text)
{
	uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
	size_t len = strlen(text);
	uint8_t header[2 + 4];
	header[0] = 0x81;
	header[1] = 0x80 | (uint8_t)len;
	memcpy(header + 2, mask, 4);
	send_all(fd, header, sizeof(header));

	uint8_t *masked = (uint8_t *)malloc(len);
	assert(masked != NULL);
	for (size_t i = 0; i < len; ++i)
		masked[i] = (uint8_t)text[i] ^ mask[i % 4];
	send_all(fd, masked, len);
	free(masked);
}

static void expect_text_message(int fd, const char *expected)
{
	uint8_t header[2];
	recv_exact(fd, header, sizeof(header));
	assert((header[0] & 0x0F) == WSFS_OPCODE_TEXT);
	assert((header[0] & 0x80) != 0); /* FIN */
	assert((header[1] & 0x80) == 0); /* server frames unmasked */
	size_t len = header[1] & 0x7F;
	assert(len == strlen(expected));

	uint8_t *payload = (uint8_t *)malloc(len);
	assert(payload != NULL);
	recv_exact(fd, payload, len);
	assert(memcmp(payload, expected, len) == 0);
	free(payload);
}

static void send_binary_message(int fd, const uint8_t *data, size_t len)
{
	uint8_t mask[4] = {0x21, 0x22, 0x23, 0x24};
	uint8_t header[2 + 4];
	header[0] = 0x82;
	header[1] = 0x80 | (uint8_t)len;
	memcpy(header + 2, mask, 4);
	send_all(fd, header, sizeof(header));

	uint8_t *masked = (uint8_t *)malloc(len);
	assert(masked != NULL);
	for (size_t i = 0; i < len; ++i)
		masked[i] = data[i] ^ mask[i % 4];
	send_all(fd, masked, len);
	free(masked);
}

static void expect_binary_message(int fd, const uint8_t *expected, size_t len)
{
	uint8_t header[2];
	recv_exact(fd, header, sizeof(header));
	assert((header[0] & 0x0F) == WSFS_OPCODE_BINARY);
	assert((header[0] & 0x80) != 0);
	assert((header[1] & 0x80) == 0);
	assert((header[1] & 0x7F) == len);

	uint8_t *payload = (uint8_t *)malloc(len);
	assert(payload != NULL);
	recv_exact(fd, payload, len);
	assert(memcmp(payload, expected, len) == 0);
	free(payload);
}

static void send_ping(int fd, const uint8_t *payload, size_t len)
{
	uint8_t mask[4] = {0x55, 0x44, 0x33, 0x22};
	uint8_t header[2 + 4];
	header[0] = 0x89;
	header[1] = 0x80 | (uint8_t)len;
	memcpy(header + 2, mask, 4);
	send_all(fd, header, sizeof(header));

	uint8_t *masked = (uint8_t *)malloc(len);
	assert(masked != NULL);
	for (size_t i = 0; i < len; ++i)
		masked[i] = payload[i] ^ mask[i % 4];
	send_all(fd, masked, len);
	free(masked);
}

static void expect_pong(int fd, const uint8_t *expected, size_t len)
{
	uint8_t header[2];
	recv_exact(fd, header, sizeof(header));
	assert((header[0] & 0x0F) == WSFS_OPCODE_PONG);
	assert((header[0] & 0x80) != 0);
	assert((header[1] & 0x80) == 0);
	assert((header[1] & 0x7F) == len);
	uint8_t *payload = (uint8_t *)malloc(len);
	assert(payload != NULL);
	recv_exact(fd, payload, len);
	assert(memcmp(payload, expected, len) == 0);
	free(payload);
}

static void send_close(int fd, uint16_t code)
{
	uint8_t mask[4] = {0x01, 0x02, 0x03, 0x04};
	uint8_t frame[2 + 4 + 2];
	frame[0] = 0x88;
	frame[1] = 0x80 | 2;
	memcpy(frame + 2, mask, 4);
	frame[6] = (uint8_t)((code >> 8) & 0xFF) ^ mask[0];
	frame[7] = (uint8_t)(code & 0xFF) ^ mask[1];
	send_all(fd, frame, sizeof(frame));
}

int main(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.port = TEST_PORT;
	cfg.callbacks.on_open = on_open;
	cfg.callbacks.on_message = on_message;
	cfg.callbacks.on_close = on_close;

	wsfs_server_t server = {0};
	assert(wsfs_server_init(&server, &cfg) == WSFS_STATUS_OK);

	pthread_t server_thread;
	assert(pthread_create(&server_thread, NULL, server_thread_fn, &server) == 0);

	usleep(200 * 1000);

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(fd >= 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(TEST_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

	assert(perform_handshake(fd) == 0);

	const char *message = "hello wsfs";
	send_text_message(fd, message);
	expect_text_message(fd, message);

	const uint8_t bin_payload[3] = {0x01, 0x02, 0x03};
	send_binary_message(fd, bin_payload, sizeof(bin_payload));
	expect_binary_message(fd, bin_payload, sizeof(bin_payload));

	const uint8_t ping_payload[2] = {0xAA, 0xBB};
	send_ping(fd, ping_payload, sizeof(ping_payload));
	expect_pong(fd, ping_payload, sizeof(ping_payload));

	send_close(fd, 1000);

	uint8_t close_header[2];
	recv_exact(fd, close_header, sizeof(close_header));
	assert((close_header[0] & 0x0F) == WSFS_OPCODE_CLOSE);
	uint8_t close_payload[2];
	recv_exact(fd, close_payload, sizeof(close_payload));
	uint16_t code = (uint16_t)((close_payload[0] << 8) | close_payload[1]);
	assert(code == 1000);

	close(fd);

	wsfs_server_request_stop(&server);
	pthread_join(server_thread, NULL);
	wsfs_server_deinit(&server);

	printf("wsfs_echo_integration: success\n");
	return 0;
}
