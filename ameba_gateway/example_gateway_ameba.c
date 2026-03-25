#include "FreeRTOS.h"
#include "task.h"

#include <platform/platform_stdlib.h>
#include <string.h>

#include "core/inc/usb_composite.h"
#include "diag.h"
#include "example_gateway_ameba.h"
#include "hal_cache.h"
#include "usb.h"
#include "usb_gadget.h"

#define GATEWAY_AMEBA_DIAG_STAGE_MINIMAL 1

#define GATEWAY_AMEBA_TASK_STACK_SIZE 3072
#define GATEWAY_AMEBA_TASK_PRIORITY   (tskIDLE_PRIORITY + 1)
#define GATEWAY_AMEBA_HEARTBEAT_MS    1000
#define GATEWAY_AMEBA_USB_PACKET_SIZE 64

#define GATEWAY_USB_STRING_MANUFACTURER 1
#define GATEWAY_USB_STRING_PRODUCT      2
#define GATEWAY_USB_STRING_SERIAL       3
#define GATEWAY_USB_STRING_CONFIG       4
#define GATEWAY_USB_STRING_INTERFACE    5

typedef struct gateway_usb_ping_packet {
	char magic[4];
	u32 seq_no;
	u32 payload_len;
	char payload[52];
} gateway_usb_ping_packet_t;

typedef struct gateway_usb_function_ctx {
	struct usb_function function;
	struct usb_ep *in_ep;
	struct usb_ep *out_ep;
	struct usb_request *in_req;
	struct usb_request *out_req;
	u8 *in_buf;
	u8 *out_buf;
	int configured;
	int in_busy;
} gateway_usb_function_ctx_t;

static volatile int gateway_usb_started = 0;
static volatile int gateway_usb_configured = 0;
static volatile unsigned long gateway_usb_rx_packets = 0;
static volatile unsigned long gateway_usb_tx_packets = 0;
static volatile unsigned long gateway_usb_last_actual = 0;
static volatile unsigned long gateway_usb_last_seq = 0;
static volatile unsigned long gateway_usb_last_payload_len = 0;
static volatile unsigned long gateway_usb_last_req_buf_addr = 0;
static volatile unsigned long gateway_usb_last_ctx_buf_addr = 0;
static volatile u8 gateway_usb_last_magic[4] = {0};
static volatile u8 gateway_usb_last_bytes[4] = {0};
static volatile u8 gateway_usb_last_req_dump[16] = {0};
static volatile u8 gateway_usb_last_ctx_dump[16] = {0};
static volatile int gateway_usb_last_ping_valid = 0;

static int gateway_usb_function_set_alt(struct usb_function *function,
					unsigned interface, unsigned alt);
static int gateway_usb_function_setup(struct usb_function *function,
				      const struct usb_ctrlrequest *ctrl);

static struct usb_device_descriptor gateway_usb_device_desc = {
	.bLength = sizeof(gateway_usb_device_desc),
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0x00,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0BDA,
	.idProduct = 0x8195,
	.iManufacturer = GATEWAY_USB_STRING_MANUFACTURER,
	.iProduct = GATEWAY_USB_STRING_PRODUCT,
	.iSerialNumber = GATEWAY_USB_STRING_SERIAL,
	.bNumConfigurations = 0x01,
};

static struct usb_string gateway_usb_strings_tab[] = {
	{ GATEWAY_USB_STRING_MANUFACTURER, "Realtek Singapore Semiconductor" },
	{ GATEWAY_USB_STRING_PRODUCT, "AmebaPro Gateway" },
	{ GATEWAY_USB_STRING_SERIAL, "0123456789" },
	{ GATEWAY_USB_STRING_CONFIG, "AmebaPro Gateway Config" },
	{ GATEWAY_USB_STRING_INTERFACE, "AmebaPro Gateway Interface" },
	{ 0, NULL },
};

static struct usb_gadget_strings gateway_usb_stringtab = {
	.language = 0x0409,
	.strings = gateway_usb_strings_tab,
};

static struct usb_gadget_strings *gateway_usb_dev_strings[] = {
	&gateway_usb_stringtab,
	NULL,
};

static struct usb_interface_descriptor gateway_usb_intf_desc = {
	.bLength = sizeof(gateway_usb_intf_desc),
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0xff,
	.bInterfaceProtocol = 0xff,
	.iInterface = GATEWAY_USB_STRING_INTERFACE,
};

static struct usb_endpoint_descriptor gateway_usb_ep_in_fs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_out_fs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_in_hs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 512,
	.bInterval = 0,
};

static struct usb_endpoint_descriptor gateway_usb_ep_out_hs_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = 512,
	.bInterval = 0,
};

static struct usb_descriptor_header *gateway_usb_fs_descs[] = {
	(struct usb_descriptor_header *)&gateway_usb_intf_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_in_fs_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_out_fs_desc,
	NULL,
};

static struct usb_descriptor_header *gateway_usb_hs_descs[] = {
	(struct usb_descriptor_header *)&gateway_usb_intf_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_in_hs_desc,
	(struct usb_descriptor_header *)&gateway_usb_ep_out_hs_desc,
	NULL,
};

static gateway_usb_function_ctx_t gateway_usb_ctx;
static u8 gateway_usb_in_buf[GATEWAY_AMEBA_USB_PACKET_SIZE] __attribute__((aligned(32)));
static u8 gateway_usb_out_buf[GATEWAY_AMEBA_USB_PACKET_SIZE] __attribute__((aligned(32)));

static void gateway_usb_try_force_configure(void)
{
	enum usb_device_speed speed;

	if (!gateway_usb_started || gateway_usb_ctx.configured) {
		return;
	}
	if (gateway_usb_ctx.function.config == NULL ||
	    gateway_usb_ctx.function.config->cdev == NULL ||
	    gateway_usb_ctx.function.config->cdev->gadget == NULL) {
		return;
	}

	speed = gateway_usb_ctx.function.config->cdev->gadget->speed;
	if (speed != USB_SPEED_FULL && speed != USB_SPEED_HIGH) {
		return;
	}

	printf("[gateway][usb] forcing alt0 configure, speed=%d\r\n", speed);
	(void)gateway_usb_function_set_alt(&gateway_usb_ctx.function,
					   gateway_usb_intf_desc.bInterfaceNumber,
					   0);
}

static int gateway_usb_queue_out_request(gateway_usb_function_ctx_t *ctx);

static void gateway_usb_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	(void)ep;

	gateway_usb_ctx.in_busy = 0;
	if (req != NULL && req->status != 0) {
		printf("[gateway][usb] bulk IN complete status=%d actual=%u\r\n",
		       req->status, req->actual);
	}
}

static int gateway_usb_send_pong(gateway_usb_function_ctx_t *ctx, u32 seq_no)
{
	gateway_usb_ping_packet_t *pong;
	const char *payload = "gateway-usb-pong";
	unsigned int payload_len = (unsigned int)strlen(payload);
	int rc;

	if (ctx == NULL || ctx->in_ep == NULL || ctx->in_req == NULL || ctx->in_busy) {
		return -1;
	}

	memset(ctx->in_buf, 0, GATEWAY_AMEBA_USB_PACKET_SIZE);
	pong = (gateway_usb_ping_packet_t *)ctx->in_buf;
	memcpy(pong->magic, "PONG", sizeof(pong->magic));
	pong->seq_no = seq_no;
	pong->payload_len = payload_len;
	memcpy(pong->payload, payload, payload_len);

	ctx->in_req->buf = ctx->in_buf;
	ctx->in_req->dma = (dma_addr_t)ctx->in_buf;
	ctx->in_req->length = GATEWAY_AMEBA_USB_PACKET_SIZE;
	ctx->in_req->actual = 0;
	ctx->in_req->status = 0;
	ctx->in_req->zero = 0;
	ctx->in_busy = 1;

	rc = usb_ep_queue(ctx->in_ep, ctx->in_req, 0);
	if (rc != 0) {
		ctx->in_busy = 0;
		printf("[gateway][usb] failed to queue bulk IN rc=%d\r\n", rc);
		return -1;
	}

	++gateway_usb_tx_packets;
	printf("[gateway][usb] pong seq=%lu tx_packets=%lu\r\n",
	       (unsigned long)seq_no,
	       gateway_usb_tx_packets);
	return 0;
}

static void gateway_usb_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)req->context;
	const gateway_usb_ping_packet_t *ping;
	const u8 *raw;
	unsigned int i;

	(void)ep;

	if (ctx == NULL || req == NULL) {
		return;
	}

	if (req->status != 0) {
		printf("[gateway][usb] bulk OUT complete status=%d actual=%u\r\n",
		       req->status, req->actual);
		return;
	}

	++gateway_usb_rx_packets;
	gateway_usb_last_actual = req->actual;
	gateway_usb_last_req_buf_addr = (unsigned long)req->buf;
	gateway_usb_last_ctx_buf_addr = (unsigned long)ctx->out_buf;
	dcache_invalidate_by_addr((uint32_t *)ctx->out_buf, GATEWAY_AMEBA_USB_PACKET_SIZE);

	for (i = 0; i < sizeof(gateway_usb_last_req_dump); ++i) {
		if (req->buf != NULL && i < req->actual) {
			gateway_usb_last_req_dump[i] = ((const u8 *)req->buf)[i];
		} else {
			gateway_usb_last_req_dump[i] = 0;
		}
		gateway_usb_last_ctx_dump[i] = ctx->out_buf[i];
	}

	if (req->actual < 12) {
		gateway_usb_last_ping_valid = -1;
		(void)gateway_usb_queue_out_request(ctx);
		return;
	}

	raw = (const u8 *)req->buf;
	if (raw == NULL) {
		raw = ctx->out_buf;
	}
	ping = (const gateway_usb_ping_packet_t *)raw;
	gateway_usb_last_magic[0] = raw[0];
	gateway_usb_last_magic[1] = raw[1];
	gateway_usb_last_magic[2] = raw[2];
	gateway_usb_last_magic[3] = raw[3];
	gateway_usb_last_bytes[0] = raw[0];
	gateway_usb_last_bytes[1] = raw[1];
	gateway_usb_last_bytes[2] = raw[2];
	gateway_usb_last_bytes[3] = raw[3];
	gateway_usb_last_seq = ping->seq_no;
	gateway_usb_last_payload_len = ping->payload_len;
	if (memcmp(ping->magic, "PING", sizeof(ping->magic)) == 0) {
		gateway_usb_last_ping_valid = 1;
	} else {
		/* For bring-up, prove bulk IN works even if the received payload is not decoded yet. */
		gateway_usb_last_ping_valid = 2;
	}
	(void)gateway_usb_send_pong(ctx, ping->seq_no);

	(void)gateway_usb_queue_out_request(ctx);
}

static int gateway_usb_queue_out_request(gateway_usb_function_ctx_t *ctx)
{
	int rc;

	if (ctx == NULL || ctx->out_ep == NULL || ctx->out_req == NULL || !ctx->configured) {
		return -1;
	}

	memset(ctx->out_buf, 0, GATEWAY_AMEBA_USB_PACKET_SIZE);
	ctx->out_req->buf = ctx->out_buf;
	ctx->out_req->dma = (dma_addr_t)ctx->out_buf;
	ctx->out_req->length = GATEWAY_AMEBA_USB_PACKET_SIZE;
	ctx->out_req->actual = 0;
	ctx->out_req->status = 0;
	ctx->out_req->short_not_ok = 0;

	rc = usb_ep_queue(ctx->out_ep, ctx->out_req, 0);
	if (rc != 0) {
		printf("[gateway][usb] failed to queue bulk OUT rc=%d\r\n", rc);
		return -1;
	}

	return 0;
}

static int gateway_usb_function_bind(struct usb_configuration *config,
				     struct usb_function *function)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;
	int interface_id;

	interface_id = usb_interface_id(config, function);
	if (interface_id < 0) {
		return interface_id;
	}

	gateway_usb_intf_desc.bInterfaceNumber = (u8)interface_id;

	usb_ep_autoconfig_reset(config->cdev->gadget);
	ctx->in_ep = usb_ep_autoconfig(config->cdev->gadget, &gateway_usb_ep_in_fs_desc);
	ctx->out_ep = usb_ep_autoconfig(config->cdev->gadget, &gateway_usb_ep_out_fs_desc);
	if (ctx->in_ep == NULL || ctx->out_ep == NULL) {
		printf("[gateway][usb] usb_ep_autoconfig failed\r\n");
		return -1;
	}

	gateway_usb_ep_in_hs_desc.bEndpointAddress = gateway_usb_ep_in_fs_desc.bEndpointAddress;
	gateway_usb_ep_out_hs_desc.bEndpointAddress = gateway_usb_ep_out_fs_desc.bEndpointAddress;

	function->fs_descriptors = gateway_usb_fs_descs;
	function->hs_descriptors = gateway_usb_hs_descs;

	printf("[gateway][usb] function bind interface=%d ep_in=0x%02x ep_out=0x%02x\r\n",
	       interface_id,
	       gateway_usb_ep_in_fs_desc.bEndpointAddress,
	       gateway_usb_ep_out_fs_desc.bEndpointAddress);
	return 0;
}

static void gateway_usb_function_disable(struct usb_function *function)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;

	gateway_usb_configured = 0;
	ctx->configured = 0;
	ctx->in_busy = 0;
	if (ctx->in_ep != NULL) {
		usb_ep_disable(ctx->in_ep);
	}
	if (ctx->out_ep != NULL) {
		usb_ep_disable(ctx->out_ep);
	}
	printf("[gateway][usb] disabled\r\n");
}

static int gateway_usb_function_setup(struct usb_function *function,
				      const struct usb_ctrlrequest *ctrl)
{
	(void)function;
	printf("[gateway][usb] setup reqType=0x%02x req=0x%02x value=0x%04x index=0x%04x len=%u\r\n",
	       ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
	return -1;
}

static int gateway_usb_function_set_alt(struct usb_function *function,
					unsigned interface, unsigned alt)
{
	gateway_usb_function_ctx_t *ctx = (gateway_usb_function_ctx_t *)function;
	const struct usb_endpoint_descriptor *in_desc;
	const struct usb_endpoint_descriptor *out_desc;
	enum usb_device_speed speed;
	int rc;

	(void)interface;

	speed = function->config->cdev->gadget->speed;
	in_desc = (speed == USB_SPEED_HIGH) ? &gateway_usb_ep_in_hs_desc : &gateway_usb_ep_in_fs_desc;
	out_desc = (speed == USB_SPEED_HIGH) ? &gateway_usb_ep_out_hs_desc : &gateway_usb_ep_out_fs_desc;

	if (ctx->configured) {
		gateway_usb_function_disable(function);
	}

	rc = usb_ep_enable(ctx->in_ep, in_desc);
	if (rc != 0) {
		printf("[gateway][usb] enable IN endpoint failed rc=%d\r\n", rc);
		return rc;
	}

	rc = usb_ep_enable(ctx->out_ep, out_desc);
	if (rc != 0) {
		printf("[gateway][usb] enable OUT endpoint failed rc=%d\r\n", rc);
		usb_ep_disable(ctx->in_ep);
		return rc;
	}

	if (ctx->in_req == NULL) {
		ctx->in_req = usb_ep_alloc_request(ctx->in_ep, 0);
	}
	if (ctx->out_req == NULL) {
		ctx->out_req = usb_ep_alloc_request(ctx->out_ep, 0);
	}
	if (ctx->in_req == NULL || ctx->out_req == NULL) {
		printf("[gateway][usb] request allocation failed\r\n");
		gateway_usb_function_disable(function);
		return -1;
	}

	ctx->in_req->complete = gateway_usb_in_complete;
	ctx->in_req->context = ctx;
	ctx->in_req->buf = ctx->in_buf;
	ctx->in_req->dma = (dma_addr_t)ctx->in_buf;
	ctx->out_req->complete = gateway_usb_out_complete;
	ctx->out_req->context = ctx;
	ctx->out_req->buf = ctx->out_buf;
	ctx->out_req->dma = (dma_addr_t)ctx->out_buf;
	ctx->configured = 1;
	ctx->in_busy = 0;
	gateway_usb_configured = 1;

	printf("[gateway][usb] configured interface=%u alt=%u speed=%d\r\n",
	       interface, alt, speed);
	return gateway_usb_queue_out_request(ctx);
}

static int gateway_usb_function_get_alt(struct usb_function *function, unsigned interface)
{
	(void)function;
	(void)interface;
	return 0;
}

static int gateway_usb_config_bind(struct usb_configuration *config)
{
	memset(&gateway_usb_ctx, 0, sizeof(gateway_usb_ctx));
	gateway_usb_ctx.in_buf = gateway_usb_in_buf;
	gateway_usb_ctx.out_buf = gateway_usb_out_buf;
	gateway_usb_ctx.function.name = "gateway-bulk";
	gateway_usb_ctx.function.bind = gateway_usb_function_bind;
	gateway_usb_ctx.function.set_alt = gateway_usb_function_set_alt;
	gateway_usb_ctx.function.get_alt = gateway_usb_function_get_alt;
	gateway_usb_ctx.function.disable = gateway_usb_function_disable;
	gateway_usb_ctx.function.setup = gateway_usb_function_setup;
	return usb_add_function(config, &gateway_usb_ctx.function);
}

static struct usb_configuration gateway_usb_config_driver = {
	.label = "gateway-bulk",
	.iConfiguration = GATEWAY_USB_STRING_CONFIG,
	.bConfigurationValue = 1,
	.bmAttributes = 0x80,
	.MaxPower = 100,
};

static int gateway_usb_bind(struct usb_composite_dev *cdev)
{
	int rc;

	rc = usb_add_config(cdev, &gateway_usb_config_driver, gateway_usb_config_bind);
	if (rc != 0) {
		printf("[gateway][usb] usb_add_config failed rc=%d\r\n", rc);
		return rc;
	}

	(void)usb_gadget_vbus_draw(cdev->gadget, 100);
	printf("[gateway][usb] composite bind done\r\n");
	return 0;
}

static void gateway_usb_disconnect(struct usb_composite_dev *cdev)
{
	(void)cdev;
	gateway_usb_configured = 0;
	gateway_usb_ctx.configured = 0;
	gateway_usb_ctx.in_busy = 0;
	printf("[gateway][usb] disconnected\r\n");
}

static void gateway_usb_suspend(struct usb_composite_dev *cdev)
{
	(void)cdev;
	gateway_usb_configured = 0;
	gateway_usb_ctx.configured = 0;
	gateway_usb_ctx.in_busy = 0;
	printf("[gateway][usb] suspended\r\n");
}

static void gateway_usb_resume(struct usb_composite_dev *cdev)
{
	(void)cdev;
	printf("[gateway][usb] resumed\r\n");
}

static struct usb_composite_driver gateway_usb_driver = {
	.name = "gateway-usb-ping",
	.dev = &gateway_usb_device_desc,
	.strings = gateway_usb_dev_strings,
	.max_speed = USB_SPEED_HIGH,
	.bind = gateway_usb_bind,
	.disconnect = gateway_usb_disconnect,
	.suspend = gateway_usb_suspend,
	.resume = gateway_usb_resume,
};

static int gateway_usb_start(void)
{
	int status;

	if (gateway_usb_started) {
		return 0;
	}

	printf("[gateway][usb] starting vendor bulk device\r\n");
	_usb_init();
	status = wait_usb_ready();
	if (status != USB_INIT_OK) {
		if (status == USB_NOT_ATTACHED) {
			printf("[gateway][usb] no USB host attached\r\n");
		} else {
			printf("[gateway][usb] USB init failed status=%d\r\n", status);
		}
		return -1;
	}

	status = usb_composite_probe(&gateway_usb_driver);
	if (status != 0) {
		printf("[gateway][usb] usb_composite_probe failed status=%d\r\n", status);
		return -1;
	}

	gateway_usb_started = 1;
	printf("[gateway][usb] vendor bulk device initialized vid=0x0BDA pid=0x8195\r\n");
	return 0;
}

#if GATEWAY_AMEBA_DIAG_STAGE_MINIMAL
static void gateway_diag_task(void *param)
{
	unsigned long heartbeat_counter = 0;
	TickType_t last_heartbeat_tick;
	int usb_start_rc;

	(void)param;

	dbg_printf("\r\n[gateway][diag] minimal task booted\r\n");
	dbg_printf("[gateway][diag] scheduler is alive tick=%lu heap=%lu\r\n",
		   (unsigned long)xTaskGetTickCount(),
		   (unsigned long)xPortGetFreeHeapSize());
	dbg_printf("[gateway][diag] starting USB bring-up from minimal task\r\n");

	vTaskDelay(pdMS_TO_TICKS(200));
	usb_start_rc = gateway_usb_start();
	dbg_printf("[gateway][diag] gateway_usb_start rc=%d\r\n", usb_start_rc);

	last_heartbeat_tick = xTaskGetTickCount();
	for (;;) {
		gateway_usb_try_force_configure();
		if ((xTaskGetTickCount() - last_heartbeat_tick) >= pdMS_TO_TICKS(GATEWAY_AMEBA_HEARTBEAT_MS)) {
			last_heartbeat_tick = xTaskGetTickCount();
			dbg_printf("[gateway][diag] heartbeat counter=%lu tick=%lu heap=%lu usb_started=%d usb_cfg=%d rx=%lu tx=%lu\r\n",
				   heartbeat_counter++,
				   (unsigned long)xTaskGetTickCount(),
				   (unsigned long)xPortGetFreeHeapSize(),
				   gateway_usb_started,
				   gateway_usb_configured,
				   gateway_usb_rx_packets,
				   gateway_usb_tx_packets);
			dbg_printf("[gateway][usbdbg] last_valid=%d actual=%lu magic=%02x %02x %02x %02x seq=%lu payload=%lu bytes=%02x %02x %02x %02x\r\n",
				   gateway_usb_last_ping_valid,
				   gateway_usb_last_actual,
				   gateway_usb_last_magic[0],
				   gateway_usb_last_magic[1],
				   gateway_usb_last_magic[2],
				   gateway_usb_last_magic[3],
				   gateway_usb_last_seq,
				   gateway_usb_last_payload_len,
				   gateway_usb_last_bytes[0],
				   gateway_usb_last_bytes[1],
				   gateway_usb_last_bytes[2],
				   gateway_usb_last_bytes[3]);
			dbg_printf("[gateway][usbdbg2] req=0x%08lx ctx=0x%08lx req[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x ctx[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
				   gateway_usb_last_req_buf_addr,
				   gateway_usb_last_ctx_buf_addr,
				   gateway_usb_last_req_dump[0],
				   gateway_usb_last_req_dump[1],
				   gateway_usb_last_req_dump[2],
				   gateway_usb_last_req_dump[3],
				   gateway_usb_last_req_dump[4],
				   gateway_usb_last_req_dump[5],
				   gateway_usb_last_req_dump[6],
				   gateway_usb_last_req_dump[7],
				   gateway_usb_last_ctx_dump[0],
				   gateway_usb_last_ctx_dump[1],
				   gateway_usb_last_ctx_dump[2],
				   gateway_usb_last_ctx_dump[3],
				   gateway_usb_last_ctx_dump[4],
				   gateway_usb_last_ctx_dump[5],
				   gateway_usb_last_ctx_dump[6],
				   gateway_usb_last_ctx_dump[7]);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
#endif

static void gateway_ameba_task(void *param)
{
	unsigned long heartbeat_counter = 0;
	TickType_t last_heartbeat_tick;

	(void)param;

	dbg_printf("\r\n[gateway] AmebaPro gateway firmware booted\r\n");
	dbg_printf("[gateway] UART log is alive\r\n");
	dbg_printf("[gateway] FreeRTOS tick=%lu heap=%lu\r\n",
		   (unsigned long)xTaskGetTickCount(),
		   (unsigned long)xPortGetFreeHeapSize());

	(void)gateway_usb_start();
	last_heartbeat_tick = xTaskGetTickCount();

	for (;;) {
		gateway_usb_try_force_configure();
		if ((xTaskGetTickCount() - last_heartbeat_tick) >= pdMS_TO_TICKS(GATEWAY_AMEBA_HEARTBEAT_MS)) {
			last_heartbeat_tick = xTaskGetTickCount();
			dbg_printf("[gateway] heartbeat counter=%lu tick=%lu heap=%lu usb_started=%d usb_cfg=%d rx=%lu tx=%lu\r\n",
				   heartbeat_counter++,
				   (unsigned long)xTaskGetTickCount(),
				   (unsigned long)xPortGetFreeHeapSize(),
				   gateway_usb_started,
				   gateway_usb_configured,
				   gateway_usb_rx_packets,
				   gateway_usb_tx_packets);
			dbg_printf("[gateway][usbdbg] last_valid=%d actual=%lu magic=%02x %02x %02x %02x seq=%lu payload=%lu bytes=%02x %02x %02x %02x\r\n",
				   gateway_usb_last_ping_valid,
				   gateway_usb_last_actual,
				   gateway_usb_last_magic[0],
				   gateway_usb_last_magic[1],
				   gateway_usb_last_magic[2],
				   gateway_usb_last_magic[3],
				   gateway_usb_last_seq,
				   gateway_usb_last_payload_len,
				   gateway_usb_last_bytes[0],
				   gateway_usb_last_bytes[1],
				   gateway_usb_last_bytes[2],
				   gateway_usb_last_bytes[3]);
			dbg_printf("[gateway][usbdbg2] req=0x%08lx ctx=0x%08lx req[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x ctx[0..7]=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
				   gateway_usb_last_req_buf_addr,
				   gateway_usb_last_ctx_buf_addr,
				   gateway_usb_last_req_dump[0],
				   gateway_usb_last_req_dump[1],
				   gateway_usb_last_req_dump[2],
				   gateway_usb_last_req_dump[3],
				   gateway_usb_last_req_dump[4],
				   gateway_usb_last_req_dump[5],
				   gateway_usb_last_req_dump[6],
				   gateway_usb_last_req_dump[7],
				   gateway_usb_last_ctx_dump[0],
				   gateway_usb_last_ctx_dump[1],
				   gateway_usb_last_ctx_dump[2],
				   gateway_usb_last_ctx_dump[3],
				   gateway_usb_last_ctx_dump[4],
				   gateway_usb_last_ctx_dump[5],
				   gateway_usb_last_ctx_dump[6],
				   gateway_usb_last_ctx_dump[7]);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void example_gateway_ameba(void)
{
#if GATEWAY_AMEBA_DIAG_STAGE_MINIMAL
	dbg_printf("[gateway][diag] staging minimal task only\r\n");
	if (xTaskCreate(gateway_diag_task,
			"gw_diag",
			GATEWAY_AMEBA_TASK_STACK_SIZE,
			NULL,
			GATEWAY_AMEBA_TASK_PRIORITY,
			NULL) != pdPASS) {
		dbg_printf("[gateway][diag] failed to create minimal task\r\n");
	}
#else
	if (xTaskCreate(gateway_ameba_task,
			"gateway_ameba",
			GATEWAY_AMEBA_TASK_STACK_SIZE,
			NULL,
			GATEWAY_AMEBA_TASK_PRIORITY,
			NULL) != pdPASS) {
		dbg_printf("[gateway] failed to create gateway task\r\n");
	}
#endif
}
