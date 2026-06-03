#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// UART configuration (adjust pins as needed for your board)
#ifndef UART_PORT_NUM
#include "driver/uart.h"
#define UART_PORT_NUM UART_NUM_1
#endif

#ifndef UART_TX_PIN
#define UART_TX_PIN 24
#endif

#ifndef UART_RX_PIN
#define UART_RX_PIN 23
#endif

#ifndef UART_BAUD_RATE
#define UART_BAUD_RATE 115200
#endif

// Initialize UART for PIC communication
esp_err_t uart_app_init(void);

// Send a null-terminated string over UART
esp_err_t uart_app_send(const char *str);



// Start a background task that reads from UART and logs to terminal
esp_err_t uart_app_start_rx_task(void);

//To send raw byte
esp_err_t uart_app_send_raw(const uint8_t *data, size_t len);

typedef enum {
    UART_SAMPLE_TYPE_DATA  = 0,  // 일반 CSV 데이터
    UART_SAMPLE_TYPE_BEGIN = 1,  // 측정 시작
    UART_SAMPLE_TYPE_END   = 2,  // 측정 끝
} uart_sample_type_t;

typedef struct {
    uart_sample_type_t type;
    uint32_t second;
    uint32_t adc;
    uint32_t voltage_mv;
    int32_t  current_uA;
} uart_sample_t;

void uart_set_measurement_queue(QueueHandle_t queue);
