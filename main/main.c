#include "esp_log.h"
#include "esp_wifi.h"
#include "tinyusb.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "usb_hid_ds4.h"
#include "web_server.h"
#include <string.h>

static const char *TAG = "MAIN";

#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASSWORD
#define WIFI_MAX_RETRY 10

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
static int retry_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGW(TAG, "Reintentando WiFi (%d/%d)", retry_count, WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Abre http://" IPSTR ":7777 en tu navegador", IP2STR(&event->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
}

static void hid_task(void *pvParam) {
    bool was_mounted   = false;
    bool was_suspended = false;
    bool was_ready     = false;
    uint32_t hb = 0, queued = 0, rejected = 0, not_ready = 0;
    TickType_t last_wake = xTaskGetTickCount();

#if configTICK_RATE_HZ < 250
#error "The HID task needs a FreeRTOS tick of 4 ms or less"
#endif

    while (1) {
        bool mounted   = tud_mounted();
        bool suspended = tud_suspended();
        bool ready     = mounted && !suspended && tud_hid_ready();

        if (mounted && !was_mounted)
            ESP_LOGI(TAG, ">>> USB MOUNT (consola enumeró) <<<");
        if (!mounted && was_mounted)
            ESP_LOGW(TAG, "<<< USB UNMOUNT >>>");
        if (suspended && !was_suspended)
            ESP_LOGW(TAG, "USB SUSPENDED");
        if (!suspended && was_suspended)
            ESP_LOGI(TAG, "USB RESUMED");
        if (ready && !was_ready)
            ESP_LOGI(TAG, "HID READY");

        was_mounted   = mounted;
        was_suspended = suspended;
        was_ready     = ready;

        if (ready) {
            ds4_action_t snapshot;
            web_controller_state_snapshot(&snapshot);
            if (ds4_send_report(&snapshot)) queued++;
            else rejected++;
        } else {
            not_ready++;
        }

        if (++hb >= 1250) {
            hb = 0;
            ESP_LOGI(TAG, "HID: mount=%d susp=%d ready=%d queued=%lu rejected=%lu not_ready=%lu",
                     mounted, suspended, ready, (unsigned long)queued,
                     (unsigned long)rejected, (unsigned long)not_ready);
            queued = rejected = not_ready = 0;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(4));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "Iniciando USB HID DS4...");
    usb_hid_ds4_init();

    ESP_LOGI(TAG, "Conectando a WiFi...");
    wifi_init();

    ESP_LOGI(TAG, "Iniciando servidor web...");
    web_server_init();

    xTaskCreate(hid_task, "hid_task", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "Todo listo. Conecta el ESP32-S3 por USB a la PS4.");
}
