#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "gatts_table_creat_demo.h"
#include "esp_gatt_common_api.h"

#include "ble_task.h"
#include "uart.h"

#define GATTS_TABLE_TAG "GATTS_TABLE_DEMO"

#define PROFILE_NUM                 1
#define PROFILE_APP_IDX             0
#define ESP_APP_ID                  0x55
#define SAMPLE_DEVICE_NAME          "SMART-Organ"
#define SVC_INST_ID                 0

/* The max length of characteristic value. When the GATT client performs a write or prepare write operation,
*  the data length must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
*/
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 500
#define PREPARE_BUF_MAX_SIZE        1024
#define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))

#define ADV_CONFIG_FLAG             (1 << 0)
#define SCAN_RSP_CONFIG_FLAG        (1 << 1)

static uint8_t adv_config_done       = 0;

uint16_t SMART_Organ_handle_table[HRS_IDX_NB];

#define UART_SAMPLE_QUEUE_DEPTH      64
#define BLE_NOTIFY_BATCH_SAMPLES      1
#define BLE_NOTIFY_TASK_STACK        4096
#define BLE_NOTIFY_TASK_PRIO        (tskIDLE_PRIORITY + 6)
#define BLE_NOTIFY_FLUSH_MS         100

typedef enum {
    BLE_SAMPLE_TYPE_DATA  = 0,  // 평소 샘플
    BLE_SAMPLE_TYPE_BEGIN = 1,  // 묶음 시작
    BLE_SAMPLE_TYPE_END   = 2   // 묶음 끝
} ble_sample_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;            // 0=DATA, 1=BEGIN, 2=END  (uart_sample_t.type 그대로 복사)
    uint32_t second;          // ms 단위 타임스탬프 (DATA일 때만 의미 있음)
    uint16_t adc;
    int32_t  current_mA;      // 정수부 (mA)
    uint16_t current_uA_frac; // 소수부 (0~999, uA 단위)
} ble_notify_sample_t;

static QueueHandle_t s_uart_sample_queue = NULL;
static TaskHandle_t s_ble_notify_task_handle = NULL;
static bool s_notify_enabled = false;
static esp_gatt_if_t s_notify_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_notify_conn_id = 0xFFFF;

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t prepare_write_env;
static void ble_notify_task(void *arg);
static bool ble_notify_ready(void);
static void ble_flush_samples(ble_notify_sample_t *samples, size_t count);

//이 장치는 SMART-Organ이라는 이름을 가진 BLE 기기이고, 0x00FF UUID의 GATT 서비스를 제공한다”는 광고 신호를 만드는 부분
#define CONFIG_SET_RAW_ADV_DATA
#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] = {
    /* Flags */
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    /* TX Power Level */
    0x02, ESP_BLE_AD_TYPE_TX_PWR, 0xEB,
    /* Complete 16-bit Service UUIDs */
    0x03, ESP_BLE_AD_TYPE_16SRV_CMPL, 0xFF, 0x00,
    /* Complete Local Name */
    0x0C, ESP_BLE_AD_TYPE_NAME_CMPL,
    'S','M','A','R','T','-','O','r','g','a','n'
};

static uint8_t raw_scan_rsp_data[] = {
    /* Flags */
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    /* TX Power Level */
    0x02, ESP_BLE_AD_TYPE_TX_PWR, 0xEB,
    /* Complete 16-bit Service UUIDs */
    0x03, ESP_BLE_AD_TYPE_16SRV_CMPL, 0xFF, 0x00
};

#else
static uint8_t service_uuid[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};


/* The length of adv data must be less than 31 bytes */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval        = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance          = 0x00,
    .manufacturer_len    = 0,    //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, //test_manufacturer,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, //&test_manufacturer[0],
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
#endif /* CONFIG_SET_RAW_ADV_DATA */

//광고(Advertising) 동작 방식을 설정하는 구조체, 모든 장치가 스캔·연결 가능하며 20~40 ms 간격으로 광고 송출.
static esp_ble_adv_params_t adv_params = {
    .adv_int_min         = 0x20,
    .adv_int_max         = 0x40,
    .adv_type            = ADV_TYPE_IND,
    .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
    .channel_map         = ADV_CHNL_ALL,
    .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
//GATT 프로파일(서비스+특성+디스크립터) 의 상태를 저장하는 구조체
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
                                        
/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
//GATT 프로파일 인스턴스(여기서는 1개만 존재)를 배열 형태로 정의.
static struct gatts_profile_inst smart_organ_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/* UUIDs and properties for SMART_Organ profile */
#define UUID_SVC        0x00FF
#define UUID_CHAR_RW    0xFF01
#define UUID_CHAR_NTF   0xFF02

static const uint16_t primary_service_uuid         = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid   = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint16_t uuid_svc_value    = UUID_SVC;
static const uint16_t uuid_char_rw_val  = UUID_CHAR_RW;
static const uint16_t uuid_char_ntf_val = UUID_CHAR_NTF;
static const uint8_t  prop_rw_value     = (ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE);
static const uint8_t  prop_ntf_value    = (ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY);
static const uint8_t  notify_ccc_init[2] = {0x00, 0x00};

/* GATT Database: Service + RW + NTF + CCCD */
static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    /* Service Declaration */
    [IDX_SVC] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
      sizeof(uint16_t), sizeof(uuid_svc_value), (uint8_t *)&uuid_svc_value}},

    /* Characteristic RW Declaration */
    [IDX_CHAR_RW] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&prop_rw_value}},

    /* Characteristic RW Value - BY_APP response */
    [IDX_CHAR_VAL_RW] =
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_rw_val, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, 0, NULL}},

    /* Characteristic NTF Declaration */
    [IDX_CHAR_NTF] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&prop_ntf_value}},

    /* Characteristic NTF Value (AUTO_RSP) */
    [IDX_CHAR_VAL_NTF] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&uuid_char_ntf_val, ESP_GATT_PERM_READ,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, 0, NULL}},

    /* CCCD for NTF */
    [IDX_CHAR_CFG_NTF] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(notify_ccc_init), (uint8_t *)notify_ccc_init}},
};

//GAP(GENERIC ACCESS PROFILE) 이벤트를 처리하는 함수
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    #ifdef CONFIG_SET_RAW_ADV_DATA
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
    #else
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
    #endif
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            /* advertising start complete event to indicate advertising start successfully or failed */
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed");
            }else{
                ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, conn_int = %d, latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
            break;
        default:
            break;
    }
}

//긴 입력 받을때 조각 씩 받는 함수
static void example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.offset > PREPARE_BUF_MAX_SIZE) {
        status = ESP_GATT_INVALID_OFFSET;
    } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
        status = ESP_GATT_INVALID_ATTR_LEN;
    }
    if (status == ESP_GATT_OK && prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    }

    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp){
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL){
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK) {
               ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        }else{
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    }
    if (status != ESP_GATT_OK){
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}
//Prepare Write로 조각조각 받은 데이터를 최종적으로 “실행(Execute)”하거나 “취소(Cancel)”하는 단계
static void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf){
        ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TABLE_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}
//지금 Notify를 보낼 수 있는 상태인지 확인하는 함수.
static bool ble_notify_ready(void)
{
    return s_notify_enabled &&
           s_notify_gatts_if != ESP_GATT_IF_NONE &&
           SMART_Organ_handle_table[IDX_CHAR_VAL_NTF] != 0 &&
           s_notify_conn_id != 0xFFFF;
}

//samples 배열에 들어 있는 샘플들을 한 번에 BLE Notify로 쏘는 함수.
static void ble_flush_samples(ble_notify_sample_t *samples, size_t count)
{
    if (!ble_notify_ready() || count == 0) {
        return;
    }

    size_t payload_len = count * sizeof(ble_notify_sample_t);

    const ble_notify_sample_t *first = &samples[0];
    ESP_LOGI(GATTS_TABLE_TAG,
             "BLE Notify: %u samples (payload=%u bytes) conn=%u "
             "first: type=%u, t=%u0 ms, I=%ld.%03u uA",
             (unsigned)count,
             (unsigned)payload_len,
             (unsigned)s_notify_conn_id,
             (unsigned)first->type,
             (unsigned)first->second,
             (long)first->current_mA,
             (unsigned)first->current_uA_frac);

    esp_err_t err = esp_ble_gatts_send_indicate(
        s_notify_gatts_if,
        s_notify_conn_id,
        SMART_Organ_handle_table[IDX_CHAR_VAL_NTF],
        payload_len,
        (uint8_t *)samples,
        false);

    if (err != ESP_OK) {
        ESP_LOGW(GATTS_TABLE_TAG, "notify failed: %s", esp_err_to_name(err));
    }
}


//UART에서 들어온 샘플 큐를 읽어서 batch로 묶고, 주기적으로 BLE Notify를 보내는 FreeRTOS task.
//UART 샘플 큐를 소비해서 4개씩 또는 100ms마다 한 번씩 BLE Notify로 묶어서 보내는 background task
static void ble_notify_task(void *arg)
{
    uart_sample_t input;
    ble_notify_sample_t batch[BLE_NOTIFY_BATCH_SAMPLES];
    size_t batch_count = 0;
    const TickType_t flush_ticks = pdMS_TO_TICKS(BLE_NOTIFY_FLUSH_MS);

    while (1) {
        if (xQueueReceive(s_uart_sample_queue, &input, flush_ticks) == pdTRUE) {

            if (!s_notify_enabled) {
                // notify 꺼져 있을 때도 큐는 드레인만 해줌
                continue;
            }

            // 1) BEGIN / END 마커 처리
            if (input.type == UART_SAMPLE_TYPE_BEGIN ||
                input.type == UART_SAMPLE_TYPE_END) {

                // 먼저 현재까지 쌓인 DATA 배치를 flush
                if (batch_count > 0) {
                    ble_flush_samples(batch, batch_count);
                    batch_count = 0;
                }

                // BEGIN/END는 한 샘플만 단독으로 바로 보냄
                ble_notify_sample_t marker = {0};
                marker.type = (uint8_t)input.type;
                marker.second = input.second;        // 필요 없으면 0으로 둬도 됨
                marker.adc    = 0; 
                marker.current_mA = 0;
                marker.current_uA_frac = 0;

                ble_flush_samples(&marker, 1);
                continue;
            }

            // 2) 그 외는 모두 DATA 샘플
            if (input.type != UART_SAMPLE_TYPE_DATA) {
                ESP_LOGW(GATTS_TABLE_TAG,
                         "Unknown uart_sample type=%d, dropping", (int)input.type);
                continue;
            }

            // DATA 샘플을 batch에 쌓기
            ble_notify_sample_t *slot = &batch[batch_count++];
            slot->type = (uint8_t)input.type;
            slot->second = input.second;
            slot->adc    = (uint16_t)input.adc;
            slot->current_mA = input.current_uA / 1000;
            int32_t frac = input.current_uA % 1000;
            if (frac < 0) {
                frac = -frac;
            }
            slot->current_uA_frac = (uint16_t)frac;

            if (batch_count >= BLE_NOTIFY_BATCH_SAMPLES) {
                ble_flush_samples(batch, batch_count);
                batch_count = 0;
            }
        } else if (batch_count > 0) {
            // flush timeout 발생 → 남아 있던 DATA 샘플만 flush
            ble_flush_samples(batch, batch_count);
            batch_count = 0;
        }
    }
}


//GATT 프로파일 콜백(프로파일별 이벤트 처리기) 로서, 연결·읽기·쓰기·서비스 생성 등 GATT 단의 모든 이벤트를 처리
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    //앱 등록 완료 → 광고 설정 + Attribute Table 생성
    switch (event) {
        case ESP_GATTS_REG_EVT:{
            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);
            if (set_dev_name_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
            }
            #ifdef CONFIG_SET_RAW_ADV_DATA
            esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
            if (raw_adv_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
            }
            adv_config_done |= ADV_CONFIG_FLAG;
            esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
            if (raw_scan_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
            }
            adv_config_done |= SCAN_RSP_CONFIG_FLAG;
            #else
            //config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
            }
            adv_config_done |= ADV_CONFIG_FLAG;
            //config scan response data
            ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
            }
            adv_config_done |= SCAN_RSP_CONFIG_FLAG;
            #endif
            esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, SVC_INST_ID);
            if (create_attr_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
            }
        }
        break;

            //클라이언트가 읽기 요청
        case ESP_GATTS_READ_EVT: {
            if (param->read.handle == SMART_Organ_handle_table[IDX_CHAR_VAL_RW]) {
                
                //보낼 데이터 정의
                uint8_t buf[2] = {0x01, 0x07};
                
                //응답 구조체 생성 및 채우기
                esp_gatt_rsp_t rsp = {0};
                rsp.attr_value.handle = param->read.handle;
                rsp.attr_value.len = sizeof(buf);
                memcpy(rsp.attr_value.value, buf, sizeof(buf));

                //응답 전송
                esp_ble_gatts_send_response(gatts_if, 
                                           param->read.conn_id,
                                           param->read.trans_id,
                                           ESP_GATT_OK,
                                           &rsp);
                //터미널 출력용 로그
                ESP_LOGI(GATTS_TABLE_TAG, "READ RW: len=%d HEX=%02X %02X", rsp.attr_value.len, buf[0], buf[1]);

            }
            break;
        }
        //쓰기 요청
        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep){
                /* Log incoming write */
                ESP_LOGI(GATTS_TABLE_TAG, "WRITE EVT: handle=%d len=%d", param->write.handle, param->write.len);
                ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, param->write.value, param->write.len);

                /* CCCD for NTF */
                if (param->write.handle == SMART_Organ_handle_table[IDX_CHAR_CFG_NTF] && param->write.len == 2) {
                    uint16_t cccd = param->write.value[1] << 8 | param->write.value[0];
                    if (cccd == 0x0001) {
                        s_notify_enabled = true;
                        ESP_LOGI(GATTS_TABLE_TAG, "notify enable (conn %d)", param->write.conn_id);
                    } else if (cccd == 0x0000) {
                        s_notify_enabled = false;
                        ESP_LOGI(GATTS_TABLE_TAG, "notify disable");
                    } else {
                        ESP_LOGW(GATTS_TABLE_TAG, "unknown CCCD value: 0x%04x", cccd);
                    }
                }

                /* Trigger UART send when RW characteristic receives "01" */
                if (param->write.handle == SMART_Organ_handle_table[IDX_CHAR_VAL_RW]) {


                    const uint8_t *v = param->write.value;
                    int len = param->write.len;

                    if (len == 1) {

                        // 1바이트 그대로 UART로 전송
                        ESP_LOGI(GATTS_TABLE_TAG, "RW char: forwarding 1 raw byte 0x%02X to UART", v[0]);
                        uart_app_send_raw(v, 1);
                    } else {
                        ESP_LOGW(GATTS_TABLE_TAG, "RW char write len=%d (expected 1). Ignored.", len);
                    }
                }
                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            } else {
                /* handle prepare write */
                example_prepare_write_event_env(gatts_if, &prepare_write_env, param);
            }
            break;
        //Prepare Write 커밋/취소
        case ESP_GATTS_EXEC_WRITE_EVT:
            // the length of gattc prepare write data must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            example_exec_write_event_env(&prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        //Notify/Indicate 확인 콜백
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d", param->conf.status, param->conf.handle);
            break;
        //서비스 시작됨
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        //연결됨
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
            ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
            s_notify_conn_id = param->connect.conn_id;
            s_notify_enabled = false;
            {
                esp_ble_conn_update_params_t conn_params = {0};
                memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
                /* For the iOS system, please refer to Apple official documents about the BLE connection parameters restrictions. */
                conn_params.latency = 0;
                conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
                conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
                conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
                //start sent the update connection parameters to the peer device.
                esp_ble_gap_update_conn_params(&conn_params);
            }
            break;
        //연결 끊김
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
            s_notify_conn_id = 0xFFFF;
            s_notify_enabled = false;
            if (s_uart_sample_queue) {
                xQueueReset(s_uart_sample_queue);
            }
            esp_ble_gap_start_advertising(&adv_params);
            break;

        //Attribute Table 생성 결과
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            if (param->add_attr_tab.status != ESP_GATT_OK){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            }
            else if (param->add_attr_tab.num_handle != HRS_IDX_NB){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, HRS_IDX_NB);
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d",param->add_attr_tab.num_handle);
                memcpy(SMART_Organ_handle_table, param->add_attr_tab.handles, sizeof(SMART_Organ_handle_table));
                esp_ble_gatts_start_service(SMART_Organ_handle_table[IDX_SVC]);
            }
            break;
        }
        case ESP_GATTS_STOP_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        case ESP_GATTS_UNREG_EVT:
        case ESP_GATTS_DELETE_EVT:
        default:
            break;
    }
}

//GATT 서버의 “이벤트 디스패처(분배기)” 역할. 스택에서 올라오는 모든 GATT 이벤트를 받아서, 각 프로파일의 콜백으로 알맞게 전달.
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            smart_organ_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
            // 👉 BLE Notify용 인터페이스도 여기서 저장
            s_notify_gatts_if = gatts_if;
            ESP_LOGI(GATTS_TABLE_TAG, "GATTS_REG_EVT: gatts_if=%d stored for notify", gatts_if);
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == smart_organ_profile_tab[idx].gatts_if) {
                if (smart_organ_profile_tab[idx].gatts_cb) {
                    smart_organ_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void ble_task_init(void)
{
    esp_err_t ret;

    // Initialize UART and shared queue before starting worker tasks
    ESP_ERROR_CHECK(uart_app_init());

    s_uart_sample_queue = xQueueCreate(UART_SAMPLE_QUEUE_DEPTH, sizeof(uart_sample_t));
    if (s_uart_sample_queue == NULL) {
        ESP_LOGE(GATTS_TABLE_TAG, "Failed to allocate UART sample queue");
        return;
    }
    uart_set_measurement_queue(s_uart_sample_queue);

    if (xTaskCreate(ble_notify_task, "ble_notify", BLE_NOTIFY_TASK_STACK, NULL,
                    BLE_NOTIFY_TASK_PRIO, &s_ble_notify_task_handle) != pdPASS) {
        ESP_LOGE(GATTS_TABLE_TAG, "Failed to create BLE notify task");
        return;
    }

    ESP_ERROR_CHECK(uart_app_start_rx_task());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));     //클래식 BT(브리/EDR) 메모리를 반납해서 RAM 절약. BLE만 쓸 것이므로 Classic BT용 버퍼를 해제.

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();    //BT 컨트롤러 기본 설정 구조체를 생성(기본값 매크로로 초기화).
    ret = esp_bt_controller_init(&bt_cfg);                                      //BT 컨트롤러 드라이버 초기화. 아직 전원(Enable)은 아님.
    //초기화 실패 시 에러 로그 출력 후 함수 종료.
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);    //BT 컨트롤러 활성화(전원 인가). 모드는 BLE 전용으로 설정.
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();                         //Bluedroid(ESP-IDF의 BT/BLE 호스트 스택) 초기화. 아직 Enable은 아님.
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();                       //Bluedroid 스택 활성화. 여기부터 BLE API 사용 가능.
    if (ret) {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);     //GATT Server 콜백 등록. GATT 이벤트가 발생할 때 gatts_event_handler가 호출됨.
    if (ret){
        ESP_LOGE(GATTS_TABLE_TAG, "gatts register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);         //GAP 콜백 등록. 광고 시작/정지, 연결, 보안 등 링크 레벨 이벤트는 gap_event_handler로 전달됨.
    if (ret){
        ESP_LOGE(GATTS_TABLE_TAG, "gap register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gatts_app_register(ESP_APP_ID);                   //GATT 애플리케이션 등록. ESP_APP_ID 식별자로 GATT 프로파일을 OS에 등록(이후 GATTS_REG_EVT 콜백이 옴).
    if (ret){
        ESP_LOGE(GATTS_TABLE_TAG, "gatts app register error, error code = %x", ret);
        return;
    }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);      //로컬 GATT MTU 크기 설정(최대 500바이트로 요청). 실제 MTU는 연결 상대와 협상되어 min(내요청, 상대요청, 컨트롤러한계)가 적용됨.
    if (local_mtu_ret){
        ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
}
