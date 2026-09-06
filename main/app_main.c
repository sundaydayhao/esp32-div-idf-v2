#include "div_board.h"
#include "div_storage.h"
#include "div_service.h"
#include "div_ui.h"
#include "esp_check.h"
void app_main(void)
{
    ESP_ERROR_CHECK(div_board_init());
    ESP_ERROR_CHECK(div_storage_init());
    ESP_ERROR_CHECK(div_service_init());
    esp_err_t err = div_ui_init();
    if (err != ESP_OK) div_report("Display unavailable: %s; use UART console", esp_err_to_name(err));
    ESP_ERROR_CHECK(div_console_init());
}
