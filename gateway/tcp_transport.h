#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
typedef intptr_t tcp_socket_t;
#define TCP_TRANSPORT_INVALID_SOCKET ((tcp_socket_t)(-1))
#else
typedef int tcp_socket_t;
#define TCP_TRANSPORT_INVALID_SOCKET (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

tcp_socket_t tcp_transport_listen(uint16_t port, int backlog);
tcp_socket_t tcp_transport_accept(tcp_socket_t listen_fd);
tcp_socket_t tcp_transport_connect(const char *host, uint16_t port);
int tcp_transport_read_exact(tcp_socket_t fd, void *buf, size_t len);
int tcp_transport_write_all(tcp_socket_t fd, const void *buf, size_t len);
int tcp_transport_peek_status(tcp_socket_t fd);
void tcp_transport_shutdown_close(tcp_socket_t fd);

#ifdef __cplusplus
}
#endif

#endif
