#include "common.h"

QueueHandle_t uart_queue;
QueueHandle_t FileName = NULL;
QueueHandle_t temperature_queue;

uart_event_t event;

SemaphoreHandle_t xSwitchSemaphore = NULL;
SemaphoreHandle_t ySwitchSemaphore = NULL;
SemaphoreHandle_t zSwitchSemaphore = NULL;
SemaphoreHandle_t eSwitchSemaphore = NULL;
SemaphoreHandle_t StartButtonSemaphore = NULL;
SemaphoreHandle_t TestButtonSemaphore = NULL;
SemaphoreHandle_t parseSemaphore = NULL;
SemaphoreHandle_t tempReachedSemaphore = NULL;

SemaphoreHandle_t SDCardMutex = NULL;
SemaphoreHandle_t i2c_mutex = NULL;

SemaphoreHandle_t allowMove = NULL;
SemaphoreHandle_t nohead = NULL;

TaskHandle_t parserHandle = NULL;

MoveCmd_t head = {
    .target_x = 0.0f,
    .target_y = 0.0f,
    .target_z = 0.0f,
    .moveE = 0.0f,
    .center_x = 0.0f,
    .center_y = 0.0f,
    .circleDir = false,
    .feed_rate_hz = 5000};
