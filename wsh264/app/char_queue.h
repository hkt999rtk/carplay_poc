#pragma once

#include <stdlib.h>

#define SZ_QUEUE	65535
typedef struct char_queue_s {
	uint8_t buffer[SZ_QUEUE];
	uint32_t head;
	uint32_t tail;
} char_queue_t;

typedef struct char_chunk_s {
    uint8_t *pbuf;
    size_t size;
} char_chunk_t;

void char_queue_init(char_queue_t *q);
void char_queue_push(char_queue_t *q, uint8_t c);
void char_queue_pop(char_queue_t *q, uint8_t *c);

bool char_queue_empty(char_queue_t *q);
bool char_queue_full(char_queue_t *q);

void char_start_chunk(char_queue_t *q);
char_chunk_t *char_next_chunk(char_queue_t *q);
void char_free_chunk(char_chunk_t *pc);