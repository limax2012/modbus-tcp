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
#include "esp_netif.h"

#define SERVER_IP "192.168.4.1" // ESP32 Server AP's IP
#define SERVER_PORT 502
#define WIFI_SSID "ModbusESP32"
#define WIFI_PASS "12345678"

static const char *TAG = "MODBUS_CLIENT";
static EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta() {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void modbus_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed, cannot start TCP client.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi connected, attempting TCP connection to %s:%d", SERVER_IP, SERVER_PORT);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed! errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connecting to server...");
    int connection_status = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connection_status < 0) {
        ESP_LOGE(TAG, "TCP connection to %s:%d failed! errno=%d", SERVER_IP, SERVER_PORT, errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connected to Modbus server at %s:%d", SERVER_IP, SERVER_PORT);

    uint8_t request[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x04, 0x00, 0x02};
    if (send(sock, request, sizeof(request), 0) < 0) {
        ESP_LOGE(TAG, "Failed to send Modbus request! errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Sent Modbus request");

    uint8_t buffer[260];
    int len = recv(sock, buffer, sizeof(buffer), 0);
    if (len > 0) {
        ESP_LOGI(TAG, "Received response:");
        for (int i = 0; i < len; i++) printf("%02X ", buffer[i]);
        printf("\n");

        uint16_t reg1 = (buffer[9] << 8) | buffer[10];
        uint16_t reg2 = (buffer[11] << 8) | buffer[12];
        ESP_LOGI(TAG, "Register Values: %04X %04X", reg1, reg2);
    } else {
        ESP_LOGE(TAG, "Failed to receive data from server! errno=%d", errno);
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();
    xTaskCreate(modbus_client_task, "modbus_client_task", 4096, NULL, 5, NULL);
}
