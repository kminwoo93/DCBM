idf_component_register(SRCS "uart.c" "ble_task.c" "main.c"
                    PRIV_REQUIRES bt nvs_flash driver
                    INCLUDE_DIRS ".")
