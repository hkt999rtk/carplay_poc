#include "FreeRTOS.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct bridge_t_ {
  TaskFunction_t pfunc;
  void *param;
} bridge_t;

static void *bridge(void* data)
{
  bridge_t *b = (bridge_t *)data;
  b->pfunc(b->param);
  pthread_exit(NULL);
  free(b);
}

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char * const pcName,
  configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters, UBaseType_t uxPriority,
  TaskHandle_t *pxCreatedTask)
{
    bridge_t *b = (bridge_t *)malloc(sizeof(bridge_t));
    b->pfunc = pvTaskCode;
    b->param = pvParameters;

    pthread_t t;
    pthread_create(&t, NULL, bridge, b);

    return pdPASS;
}

void vTaskDelete( TaskHandle_t xTask )
{
    // do nothing
}

void vTaskDelay( const TickType_t xTicksToDelay )
{
    usleep(xTicksToDelay * 1000);
}