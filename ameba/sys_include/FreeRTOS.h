#ifndef _FAKE_FREERTOS_H_
#define _FAKE_FREERTOS_H_

#define tskIDLE_PRIORITY    0
#define pdTRUE      1
#define pdFALSE     0
#define pdPASS      pdTRUE

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;
typedef int configSTACK_DEPTH_TYPE;
typedef void * TaskHandle_t;
typedef void (*TaskFunction_t)(void *param);

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode,
                            const char * const pcName,
                            configSTACK_DEPTH_TYPE usStackDepth,
                            void *pvParameters,
                            UBaseType_t uxPriority,
                            TaskHandle_t *pxCreatedTask
                          );
void vTaskDelete( TaskHandle_t xTask );
void vTaskDelay( const TickType_t xTicksToDelay );

#endif


