#ifndef GATEWAY_CLIENT_TRANSPORT_H
#define GATEWAY_CLIENT_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gateway_client_transport_kind {
	GATEWAY_CLIENT_TRANSPORT_TCP = 1,
	GATEWAY_CLIENT_TRANSPORT_USB = 2,
} gateway_client_transport_kind_t;

typedef struct gateway_client_transport_options {
	gateway_client_transport_kind_t kind;
	const char *host;
	uint16_t port;
	uint16_t usb_vid;
	uint16_t usb_pid;
	int usb_poll_ms;
	int usb_transfer_timeout_ms;
} gateway_client_transport_options_t;

typedef struct gateway_client_transport {
	gateway_client_transport_kind_t kind;
	volatile int stop_requested;
	int tcp_fd;
	void *usb_ctx;
	void *usb_handle;
	int usb_interface_number;
	uint8_t usb_ep_in;
	uint8_t usb_ep_out;
	int usb_transfer_timeout_ms;
	uint16_t usb_vid;
	uint16_t usb_pid;
} gateway_client_transport_t;

void gateway_client_transport_options_default(gateway_client_transport_options_t *options);
int gateway_client_transport_open(gateway_client_transport_t *transport,
				  const gateway_client_transport_options_t *options);
int gateway_client_transport_read_exact(gateway_client_transport_t *transport, void *buf, size_t len);
void gateway_client_transport_request_stop(gateway_client_transport_t *transport);
void gateway_client_transport_close(gateway_client_transport_t *transport);
const char *gateway_client_transport_name(const gateway_client_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif
