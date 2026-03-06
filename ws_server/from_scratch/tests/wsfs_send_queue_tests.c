#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wsfs_internal.h"

static ssize_t test_send_return = -1;
static int test_send_call_count = 0;

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	(void)fd;
	(void)buf;
	(void)flags;
	++test_send_call_count;
	if (test_send_return >= 0)
		return test_send_return;
	return (ssize_t)len;
}

static void initialize_server(wsfs_server_t *server,
			      wsfs_server_impl_t **impl_out)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);
	cfg.max_clients = 1;
	cfg.port = 19092;

	assert(wsfs_server_init(server, &cfg) == WSFS_STATUS_OK);

	wsfs_server_impl_t *impl = wsfs_server_get_impl(server);
	assert(impl != NULL);

	/* prepare fake listener slot */
	impl->slot_count = cfg.max_clients + 1;
	assert(impl->slot_count >= 2);
	impl->slots[0].fd = WSFS_INVALID_SOCKET;
	impl->slots[0].conn = NULL;
	impl->slots[0].wants_write = 0;
	wsfs_refresh_fd_sets(impl);

	if (impl_out)
		*impl_out = impl;
}

static void test_queue_basic(void)
{
	wsfs_server_t server = {0};
	wsfs_server_impl_t *impl = NULL;
	initialize_server(&server, &impl);

	wsfs_connection_impl_t conn;
	assert(wsfs_connection_init(&conn, impl, WSFS_INVALID_SOCKET) ==
	       WSFS_STATUS_OK);
	conn.slot_index = 1;
	impl->slots[conn.slot_index].fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].wants_write = 0;
	wsfs_refresh_fd_sets(impl);

	const char *payload = "queue-test";
	wsfs_status_t st = wsfs_connection_queue_frame(
		&conn, WSFS_OPCODE_TEXT, 1, (const uint8_t *)payload,
		strlen(payload), 0);
	assert(st == WSFS_STATUS_OK);
	assert(conn.send_queue.count == 1);
	assert(conn.send_queue.frames[0].length ==
	       2 + strlen(payload)); /* header + payload */
	assert(impl->slots[conn.slot_index].wants_write == 1);

	wsfs_connection_deinit(&conn);
	wsfs_server_deinit(&server);
}

static void test_queue_overflow(void)
{
	wsfs_server_t server = {0};
	wsfs_server_impl_t *impl = NULL;
	initialize_server(&server, &impl);

	wsfs_connection_impl_t conn;
	assert(wsfs_connection_init(&conn, impl, WSFS_INVALID_SOCKET) ==
	       WSFS_STATUS_OK);
	conn.slot_index = 1;

	size_t big_len = WSFS_DEFAULT_SEND_QUEUE_CAP + 1;
	uint8_t *payload = (uint8_t *)malloc(big_len);
	assert(payload != NULL);

	wsfs_status_t st = wsfs_connection_queue_frame(
		&conn, WSFS_OPCODE_BINARY, 1, payload, big_len, 0);
	assert(st == WSFS_STATUS_PROTOCOL_ERROR);
	assert(conn.send_queue.count == 0);

	free(payload);
	wsfs_connection_deinit(&conn);
	wsfs_server_deinit(&server);
}

static void test_close_helper(void)
{
	wsfs_server_t server = {0};
	wsfs_server_impl_t *impl = NULL;
	initialize_server(&server, &impl);

	wsfs_connection_impl_t conn;
	assert(wsfs_connection_init(&conn, impl, WSFS_INVALID_SOCKET) ==
	       WSFS_STATUS_OK);
	conn.slot_index = 1;
	conn.state = WSFS_CONN_OPEN;
	impl->slots[conn.slot_index].fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].wants_write = 0;
	wsfs_refresh_fd_sets(impl);

	wsfs_status_t st = wsfs_connection_queue_close(&conn, 1000);
	assert(st == WSFS_STATUS_OK);
	assert(conn.close_sent == 1);
	assert(conn.state == WSFS_CONN_CLOSING);
	assert(conn.send_queue.count == 1);

	wsfs_connection_deinit(&conn);
	wsfs_server_deinit(&server);
}

static void test_flush_underflow_guard(void)
{
	wsfs_server_t server = {0};
	wsfs_server_impl_t *impl = NULL;
	initialize_server(&server, &impl);

	wsfs_connection_impl_t conn;
	assert(wsfs_connection_init(&conn, impl, WSFS_INVALID_SOCKET) ==
	       WSFS_STATUS_OK);
	conn.slot_index = 1;
	conn.state = WSFS_CONN_OPEN;
	conn.fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].conn = &conn;
	impl->slots[conn.slot_index].wants_write = 0;
	wsfs_refresh_fd_sets(impl);

	const uint8_t payload[] = {0x01, 0x02, 0x03};
	wsfs_status_t st = wsfs_connection_queue_frame(&conn, WSFS_OPCODE_BINARY,
						     1, payload,
						     sizeof(payload), 0);
	assert(st == WSFS_STATUS_OK);
	assert(conn.send_queue.count == 1);

	size_t frame_len = conn.send_queue.frames[0].length;
	assert(frame_len > 0);

	conn.send_queue.queued_bytes = 1;
	conn.fd = 123;
	impl->slots[conn.slot_index].fd = conn.fd;
	wsfs_refresh_fd_sets(impl);
	test_send_return = (ssize_t)frame_len;
	test_send_call_count = 0;

	st = wsfs_connection_flush_pending(&conn, NULL);
	assert(st == WSFS_STATUS_OK);
	assert(test_send_call_count == 1);
	assert(conn.send_queue.count == 0);
	assert(conn.send_queue.queued_bytes == 0);

	test_send_return = -1;
	test_send_call_count = 0;
	conn.fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].fd = WSFS_INVALID_SOCKET;
	impl->slots[conn.slot_index].conn = NULL;
	wsfs_refresh_fd_sets(impl);

	wsfs_connection_deinit(&conn);
	wsfs_server_deinit(&server);
}

int main(void)
{
	test_queue_basic();
	test_queue_overflow();
	test_close_helper();
	test_flush_underflow_guard();

	printf("wsfs_send_queue_tests: all tests passed\n");
	return 0;
}
