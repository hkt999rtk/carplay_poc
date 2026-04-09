#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <ws.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <cJSON.h>
#include "crypto_stream.h"
#include "myvector.hpp"
#include "mystring.hpp"
#include "bitrate_ctrl.h"

extern "C" {
#include "base64.h"
#include "gateway_proto.h"
}

static bitrate_ctrl_t b_ctrl;

typedef void (*process_cmd_t)(ws_cli_conn_t *conn, cJSON *obj);

typedef struct app_cmd_t_ {
	const char *name;
	process_cmd_t func;
} app_cmd_t;

// user data here
enum {
	STATE_IDLE = 0x00,
	STATE_CLOSING_VIDEO = 0x01,
	STATE_STREAMING_VIDEO = 0x02,
	STATE_STREAMING_VIDEO2 = 0x04,
	STATE_CLOSING_AUDIO = 0x8,
	STATE_STREAMING_AUDIO = 0x10,
};

typedef struct chunk_t_ {
	uint8_t *start;
	size_t size;
} chunk_t;

typedef struct app_t_ {
	int state;
	pthread_t thread;
	ws_cli_conn_t *conn; // bidirectional link
	pthread_mutex_t crypto_lock;
	pthread_mutex_t tx_lock;
	pthread_mutex_t priority_lock;
	pthread_cond_t priority_cond;
	uint8_t session_nonce[CRYPTO_STREAM_NONCE_SIZE];
	uint32_t media_seq;
	uint32_t audio_chunk_index;
	uint32_t video_frame_id;
	unsigned int pending_audio_packets;
	int crypto_init_sent;
} app_t;

#define VIDEO_FRAGMENT_DATA_SIZE (16u * 1024u)
#define VIDEO_FRAGMENT_HEADER_SIZE 16u
#define AUDIO_CHUNK_SIZE (8u * 1024u)
#define AUDIO_BYTES_PER_SECOND 32000u

static void write_u32le(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value & 0xffu);
	out[1] = (uint8_t)((value >> 8) & 0xffu);
	out[2] = (uint8_t)((value >> 16) & 0xffu);
	out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void write_u16le(uint8_t *out, uint16_t value)
{
	out[0] = (uint8_t)(value & 0xffu);
	out[1] = (uint8_t)((value >> 8) & 0xffu);
}

static uint8_t video_payload_flags(const uint8_t *payload, size_t payload_len)
{
	uint8_t flags = 0;
	uint8_t nal_type;

	if (payload == NULL || payload_len == 0u)
		return 0u;
	nal_type = (uint8_t)(payload[0] & 0x1fu);
	if (nal_type == 5u)
		flags |= GATEWAY_PROTO_FLAG_KEYFRAME;
	if (nal_type == 7u || nal_type == 8u)
		flags |= GATEWAY_PROTO_FLAG_CONFIG;
	return flags;
}

static void begin_audio_priority(app_t *app)
{
	pthread_mutex_lock(&app->priority_lock);
	++app->pending_audio_packets;
	pthread_cond_broadcast(&app->priority_cond);
	pthread_mutex_unlock(&app->priority_lock);
}

static void end_audio_priority(app_t *app)
{
	pthread_mutex_lock(&app->priority_lock);
	if (app->pending_audio_packets > 0u)
		--app->pending_audio_packets;
	pthread_cond_broadcast(&app->priority_cond);
	pthread_mutex_unlock(&app->priority_lock);
}

static void wait_for_audio_priority(app_t *app)
{
	pthread_mutex_lock(&app->priority_lock);
	while (app->pending_audio_packets > 0u)
		pthread_cond_wait(&app->priority_cond, &app->priority_lock);
	pthread_mutex_unlock(&app->priority_lock);
}

static int send_crypto_init_locked(app_t *app)
{
	unsigned char *encoded = NULL;
	char *clean = NULL;
	size_t encoded_len = 0;
	size_t clean_len = 0;
	char json[256];

	if (app->crypto_init_sent)
		return 0;

	encoded = base64_encode(app->session_nonce, CRYPTO_STREAM_NONCE_SIZE, &encoded_len);
	if (encoded == NULL)
		return -1;

	clean = (char *)malloc(encoded_len + 1u);
	if (clean == NULL) {
		free(encoded);
		return -1;
	}
	for (size_t i = 0; i < encoded_len; ++i) {
		if (encoded[i] != '\n' && encoded[i] != '\r')
			clean[clean_len++] = (char)encoded[i];
	}
	clean[clean_len] = '\0';
	snprintf(json, sizeof(json),
		 "{\"type\":\"crypto_init\",\"result\":{\"status\":\"ok\",\"cipher\":\"chacha20\",\"protocol_version\":1,\"key_id\":\"carplay-poc-static-v1\",\"session_nonce\":\"%s\"}}",
		 clean);
	free(clean);
	free(encoded);

	if (ws_sendframe(app->conn, json, strlen(json), WS_FR_OP_TXT) < 0)
		return -1;
	app->crypto_init_sent = 1;
	return 0;
}

static int send_encrypted_media(app_t *app, uint8_t stream_id,
				const uint8_t *payload, size_t payload_len)
{
	uint8_t *packet;
	size_t packet_size;
	uint32_t seq_no;
	int encoded_size;
	int rc;

	if (app == NULL || payload == NULL)
		return -1;

	if (stream_id == CRYPTO_STREAM_ID_AUDIO)
		begin_audio_priority(app);
	else
		wait_for_audio_priority(app);

	packet_size = crypto_stream_packet_size(payload_len);
	packet = (uint8_t *)malloc(packet_size);
	if (packet == NULL) {
		if (stream_id == CRYPTO_STREAM_ID_AUDIO)
			end_audio_priority(app);
		return -1;
	}

	pthread_mutex_lock(&app->tx_lock);
	pthread_mutex_lock(&app->crypto_lock);
	rc = send_crypto_init_locked(app);
	seq_no = app->media_seq++;
	pthread_mutex_unlock(&app->crypto_lock);
	if (rc != 0) {
		pthread_mutex_unlock(&app->tx_lock);
		if (stream_id == CRYPTO_STREAM_ID_AUDIO)
			end_audio_priority(app);
		free(packet);
		return -1;
	}

	encoded_size = crypto_stream_encrypt_packet(
		packet, packet_size, stream_id, seq_no, app->session_nonce,
		payload, payload_len);
	if (encoded_size < 0) {
		pthread_mutex_unlock(&app->tx_lock);
		if (stream_id == CRYPTO_STREAM_ID_AUDIO)
			end_audio_priority(app);
		free(packet);
		return -1;
	}

	rc = ws_sendframe(app->conn, (const char *)packet, (uint64_t)encoded_size, WS_FR_OP_BIN);
	pthread_mutex_unlock(&app->tx_lock);
	if (stream_id == CRYPTO_STREAM_ID_AUDIO)
		end_audio_priority(app);
	free(packet);
	return rc;
}

static int send_video_media(app_t *app, const uint8_t *payload, size_t payload_len)
{
	uint8_t flags;
	uint16_t frag_count;
	uint16_t frag_index;
	uint32_t frame_id;
	size_t offset;

	if (payload == NULL || payload_len == 0u)
		return -1;

	if (payload_len <= VIDEO_FRAGMENT_DATA_SIZE) {
		return send_encrypted_media(app, CRYPTO_STREAM_ID_VIDEO, payload, payload_len);
	}

	flags = video_payload_flags(payload, payload_len);
	frag_count = (uint16_t)((payload_len + VIDEO_FRAGMENT_DATA_SIZE - 1u) / VIDEO_FRAGMENT_DATA_SIZE);
	frame_id = app->video_frame_id++;
	offset = 0u;

	for (frag_index = 0; frag_index < frag_count; ++frag_index) {
		size_t frag_len = payload_len - offset;
		uint8_t *packet;
		int rc;

		if (frag_len > VIDEO_FRAGMENT_DATA_SIZE)
			frag_len = VIDEO_FRAGMENT_DATA_SIZE;
		packet = (uint8_t *)malloc(VIDEO_FRAGMENT_HEADER_SIZE + frag_len);
		if (packet == NULL)
			return -1;

		memcpy(packet, "VFG1", 4);
		packet[4] = flags;
		packet[5] = 0u;
		write_u16le(packet + 6, frag_index);
		write_u16le(packet + 8, frag_count);
		write_u16le(packet + 10, 0u);
		write_u32le(packet + 12, frame_id);
		memcpy(packet + VIDEO_FRAGMENT_HEADER_SIZE, payload + offset, frag_len);
		rc = send_encrypted_media(app, CRYPTO_STREAM_ID_VIDEO,
					  packet, VIDEO_FRAGMENT_HEADER_SIZE + frag_len);
		free(packet);
		if (rc < 0)
			return rc;
		offset += frag_len;
	}

	return 0;
}

size_t check_size(uint8_t *start, size_t size)
{
	for (int i=0; i<(int)size; i++) {
		if ((start[i] == 0x01) && (start[i-1] == 0x00) && (start[i-2] == 0x00)) {
			if (((i-3)>=0) && (start[i-3] == 0)) {
				return i-3;
			} else {
				return i-2;
			}
		}
	}

	return size;
}

void next_chunk(uint8_t *start, size_t size, chunk_t *pc)
{
	memset(pc, 0, sizeof(chunk_t));
	for (int i=2; i<(int)size; i++) {
		if ((start[i] == 0x01) && (start[i-1] == 0x00) && (start[i-2] == 0x00)) {
			i++;
			pc->start = start + i;
			pc->size = check_size(pc->start, size-i);
			return;
		}
	}
}

enum {
	NALU_TYPE_SLICE = 1,
	NALU_TYPE_DPA,
	NALU_TYPE_DPB,
	NALU_TYPE_DPC,
	NALU_TYPE_IDR,
	NALU_TYPE_SEI,
	NALU_TYPE_SPS,
	NALU_TYPE_PPS,
	NALU_TYPE_AUD,
	NALU_TYPE_EOSEQ,
	NALU_TYPE_EOSTREAM,
	NALU_TYPE_FILL
};

#define BASELINE_PROFILE		66
#define MAIN_PROFILE			77
#define EXTENDED_PROFILE		88
#define HIGH_PROFILE			100
#define HIGH_10_PROFILE			110
#define HIGH_422_PROFILE		122
#define HIGH_444_PROFILE		144

static uint8_t *video_bitstream = NULL;
static size_t video_bs_size = 0;
static uint8_t *audio_bitstream = NULL;
static size_t audio_bs_size = 0;

static void build_sibling_path(const char *source_path, const char *filename,
			       char *out, size_t out_size)
{
	const char *slash = strrchr(source_path, '/');
#ifdef _WIN32
	const char *backslash = strrchr(source_path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		slash = backslash;
#endif
	if (slash == NULL) {
		snprintf(out, out_size, "%s", filename);
		return;
	}

	size_t dir_len = (size_t)(slash - source_path + 1);
	if (dir_len + strlen(filename) + 1 > out_size) {
		fprintf(stderr, "path too long for sibling file: %s\n", filename);
		exit(1);
	}

	memcpy(out, source_path, dir_len);
	snprintf(out + dir_len, out_size - dir_len, "%s", filename);
}

static void read_bitstream(const char *filename, uint8_t **bs, size_t *size)
{
	uint8_t *bitstream = NULL;;
	size_t bs_size;

	FILE *infile;
	infile = fopen(filename, "rb");
	if (infile == NULL) {
		printf("error: cannot open bitstream (%s)\n", filename);
		exit(1);
	}
	fseek(infile, 0, SEEK_END);
	bs_size = ftell(infile);
	fseek(infile, 0, SEEK_SET);

	if (bitstream != NULL)
		free(bitstream);

	bitstream = (uint8_t *)malloc(bs_size);
	fread(bitstream, 1, bs_size, infile);
	fclose(infile);

	*bs = bitstream;
	*size = bs_size;
}

#if 0
class TrackObject {
	public:
		int m_minx;
		int m_miny;
		int m_maxx;
		int m_maxy;
		int m_oid;
		mystring m_className;

	public:
		TrackObject(int minx, int miny, int maxx, int maxy, int oid, const char *className) {
			m_minx = minx;
			m_miny = miny;
			m_maxx = maxx;
			m_maxy = maxy;
			m_oid = oid;
			m_className = mystring(className);
		}
		TrackObject(TrackObject &m) {
			*this = m;
		}

		TrackObject &operator=(const TrackObject &m) {
			m_minx = m.m_minx;
			m_miny = m.m_miny;
			m_maxx = m.m_maxx;
			m_maxy = m.m_maxy;
			m_oid = m.m_oid;
			m_className = m.m_className;
			return *this;
		}
		~TrackObject() { }
};

static myvector<TrackObject> json_vec;

typedef myvector<TrackObject> track_vec_t;
static void parse_json(char *msg, track_vec_t &vec)
{
    cJSON *root = cJSON_Parse(msg);
    cJSON *status = cJSON_GetObjectItem(root, "status");
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (status && result) {
        if (strcmp(status->valuestring, "ok") == 0) {
            cJSON *obj;
            cJSON *detections = cJSON_GetObjectItem(result, "detection");
            cJSON_ArrayForEach(obj, detections) {
                cJSON *minx = cJSON_GetObjectItem(obj, "minx");
                cJSON *miny = cJSON_GetObjectItem(obj, "miny");
                cJSON *maxx = cJSON_GetObjectItem(obj, "maxx");
                cJSON *maxy = cJSON_GetObjectItem(obj, "maxy");
				cJSON *oid = cJSON_GetObjectItem(obj, "oid");
                cJSON *className = cJSON_GetObjectItem(obj, "class");
				TrackObject to(minx->valueint, miny->valueint, maxx->valueint, maxy->valueint, oid->valueint, "person");
				vec.push_back(to);
            }
        }
    }
    cJSON_Delete(root);
}

static void read_json(const char *filename, myvector<TrackObject> &vec)
{
	FILE *infile = fopen(filename, "rt");
	if (infile==NULL) {
		printf("cannot open file [%s]\n", filename);
		exit(0);
	}
	while (!feof(infile)) {
		char msg[1024];
		memset(msg, 0, sizeof(msg));
		fgets(msg, sizeof(msg), infile);
		if (strlen(msg)>0) 
			parse_json(msg, vec);
	}
	fclose(infile);
	printf("json: %d lines\n", vec.size());
}
#endif

void *audio_streaming(void *data) // 16k, 16bits, mono
{
	(void)data;
	app_t *app = (app_t *)data;
	uint8_t *start = audio_bitstream;
	int size = audio_bs_size;
	size_t slice = AUDIO_CHUNK_SIZE;
	int cursor = 0;

	if (start == NULL || size <= 0)
		pthread_exit(NULL);

	for (;;) {
		uint8_t *packet;
		size_t packet_size;
		size_t chunk_size;
		useconds_t sleep_us;

		if ((app->state & STATE_CLOSING_AUDIO) == STATE_CLOSING_AUDIO)
			break;

		chunk_size = slice;
		if ((size_t)size < chunk_size)
			chunk_size = (size_t)size;
		if (cursor + (int)chunk_size > size) {
			cursor = 0;
		}
		packet_size = 12u + chunk_size;
		packet = (uint8_t *)malloc(packet_size);
		if (packet == NULL)
			break;
		memcpy(packet, "AUD0", 4);
		write_u32le(packet + 4, app->audio_chunk_index++);
		write_u32le(packet + 8, (uint32_t)cursor);
		memcpy(packet + 12, &start[cursor], chunk_size);
		send_encrypted_media(app, CRYPTO_STREAM_ID_AUDIO, packet, packet_size);
		free(packet);
		cursor += (int)chunk_size;
		sleep_us = (useconds_t)(((uint64_t)chunk_size * 1000000ull) / AUDIO_BYTES_PER_SECOND);
		usleep(sleep_us);
	}
	app->state &= (~STATE_STREAMING_AUDIO);
	app->state &= (~STATE_CLOSING_AUDIO);
	pthread_exit(NULL);
}


// thread for handliing H.264 bitstream
void *h264_streaming(void* data)
{
	(void)data;

	app_t *app = (app_t *)data;
	uint8_t *start = video_bitstream;
	int size = video_bs_size;
	for (;;) {
		chunk_t chunk;
		if ((app->state & STATE_CLOSING_VIDEO) == STATE_CLOSING_VIDEO)
			break;

		next_chunk(start, size, &chunk);
		//printf("video_bs_size=%lu\n", video_bs_size);
		//printf("chunk.start=%p, size=%lu\n", chunk.start, chunk.size);
		if (chunk.start == 0) {
			start = video_bitstream;
			size = video_bs_size;
			continue;
		}

		int32_t qp = bitrate_ctrl_update_frame(&b_ctrl, chunk.start, chunk.size);
		size -= ((chunk.start - start) + chunk.size);
		start = chunk.start + chunk.size;;

		send_video_media(app, chunk.start, chunk.size);

		int nal_type = *chunk.start & 0x1f;
		if ((nal_type>=1) && (nal_type<=5)) {
			//printf("nal_type=%d\n", nal_type);
			usleep(33000); // 30fps
		}
	}
	app->state &= (~STATE_CLOSING_VIDEO);
	app->state &= (~STATE_STREAMING_VIDEO);
	pthread_exit(NULL);
}


static void start_video_stream(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	app_t *app = (app_t *)get_userdata(conn);

	if (!(app->state & STATE_STREAMING_VIDEO)) {
		app->conn = conn;
		app->state |= STATE_STREAMING_VIDEO;
		printf("create video thread\n");
		pthread_create(&app->thread, NULL, h264_streaming, app);
	} else {
		printf("app is not in idle state");
	}
}

static void start_video_stream2(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	app_t *app = (app_t *)get_userdata(conn);
	app->state |= STATE_STREAMING_VIDEO2;
}

static void stop_video_stream2(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	app_t *app = (app_t *)get_userdata(conn);
	app->state &= (~STATE_STREAMING_VIDEO2);
}

static void stop_video_stream(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	app_t *app = (app_t *)get_userdata(conn);
	printf("stop_stream called\n");
	if (app->state & STATE_STREAMING_VIDEO) {
		app->state |= STATE_CLOSING_VIDEO;
		for (;;) {
			if (!(app->state & STATE_STREAMING_VIDEO))
				break;
			usleep(5000);
		}
	}
}

static void start_audio_stream(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	app_t *app = (app_t *)get_userdata(conn);

	if (!(app->state & STATE_STREAMING_AUDIO)) {
		app->conn = conn;
		app->state |= STATE_STREAMING_AUDIO;
		printf("create audio thread\n");
		pthread_create(&app->thread, NULL, audio_streaming, app);
	} else {
		printf("app is not in idle state");
	}
}

static void stop_audio_stream(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)(obj);
	app_t *app = (app_t *)get_userdata(conn);
	printf("stop_audio_stream\n");
	if (app->state & STATE_STREAMING_AUDIO) {
		app->state |= STATE_CLOSING_AUDIO;
		for (;;) {
			if (!(app->state & STATE_STREAMING_AUDIO))
				break;
			usleep(5000);
		}
	}
}

static void start_mp4_record(ws_cli_conn_t *conn, cJSON *obj)
{
	printf("start mp4 recording\n");
	cJSON *param = cJSON_GetObjectItem(obj, "param");
	cJSON *folder = cJSON_GetObjectItem(param, "folder");
	cJSON *filename = cJSON_GetObjectItem(param, "filename");
	cJSON *record_seconds = cJSON_GetObjectItem(param, "record_seconds");
	printf("folder: %s\n", folder->valuestring);
	printf("filename: %s\n", folder->valuestring);
	printf("record_seconds: %d\n", record_seconds->valueint);
}

static void stop_mp4_record(ws_cli_conn_t *conn, cJSON *obj)
{
	printf("stop mp4 recording\n");
}

static void mp4_record_status(ws_cli_conn_t *conn, cJSON *obj)
{
	printf("mp4_record_status\n");
}

static void get_version(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("get_version\n");
	const char *resp = "{\"type\": \"get_version\", \"result\": { \"value\" : [\"1.0.0\"]}}";
	ws_sendframe(conn, resp, strlen(resp), WS_FR_OP_TXT);
}

static void set_smartbitrate(ws_cli_conn_t *conn, cJSON *obj)
{
	// set smart bitrate
}

#define CONF_JSON "config.json"

void write_config(char *config)
{
	FILE *outfile = fopen(CONF_JSON, "wb");
	if (outfile == NULL) {
		printf("error: cannot open json (%s) for write\n", CONF_JSON);
		exit(1);
	}
	fwrite(config, 1, strlen(config), outfile);
	fclose(outfile);
}

char *read_config()
{
	FILE *infile = fopen(CONF_JSON, "rb");
	if (infile == NULL) {
		printf("error: cannot open json (%s) for read\n", CONF_JSON);
		exit(1);
	}
	fseek(infile, 0, SEEK_END);
	size_t s = ftell(infile);
	char *m = (char *)malloc(s+1);
	if (m==NULL) {
		printf("error: cannot allocate memory\n");
		exit(1);
	}
	memset(m, 0, s+1);
	fseek(infile, 0, SEEK_SET);
	fread(m, 1, s, infile);
	fclose(infile);

	return m;
}
		
static void set_config(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("write_config\n");
	//write_config()
}

static void get_config(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("get_config\n");
	char *r = read_config();
	if (r) {
		ws_sendframe(conn, r, strlen(r), WS_FR_OP_TXT);
	}
}

// "param" : {"enable":1, "Tbase": 2, "Tlum": 3, "dynamic_thr": 1, "trigger_blocks": 3} //Tbase:0 ~ 40, Tlum: 0 ~ 5, dynamic_thr 0/1, trigger_blocks:1~256
static void motion_detection(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("motion_detection\n");
	cJSON *param = cJSON_GetObjectItem(obj, "param");
	if (param) {
		cJSON *enable = cJSON_GetObjectItem(param, "enable");
		cJSON *tbase = cJSON_GetObjectItem(param, "Tbase");
		cJSON *tlum = cJSON_GetObjectItem(param, "Tlum");
		cJSON *dynamic_thr = cJSON_GetObjectItem(param, "dynamic_thr");
		cJSON *trigger_blocks = cJSON_GetObjectItem(param, "trigger_blocks");
		if (enable && tbase && tlum && dynamic_thr && trigger_blocks) {
			printf("enable=%d, tbase=%d, tlum=%d, dynamic_thr=%d, trigger_blocks=%d\n",
				enable->valueint, tbase->valueint, tlum->valueint, dynamic_thr->valueint, trigger_blocks->valueint);
		}
	}
}

static void ircut_onoff(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("ircut_onoff\n");
	cJSON *param = cJSON_GetObjectItem(obj, "param");
	cJSON *on = cJSON_GetObjectItem(param, "on");
	printf("on=%d\n", on->valueint);
}

static void isp_fps(ws_cli_conn_t *conn, cJSON *obj)
{
	(void)obj;
	printf("isp_fps: ");
	cJSON *param = cJSON_GetObjectItem(obj, "param");
	cJSON *isp_fps = cJSON_GetObjectItem(param, "isp_fps");
	printf(" %d\n", isp_fps->valueint);
}

static void media_onopen(ws_cli_conn_t *client)
{
	printf("media_onopen called\n");
}

static void media_onclose(ws_cli_conn_t *client)
{
	printf("media_onclose called\n");
}

static void media_onmessage(ws_cli_conn_t *client, const unsigned char *msg, uint64_t size, int type)
{
	char *cli;
	cli = ws_getaddress(client);

	if ((type & WS_FR_OP_TXT) == WS_FR_OP_TXT) {
		printf("receive: %s (size: %" PRId64 ", type: %d), from: %s\n", msg, size, type, cli);
	} else if ((type & WS_FR_OP_BIN) == WS_FR_OP_BIN) {
	}
}

void *media_thread(void *data)
{
	struct ws_events evs;
	evs.onopen = media_onopen;
	evs.onclose = media_onclose;
	evs.onmessage = media_onmessage;
	ws_socket(&evs, 8082, 0, 1000);

	return NULL;
}


static app_cmd_t app_cmd[] = {
	{"start_stream", start_video_stream},
	{"start_stream2", start_video_stream2},
	{"stop_stream", stop_video_stream},
	{"start_audio_stream", start_audio_stream},
	{"stop_audio_stream", stop_audio_stream},
	{"get_version", get_version},
	{"set_smartbitrate", set_smartbitrate},
	{"set_config", set_config},
	{"get_config", get_config},
	{"md", motion_detection},
	{"ircut", ircut_onoff},
	{"isp_fps", isp_fps},
	{"start_mp4_record", start_mp4_record},
	{"stop_mp4_record", stop_mp4_record},
	{"mp4_record_status", mp4_record_status},
	{0, 0}
};

static void onopen(ws_cli_conn_t *client)
{
	char *cli;
	cli = ws_getaddress(client);
	app_t *app = (app_t *)malloc(sizeof(app_t));
	memset(app, 0, sizeof(app_t));

	app->state = STATE_IDLE;
	crypto_stream_fill_random(app->session_nonce, sizeof(app->session_nonce));
	pthread_mutex_init(&app->crypto_lock, NULL);
	pthread_mutex_init(&app->tx_lock, NULL);
	pthread_mutex_init(&app->priority_lock, NULL);
	pthread_cond_init(&app->priority_cond, NULL);
	printf("set user data ! %p\n", (void *)app);
	set_userdata(client, app);
	printf("set user data ok !\n");
	printf("connection opened, addr: %s\n", cli);
}

void onclose(ws_cli_conn_t *client)
{
	char *cli;
	cli = ws_getaddress(client);
	printf("connection closed, addr: %s\n", cli);
	app_t *app = (app_t *)get_userdata(client);
	stop_video_stream(client, NULL);
	stop_audio_stream(client, NULL);

	pthread_mutex_destroy(&app->crypto_lock);
	pthread_mutex_destroy(&app->tx_lock);
	pthread_cond_destroy(&app->priority_cond);
	pthread_mutex_destroy(&app->priority_lock);
	free(app);
}

static void process_command(ws_cli_conn_t *client, const char *msg)
{
	cJSON *root = cJSON_Parse(msg);
	cJSON *r_cmd = cJSON_GetObjectItem(root, "cmd");
	if (r_cmd) {
		app_cmd_t *cmd = (app_cmd_t *)app_cmd;
		for (;;) {
			if (cmd->name == NULL)
				break;

			if (strcmp(cmd->name, r_cmd->valuestring)==0) {
				cmd->func(client, root);
				break;
			}
			cmd++;
		}
	}
}

static void onmessage(ws_cli_conn_t *client, const unsigned char *msg, uint64_t size, int type)
{
	char *cli;
	cli = ws_getaddress(client);

	if ((type & WS_FR_OP_TXT) == WS_FR_OP_TXT) {
		printf("receive: %s (size: %" PRId64 ", type: %d), from: %s\n", msg, size, type, cli);
		process_command(client, (const char *)msg);
	} else if ((type & WS_FR_OP_BIN) == WS_FR_OP_BIN) {
	}
}

//#define BS_FILE "ameba_pro_640_360.h264"

void *ai_thread(void *bs_filename)
{
	char audio_path[PATH_MAX];

	bitrate_ctrl_init(&b_ctrl, 1500000, 30);
	read_bitstream((char *)bs_filename, &video_bitstream, &video_bs_size);
	build_sibling_path((const char *)bs_filename, "sound.raw",
			 audio_path, sizeof(audio_path));
	read_bitstream(audio_path, &audio_bitstream, &audio_bs_size);

	// printf("video size: %zu\n", video_bs_size);
	// printf("audio size: %zu\n", audio_bs_size);

	struct ws_events evs;
	evs.onopen    = &onopen;
	evs.onclose   = &onclose;
	evs.onmessage = &onmessage;
	ws_socket(&evs, 8081, 0, 1000);

	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t ai_th;

	if (argc < 2) {
		printf("Usage: %s <bitstream file>\n", argv[0]);
		exit(1);
	}
	//pthread_create(system_info_th, NULL, ai_thread, NULL);
	//media_thread(NULL);
	ai_thread(argv[1]);
}
