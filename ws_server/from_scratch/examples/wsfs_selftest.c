/* Simple sanity test for examples: verifies that default configuration
 * values remain stable and that server init/deinit succeed. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wsfs_server.h"

int main(void)
{
	wsfs_server_config_t cfg;
	wsfs_server_config_defaults(&cfg);

	assert(strcmp(cfg.host, "0.0.0.0") == 0);
	assert(cfg.port == 9001);
	assert(cfg.max_clients == 16);
	assert(cfg.recv_buffer_size == 4096);
	assert(cfg.max_frame_size == 1024 * 1024);
	assert(cfg.handshake_buffer_cap == 8 * 1024);

	wsfs_server_t server = {0};
	assert(wsfs_server_init(&server, &cfg) == WSFS_STATUS_OK);
	wsfs_server_deinit(&server);

	printf("wsfs_selftest: defaults and init/deinit validated\n");
	return 0;
}
