#include "tcp_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int tcp_transport_listen(uint16_t port, int backlog)
{
	int fd;
	int on = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
		close(fd);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	if (listen(fd, backlog) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int tcp_transport_accept(int listen_fd)
{
	for (;;) {
		int fd = accept(listen_fd, NULL, NULL);
		if (fd >= 0)
			return fd;
		if (errno != EINTR)
			return -1;
	}
}

int tcp_transport_connect(const char *host, uint16_t port)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *rp;
	char port_str[16];
	int fd = -1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

	if (getaddrinfo(host, port_str, &hints, &result) != 0)
		return -1;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}

	freeaddrinfo(result);
	return fd;
}

int tcp_transport_read_exact(int fd, void *buf, size_t len)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t offset = 0;

	while (offset < len) {
		ssize_t n = recv(fd, ptr + offset, len - offset, 0);
		if (n == 0)
			return 0;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		offset += (size_t)n;
	}
	return 1;
}

int tcp_transport_write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *ptr = (const uint8_t *)buf;
	size_t offset = 0;

	while (offset < len) {
		ssize_t n = send(fd, ptr + offset, len - offset, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		offset += (size_t)n;
	}
	return 0;
}

int tcp_transport_peek_status(int fd)
{
	uint8_t b;
	ssize_t n = recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
	if (n > 0)
		return 1;
	if (n == 0)
		return 0;
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return 2;
	return -1;
}

void tcp_transport_shutdown_close(int fd)
{
	if (fd < 0)
		return;
	shutdown(fd, SHUT_RDWR);
	close(fd);
}
