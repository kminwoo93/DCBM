#include "uart.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/uart.h"

static const char *UART_TAG = "UART_APP";
static TaskHandle_t s_uart_rx_task_handle = NULL;
static char line_buf[256];
static QueueHandle_t s_measurement_queue = NULL;

void uart_set_measurement_queue(QueueHandle_t queue)
{
    s_measurement_queue = queue;
}

static void uart_rx_task(void *arg)
{
    uint8_t rx_buf[128];
    static int line_pos = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)rx_buf[i];

                // 한 줄(\n 또는 \r) 완성
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';

                        // 공백 스킵
                        char *p = line_buf;
                        while (*p == ' ' || *p == '\t') p++;

                        // 1) BEGIN / END 마커 먼저 체크
                        if (strcmp(p, "BEGIN") == 0) {
                            ESP_LOGI(UART_TAG, "UART marker: BEGIN");
                            if (s_measurement_queue) {
                                uart_sample_t sample = {
                                    .type       = UART_SAMPLE_TYPE_BEGIN,
                                    .second     = 0,
                                    .adc        = 0,
                                    .voltage_mv = 0,
                                    .current_uA = 0,
                                };
                                if (xQueueSend(s_measurement_queue, &sample, 0) != pdTRUE) {
                                    ESP_LOGW(UART_TAG, "sensor queue full, dropping BEGIN marker");
                                }
                            }
                        }
                        else if (strcmp(p, "END") == 0) {
                            ESP_LOGI(UART_TAG, "UART marker: END");
                            if (s_measurement_queue) {
                                uart_sample_t sample = {
                                    .type       = UART_SAMPLE_TYPE_END,
                                    .second     = 0,
                                    .adc        = 0,
                                    .voltage_mv = 0,
                                    .current_uA = 0,
                                };
                                if (xQueueSend(s_measurement_queue, &sample, 0) != pdTRUE) {
                                    ESP_LOGW(UART_TAG, "sensor queue full, dropping END marker");
                                }
                            }
                        }
                        // 2) 그 외는 기존 CSV ("sec,ADC,v_mV,i_nA")
                        else {
                            unsigned int second, ADC, v_mV;
                            long i_nA;
                            if (sscanf(p, "%u,%u,%u,%ld", &second, &ADC, &v_mV, &i_nA) == 4) {
                                int i_uA = (int)i_nA;  // nA 그대로 int로
                                ESP_LOGI(UART_TAG,
                                         "UART CSV → msec=%u00 | ADC=%u | V=%u mV | I=%d.%03d uA",
                                         second, ADC, v_mV, i_uA / 1000, abs(i_uA % 1000));

                                if (s_measurement_queue) {
                                    uart_sample_t sample = {
                                        .type       = UART_SAMPLE_TYPE_DATA,
                                        .second     = second,
                                        .adc        = ADC,
                                        .voltage_mv = v_mV,
                                        .current_uA = i_uA,
                                    };
                                    if (xQueueSend(s_measurement_queue, &sample, 0) != pdTRUE) {
                                        ESP_LOGW(UART_TAG, "sensor queue full, dropping sample");
                                    }
                                }
                            } else {
                                ESP_LOGW(UART_TAG, "Parse error: %s", p);
                            }
                        }

                        // 한 줄 처리 끝났으면 리셋
                        line_pos = 0;
                    }
                }
                // 개행이 아니면 line_buf에 계속 누적
                else {
                    if (line_pos < (int)sizeof(line_buf) - 1) {
                        line_buf[line_pos++] = c;
                    } else {
                        // 라인 너무 길면 버리고 리셋
                        ESP_LOGW(UART_TAG, "Line buffer overflow, dropping line");
                        line_pos = 0;
                    }
                }
            }
        }
    }
}


esp_err_t uart_app_init(void)
{
    const int rx_buffer_size = 1024;

    // Install UART driver with RX buffer; no TX buffer, no event queue
    esp_err_t err = uart_driver_install(UART_PORT_NUM, rx_buffer_size, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(UART_TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(UART_TAG, "Initialized UART%u TX=%d RX=%d %d baud", (unsigned)UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t uart_app_send(const char *str)
{
    if (str == NULL) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(str);
    if (len == 0) return ESP_OK;

    int tx_bytes = uart_write_bytes(UART_PORT_NUM, str, len);
    if (tx_bytes < 0) {
        ESP_LOGE(UART_TAG, "uart_write_bytes failed");
        return ESP_FAIL;
    }
    // Optionally wait briefly for TX to complete
    (void)uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    ESP_LOGI(UART_TAG, "Sent %d bytes", tx_bytes);
    return ESP_OK;
}

esp_err_t uart_app_start_rx_task(void)
{
    if (s_uart_rx_task_handle != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, tskIDLE_PRIORITY + 5, &s_uart_rx_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(UART_TAG, "Failed to create UART RX task");
        s_uart_rx_task_handle = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(UART_TAG, "UART RX task started");
    return ESP_OK;
}

esp_err_t uart_app_send_raw(const uint8_t *data, size_t len)
{
    if (data == NULL) return ESP_ERR_INVALID_ARG;
    if (len == 0) return ESP_OK;

    int tx_bytes = uart_write_bytes(UART_PORT_NUM, (const char *)data, len);
    if (tx_bytes < 0) {
        ESP_LOGE(UART_TAG, "uart_write_bytes (raw) failed");
        return ESP_FAIL;
    }

    // 필요하면 TX 완료까지 잠깐 대기
    (void)uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    ESP_LOGI(UART_TAG, "Sent %d raw bytes", tx_bytes);
    return ESP_OK;
}
