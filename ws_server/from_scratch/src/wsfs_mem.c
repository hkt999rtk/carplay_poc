#include <stdlib.h>

#include "wsfs_internal.h"

void *wsfs_realloc_or_free(void *ptr, size_t new_size)
{
	if (new_size == 0) {
		free(ptr);
		return NULL;
	}

	void *resized = realloc(ptr, new_size);
	if (resized == NULL) {
		free(ptr);
	}

	return resized;
}
