#include "transport.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libusb.h>

static int gateway_client_transport_usb_find_interface(
	libusb_device *device, int *interface_number, uint8_t *ep_in, uint8_t *ep_out)
{
	struct libusb_config_descriptor *config = NULL;
	int rc;

	if (device == NULL || interface_number == NULL || ep_in == NULL || ep_out == NULL)
		return -1;

	rc = libusb_get_active_config_descriptor(device, &config);
	if (rc != 0)
		rc = libusb_get_config_descriptor(device, 0, &config);
	if (rc != 0 || config == NULL)
		return -1;

	for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
		const struct libusb_interface *intf = &config->interface[i];
		for (int j = 0; j < intf->num_altsetting; ++j) {
			const struct libusb_interface_descriptor *alt = &intf->altsetting[j];
			uint8_t found_in = 0;
			uint8_t found_out = 0;

			for (uint8_t k = 0; k < alt->bNumEndpoints; ++k) {
				const struct libusb_endpoint_descriptor *ep = &alt->endpoint[k];

				if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
					continue;
				if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
					found_in = ep->bEndpointAddress;
				else
					found_out = ep->bEndpointAddress;
			}

			if (found_in != 0 && found_out != 0) {
				*interface_number = alt->bInterfaceNumber;
				*ep_in = found_in;
				*ep_out = found_out;
				libusb_free_config_descriptor(config);
				return 0;
			}
		}
	}

	libusb_free_config_descriptor(config);
	return -1;
}

static int gateway_client_transport_usb_open_matching(
	libusb_context *ctx, uint16_t vid, uint16_t pid,
	libusb_device_handle **out_handle, int *interface_number, uint8_t *ep_in, uint8_t *ep_out)
{
	libusb_device **devices = NULL;
	ssize_t count;
	int found = -1;

	count = libusb_get_device_list(ctx, &devices);
	if (count < 0)
		return -1;

	for (ssize_t i = 0; i < count; ++i) {
		struct libusb_device_descriptor desc;
		libusb_device_handle *handle = NULL;

		if (libusb_get_device_descriptor(devices[i], &desc) != 0)
			continue;
		if (desc.idVendor != vid || desc.idProduct != pid)
			continue;
		if (libusb_open(devices[i], &handle) != 0 || handle == NULL)
			continue;
		if (gateway_client_transport_usb_find_interface(
			    devices[i], interface_number, ep_in, ep_out) == 0) {
			*out_handle = handle;
			found = 0;
			break;
		}
		libusb_close(handle);
	}

	libusb_free_device_list(devices, 1);
	return found;
}

int gateway_client_transport_usb_open(gateway_client_transport_t *transport,
				      const gateway_client_transport_options_t *options)
{
	libusb_context *ctx = NULL;
	libusb_device_handle *handle = NULL;
	int interface_number = -1;
	uint8_t ep_in = 0;
	uint8_t ep_out = 0;
	int rc;
	int announced_wait = 0;

	if (transport == NULL || options == NULL)
		return -1;

	rc = libusb_init(&ctx);
	if (rc != 0)
		return -1;

	while (!transport->stop_requested) {
		rc = gateway_client_transport_usb_open_matching(
			ctx, options->usb_vid, options->usb_pid, &handle, &interface_number, &ep_in, &ep_out);
		if (rc == 0)
			break;
		if (!announced_wait) {
			fprintf(stderr,
				"gateway_client: waiting for USB device %04x:%04x\n",
				(unsigned)options->usb_vid, (unsigned)options->usb_pid);
			announced_wait = 1;
		}
		usleep((useconds_t)options->usb_poll_ms * 1000u);
	}

	if (handle == NULL) {
		libusb_exit(ctx);
		return -1;
	}

	(void)libusb_set_auto_detach_kernel_driver(handle, 1);
	rc = libusb_claim_interface(handle, interface_number);
	if (rc != 0) {
		libusb_close(handle);
		libusb_exit(ctx);
		return -1;
	}
	rc = libusb_set_interface_alt_setting(handle, interface_number, 0);
	if (rc != 0) {
		libusb_release_interface(handle, interface_number);
		libusb_close(handle);
		libusb_exit(ctx);
		fprintf(stderr, "gateway_client: failed to set alt 0 on interface %d: %s\n",
			interface_number, libusb_error_name(rc));
		return -1;
	}

	transport->usb_ctx = ctx;
	transport->usb_handle = handle;
	transport->usb_interface_number = interface_number;
	transport->usb_ep_in = ep_in;
	transport->usb_ep_out = ep_out;

	fprintf(stderr,
		"gateway_client: claimed USB interface %d (%04x:%04x, ep_in=0x%02x, ep_out=0x%02x)\n",
		interface_number, (unsigned)options->usb_vid, (unsigned)options->usb_pid,
		(unsigned)ep_in, (unsigned)ep_out);

	return 0;
}

int gateway_client_transport_usb_read_exact(gateway_client_transport_t *transport, void *buf, size_t len)
{
	uint8_t *ptr = (uint8_t *)buf;
	size_t offset = 0;

	if (transport == NULL || transport->usb_handle == NULL)
		return -1;

	while (offset < len && !transport->stop_requested) {
		int transferred = 0;
		int chunk_len = (int)((len - offset) > (size_t)INT_MAX ? INT_MAX : (len - offset));
		int rc = libusb_bulk_transfer((libusb_device_handle *)transport->usb_handle,
					      transport->usb_ep_in, ptr + offset, chunk_len,
					      &transferred, transport->usb_transfer_timeout_ms);

		if (rc == 0) {
			offset += (size_t)transferred;
			continue;
		}
		if (rc == LIBUSB_ERROR_TIMEOUT)
			continue;
		if (rc == LIBUSB_ERROR_INTERRUPTED)
			continue;
		if (rc == LIBUSB_ERROR_NO_DEVICE)
			return 0;
		fprintf(stderr, "gateway_client: libusb bulk read failed: %s\n", libusb_error_name(rc));
		return -1;
	}

	return offset == len ? 1 : 0;
}

int gateway_client_transport_usb_write_all(gateway_client_transport_t *transport, const void *buf,
					   size_t len)
{
	const uint8_t *ptr = (const uint8_t *)buf;
	size_t offset = 0;

	if (transport == NULL || transport->usb_handle == NULL)
		return -1;

	while (offset < len && !transport->stop_requested) {
		int transferred = 0;
		int chunk_len = (int)((len - offset) > (size_t)INT_MAX ? INT_MAX : (len - offset));
		int rc = libusb_bulk_transfer((libusb_device_handle *)transport->usb_handle,
					      transport->usb_ep_out, (unsigned char *)(ptr + offset),
					      chunk_len, &transferred,
					      transport->usb_transfer_timeout_ms);

		if (rc == 0) {
			if (transferred <= 0) {
				fprintf(stderr, "gateway_client: libusb bulk write made no progress\n");
				return -1;
			}
			offset += (size_t)transferred;
			continue;
		}
		if (rc == LIBUSB_ERROR_TIMEOUT)
			continue;
		if (rc == LIBUSB_ERROR_INTERRUPTED)
			continue;
		fprintf(stderr, "gateway_client: libusb bulk write failed: %s\n",
			libusb_error_name(rc));
		return -1;
	}

	return offset == len ? 0 : -1;
}

void gateway_client_transport_usb_request_stop(gateway_client_transport_t *transport)
{
	(void)transport;
}

void gateway_client_transport_usb_close(gateway_client_transport_t *transport)
{
	libusb_device_handle *handle;
	libusb_context *ctx;

	if (transport == NULL)
		return;

	handle = (libusb_device_handle *)transport->usb_handle;
	ctx = (libusb_context *)transport->usb_ctx;

	if (handle != NULL) {
		if (transport->usb_interface_number >= 0)
			libusb_release_interface(handle, transport->usb_interface_number);
		libusb_close(handle);
	}
	if (ctx != NULL)
		libusb_exit(ctx);

	transport->usb_handle = NULL;
	transport->usb_ctx = NULL;
	transport->usb_interface_number = -1;
	transport->usb_ep_in = 0;
	transport->usb_ep_out = 0;
}
