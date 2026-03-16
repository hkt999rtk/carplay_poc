#include "tcp_transport.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef _WIN32
static int tcp_transport_ensure_winsock(void)
{
	static int initialized = 0;
	WSADATA wsa_data;

	if (initialized)
		return 0;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		return -1;
	initialized = 1;
	return 0;
}

static void tcp_transport_close_socket(tcp_socket_t fd)
{
	closesocket((SOCKET)fd);
}
#else
static int tcp_transport_ensure_winsock(void)
{
	return 0;
}

static void tcp_transport_close_socket(tcp_socket_t fd)
{
	close(fd);
}
#endif

tcp_socket_t tcp_transport_listen(uint16_t port, int backlog)
{
	tcp_socket_t fd;
	int on = 1;
	struct sockaddr_in addr;

	if (tcp_transport_ensure_winsock() != 0)
		return TCP_TRANSPORT_INVALID_SOCKET;

	fd = (tcp_socket_t)socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
	if ((SOCKET)fd == INVALID_SOCKET)
		return TCP_TRANSPORT_INVALID_SOCKET;
	if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on)) != 0) {
#else
	if (fd < 0)
		return TCP_TRANSPORT_INVALID_SOCKET;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
#endif
		tcp_transport_close_socket(fd);
		return TCP_TRANSPORT_INVALID_SOCKET;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind((SOCKET)fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		tcp_transport_close_socket(fd);
		return TCP_TRANSPORT_INVALID_SOCKET;
	}
	if (listen((SOCKET)fd, backlog) != 0) {
		tcp_transport_close_socket(fd);
		return TCP_TRANSPORT_INVALID_SOCKET;
	}
	return fd;
}

tcp_socket_t tcp_transport_accept(tcp_socket_t listen_fd)
{
	for (;;) {
#ifdef _WIN32
		SOCKET fd = accept((SOCKET)listen_fd, NULL, NULL);
		if (fd != INVALID_SOCKET)
			return (tcp_socket_t)fd;
		if (WSAGetLastError() != WSAEINTR)
			return TCP_TRANSPORT_INVALID_SOCKET;
#else
		tcp_socket_t fd = accept(listen_fd, NULL, NULL);
		if (fd >= 0)
			return fd;
		if (errno != EINTR)
			return TCP_TRANSPORT_INVALID_SOCKET;
#endif
	}
}

tcp_socket_t tcp_transport_connect(const char *host, uint16_t port)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *rp;
	char port_str[16];
	tcp_socket_t fd = TCP_TRANSPORT_INVALID_SOCKET;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

	if (tcp_transport_ensure_winsock() != 0)
		return TCP_TRANSPORT_INVALID_SOCKET;
	if (getaddrinfo(host, port_str, &hints, &result) != 0)
		return TCP_TRANSPORT_INVALID_SOCKET;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		fd = (tcp_socket_t)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#ifdef _WIN32
		if ((SOCKET)fd == INVALID_SOCKET)
#else
		if (fd < 0)
#endif
			continue;
		if (connect((SOCKET)fd, rp->ai_addr, (int)rp->ai_addrlen) == 0)
			break;
		tcp_transport_close_socket(fd);
		fd = TCP_TRANSPORT_INVALID_SOCKET;
	}

	freeaddrinfo(result);
	return fd;
}

int tcp_transport_read_exact(tcp_socket_t fd, void *buf, size_t len)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t offset = 0;

	while (offset < len) {
#ifdef _WIN32
		int chunk = (int)((len - offset) > (size_t)INT_MAX ? INT_MAX : (int)(len - offset));
		int n = recv((SOCKET)fd, (char *)(ptr + offset), chunk, 0);
#else
		int n = (int)recv(fd, ptr + offset, len - offset, 0);
#endif
		if (n == 0)
			return 0;
		if (n < 0) {
#ifdef _WIN32
			if (WSAGetLastError() == WSAEINTR)
#else
			if (errno == EINTR)
#endif
				continue;
			return -1;
		}
		offset += (size_t)n;
	}
	return 1;
}

int tcp_transport_write_all(tcp_socket_t fd, const void *buf, size_t len)
{
	const uint8_t *ptr = (const uint8_t *)buf;
	size_t offset = 0;

	while (offset < len) {
#ifdef _WIN32
		int chunk = (int)((len - offset) > (size_t)INT_MAX ? INT_MAX : (int)(len - offset));
		int n = send((SOCKET)fd, (const char *)(ptr + offset), chunk, 0);
#else
		int n = (int)send(fd, ptr + offset, len - offset, MSG_NOSIGNAL);
#endif
		if (n < 0) {
#ifdef _WIN32
			if (WSAGetLastError() == WSAEINTR)
#else
			if (errno == EINTR)
#endif
				continue;
			return -1;
		}
		offset += (size_t)n;
	}
	return 0;
}

int tcp_transport_peek_status(tcp_socket_t fd)
{
	uint8_t b;
#ifdef _WIN32
	fd_set rfds;
	struct timeval timeout;
	int rc;

	FD_ZERO(&rfds);
	FD_SET((SOCKET)fd, &rfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	rc = select(0, &rfds, NULL, NULL, &timeout);
	if (rc == 0)
		return 2;
	if (rc < 0)
		return -1;

	int n = recv((SOCKET)fd, (char *)&b, 1, MSG_PEEK);
#else
	int n = (int)recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
#endif
	if (n > 0)
		return 1;
	if (n == 0)
		return 0;
#ifdef _WIN32
	if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINTR)
#else
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
#endif
		return 2;
	return -1;
}

void tcp_transport_shutdown_close(tcp_socket_t fd)
{
	if (fd == TCP_TRANSPORT_INVALID_SOCKET)
		return;
#ifdef _WIN32
	shutdown((SOCKET)fd, SD_BOTH);
#else
	shutdown(fd, SHUT_RDWR);
#endif
	tcp_transport_close_socket(fd);
}
