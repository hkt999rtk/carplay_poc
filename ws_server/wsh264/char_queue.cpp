#include "char_queue.h"
#include <iostream>
#include <unistd.h>
#include <sys/time.h>

using namespace std;
void char_queue_init(char_queue_t *q)
{
	memset(q, 0, sizeof(char_queue_t));
}

void char_queue_push(char_queue_t *q, uint8_t c)
{
	q->buffer[q->tail] = c;
	q->tail = (q->tail + 1) % sizeof(q->buffer);
}

void char_queue_pop(char_queue_t *q, uint8_t *c)
{
	*c = q->buffer[q->head];
	q->head = (q->head + 1) % sizeof(q->buffer);
}

bool char_queue_empty(char_queue_t *q)
{
	return q->head == q->tail;
}

bool char_queue_full(char_queue_t *q)
{
	return (q->tail + 1) % sizeof(q->buffer) == q->head;
}

void char_start_chunk(char_queue_t *q)
{
    uint8_t c;
    int state = 0;
    while (state != 4) {
        if (char_queue_empty(q)) {
            struct timespec tim, tim2;
            tim.tv_sec = 0;
            tim.tv_nsec = 1000000;
            nanosleep(&tim, &tim2);
            continue;
        }
        char_queue_pop(q, &c);
        switch (state) {
            case 0:
            case 1:
            case 2:
                if (c == 0x00) state++;
                break;
            case 3:
                if (c == 0x01) state++; else state = 0;
                break;
        }
    }
}

#define DEFAULT_CHUNK_SIZE	16384
void char_chunk_putc(char_chunk_t *pc, uint8_t c)
{
    if (pc->size == 0) {
        pc->pbuf = (uint8_t *)realloc(pc->pbuf, pc->size + DEFAULT_CHUNK_SIZE);
        pc->size += DEFAULT_CHUNK_SIZE;
    }
    pc->pbuf[pc->size - 1] = c;
    pc->size--;
}

char_chunk_t *char_next_chunk(char_queue_t *q)
{
    static struct timeval start = {0, 0};
    static long avg_ms = 30;
    struct timeval end;

    char_chunk_t *pc = (char_chunk_t *)malloc(sizeof(char_chunk_t));
    if (pc == nullptr) {
        cerr << "malloc failed" << endl;
        return nullptr;
    }
    memset(pc, 0, sizeof(char_chunk_t));
    uint8_t c;

    // continue reading until next tag
    int state = 0;
    while (state != 4) {
        if (char_queue_empty(q)) {
            struct timespec tim, tim2;
            tim.tv_sec = 0;
            tim.tv_nsec = 1000000;
            nanosleep(&tim, &tim2);
            continue;
        }
        char_queue_pop(q, &c);
        char_chunk_putc(pc, c);
        switch (state) {
            case 0:
            case 1:
            case 2:
                if (c == 0x00) state++;
                break;
            case 3:
                if (c == 0x01) state++; else state = 0;
                break;
        }
    }
    if (start.tv_sec == 0) {
        gettimeofday(&start, nullptr);
    } else {
        gettimeofday(&end, nullptr);
        long elapsed_time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        start = end;
        avg_ms = (avg_ms * 9 + elapsed_time_ms) / 10;
        cout << "avg_ms: " << avg_ms << endl;
    }

    return pc;
}

void char_free_chunk(char_chunk_t *pc)
{
    if (pc != nullptr) {
        if (pc->pbuf != nullptr) {
            free(pc->pbuf);
        }
        free(pc);
    }
}