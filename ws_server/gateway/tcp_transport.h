#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int tcp_transport_listen(uint16_t port, int backlog);
int tcp_transport_accept(int listen_fd);
int tcp_transport_connect(const char *host, uint16_t port);
int tcp_transport_read_exact(int fd, void *buf, size_t len);
int tcp_transport_write_all(int fd, const void *buf, size_t len);
int tcp_transport_peek_status(int fd);
void tcp_transport_shutdown_close(int fd);

#ifdef __cplusplus
}
#endif

#endif
