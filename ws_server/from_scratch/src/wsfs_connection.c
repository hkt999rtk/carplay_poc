#include <stdlib.h>
#include <string.h>

#include "wsfs_internal.h"
#include <utf8.h>

#define WSFS_INITIAL_QUEUE_CAP 4

static wsfs_status_t wsfs_buffer_reserve(wsfs_buffer_t *buffer,
					      size_t capacity)
{
	if (buffer->capacity >= capacity)
		return WSFS_STATUS_OK;

	uint8_t *next = wsfs_realloc_or_free(buffer->data,
					      capacity * sizeof(uint8_t));
	if (next == NULL)
		return WSFS_STATUS_ALLOCATION_FAILED;

	buffer->data = next;
	buffer->capacity = capacity;
	return WSFS_STATUS_OK;
}

static void wsfs_send_queue_clear(wsfs_connection_impl_t *conn)
{
	if (conn->send_queue.frames == NULL)
		return;

	for (size_t i = 0; i < conn->send_queue.count; ++i)
		wsfs_realloc_or_free(conn->send_queue.frames[i].data, 0);

	wsfs_realloc_or_free(conn->send_queue.frames, 0);
	conn->send_queue.frames = NULL;
	conn->send_queue.count = 0;
	conn->send_queue.capacity = 0;
	conn->send_queue.queued_bytes = 0;
}

wsfs_status_t wsfs_connection_init(wsfs_connection_impl_t *conn,
	   wsfs_server_impl_t *server, wsfs_socket_t fd)
{
	if (conn == NULL || server == NULL) {
		WSFS_DEBUG_PRINTF("[wsfs] connection_init invalid argument\n");
		return WSFS_STATUS_INVALID_ARGUMENT;
	}

	memset(conn, 0, sizeof(*conn));
	conn->api.impl = conn;
	conn->fd = fd;
	conn->state = WSFS_CONN_HANDSHAKE;
	conn->server = server;
	conn->slot_index = 0;
	conn->peer_port = 0;
	conn->user_data = NULL;

	wsfs_status_t status =
		wsfs_frame_reader_init(&conn->frame_reader, 0);
	if (status != WSFS_STATUS_OK) {
		WSFS_DEBUG_PRINTF("[wsfs] frame_reader_init failed status=%d\n",
				 (int)status);
		return status;
	}

	return WSFS_STATUS_OK;
}

void wsfs_connection_deinit(wsfs_connection_impl_t *conn)
{
	if (conn == NULL)
		return;

	wsfs_realloc_or_free(conn->handshake_buffer.data, 0);
	wsfs_realloc_or_free(conn->frame_reader.payload.data, 0);
	wsfs_realloc_or_free(conn->message_buffer.data, 0);

	wsfs_send_queue_clear(conn);

	if (conn->fd != WSFS_INVALID_SOCKET)
		wsfs_socket_close(conn->fd);

	memset(conn, 0, sizeof(*conn));
}

static wsfs_status_t wsfs_send_queue_reserve(wsfs_connection_impl_t *conn,
					     size_t new_capacity)
{
	if (conn->send_queue.capacity >= new_capacity)
		return WSFS_STATUS_OK;

	wsfs_send_frame_t *frames =
		(wsfs_send_frame_t *)wsfs_realloc_or_free(
			conn->send_queue.frames,
			new_capacity * sizeof(wsfs_send_frame_t));
	if (frames == NULL) {
		WSFS_DEBUG_PRINTF("[wsfs] send_queue_reserve alloc failed new_capacity=%zu\n",
				 new_capacity);
		return WSFS_STATUS_ALLOCATION_FAILED;
	}

	conn->send_queue.frames = frames;
	conn->send_queue.capacity = new_capacity;
	return WSFS_STATUS_OK;
}

wsfs_status_t wsfs_enqueue_raw(wsfs_connection_impl_t *conn,
	       const uint8_t *data, size_t length)
{
	if (conn == NULL || conn->server == NULL) {
		WSFS_DEBUG_PRINTF("[wsfs] enqueue_raw invalid argument\n");
		return WSFS_STATUS_INVALID_ARGUMENT;
	}
	if (length == 0)
		return WSFS_STATUS_OK;

	size_t new_total = conn->send_queue.queued_bytes + length;
	size_t cap = WSFS_DEFAULT_SEND_QUEUE_CAP;
	if (new_total > cap) {
		WSFS_DEBUG_PRINTF(
			"[wsfs] enqueue_raw overflow queued=%zu attempt=%zu cap=%zu\n",
			conn->send_queue.queued_bytes, length, cap);
		return WSFS_STATUS_PROTOCOL_ERROR;
	}

	if (conn->send_queue.count == conn->send_queue.capacity) {
		size_t next_capacity = conn->send_queue.capacity == 0
			         ? WSFS_INITIAL_QUEUE_CAP
			         : conn->send_queue.capacity * 2;
		wsfs_status_t reserve =
			wsfs_send_queue_reserve(conn, next_capacity);
		if (reserve != WSFS_STATUS_OK) {
			WSFS_DEBUG_PRINTF(
				"[wsfs] enqueue_raw reserve failed status=%d\n",
				(int)reserve);
			return reserve;
		}
	}

	int flush_immediately = conn->send_queue.count == 0;

	uint8_t *frame = (uint8_t *)malloc(length);
	if (frame == NULL) {
		WSFS_DEBUG_PRINTF(
			"[wsfs] enqueue_raw malloc failed length=%zu\n", length);
		return WSFS_STATUS_ALLOCATION_FAILED;
	}

	memcpy(frame, data, length);

	size_t index = conn->send_queue.count++;
	conn->send_queue.frames[index].data = frame;
	conn->send_queue.frames[index].length = length;
	conn->send_queue.frames[index].offset = 0;
	conn->send_queue.queued_bytes = new_total;

	wsfs_update_poll_events(conn->server, conn);

	if (flush_immediately && conn->state == WSFS_CONN_OPEN &&
	    conn->fd != WSFS_INVALID_SOCKET) {
		wsfs_status_t flush_status =
			wsfs_connection_flush_pending(conn, NULL);
		if (flush_status != WSFS_STATUS_OK &&
		    flush_status != WSFS_STATUS_PROTOCOL_ERROR) {
			WSFS_DEBUG_PRINTF(
				"[wsfs] enqueue_raw flush failed status=%d\n",
				(int)flush_status);
			return flush_status;
		}
		/* Flush may report WSFS_STATUS_PROTOCOL_ERROR if a close handshake
		 * completes; treat that as success for the caller. */
	}
	return WSFS_STATUS_OK;
}

void wsfs_update_poll_events(wsfs_server_impl_t *server,
	     wsfs_connection_impl_t *conn)
{
	if (server == NULL || conn == NULL)
		return;
	if (conn->slot_index >= server->slot_count)
		return;

	wsfs_slot_t *slot = &server->slots[conn->slot_index];
	slot->wants_write = conn->send_queue.count > 0 ? 1 : 0;
	wsfs_refresh_fd_sets(server);
}

wsfs_status_t wsfs_connection_flush_pending(wsfs_connection_impl_t *conn,
				  uint16_t *close_code)
{
	if (conn == NULL || conn->server == NULL) {
		WSFS_DEBUG_PRINTF("[wsfs] flush_pending invalid argument\n");
		return WSFS_STATUS_INVALID_ARGUMENT;
	}
	if (conn->fd == WSFS_INVALID_SOCKET)
		return WSFS_STATUS_OK;

	while (conn->send_queue.count > 0) {
		wsfs_send_frame_t *frame = &conn->send_queue.frames[0];
		size_t remaining = frame->length - frame->offset;
		ssize_t sent = send(conn->fd, frame->data + frame->offset,
				 remaining, MSG_NOSIGNAL);
		if (sent < 0) {
			int err = wsfs_socket_errno();
			if (err == EINTR)
				continue;
			if (err == EAGAIN || err == EWOULDBLOCK)
				break;
			WSFS_DEBUG_PRINTF(
				"[wsfs] flush_pending send error err=%d queued=%zu\n",
				err, conn->send_queue.queued_bytes);
			return WSFS_STATUS_IO_ERROR;
		}
		if (sent == 0)
			break;

		frame->offset += (size_t)sent;
		size_t queued_before = conn->send_queue.queued_bytes;
		if (queued_before < (size_t)sent) {
			WSFS_DEBUG_PRINTF(
				"[wsfs] flush_pending underflow guard queued=%zu sent=%zd frame_len=%zu offset=%zu\n",
				queued_before, sent, frame->length, frame->offset);
			conn->send_queue.queued_bytes = 0;
		} else {
			conn->send_queue.queued_bytes = queued_before - (size_t)sent;
		}

		if (frame->offset == frame->length) {
			wsfs_realloc_or_free(frame->data, 0);
			if (conn->send_queue.count > 1) {
				memmove(conn->send_queue.frames,
					conn->send_queue.frames + 1,
					(--conn->send_queue.count) *
					 sizeof(wsfs_send_frame_t));
			} else {
				conn->send_queue.count = 0;
			}
		} else {
			break;
		}
	}

	wsfs_update_poll_events(conn->server, conn);

	if (conn->state == WSFS_CONN_CLOSING && conn->send_queue.count == 0) {
		conn->state = WSFS_CONN_CLOSED;
		if (close_code != NULL)
			*close_code = conn->close_received ? 1000 : 1006;
		WSFS_DEBUG_PRINTF("[wsfs] flush_pending closing handshake complete\n");
		return WSFS_STATUS_PROTOCOL_ERROR;
	}

	return WSFS_STATUS_OK;
}

static wsfs_status_t wsfs_build_frame(wsfs_connection_impl_t *conn,
				       wsfs_opcode_t opcode, uint8_t fin,
				       const uint8_t *payload, size_t length,
				       int is_control)
{
	if (conn == NULL) {
		WSFS_DEBUG_PRINTF("[wsfs] build_frame invalid connection\n");
		return WSFS_STATUS_INVALID_ARGUMENT;
	}

	if (is_control && length > 125) {
		WSFS_DEBUG_PRINTF(
			"[wsfs] build_frame control payload too large length=%zu\n",
			length);
		return WSFS_STATUS_PROTOCOL_ERROR;
	}

	size_t header = 2;
	uint8_t len_code;
	if (length <= 125) {
		len_code = (uint8_t)length;
	} else if (length <= 0xFFFF) {
		header += 2;
		len_code = 126;
	} else {
		header += 8;
		len_code = 127;
	}

	size_t total = header + length;
	uint8_t *frame = (uint8_t *)malloc(total);
	if (frame == NULL) {
		WSFS_DEBUG_PRINTF(
			"[wsfs] build_frame malloc failed total=%zu\n", total);
		return WSFS_STATUS_ALLOCATION_FAILED;
	}

	frame[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
	frame[1] = len_code;
	size_t offset = 2;
	if (len_code == 126) {
		frame[offset++] = (uint8_t)((length >> 8) & 0xFF);
		frame[offset++] = (uint8_t)(length & 0xFF);
	} else if (len_code == 127) {
		uint64_t value = (uint64_t)length;
		for (int i = 7; i >= 0; --i)
			frame[offset++] = (uint8_t)((value >> (i * 8)) & 0xFF);
	}

	if (length > 0 && payload != NULL)
		memcpy(frame + offset, payload, length);

	do {
		wsfs_status_t status = wsfs_enqueue_raw(conn, frame, total);
		if (status != WSFS_STATUS_OK) {
			WSFS_DEBUG_PRINTF(
				"[wsfs] build_frame enqueue failed status=%d total=%zu\n",
				(int)status, total);
			wsfs_realloc_or_free(frame, 0);
			return status;
		}
	} while (0);

	wsfs_realloc_or_free(frame, 0);
	return WSFS_STATUS_OK;
}

wsfs_status_t wsfs_connection_queue_frame(wsfs_connection_impl_t *conn,
					  wsfs_opcode_t opcode, uint8_t fin,
					  const uint8_t *payload, size_t length,
					  int is_control)
{
	return wsfs_build_frame(conn, opcode, fin, payload, length, is_control);
}

wsfs_status_t wsfs_connection_queue_close(wsfs_connection_impl_t *conn,
					  uint16_t close_code)
{
	uint8_t payload[2];
	size_t length = 0;
	if (close_code != 0) {
		payload[0] = (uint8_t)((close_code >> 8) & 0xFF);
		payload[1] = (uint8_t)(close_code & 0xFF);
		length = 2;
	}

	wsfs_status_t status =
		wsfs_connection_queue_frame(conn, WSFS_OPCODE_CLOSE, 1,
				payload, length, 1);
	if (status == WSFS_STATUS_OK) {
		conn->close_sent = 1;
		if (conn->state != WSFS_CONN_CLOSING &&
		    conn->state != WSFS_CONN_CLOSED)
			conn->state = WSFS_CONN_CLOSING;
	} else {
		WSFS_DEBUG_PRINTF(
			"[wsfs] queue_close failed status=%d close_code=%u\n",
			(int)status, (unsigned)close_code);
	}

	return status;
}
