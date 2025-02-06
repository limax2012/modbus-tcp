idf_component_register(SRCS "modbus-tcp-client.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event nvs_flash lwip)
