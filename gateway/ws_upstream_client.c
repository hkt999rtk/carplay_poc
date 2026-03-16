#include "ws_upstream_client.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "crypto_stream.h"
#include "tcp_transport.h"

static int ws_send_masked_frame(int fd, uint8_t opcode,
				const uint8_t *payload, size_t length)
{
	uint8_t header[14];
	uint8_t mask[4];
	uint8_t *masked = NULL;
	size_t header_len = 0;

	if (crypto_stream_fill_random(mask, sizeof(mask)) != 0)
		return -1;

	header[header_len++] = (uint8_t)(0x80 | opcode);
	if (length <= 125u) {
		header[header_len++] = (uint8_t)(0x80u | (uint8_t)length);
	} else if (length <= 0xFFFFu) {
		header[header_len++] = 0x80u | 126u;
		header[header_len++] = (uint8_t)(length >> 8);
		header[header_len++] = (uint8_t)length;
	} else {
		header[header_len++] = 0x80u | 127u;
		for (int i = 7; i >= 0; --i)
			header[header_len++] = (uint8_t)(length >> (i * 8));
	}

	memcpy(header + header_len, mask, sizeof(mask));
	header_len += sizeof(mask);
	if (tcp_transport_write_all(fd, header, header_len) != 0)
		return -1;

	if (length == 0u)
		return 0;

	masked = (uint8_t *)malloc(length);
	if (masked == NULL)
		return -1;
	for (size_t i = 0; i < length; ++i)
		masked[i] = payload[i] ^ mask[i % 4u];
	if (tcp_transport_write_all(fd, masked, length) != 0) {
		free(masked);
		return -1;
	}
	free(masked);
	return 0;
}

static int ws_read_some(ws_upstream_client_t *client, uint8_t *dest, size_t len)
{
	size_t copied = 0;

	while (copied < len) {
		if (client->prebuffer != NULL && client->prebuffer_off < client->prebuffer_len) {
			size_t avail = client->prebuffer_len - client->prebuffer_off;
			size_t take = avail;
			if (take > (len - copied))
				take = len - copied;
			memcpy(dest + copied, client->prebuffer + client->prebuffer_off, take);
			client->prebuffer_off += take;
			copied += take;
			if (client->prebuffer_off == client->prebuffer_len) {
				free(client->prebuffer);
				client->prebuffer = NULL;
				client->prebuffer_len = 0;
				client->prebuffer_off = 0;
			}
			continue;
		}

		{
			ssize_t n = recv(client->fd, dest + copied, len - copied, 0);
			if (n == 0)
				return 0;
			if (n < 0) {
				if (errno == EINTR)
					continue;
				return -1;
			}
			copied += (size_t)n;
		}
	}

	return 1;
}

int ws_upstream_connect(ws_upstream_client_t *client, const char *host,
			uint16_t port, const char *path)
{
	char request[512];
	char response[4096];
	size_t total = 0;
	int written;

	if (client == NULL || host == NULL)
		return -1;

	memset(client, 0, sizeof(*client));
	client->fd = tcp_transport_connect(host, port);
	if (client->fd < 0)
		return -1;

	if (path == NULL)
		path = "/";
	written = snprintf(
		request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		"Host: %s:%u\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		path, host, (unsigned)port);
	if (written < 0 || (size_t)written >= sizeof(request)) {
		ws_upstream_close(client);
		return -1;
	}

	if (tcp_transport_write_all(client->fd, request, (size_t)written) != 0) {
		ws_upstream_close(client);
		return -1;
	}

	while (total + 1u < sizeof(response)) {
		ssize_t n = recv(client->fd, response + total, sizeof(response) - total - 1u, 0);
		char *header_end;

		if (n == 0) {
			ws_upstream_close(client);
			return -1;
		}
		if (n < 0) {
			if (errno == EINTR)
				continue;
			ws_upstream_close(client);
			return -1;
		}
		total += (size_t)n;
		response[total] = '\0';
		header_end = strstr(response, "\r\n\r\n");
		if (header_end != NULL) {
			size_t header_len = (size_t)(header_end - response) + 4u;
			size_t extra = total - header_len;
			if (strstr(response, "101") == NULL) {
				ws_upstream_close(client);
				return -1;
			}
			if (extra > 0u) {
				client->prebuffer = (uint8_t *)malloc(extra);
				if (client->prebuffer == NULL) {
					ws_upstream_close(client);
					return -1;
				}
				memcpy(client->prebuffer, response + header_len, extra);
				client->prebuffer_len = extra;
				client->prebuffer_off = 0;
			}
			return 0;
		}
	}

	ws_upstream_close(client);
	return -1;
}

int ws_upstream_fd(const ws_upstream_client_t *client)
{
	if (client == NULL)
		return -1;
	return client->fd;
}

int ws_upstream_send_text(ws_upstream_client_t *client, const char *text)
{
	if (client == NULL || text == NULL)
		return -1;
	return ws_send_masked_frame(client->fd, 0x1u, (const uint8_t *)text, strlen(text));
}

int ws_upstream_send_close(ws_upstream_client_t *client, uint16_t code)
{
	uint8_t payload[2];

	if (client == NULL)
		return -1;
	payload[0] = (uint8_t)(code >> 8);
	payload[1] = (uint8_t)code;
	return ws_send_masked_frame(client->fd, 0x8u, payload, sizeof(payload));
}

int ws_upstream_recv_frame(ws_upstream_client_t *client, uint8_t *opcode_out,
			   uint8_t **payload_out, size_t *payload_len_out)
{
	uint8_t header[2];
	uint8_t mask[4] = {0};
	uint8_t *payload = NULL;
	size_t payload_len;
	int rc;

	if (client == NULL || opcode_out == NULL || payload_out == NULL || payload_len_out == NULL)
		return -1;

	rc = ws_read_some(client, header, sizeof(header));
	if (rc <= 0)
		return -1;

	*opcode_out = (uint8_t)(header[0] & 0x0Fu);
	payload_len = (size_t)(header[1] & 0x7Fu);
	if (payload_len == 126u) {
		uint8_t ext[2];

		rc = ws_read_some(client, ext, sizeof(ext));
		if (rc <= 0)
			return -1;
		payload_len = ((size_t)ext[0] << 8) | (size_t)ext[1];
	} else if (payload_len == 127u) {
		uint8_t ext[8];

		payload_len = 0u;
		rc = ws_read_some(client, ext, sizeof(ext));
		if (rc <= 0)
			return -1;
		for (int i = 0; i < 8; ++i)
			payload_len = (payload_len << 8) | (size_t)ext[i];
	}

	if ((header[1] & 0x80u) != 0u) {
		rc = ws_read_some(client, mask, sizeof(mask));
		if (rc <= 0)
			return -1;
	}

	if (payload_len > 0u) {
		payload = (uint8_t *)malloc(payload_len);
		if (payload == NULL)
			return -1;
		rc = ws_read_some(client, payload, payload_len);
		if (rc <= 0) {
			free(payload);
			return -1;
		}
		if ((header[1] & 0x80u) != 0u) {
			for (size_t i = 0; i < payload_len; ++i)
				payload[i] ^= mask[i % 4u];
		}
	}

	*payload_out = payload;
	*payload_len_out = payload_len;
	return 0;
}

void ws_upstream_close(ws_upstream_client_t *client)
{
	if (client == NULL)
		return;
	free(client->prebuffer);
	client->prebuffer = NULL;
	client->prebuffer_len = 0;
	client->prebuffer_off = 0;
	tcp_transport_shutdown_close(client->fd);
	client->fd = -1;
}
