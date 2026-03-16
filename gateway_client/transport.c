#include "transport.h"

#include <string.h>

#include "tcp_transport.h"

int gateway_client_transport_usb_open(gateway_client_transport_t *transport,
				      const gateway_client_transport_options_t *options);
int gateway_client_transport_usb_read_exact(gateway_client_transport_t *transport, void *buf,
					    size_t len);
void gateway_client_transport_usb_request_stop(gateway_client_transport_t *transport);
void gateway_client_transport_usb_close(gateway_client_transport_t *transport);

void gateway_client_transport_options_default(gateway_client_transport_options_t *options)
{
	if (options == NULL)
		return;

	memset(options, 0, sizeof(*options));
	options->kind = GATEWAY_CLIENT_TRANSPORT_USB;
	options->host = "127.0.0.1";
	options->port = 19000u;
	options->usb_vid = 0x0BDAu;
	options->usb_pid = 0x8195u;
	options->usb_poll_ms = 250;
	options->usb_transfer_timeout_ms = 100;
}

int gateway_client_transport_open(gateway_client_transport_t *transport,
				  const gateway_client_transport_options_t *options)
{
	if (transport == NULL || options == NULL)
		return -1;

	memset(transport, 0, sizeof(*transport));
	transport->kind = options->kind;
	transport->tcp_fd = TCP_TRANSPORT_INVALID_SOCKET;
	transport->usb_interface_number = -1;
	transport->usb_vid = options->usb_vid;
	transport->usb_pid = options->usb_pid;
	transport->usb_transfer_timeout_ms = options->usb_transfer_timeout_ms;

	if (options->kind == GATEWAY_CLIENT_TRANSPORT_USB)
		return gateway_client_transport_usb_open(transport, options);

	transport->tcp_fd = tcp_transport_connect(options->host, options->port);
	return transport->tcp_fd != TCP_TRANSPORT_INVALID_SOCKET ? 0 : -1;
}

int gateway_client_transport_read_exact(gateway_client_transport_t *transport, void *buf, size_t len)
{
	if (transport == NULL)
		return -1;
	if (transport->kind == GATEWAY_CLIENT_TRANSPORT_USB)
		return gateway_client_transport_usb_read_exact(transport, buf, len);
	return tcp_transport_read_exact(transport->tcp_fd, buf, len);
}

void gateway_client_transport_request_stop(gateway_client_transport_t *transport)
{
	if (transport == NULL)
		return;

	transport->stop_requested = 1;
	if (transport->kind == GATEWAY_CLIENT_TRANSPORT_USB) {
		gateway_client_transport_usb_request_stop(transport);
		return;
	}

	if (transport->tcp_fd != TCP_TRANSPORT_INVALID_SOCKET) {
		tcp_transport_shutdown_close(transport->tcp_fd);
		transport->tcp_fd = TCP_TRANSPORT_INVALID_SOCKET;
	}
}

void gateway_client_transport_close(gateway_client_transport_t *transport)
{
	if (transport == NULL)
		return;

	if (transport->kind == GATEWAY_CLIENT_TRANSPORT_USB) {
		gateway_client_transport_usb_close(transport);
		return;
	}

	if (transport->tcp_fd != TCP_TRANSPORT_INVALID_SOCKET) {
		tcp_transport_shutdown_close(transport->tcp_fd);
		transport->tcp_fd = TCP_TRANSPORT_INVALID_SOCKET;
	}
}

const char *gateway_client_transport_name(const gateway_client_transport_t *transport)
{
	if (transport == NULL)
		return "unknown";
	return transport->kind == GATEWAY_CLIENT_TRANSPORT_USB ? "usb" : "tcp";
}
