#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#define PORT 502
#define BUFFER_SIZE 260

static const char *TAG = "MODBUS_SERVER";

// Example holding registers (for testing)
uint16_t holding_registers[10] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666};

void process_modbus_request(int client_sock, uint8_t *request, int request_len) {
    if (request_len < 12) {
        ESP_LOGE(TAG, "Invalid Modbus TCP request (too short)");
        return;
    }

    uint16_t transaction_id = (request[0] << 8) | request[1];
    uint8_t function_code = request[7];
    uint16_t start_addr = (request[8] << 8) | request[9];
    uint16_t num_regs = (request[10] << 8) | request[11];

    ESP_LOGI(TAG, "Received Modbus request: FC=0x%02X, StartAddr=%d, NumRegs=%d", function_code, start_addr, num_regs);

    uint8_t response[BUFFER_SIZE];
    memcpy(response, request, 7); // Copy Transaction ID & Protocol ID
    response[4] = 0x00;
    response[5] = (num_regs * 2) + 3; // Response length
    response[7] = function_code;
    response[8] = num_regs * 2; // Byte count

    if (start_addr + num_regs > 10) {
        ESP_LOGE(TAG, "Invalid register request: Out of range");
        response[7] = function_code | 0x80; // Exception response
        response[8] = 0x02; // Illegal Data Address
        send(client_sock, response, 9, 0);
        return;
    }

    int response_len = 9;
    for (int i = 0; i < num_regs; i++) {
        response[response_len++] = holding_registers[start_addr + i] >> 8;
        response[response_len++] = holding_registers[start_addr + i] & 0xFF;
    }

    send(client_sock, response, response_len, 0);
}

void modbus_server_task(void *pvParameters) {
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in server_addr = {.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);

    ESP_LOGI(TAG, "Modbus TCP Server listening on port %d", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock < 0) continue;
        ESP_LOGI(TAG, "Client connected");

        uint8_t buffer[BUFFER_SIZE];
        int len = recv(client_sock, buffer, sizeof(buffer), 0);
        if (len > 0) process_modbus_request(client_sock, buffer, len);

        close(client_sock);
    }

    close(server_sock);
    vTaskDelete(NULL);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t wifi_config = {
        .ap = {.ssid = "ModbusESP32", .ssid_len = strlen("ModbusESP32"), .password = "12345678", .max_connection = 4, .authmode = WIFI_AUTH_WPA_WPA2_PSK}};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(modbus_server_task, "modbus_server_task", 4096, NULL, 5, NULL);
}
