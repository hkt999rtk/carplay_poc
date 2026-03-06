#include <FreeRTOS.h>
#include <platform_stdlib.h>
#include <httpd/httpd.h>
#include <stdint.h>

#include "htdocs_bin.c"

typedef struct web_path_s {
    const uint8_t *path;
    const uint8_t *mime;
	const uint8_t *encoding;
    const uint8_t *data;
    uint32_t data_len;
} web_path_t;

static uint8_t *_read_int32(uint8_t *p, int32_t *value)
{
    *value = *((int32_t *)p);
    return p + sizeof(int32_t);
}
#define align4(n)   (((n+3)>>2)<<2)

typedef int (*enumerate_t)(web_path_t *web_path, void *user_data);
static void enumerate_items(uint8_t *p, enumerate_t pfunc, void *user_data)
{
	web_path_t item;
    for (;;) {
		int32_t len;
        p = _read_int32(p, &len);
        if (len == 0) 
            break;

        item.path = p;
        p += align4(len);

        p = _read_int32(p, &len);
        item.mime = p;
        p += align4(len);

		p = _read_int32(p, &len);
		item.encoding = p;
		p += align4(len);

        p = _read_int32(p, &len);
        item.data = p;
		item.data_len = len;
        p += align4(len);

		if (pfunc(&item, user_data) == 0) { // stop
			return;
		}
    }
	memset(&item, 0, sizeof(web_path_t));
}

typedef struct match_s {
	const char *target;
	int target_len;
	web_path_t *web_path;
} match_t;

static int match_name(web_path_t *web_path, void *user_data)
{
	match_t *match = (match_t *)user_data;
	if (strncmp((char *)web_path->path, match->target, match->target_len) == 0) {
		if (strlen((char *)web_path->path) == match->target_len) {
			memcpy(match->web_path, web_path, sizeof(web_path_t));
			return 0; // found ! and stop searching
		}
	}

	return 1; // continue
}

static void search_path(uint8_t *p, web_path_t *web_path, const char *target, int target_len) 
{
	match_t match;

	if ((target_len==1) && (target[0] == '/')) {
		target = (const char *)"/index";
		target_len = strlen(target);
	}
	match.target = target;
	match.target_len = target_len;
	match.web_path = web_path;

	enumerate_items(p, match_name, &match);
}

static void file_serve(struct httpd_conn *conn)
{
	if (httpd_request_is_method(conn, "GET")) {
		web_path_t item;
		memset(&item, 0, sizeof(web_path_t));

		if ((conn->request.path_len == 1) && (conn->request.path[0] == '/')) {
			httpd_response_write_header_start(conn, "302 Found", (char *)"text/plain", 0);
			httpd_response_write_header(conn, "Location", "/main.html");
			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);
			httpd_conn_close(conn);
			return;
		}

		search_path(htdocs_bin, &item, (char *)conn->request.path, conn->request.path_len);
		if ((item.path == 0) && (conn->request.path_len > 5)) {
			const char *suffix = conn->request.path + conn->request.path_len - 5;
			if (strncmp(suffix, ".html", 5) == 0) {
				search_path(htdocs_bin, &item, (char *)conn->request.path, conn->request.path_len - 5);
			}
		}
		if (item.path == 0) {
			// not found
			const char *not_found = "<html>Not found</html>";
			httpd_response_write_header_start(conn, "404 NOT FOUND", (char *)"text/html", strlen(not_found));
			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);
			httpd_response_write_data(conn, (void *)not_found, strlen(not_found));
		} else {
			// found and serve
			httpd_response_write_header_start(conn, "200 OK", (char *)item.mime, item.data_len);
			if (strlen((char *)item.encoding) > 0) {
				httpd_response_write_header(conn, "Content-Encoding", (char *)item.encoding);
			}

			httpd_response_write_header(conn, "Connection", "close");
			httpd_response_write_header_finish(conn);
			httpd_response_write_data(conn, (void *)item.data, item.data_len);
		}
	} else {
		// HTTP/1.1 405 Method Not Allowed
		httpd_response_method_not_allowed(conn, NULL);
	}

	httpd_conn_close(conn);
}


int register_callback(web_path_t *web_path, void *user_data)
{
	httpd_reg_page_callback((const char *)web_path->path, file_serve);
	return 1; // continue
}

static void register_callbacks()
{
	enumerate_items(htdocs_bin, register_callback, 0);
	httpd_reg_page_callback("/", file_serve);
	httpd_reg_page_callback("/main.html", file_serve);
}

static void example_httpd_thread(void *param)
{
	/* To avoid gcc warnings */
	(void) param;
	register_callbacks();

	if (httpd_start(9090, 5, 4096, HTTPD_THREAD_SINGLE, HTTPD_SECURE_NONE) != 0) {
		printf("ERROR: httpd_start");
		httpd_clear_page_callbacks();
	}

	vTaskDelete(NULL);
}


void example_httpd(void) {
	if (xTaskCreate(example_httpd_thread, ((const char *)"example_httpd_thread"), 2048, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
		printf("\n\r%s xTaskCreate(example_httpd_thread) failed", __FUNCTION__);
	}
}


int main(int argc, char **argv)
{
	example_httpd();
	for (;;) vTaskDelay(1000);
}
