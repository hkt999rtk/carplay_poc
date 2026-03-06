#ifndef WS_UPSTREAM_CLIENT_H
#define WS_UPSTREAM_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ws_upstream_client {
	int fd;
	uint8_t *prebuffer;
	size_t prebuffer_len;
	size_t prebuffer_off;
} ws_upstream_client_t;

int ws_upstream_connect(ws_upstream_client_t *client, const char *host,
			uint16_t port, const char *path);
int ws_upstream_fd(const ws_upstream_client_t *client);
int ws_upstream_send_text(ws_upstream_client_t *client, const char *text);
int ws_upstream_send_close(ws_upstream_client_t *client, uint16_t code);
int ws_upstream_recv_frame(ws_upstream_client_t *client, uint8_t *opcode_out,
			   uint8_t **payload_out, size_t *payload_len_out);
void ws_upstream_close(ws_upstream_client_t *client);

#ifdef __cplusplus
}
#endif

#endif
