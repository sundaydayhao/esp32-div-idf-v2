#include "app.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "app_main";

static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        const esp_partition_t* partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
        if (partition != nullptr) {
            ESP_ERROR_CHECK(esp_partition_erase_range(partition, 0, partition->size));
        }
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting ESP-IDF application");
    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_loop_err);
    }

    esp32_div_app_init();
    while (true) {
        esp32_div_app_run_once();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
