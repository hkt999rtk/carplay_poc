#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ws.h"
#include "wsfs_server.h"
#include "wsfs_internal.h"

typedef struct ws_server_bridge {
	wsfs_server_t server;
	wsfs_server_impl_t *impl;
	struct ws_events events;
	struct ws_server_bridge *next;
} ws_server_bridge_t;

struct ws_connection {
	wsfs_connection_t *conn;
	wsfs_server_impl_t *server_impl;
	void *user_data;
	char peer_ip[64];
	int state;
	struct ws_connection *next;
};

static pthread_mutex_t g_wsfs_bridge_lock = PTHREAD_MUTEX_INITIALIZER;
static ws_server_bridge_t *g_wsfs_servers = NULL;
static ws_cli_conn_t *g_wsfs_clients = NULL;

static ws_server_bridge_t *wsfs_bridge_find_server(wsfs_server_impl_t *impl)
{
	ws_server_bridge_t *cur = g_wsfs_servers;

	while (cur != NULL) {
		if (cur->impl == impl)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static ws_cli_conn_t *wsfs_bridge_find_client(wsfs_connection_t *conn)
{
	ws_cli_conn_t *cur = g_wsfs_clients;

	while (cur != NULL) {
		if (cur->conn == conn)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static void wsfs_bridge_register_server(ws_server_bridge_t *bridge)
{
	pthread_mutex_lock(&g_wsfs_bridge_lock);
	bridge->next = g_wsfs_servers;
	g_wsfs_servers = bridge;
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
}

static void wsfs_bridge_unregister_server(ws_server_bridge_t *bridge)
{
	ws_server_bridge_t **link = &g_wsfs_servers;

	pthread_mutex_lock(&g_wsfs_bridge_lock);
	while (*link != NULL) {
		if (*link == bridge) {
			*link = bridge->next;
			break;
		}
		link = &(*link)->next;
	}
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
}

static void wsfs_bridge_register_client(ws_cli_conn_t *client)
{
	pthread_mutex_lock(&g_wsfs_bridge_lock);
	client->next = g_wsfs_clients;
	g_wsfs_clients = client;
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
}

static void wsfs_bridge_unregister_client(ws_cli_conn_t *client)
{
	ws_cli_conn_t **link = &g_wsfs_clients;

	pthread_mutex_lock(&g_wsfs_bridge_lock);
	while (*link != NULL) {
		if (*link == client) {
			*link = client->next;
			break;
		}
		link = &(*link)->next;
	}
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
}

static ws_server_bridge_t *wsfs_bridge_server_for_conn(wsfs_connection_t *conn)
{
	wsfs_connection_impl_t *impl = wsfs_connection_get_impl(conn);

	if (impl == NULL)
		return NULL;

	pthread_mutex_lock(&g_wsfs_bridge_lock);
	ws_server_bridge_t *bridge = wsfs_bridge_find_server(impl->server);
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
	return bridge;
}

static ws_cli_conn_t *wsfs_bridge_client_for_conn(wsfs_connection_t *conn)
{
	pthread_mutex_lock(&g_wsfs_bridge_lock);
	ws_cli_conn_t *client = wsfs_bridge_find_client(conn);
	pthread_mutex_unlock(&g_wsfs_bridge_lock);
	return client;
}

static int wsfs_bridge_msg_type(wsfs_opcode_t opcode)
{
	switch (opcode) {
	case WSFS_OPCODE_TEXT:
		return WS_FR_OP_TXT;
	case WSFS_OPCODE_BINARY:
		return WS_FR_OP_BIN;
	case WSFS_OPCODE_CLOSE:
		return WS_FR_OP_CLSE;
	case WSFS_OPCODE_PING:
		return WS_FR_OP_PING;
	case WSFS_OPCODE_PONG:
		return WS_FR_OP_PONG;
	default:
		return WS_FR_OP_UNSUPPORTED;
	}
}

static void wsfs_bridge_on_open(wsfs_connection_t *conn)
{
	ws_server_bridge_t *bridge = wsfs_bridge_server_for_conn(conn);
	ws_cli_conn_t *client;
	const char *peer_ip;

	if (bridge == NULL)
		return;

	client = (ws_cli_conn_t *)calloc(1, sizeof(*client));
	if (client == NULL)
		return;

	client->conn = conn;
	client->state = WS_STATE_OPEN;
	client->server_impl = wsfs_connection_get_impl(conn)->server;
	peer_ip = wsfs_connection_peer_ip(conn);
	if (peer_ip != NULL)
		snprintf(client->peer_ip, sizeof(client->peer_ip), "%s", peer_ip);
	else
		client->peer_ip[0] = '\0';

	wsfs_bridge_register_client(client);

	if (bridge->events.onopen != NULL)
		bridge->events.onopen(client);
}

static void wsfs_bridge_on_message(wsfs_connection_t *conn,
				   const wsfs_message_view_t *msg)
{
	ws_server_bridge_t *bridge;
	ws_cli_conn_t *client;
	int type;

	if (msg == NULL)
		return;

	bridge = wsfs_bridge_server_for_conn(conn);
	client = wsfs_bridge_client_for_conn(conn);
	if (bridge == NULL || client == NULL)
		return;

	type = wsfs_bridge_msg_type(msg->opcode);
	if (type == WS_FR_OP_UNSUPPORTED)
		return;

	if (bridge->events.onmessage != NULL)
		bridge->events.onmessage(client, msg->data, (uint64_t)msg->length, type);
}

static void wsfs_bridge_on_close(wsfs_connection_t *conn, uint16_t close_code)
{
	ws_server_bridge_t *bridge = wsfs_bridge_server_for_conn(conn);
	ws_cli_conn_t *client = wsfs_bridge_client_for_conn(conn);

	(void)close_code;

	if (client == NULL)
		return;

	client->state = WS_STATE_CLOSED;
	if (bridge != NULL && bridge->events.onclose != NULL)
		bridge->events.onclose(client);

	wsfs_bridge_unregister_client(client);
	free(client);
}

char *ws_getaddress(ws_cli_conn_t *client)
{
	if (client == NULL)
		return NULL;
	return client->peer_ip;
}

void set_userdata(ws_cli_conn_t *client, void *user_data)
{
	if (client == NULL)
		return;
	client->user_data = user_data;
}

void *get_userdata(ws_cli_conn_t *client)
{
	if (client == NULL)
		return NULL;
	return client->user_data;
}

int ws_sendframe(ws_cli_conn_t *client, const char *msg, uint64_t size, int type)
{
	wsfs_status_t status;

	if (client == NULL || client->conn == NULL)
		return -1;

	switch (type) {
	case WS_FR_OP_TXT:
		status = wsfs_connection_send_text(client->conn, msg, (size_t)size);
		break;
	case WS_FR_OP_BIN:
		status = wsfs_connection_send_binary(
			client->conn, (const uint8_t *)msg, (size_t)size);
		break;
	case WS_FR_OP_CLSE:
		status = wsfs_connection_close(client->conn, WS_CLSE_NORMAL);
		client->state = WS_STATE_CLOSING;
		break;
	default:
		return -1;
	}

	return status == WSFS_STATUS_OK ? 0 : -1;
}

int ws_sendframe_txt(ws_cli_conn_t *client, const char *msg)
{
	if (msg == NULL)
		return -1;
	return ws_sendframe(client, msg, strlen(msg), WS_FR_OP_TXT);
}

int ws_sendframe_bin(ws_cli_conn_t *client, const char *msg, uint64_t size)
{
	return ws_sendframe(client, msg, size, WS_FR_OP_BIN);
}

int ws_get_state(ws_cli_conn_t *client)
{
	if (client == NULL)
		return WS_STATE_CLOSED;
	return client->state;
}

int ws_close_client(ws_cli_conn_t *client)
{
	if (client == NULL || client->conn == NULL)
		return -1;
	client->state = WS_STATE_CLOSING;
	return wsfs_connection_close(client->conn, WS_CLSE_NORMAL) == WSFS_STATUS_OK ? 0 : -1;
}

void ws_ping(ws_cli_conn_t *cli, int threshold)
{
	(void)cli;
	(void)threshold;
}

int ws_socket(struct ws_events *evs, uint16_t port, int thread_loop,
	      uint32_t timeout_ms)
{
	ws_server_bridge_t bridge;
	wsfs_server_config_t config;
	wsfs_status_t status;

	(void)thread_loop;
	(void)timeout_ms;

	memset(&bridge, 0, sizeof(bridge));
	if (evs != NULL)
		bridge.events = *evs;

	wsfs_server_config_defaults(&config);
	config.port = port;
	config.max_clients = MAX_CLIENTS;
	config.recv_buffer_size = MESSAGE_LENGTH;
	config.max_frame_size = MAX_FRAME_LENGTH;
	config.callbacks.on_open = wsfs_bridge_on_open;
	config.callbacks.on_message = wsfs_bridge_on_message;
	config.callbacks.on_close = wsfs_bridge_on_close;

	status = wsfs_server_init(&bridge.server, &config);
	if (status != WSFS_STATUS_OK)
		return -1;

	bridge.impl = (wsfs_server_impl_t *)bridge.server.impl;
	wsfs_bridge_register_server(&bridge);
	status = wsfs_server_run(&bridge.server);
	wsfs_bridge_unregister_server(&bridge);
	wsfs_server_deinit(&bridge.server);

	return status == WSFS_STATUS_OK ? 0 : -1;
}
