#include <platform_opts.h>
#include "FreeRTOS.h"
#include "task.h"
#include <platform/platform_stdlib.h>
#include <lwip_netconf.h>
#include <lwip/sockets.h>
#include "wifi_conf.h"
#include "wifi_constants.h"
#include "wifi_structures.h"
#include "wifi_ind.h"
#include "lwip_netconf.h"

#if CONFIG_EXAMPLE_GATEWAY_AMEBA
#include "example_gateway_ameba.h"
#endif

#if CONFIG_USE_POLARSSL
#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/error.h"
#include "polarssl/memory.h"
#elif CONFIG_USE_MBEDTLS
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#endif 

#if CONFIG_EXAMPLE_HIGH_LOAD_MEMORY_USE

extern struct netif xnetif[NET_IF_NUM];

#define PRIORITY_OFFSET		1
#define SAMPLE_MONITOR_STACK_SIZE 2048
#define SAMPLE_TARGET_SCAN_BUFLEN 512
#define SAMPLE_CONNECT_RETRY_TICKS 5000
#define SAMPLE_TARGET_SSID "Kevins Home"
#define SAMPLE_TARGET_PASSWORD "2828189028"

typedef struct sample_scan_info {
	int attempted;
	int rc;
	int found;
	s32 best_rssi;
	u8 channel;
	rtw_security_t security;
	u8 bssid[6];
	char ssid[33];
} sample_scan_info_t;

static void heap_monitor_handler(void *param);
void ssl_client_thread(char *server_host);
static void ssl_client_handler(void *param);
static void udp_server_handler(void *param);
static void udp_client_handler(void *param);
static void sample_log_wifi_status(void);
static void sample_maybe_start_gateway(void);
static int sample_wifi_link_ready(void);
static void sample_wifi_register_event_handlers(void);
static void sample_wifi_event_connect_hdl(char *buf, int buf_len, int flags, void *handler_user_data);
static void sample_wifi_event_disconnect_hdl(char *buf, int buf_len, int flags, void *handler_user_data);
static void sample_wifi_event_handshake_hdl(char *buf, int buf_len, int flags, void *handler_user_data);
static int sample_scan_target_result(char *buf, int buflen, char *target_ssid, void *user_data);
static void sample_scan_named_ssid(sample_scan_info_t *info, const char *ssid);
static void sample_attempt_wifi_connect(const char *reason);
static volatile int sample_wifi_connect_attempted = 0;
static volatile int sample_wifi_connect_ok = 0;
static volatile int sample_gateway_hook_called = 0;
static volatile int sample_gateway_hook_returned = 0;
static volatile int sample_wifi_event_connect_count = 0;
static volatile int sample_wifi_event_disconnect_count = 0;
static volatile int sample_wifi_event_handshake_count = 0;
static volatile unsigned long sample_wifi_retry_count = 0;
static volatile unsigned long sample_wifi_last_attempt_tick = 0;
static volatile int sample_wifi_event_connect_len = 0;
static volatile int sample_wifi_event_disconnect_len = 0;
static volatile int sample_wifi_event_handshake_len = 0;
static unsigned char sample_wifi_event_connect_mac[6] = {0};
static unsigned char sample_wifi_event_disconnect_mac[6] = {0};
static char sample_wifi_event_handshake_msg[32] = {0};
static sample_scan_info_t sample_scan_hotspot = {0};
static sample_scan_info_t sample_scan_home = {0};

/*
 * @brief  Memory usage for high-load use case.
 *			Contains 3 TLS sessions and 6 UDP sessions.
 * @note  Process Flow:
 *		- Start heap monitor thread
 *		- Enable Wi-Fi with STA mode
 *		- Connect to AP by WPA2-AES
 *		- Start 3 TLS sessions
 *		- Start 6 UDP sessions
 */
static void high_load_case_memory_usage(void){
	printf("\n\nMemory usage for high-load use case...\n");

#if CONFIG_EXAMPLE_GATEWAY_AMEBA
	gateway_ameba_register_atcmd();
#endif

	
	/**********************************************************************************
	*	1. Start heap monitor thread	
	**********************************************************************************/
	// Start a thread that would check minimun available heap size periodically during the execution.
	if(xTaskCreate(heap_monitor_handler, "heap_monitor_handler", SAMPLE_MONITOR_STACK_SIZE, (void *) NULL, tskIDLE_PRIORITY + 1 + PRIORITY_OFFSET, NULL) != pdPASS)
		printf("\r\nERROR: Create heap monitor task failed.");

	
	/**********************************************************************************
	*	2. Enable Wi-Fi with STA mode		
	**********************************************************************************/
	printf("\n\rEnable Wi-Fi\n");
	if(wifi_on(RTW_MODE_STA) < 0){
		printf("\n\rERROR: wifi_on failed\n");
		return;
	}
	sample_wifi_register_event_handlers();
	
	
	/**********************************************************************************
	*	3. Connect to AP by WPA2-AES		
	**********************************************************************************/	
	// Connect to AP with PSK-WPA2-AES.
	sample_scan_named_ssid(&sample_scan_hotspot, SAMPLE_TARGET_SSID);
	sample_scan_named_ssid(&sample_scan_home, "KeviniPhone");
	sample_attempt_wifi_connect("initial");

	sample_maybe_start_gateway();

	/*
	 * Keep this validation image focused on STA bring-up.
	 * Skip the TLS/UDP load steps so the UART log stays simple.
	 */
	return;
	
	
	/**********************************************************************************
	*	4. Start 3 TLS sessions	
	**********************************************************************************/	
	// Start SSL connection
	// For each session, it will setup the connection and transfer data 10 times with 5s interval
	ssl_client_thread("192.168.1.101");
	ssl_client_thread("192.168.1.101");
	ssl_client_thread("192.168.1.101");

	
	/**********************************************************************************
	*	5. Start 6 UDP sessions (5 servers, 1 client)
	**********************************************************************************/	
	// Start UDP servers for ports from 6001 to 6005 by using socket select
	if(xTaskCreate(udp_server_handler, "udp_server_handler", 1024, NULL, tskIDLE_PRIORITY + 1 + PRIORITY_OFFSET, NULL) != pdPASS)
		printf("\r\nUDP ERROR: Create UDP server task failed.");

	// Start UDP client that simply sends data to 192.168.1.101 port 6001
	if(xTaskCreate(udp_client_handler, "udp_client_handler", 1024,NULL, tskIDLE_PRIORITY + 1 + PRIORITY_OFFSET, NULL) != pdPASS)
		printf("\r\nUDP ERROR: Create UDP client task failed.");
}

// Heap monitor which will report the minimum available heap size during the execution.
static int minHeap = 0;
static void heap_monitor_handler(void *param){
	while(1){
		int getHeap = xPortGetFreeHeapSize();
		if(minHeap == 0 || minHeap > getHeap)
			minHeap = getHeap;
		if (!sample_wifi_link_ready()) {
			unsigned long now = (unsigned long)xTaskGetTickCount();
			if (sample_wifi_last_attempt_tick == 0 ||
			    (now - sample_wifi_last_attempt_tick) >= SAMPLE_CONNECT_RETRY_TICKS) {
				sample_attempt_wifi_connect("retry");
			}
		}
		sample_maybe_start_gateway();
		printf("\n\nMin Available Heap: %d\n", minHeap);
		sample_log_wifi_status();
#if CONFIG_EXAMPLE_GATEWAY_AMEBA
		gateway_ameba_log_status();
#endif
		vTaskDelay(1000);
	}	
	vTaskDelete(NULL);
}

static void sample_log_wifi_status(void)
{
	unsigned char *ip = LwIP_GetIP(&xnetif[0]);
	rtw_wifi_setting_t setting;
	unsigned char bssid[6] = {0};
	int has_ip = 0;
	int has_link = 0;
	int setting_rc = RTW_ERROR;
	int bssid_rc = RTW_ERROR;

	if (ip != NULL) {
		has_ip = (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
	}

	memset(&setting, 0, sizeof(setting));
	setting_rc = wifi_get_setting(WLAN0_NAME, &setting);
	bssid_rc = wifi_get_ap_bssid(bssid);
	has_link = (setting_rc == RTW_SUCCESS && setting.ssid[0] != '\0');

	printf("[sample][wifi] tick=%lu has_ip=%d has_link=%d ip=%u.%u.%u.%u\r\n",
	       (unsigned long)xTaskGetTickCount(),
	       has_ip,
	       has_link,
	       ip ? ip[0] : 0,
	       ip ? ip[1] : 0,
	       ip ? ip[2] : 0,
	       ip ? ip[3] : 0);
	printf("[sample][state] connect_attempted=%d connect_ok=%d hook_called=%d hook_returned=%d\r\n",
	       sample_wifi_connect_attempted,
	       sample_wifi_connect_ok,
	       sample_gateway_hook_called,
	       sample_gateway_hook_returned);
	printf("[sample][retry] count=%lu last_attempt_tick=%lu\r\n",
	       sample_wifi_retry_count,
	       sample_wifi_last_attempt_tick);
	printf("[sample][scan][Kevins Home] attempted=%d rc=%d found=%d ssid=%s channel=%u security=%d rssi=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
	       sample_scan_hotspot.attempted,
	       sample_scan_hotspot.rc,
	       sample_scan_hotspot.found,
	       sample_scan_hotspot.ssid[0] ? sample_scan_hotspot.ssid : "<na>",
	       (unsigned int)sample_scan_hotspot.channel,
	       (int)sample_scan_hotspot.security,
	       (int)sample_scan_hotspot.best_rssi,
	       sample_scan_hotspot.bssid[0], sample_scan_hotspot.bssid[1], sample_scan_hotspot.bssid[2],
	       sample_scan_hotspot.bssid[3], sample_scan_hotspot.bssid[4], sample_scan_hotspot.bssid[5]);
	printf("[sample][scan][KeviniPhone] attempted=%d rc=%d found=%d ssid=%s channel=%u security=%d rssi=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
	       sample_scan_home.attempted,
	       sample_scan_home.rc,
	       sample_scan_home.found,
	       sample_scan_home.ssid[0] ? sample_scan_home.ssid : "<na>",
	       (unsigned int)sample_scan_home.channel,
	       (int)sample_scan_home.security,
	       (int)sample_scan_home.best_rssi,
	       sample_scan_home.bssid[0], sample_scan_home.bssid[1], sample_scan_home.bssid[2],
	       sample_scan_home.bssid[3], sample_scan_home.bssid[4], sample_scan_home.bssid[5]);
	printf("[sample][event] connect=%d disconnect=%d handshake=%d\r\n",
	       sample_wifi_event_connect_count,
	       sample_wifi_event_disconnect_count,
	       sample_wifi_event_handshake_count);
	printf("[sample][event-detail] connect_len=%d mac=%02x:%02x:%02x:%02x:%02x:%02x disconnect_len=%d mac=%02x:%02x:%02x:%02x:%02x:%02x handshake_len=%d msg=%s\r\n",
	       sample_wifi_event_connect_len,
	       sample_wifi_event_connect_mac[0], sample_wifi_event_connect_mac[1], sample_wifi_event_connect_mac[2],
	       sample_wifi_event_connect_mac[3], sample_wifi_event_connect_mac[4], sample_wifi_event_connect_mac[5],
	       sample_wifi_event_disconnect_len,
	       sample_wifi_event_disconnect_mac[0], sample_wifi_event_disconnect_mac[1], sample_wifi_event_disconnect_mac[2],
	       sample_wifi_event_disconnect_mac[3], sample_wifi_event_disconnect_mac[4], sample_wifi_event_disconnect_mac[5],
	       sample_wifi_event_handshake_len,
	       sample_wifi_event_handshake_msg[0] ? sample_wifi_event_handshake_msg : "<na>");
	printf("[sample][link] setting_rc=%d bssid_rc=%d ssid=%s channel=%u security=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
	       setting_rc,
	       bssid_rc,
	       (setting_rc == RTW_SUCCESS) ? (char *)setting.ssid : "<na>",
	       (unsigned int)((setting_rc == RTW_SUCCESS) ? setting.channel : 0),
	       (int)((setting_rc == RTW_SUCCESS) ? setting.security_type : -1),
	       bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static void sample_maybe_start_gateway(void)
{
#if CONFIG_EXAMPLE_GATEWAY_AMEBA
	if (!sample_wifi_link_ready() || sample_gateway_hook_called) {
		return;
	}

	printf("\n\rLaunching gateway integration task after IP ready\n");
	printf("\n\r[gateway-hook] calling example_gateway_ameba()\n");
	sample_gateway_hook_called = 1;
	example_gateway_ameba();
	sample_gateway_hook_returned = 1;
	printf("\n\r[gateway-hook] example_gateway_ameba() returned\n");
#endif
}

static int sample_wifi_link_ready(void)
{
	rtw_wifi_setting_t setting;

	memset(&setting, 0, sizeof(setting));
	if (wifi_get_setting(WLAN0_NAME, &setting) != RTW_SUCCESS) {
		return 0;
	}

	return setting.ssid[0] != '\0';
}

static void sample_wifi_register_event_handlers(void)
{
	wifi_reg_event_handler(WIFI_EVENT_CONNECT, sample_wifi_event_connect_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_DISCONNECT, sample_wifi_event_disconnect_hdl, NULL);
	wifi_reg_event_handler(WIFI_EVENT_FOURWAY_HANDSHAKE_DONE, sample_wifi_event_handshake_hdl, NULL);
}

static void sample_wifi_event_connect_hdl(char *buf, int buf_len, int flags, void *handler_user_data)
{
	(void)flags;
	(void)handler_user_data;
	sample_wifi_event_connect_count++;
	sample_wifi_event_connect_len = buf_len;
	memset(sample_wifi_event_connect_mac, 0, sizeof(sample_wifi_event_connect_mac));
	if (buf != NULL && buf_len >= 6) {
		memcpy(sample_wifi_event_connect_mac, buf, 6);
	}
}

static void sample_wifi_event_disconnect_hdl(char *buf, int buf_len, int flags, void *handler_user_data)
{
	(void)flags;
	(void)handler_user_data;
	sample_wifi_event_disconnect_count++;
	sample_wifi_event_disconnect_len = buf_len;
	memset(sample_wifi_event_disconnect_mac, 0, sizeof(sample_wifi_event_disconnect_mac));
	if (buf != NULL && buf_len >= 6) {
		memcpy(sample_wifi_event_disconnect_mac, buf, 6);
	}
}

static void sample_wifi_event_handshake_hdl(char *buf, int buf_len, int flags, void *handler_user_data)
{
	(void)flags;
	(void)handler_user_data;
	sample_wifi_event_handshake_count++;
	sample_wifi_event_handshake_len = buf_len;
	memset(sample_wifi_event_handshake_msg, 0, sizeof(sample_wifi_event_handshake_msg));
	if (buf != NULL && buf_len > 0) {
		int copy_len = buf_len;
		if (copy_len >= (int)sizeof(sample_wifi_event_handshake_msg)) {
			copy_len = (int)sizeof(sample_wifi_event_handshake_msg) - 1;
		}
		memcpy(sample_wifi_event_handshake_msg, buf, copy_len);
	}
}

static int sample_scan_target_result(char *buf, int buflen, char *target_ssid, void *user_data)
{
	sample_scan_info_t *info = (sample_scan_info_t *)user_data;
	u32 plen = 0;

	while (plen < (u32)buflen) {
		u32 len;
		u32 ssid_len;
		u32 security_mode;
		rtw_security_t security_type = RTW_SECURITY_UNKNOWN;
		s32 rssi;
		u32 channel;
		u8 *mac;
		u8 *ssid;

		len = (u8)*(buf + plen);
		if (len == 0 || len == strlen(target_ssid)) {
			break;
		}

		mac = (u8 *)(buf + plen + 1);
		rssi = *(s32 *)(buf + plen + 1 + 6);
		security_mode = (u8)*(buf + plen + 1 + 6 + 4);
		switch (security_mode) {
		case IW_ENCODE_ALG_NONE:
			security_type = RTW_SECURITY_OPEN;
			break;
		case IW_ENCODE_ALG_WEP:
			security_type = RTW_SECURITY_WEP_PSK;
			break;
		case IW_ENCODE_ALG_TKIP:
			security_type = RTW_SECURITY_WPA_TKIP_PSK;
			break;
		case IW_ENCODE_ALG_CCMP:
			security_type = RTW_SECURITY_WPA2_AES_PSK;
			break;
		default:
			security_type = RTW_SECURITY_UNKNOWN;
			break;
		}
		channel = *(buf + plen + 1 + 6 + 4 + 1 + 1);
		ssid_len = len - 1 - 6 - 4 - 1 - 1 - 1;
		ssid = (u8 *)(buf + plen + 1 + 6 + 4 + 1 + 1 + 1);
		if (ssid_len > 32) {
			ssid_len = 32;
		}

		if (ssid_len == strlen(target_ssid) && memcmp(ssid, target_ssid, ssid_len) == 0) {
			info->found++;
			if (info->found == 1 || rssi > info->best_rssi) {
				memset(info->ssid, 0, sizeof(info->ssid));
				memcpy(info->ssid, ssid, ssid_len);
				memcpy(info->bssid, mac, 6);
				info->best_rssi = rssi;
				info->channel = (u8)channel;
				info->security = security_type;
			}
		}

		plen += len;
	}

	return 0;
}

static void sample_scan_named_ssid(sample_scan_info_t *info, const char *ssid)
{
	memset(info, 0, sizeof(*info));
	info->attempted = 1;
	info->best_rssi = -127;
	info->rc = wifi_scan_networks_with_ssid(sample_scan_target_result,
	                                        info,
	                                        SAMPLE_TARGET_SCAN_BUFLEN,
	                                        (char *)ssid,
	                                        strlen(ssid));
	printf("[sample][scan] request ssid=%s rc=%d found=%d best_rssi=%d channel=%u security=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
	       ssid,
	       info->rc,
	       info->found,
	       (int)info->best_rssi,
	       (unsigned int)info->channel,
	       (int)info->security,
	       info->bssid[0], info->bssid[1], info->bssid[2],
	       info->bssid[3], info->bssid[4], info->bssid[5]);
}

static void sample_attempt_wifi_connect(const char *reason)
{
	sample_wifi_connect_attempted = 1;
	sample_wifi_last_attempt_tick = (unsigned long)xTaskGetTickCount();
	sample_wifi_retry_count++;

	printf("\n\r[sample][retry] reason=%s count=%lu target=%s\r\n",
	       reason,
	       sample_wifi_retry_count,
	       SAMPLE_TARGET_SSID);
	sample_scan_named_ssid(&sample_scan_hotspot, SAMPLE_TARGET_SSID);
	sample_scan_named_ssid(&sample_scan_home, "KeviniPhone");

	if (wifi_connect(SAMPLE_TARGET_SSID,
	                 RTW_SECURITY_WPA2_AES_PSK,
	                 SAMPLE_TARGET_PASSWORD,
	                 strlen(SAMPLE_TARGET_SSID),
	                 strlen(SAMPLE_TARGET_PASSWORD),
	                 -1,
	                 NULL) == RTW_SUCCESS) {
		sample_wifi_connect_ok = 1;
		printf("\n\rWi-Fi connected, starting DHCP\n");
		LwIP_DHCP(0, DHCP_START);
	} else {
		sample_wifi_connect_ok = -1;
		printf("\n\rERROR: wifi_connect failed\n");
	}
}

/*
 * @brief  SSL/TLS connection.
 * @note  Please refer to component\common\utilities\ssl_client.c.
 */
void ssl_client_thread(char *server_host){
	if(xTaskCreate(ssl_client_handler, "ssl_client_handler", 1150, server_host, tskIDLE_PRIORITY + 3 + PRIORITY_OFFSET, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate failed", __FUNCTION__);
}

static int my_random(void *p_rng, unsigned char *output, size_t output_len)
{
	rtw_get_random_bytes(output, output_len);
	return 0;
}

#if CONFIG_USE_POLARSSL

static void* my_malloc(size_t size)
{
	void *ptr = pvPortMalloc(size);
	return ptr;
}

#define my_free		vPortFree

static void ssl_client_handler(void *param){
	char *server_host = param;
	int server_port = 443;
	char *get_request = "GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n";
	int ret, len, server_fd = -1;
	unsigned char buf[512];
	ssl_context ssl;
	int retry_count = 0;

	memory_set_own(my_malloc, my_free);
	
	/*
	 * 1. Start the connection
	 */
	printf("\n\r  . Connecting to tcp/%s/%d...", server_host, server_port);

	if((ret = net_connect(&server_fd, server_host, server_port)) != 0) {
		printf(" failed\n\r  ! net_connect returned %d\n", ret);
		goto exit1;
	}

	printf(" ok\n");

	/*
	 * 2. Setup stuff
	 */
	printf("\n\r  . Setting up the SSL/TLS structure..." );

	if((ret = ssl_init(&ssl)) != 0) {
		printf(" failed\n\r  ! ssl_init returned %d\n", ret);
		goto exit;
	}

	ssl_set_endpoint(&ssl, SSL_IS_CLIENT);
	ssl_set_authmode(&ssl, SSL_VERIFY_NONE);
	ssl_set_rng(&ssl, my_random, NULL);
	ssl_set_bio(&ssl, net_recv, &server_fd, net_send, &server_fd);

	printf(" ok\n");

	/*
	 * 3. Handshake
	 */
	printf("\n\r  . Performing the SSL/TLS handshake...");

	while((ret = ssl_handshake(&ssl)) != 0) {
		if((ret != POLARSSL_ERR_NET_WANT_READ && ret != POLARSSL_ERR_NET_WANT_WRITE  
			&& ret != POLARSSL_ERR_NET_RECV_FAILED) || retry_count >= 5) {
			printf(" failed\n\r  ! ssl_handshake returned -0x%x\n", -ret);
			goto exit;
		}
		retry_count++;
	}

	printf(" ok\n");
	printf("\n\r  . Use ciphersuite %s\n", ssl_get_ciphersuite(&ssl));
	
	// Data transfer for 10 times
	for(int i = 0; i < 10; i++){
		/*
		 * 4. Write the GET request
		 */
		printf("\n\r  > Write to server %s:", server_host);

		len = sprintf((char *) buf, get_request);

		while((ret = ssl_write(&ssl, buf, len)) <= 0) {
			if(ret != POLARSSL_ERR_NET_WANT_READ && ret != POLARSSL_ERR_NET_WANT_WRITE) {
				printf(" failed\n\r  ! ssl_write returned %d\n", ret);
				goto exit;
			}
		}

		len = ret;
		printf(" %d bytes written\n\r\n\r%s\n", len, (char *) buf);

		/*
		 * 5. Read the HTTP response
		 */
		printf("\n\r  < Read from server %s:", server_host);

		int read_size = 0, resource_size = 0, content_len = 0, header_removed = 0;
		while((read_size = ssl_read(&ssl, buf, sizeof(buf) - 1)) > 0) {
			if(header_removed == 0) {
				char *header = NULL;

				buf[read_size] = 0;
				header = strstr(buf, "\r\n\r\n");

				if(header) {
					char *body, *content_len_pos;

					body = header + strlen("\r\n\r\n");
					*(body - 2) = 0;
					header_removed = 1;
					printf("\nHTTP Header: %s\n", buf);

					// Remove header size to get first read size of data from body head
					read_size = read_size - ((unsigned char *) body - buf);

					content_len_pos = strstr(buf, "Content-Length: ");
					if(content_len_pos) {
						content_len_pos += strlen("Content-Length: ");
						*(char*)(strstr(content_len_pos, "\r\n")) = 0;
						content_len = atoi(content_len_pos);
					}
				}
				else {
					printf("ERROR: HTTP header\n");
					goto exit;
				}
			}
			else {
				printf("\n%s\n", buf);
			}

			printf("\nread resource %d bytes\n", read_size);
			resource_size += read_size;
			memset(buf, 0, sizeof(buf));
			
			if(resource_size == content_len)
				break;
		}

		printf("\nhttp read resource size = %d bytes\n", resource_size);
		
		vTaskDelay(5000);
	}

	ssl_close_notify(&ssl);
exit:
	net_close(server_fd);
	ssl_free(&ssl);
exit1:
	vTaskDelete(NULL);
}

#elif CONFIG_USE_MBEDTLS /* CONFIG_USE_POLARSSL */

static void* my_calloc(size_t nelements, size_t elementSize)
{
	size_t size;
	void *ptr = NULL;

	size = nelements * elementSize;
	ptr = pvPortMalloc(size);

	if(ptr)
		memset(ptr, 0, size);

	return ptr;
}

#define my_free		vPortFree

static char *hl_itoa(int value){
	char *val_str;
	int tmp = value, len = 1;

	while((tmp /= 10) > 0)
		len ++;

	val_str = (char *) pvPortMalloc(len + 1);
	sprintf(val_str, "%d", value);

	return val_str;
}

static void ssl_client_handler(void *param){
	char *server_host = param;
	int server_port = 443;
	char *get_request = "GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n";
	int ret, len;
	unsigned char buf[512];
	int retry_count = 0;
	mbedtls_net_context server_fd;	
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;	

	mbedtls_platform_set_calloc_free(my_calloc, vPortFree);
	
	/*
	 * 1. Start the connection
	 */
	printf("\n\r  . Connecting to tcp/%s/%d...", server_host, server_port);

        mbedtls_net_init(&server_fd);
        char *server_port_str = hl_itoa(server_port);
	if((ret = mbedtls_net_connect(&server_fd, server_host, server_port_str, MBEDTLS_NET_PROTO_TCP)) != 0) {
		printf(" failed\n\r  ! net_connect returned %d\n", ret);
		goto exit1;
	}

	printf(" ok\n");

	/*
	 * 2. Setup stuff
	 */
	printf("\n\r  . Setting up the SSL/TLS structure..." );

	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);

	mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	if((ret = mbedtls_ssl_config_defaults(&conf,
			MBEDTLS_SSL_IS_CLIENT,
			MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {

		printf(" failed\n\r  ! ssl_config_defaults returned %d\n", ret);
		goto exit;
	}

	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_rng(&conf, my_random, NULL);

	if((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
		printf(" failed\n\r  ! ssl_setup returned %d\n", ret);
		goto exit;
	}

	printf(" ok\n");

	/*
	 * 3. Handshake
	 */
	printf("\n\r  . Performing the SSL/TLS handshake...");

	while((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
		if(ret != MBEDTLS_ERR_NET_RECV_FAILED || retry_count >= 5) {
			printf(" failed\n\r  ! ssl_handshake returned -0x%x\n", -ret);
			goto exit;
		}
		retry_count++;
	}

	printf(" ok\n");
	printf("\n\r  . Use ciphersuite %s\n", mbedtls_ssl_get_ciphersuite(&ssl));
	
	// Data transfer for 10 times
	for(int i = 0; i < 10; i++){
		/*
		 * 4. Write the GET request
		 */
		printf("\n\r  > Write to server %s:", server_host);

		len = sprintf((char *) buf, get_request);

		while((ret = mbedtls_ssl_write(&ssl, buf, len)) <= 0) {
			printf(" failed\n\r  ! ssl_write returned %d\n", ret);
			goto exit;
		}

		len = ret;
		printf(" %d bytes written\n\r\n\r%s\n", len, (char *) buf);

		/*
		 * 5. Read the HTTP response
		 */
		printf("\n\r  < Read from server %s:", server_host);

		int read_size = 0, resource_size = 0, content_len = 0, header_removed = 0;
		while((read_size = mbedtls_ssl_read(&ssl, buf, sizeof(buf) - 1)) > 0) {
			if(header_removed == 0) {
				char *header = NULL;

				buf[read_size] = 0;
				header = strstr(buf, "\r\n\r\n");

				if(header) {
					char *body, *content_len_pos;

					body = header + strlen("\r\n\r\n");
					*(body - 2) = 0;
					header_removed = 1;
					printf("\nHTTP Header: %s\n", buf);

					// Remove header size to get first read size of data from body head
					read_size = read_size - ((unsigned char *) body - buf);

					content_len_pos = strstr(buf, "Content-Length: ");
					if(content_len_pos) {
						content_len_pos += strlen("Content-Length: ");
						*(char*)(strstr(content_len_pos, "\r\n")) = 0;
						content_len = atoi(content_len_pos);
					}
				}
				else {
					printf("ERROR: HTTP header\n");
					goto exit;
				}
			}
			else {
				printf("\n%s\n", buf);
			}

			printf("\nread resource %d bytes\n", read_size);
			resource_size += read_size;
			memset(buf, 0, sizeof(buf));
			
			if(resource_size == content_len)
				break;
		}

		printf("\nhttp read resource size = %d bytes\n", resource_size);
		
		vTaskDelay(5000);
	}

	mbedtls_ssl_close_notify(&ssl);
exit:
	mbedtls_net_free(&server_fd);
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);	
exit1:
	vTaskDelete(NULL);
}
#endif /* CONFIG_USE_POLARSSL */

/*
 * @brief  Use socket select for 5 UDP server ports from 6001 to 6005.
 * @note  Please refer to component\common\example\socket_select\example_socket_select.c for socket select example.
 */
#define MAX_SOCKETS     10
#define SELECT_TIMEOUT  60
int socket_used[MAX_SOCKETS];

int udp_server_bind_socket(int server_port){
	int server_fd = -1;
	struct sockaddr_in server_addr;
	if((server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) >= 0) {
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(server_port);
		server_addr.sin_addr.s_addr = INADDR_ANY;

		int sock_opt = 1;
		setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &sock_opt, sizeof( sock_opt ));

		if(bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
			printf("Bind error\n");
			return -1;
		}

		socket_used[server_fd] = 1;

		printf("Success bind UDP port %d with socket fd %d\n", server_port, server_fd);
		return server_fd;
	}
	else {
		printf("Socket error\n");
		return -1;
	}
}

static void udp_server_handler(void *param)
{
	int max_socket_fd = -1;
	int min_socket_fd = 20;
	struct sockaddr_in client_addr;
	int addrlen = sizeof(struct sockaddr_in);
	int server_fd_1 = -1, server_fd_2 = -1, server_fd_3 = -1, server_fd_4 = -1, server_fd_5 = -1;

	memset(socket_used, 0, sizeof(socket_used));

	// bind udp server socket from port 6001 to 6005
	int server_fd = -1;
	for(int i=0; i<5; i++){
		server_fd = -1;
		server_fd = udp_server_bind_socket(6001 + i);
		if(server_fd != -1 && server_fd > max_socket_fd)
			max_socket_fd = server_fd;
		if(server_fd != -1 && server_fd < min_socket_fd)
			min_socket_fd = server_fd;
	}

	while(1) {
		int socket_fd;
		unsigned char buf[512];
		fd_set read_fds;
		struct timeval timeout;

		FD_ZERO(&read_fds);
		timeout.tv_sec = SELECT_TIMEOUT;
		timeout.tv_usec = 0;

		for(socket_fd = 0; socket_fd < MAX_SOCKETS; socket_fd ++)
			if(socket_used[socket_fd])
				FD_SET(socket_fd, &read_fds);

		if(select(max_socket_fd + 1, &read_fds, NULL, NULL, &timeout)) {
			for(socket_fd = min_socket_fd; socket_fd < max_socket_fd + 1; socket_fd ++) {
				if(socket_used[socket_fd] && FD_ISSET(socket_fd, &read_fds)) {
					int read_size = recvfrom(socket_fd, buf, sizeof(buf), 0, (struct sockaddr*) &client_addr, &addrlen);
					if(read_size > 0) {
						printf("socket fd(%d) recv data size: %d\n", socket_fd, read_size);
						sendto(socket_fd, buf, read_size, 0, (struct sockaddr*) &client_addr, addrlen);
					}
					else {
						printf("socket fd(%d) recv data failed\n", socket_fd);
						socket_used[socket_fd] = 0;
						close(socket_fd);
					}
				}
			}
		}
		else {
			printf("UDP server: no data in %d seconds\n", SELECT_TIMEOUT);
		}
	}

exit:
	for(int socket_fd = 0; socket_fd < MAX_SOCKETS; socket_fd ++)
		if(socket_used[socket_fd])
			close(socket_fd);

	vTaskDelete(NULL);
}

/*
 * @brief  Simple UDP client.
 */
static void udp_client_handler(void *param){
	char* ip = "192.168.1.101";
	int port = 6001;
	struct sockaddr_in  ser_addr;
	int client_fd = -1;
	unsigned char buf[512];

	//filling the buffer
	for (int i = 0; i < sizeof(buf); i++)
		buf[i] = (unsigned char)(i % 10);

	if( (client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		printf("\n\r[ERROR] %s: Create UDP socket failed",__func__);
		goto exit;
	}

	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_port = htons(port);
	ser_addr.sin_addr.s_addr = inet_addr(ip);

	printf("\n\r%s: Server IP=%s, port=%d", __func__,ip, port);

	while(1){
		vTaskDelay(1000);
		if(sendto(client_fd, buf, sizeof(buf),0,(struct sockaddr*)&ser_addr, sizeof(struct sockaddr_in)) < 0)
			continue;
		else
			printf("Send UDP data to %s: %d\n", ip, port);
	}

	close(client_fd);
exit:
	vTaskDelete(NULL);
}

static void example_high_load_memory_use_thread(void* param){
	vTaskDelay(4000);
	
	// High-load use case memory usage: 3 TLS + 6 UDP
	high_load_case_memory_usage();

	vTaskDelete(NULL);
}

void example_high_load_memory_use(void){
	if(xTaskCreate(example_high_load_memory_use_thread, ((const char*)"example_high_load_memory_use_thread"), 1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate failed\n", __FUNCTION__);
}

#endif /* CONFIG_EXAMPLE_HIGH_LOAD_MEMORY_USE */
