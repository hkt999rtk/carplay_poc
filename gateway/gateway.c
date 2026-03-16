#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "cJSON.h"
#include "crypto_stream.h"
#include "gateway_proto.h"
#include "tcp_transport.h"
#include "ws_upstream_client.h"

typedef struct gateway_runtime {
	pthread_mutex_t lock;
	int session_active;
	const char *upstream_host;
	uint16_t upstream_port;
} gateway_runtime_t;

typedef struct session_arg {
	gateway_runtime_t *runtime;
	tcp_socket_t client_fd;
} session_arg_t;

static void gateway_set_active(gateway_runtime_t *runtime, int active)
{
	pthread_mutex_lock(&runtime->lock);
	runtime->session_active = active;
	pthread_mutex_unlock(&runtime->lock);
}

static int gateway_is_active(gateway_runtime_t *runtime)
{
	int active;

	pthread_mutex_lock(&runtime->lock);
	active = runtime->session_active;
	pthread_mutex_unlock(&runtime->lock);
	return active;
}

static uint8_t gateway_video_flags(const uint8_t *payload, size_t payload_len)
{
	uint8_t flags = 0;
	uint8_t nal_type;

	if (payload == NULL || payload_len == 0u)
		return 0;
	nal_type = (uint8_t)(payload[0] & 0x1Fu);
	if (nal_type == 5u)
		flags |= GATEWAY_PROTO_FLAG_KEYFRAME;
	if (nal_type == 7u || nal_type == 8u)
		flags |= GATEWAY_PROTO_FLAG_CONFIG;
	return flags;
}

static int gateway_parse_crypto_init(const uint8_t *payload, size_t payload_len,
				     uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE],
				     int *have_nonce)
{
	cJSON *root;
	cJSON *type;
	cJSON *result;
	cJSON *session_nonce_json;
	unsigned char *decoded;
	size_t decoded_len;
	char *text;

	text = (char *)malloc(payload_len + 1u);
	if (text == NULL)
		return -1;
	memcpy(text, payload, payload_len);
	text[payload_len] = '\0';

	root = cJSON_Parse(text);
	free(text);
	if (root == NULL)
		return 0;

	type = cJSON_GetObjectItem(root, "type");
	if (type == NULL || type->valuestring == NULL ||
	    strcmp(type->valuestring, "crypto_init") != 0) {
		cJSON_Delete(root);
		return 0;
	}

	result = cJSON_GetObjectItem(root, "result");
	session_nonce_json = result != NULL ? cJSON_GetObjectItem(result, "session_nonce") : NULL;
	if (session_nonce_json == NULL || session_nonce_json->valuestring == NULL) {
		cJSON_Delete(root);
		return -1;
	}

	decoded = base64_decode((const unsigned char *)session_nonce_json->valuestring,
				strlen(session_nonce_json->valuestring), &decoded_len);
	if (decoded == NULL || decoded_len != CRYPTO_STREAM_NONCE_SIZE) {
		free(decoded);
		cJSON_Delete(root);
		return -1;
	}

	memcpy(session_nonce, decoded, CRYPTO_STREAM_NONCE_SIZE);
	free(decoded);
	*have_nonce = 1;
	cJSON_Delete(root);
	return 0;
}

static void *gateway_session_main(void *arg)
{
	session_arg_t *session = (session_arg_t *)arg;
	gateway_runtime_t *runtime = session->runtime;
	tcp_socket_t client_fd = session->client_fd;
	ws_upstream_client_t upstream;
	uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE];
	int have_nonce = 0;
	int upstream_ready = 0;
	uint8_t init_buf[GATEWAY_PROTO_INIT_SIZE];
	gateway_proto_init_info_t init_info;

	memset(&upstream, 0, sizeof(upstream));
	upstream.fd = TCP_TRANSPORT_INVALID_SOCKET;

	gateway_proto_default_init(&init_info);
	if (gateway_proto_write_init(init_buf, &init_info) != 0)
		goto cleanup;

	if (ws_upstream_connect(&upstream, runtime->upstream_host, runtime->upstream_port, "/gateway") != 0) {
		fprintf(stderr, "gateway: failed to connect upstream\n");
		goto cleanup;
	}
	upstream_ready = 1;

	if (tcp_transport_write_all(client_fd, init_buf, sizeof(init_buf)) != 0)
		goto cleanup;
	if (ws_upstream_send_text(&upstream, "{\"cmd\":\"start_stream\",\"param\":{\"mirror\":1}}") != 0)
		goto cleanup;
	if (ws_upstream_send_text(&upstream, "{\"cmd\":\"start_audio_stream\"}") != 0)
		goto cleanup;

	for (;;) {
		fd_set rfds;
		tcp_socket_t upstream_fd = ws_upstream_fd(&upstream);
		tcp_socket_t max_fd = client_fd > upstream_fd ? client_fd : upstream_fd;
		int rc;

		FD_ZERO(&rfds);
		FD_SET((SOCKET)client_fd, &rfds);
		FD_SET((SOCKET)upstream_fd, &rfds);

#ifdef _WIN32
		rc = select(0, &rfds, NULL, NULL, NULL);
		if (rc < 0) {
			if (WSAGetLastError() == WSAEINTR)
				continue;
			break;
		}
#else
		rc = select(max_fd + 1, &rfds, NULL, NULL, NULL);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
#endif

		if (FD_ISSET((SOCKET)client_fd, &rfds)) {
			int status = tcp_transport_peek_status(client_fd);
			if (status <= 1)
				break;
		}

		if (FD_ISSET((SOCKET)upstream_fd, &rfds)) {
			uint8_t opcode = 0;
			uint8_t *payload = NULL;
			size_t payload_len = 0;

			if (ws_upstream_recv_frame(&upstream, &opcode, &payload, &payload_len) != 0) {
				free(payload);
				break;
			}

			if (opcode == 0x1u) {
				if (gateway_parse_crypto_init(payload, payload_len, session_nonce, &have_nonce) != 0) {
					free(payload);
					break;
				}
			} else if (opcode == 0x2u) {
				uint8_t stream_id = 0;
				uint32_t seq_no = 0;
				uint8_t *plaintext;
				int plain_len;
				gateway_proto_media_info_t media_info;
				uint8_t header[GATEWAY_PROTO_PACKET_HEADER_SIZE];

				if (!have_nonce) {
					free(payload);
					continue;
				}

				plaintext = (uint8_t *)malloc(payload_len);
				if (plaintext == NULL) {
					free(payload);
					break;
				}

				plain_len = crypto_stream_decrypt_packet(
					plaintext, payload_len, &stream_id, &seq_no,
					session_nonce, payload, payload_len);
				free(payload);
				payload = NULL;
				if (plain_len < 0) {
					free(plaintext);
					break;
				}

				media_info.stream_id = stream_id;
				media_info.flags = stream_id == GATEWAY_PROTO_STREAM_VIDEO ?
					gateway_video_flags(plaintext, (size_t)plain_len) : 0u;
				media_info.seq_no = seq_no;
				media_info.payload_len = (uint32_t)plain_len;

				if (gateway_proto_write_media_header(header, &media_info) != 0 ||
				    tcp_transport_write_all(client_fd, header, sizeof(header)) != 0 ||
				    tcp_transport_write_all(client_fd, plaintext, (size_t)plain_len) != 0) {
					free(plaintext);
					break;
				}
				free(plaintext);
			} else if (opcode == 0x8u) {
				free(payload);
				payload = NULL;
				break;
			}

			free(payload);
		}
	}

cleanup:
	if (upstream_ready) {
		ws_upstream_send_text(&upstream, "{\"cmd\":\"stop_stream\"}");
		ws_upstream_send_text(&upstream, "{\"cmd\":\"stop_audio_stream\"}");
		ws_upstream_send_close(&upstream, 1000u);
	}
	ws_upstream_close(&upstream);
	tcp_transport_shutdown_close(client_fd);
	gateway_set_active(runtime, 0);
	free(session);
	return NULL;
}

int main(int argc, char **argv)
{
	uint16_t listen_port = 19000u;
	uint16_t upstream_port = 8081u;
	const char *upstream_host = "127.0.0.1";
	tcp_socket_t listen_fd;
	gateway_runtime_t runtime;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
			listen_port = (uint16_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--upstream-port") == 0 && i + 1 < argc) {
			upstream_port = (uint16_t)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--upstream-host") == 0 && i + 1 < argc) {
			upstream_host = argv[++i];
		} else {
			fprintf(stderr,
				"usage: %s [--listen-port N] [--upstream-host HOST] [--upstream-port N]\n",
				argv[0]);
			return 2;
		}
	}

	memset(&runtime, 0, sizeof(runtime));
	pthread_mutex_init(&runtime.lock, NULL);
	runtime.upstream_host = upstream_host;
	runtime.upstream_port = upstream_port;

	listen_fd = tcp_transport_listen(listen_port, 8);
	if (listen_fd == TCP_TRANSPORT_INVALID_SOCKET) {
		perror("gateway listen");
		return 1;
	}

	printf("gateway: listening on %u, upstream %s:%u\n",
	       (unsigned)listen_port, upstream_host, (unsigned)upstream_port);

	for (;;) {
		tcp_socket_t client_fd = tcp_transport_accept(listen_fd);
		if (client_fd == TCP_TRANSPORT_INVALID_SOCKET) {
			perror("gateway accept");
			continue;
		}

		if (gateway_is_active(&runtime)) {
			fprintf(stderr, "gateway: rejecting extra client\n");
			tcp_transport_shutdown_close(client_fd);
			continue;
		}

		{
			pthread_t thread;
			session_arg_t *arg = (session_arg_t *)malloc(sizeof(*arg));
			if (arg == NULL) {
				tcp_transport_shutdown_close(client_fd);
				continue;
			}
			arg->runtime = &runtime;
			arg->client_fd = client_fd;
			gateway_set_active(&runtime, 1);
			if (pthread_create(&thread, NULL, gateway_session_main, arg) != 0) {
				gateway_set_active(&runtime, 0);
				tcp_transport_shutdown_close(client_fd);
				free(arg);
				continue;
			}
			pthread_detach(thread);
		}
	}

	return 0;
}
