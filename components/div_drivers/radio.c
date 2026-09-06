#include "div_drivers.h"
#include "div_board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
static const char *TAG = "radio";
static esp_err_t exchange(spi_device_handle_t dev, const uint8_t *tx, uint8_t *rx, size_t n)
{
    spi_transaction_t t = {.length = n * 8, .tx_buffer = tx, .rx_buffer = rx};
    return spi_device_transmit(dev, &t);
}
static esp_err_t nrf_cmd(div_nrf_t *r, uint8_t cmd, const uint8_t *data, uint8_t *out, size_t n)
{
    if (!r || !r->spi || n > 32) return ESP_ERR_INVALID_ARG;
    uint8_t tx[33] = {cmd}, rx[33];
    if (data) memcpy(tx + 1, data, n);
    esp_err_t err = exchange(r->spi, tx, rx, n + 1);
    if (err == ESP_OK && out) memcpy(out, rx + 1, n);
    return err;
}
static esp_err_t nw(div_nrf_t *r, uint8_t reg, uint8_t val)
{ return nrf_cmd(r, 0x20 | reg, &val, NULL, 1); }
static esp_err_t nr(div_nrf_t *r, uint8_t reg, uint8_t *val)
{ return nrf_cmd(r, reg, NULL, val, 1); }
esp_err_t div_nrf_open(unsigned index, div_nrf_t *r)
{
    if (!r || index < 1 || index > 3) return ESP_ERR_INVALID_ARG;
#ifndef CONFIG_DIV_NRF3_ENABLED
    if (index == 3) return ESP_ERR_NOT_SUPPORTED;
#endif
    memset(r, 0, sizeof(*r));
    const int ce[] = {DIV_NRF1_CE, DIV_NRF2_CE, DIV_NRF3_CE};
    const int cs[] = {DIV_NRF1_CS, DIV_NRF2_CS, DIV_NRF3_CS};
    r->ce = ce[index - 1];
    gpio_set_level(r->ce, 0);
    spi_device_interface_config_t config = {.clock_speed_hz = 4000000, .mode = 0,
        .spics_io_num = cs[index - 1], .queue_size = 1};
    ESP_RETURN_ON_ERROR(spi_bus_add_device(DIV_RADIO_HOST, &config, &r->spi), TAG, "NRF SPI");
    esp_err_t err = nw(r, 5, 76);
    uint8_t readback = 0;
    if (err == ESP_OK) err = nr(r, 5, &readback);
    if (err == ESP_OK && readback != 76) err = ESP_ERR_NOT_FOUND;
    const uint8_t regs[][2] = {{0,0x0e},{1,0},{2,1},{3,3},{4,0},{6,0x06},{7,0x70},{0x11,32},{0x1c,1},{0x1d,4}};
    for (size_t i = 0; err == ESP_OK && i < sizeof(regs)/sizeof(regs[0]); ++i) err = nw(r, regs[i][0], regs[i][1]);
    if (err == ESP_OK) err = nrf_cmd(r, 0xe1, NULL, NULL, 0);
    if (err == ESP_OK) err = nrf_cmd(r, 0xe2, NULL, NULL, 0);
    if (err != ESP_OK) { div_nrf_close(r); return err; }
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}
void div_nrf_close(div_nrf_t *r)
{
    if (!r || !r->spi) return;
    gpio_set_level(r->ce, 0);
    nw(r, 6, 0x06);
    nw(r, 0, 0x0c);
    spi_bus_remove_device(r->spi);
    r->spi = NULL;
}
esp_err_t div_nrf_channel(div_nrf_t *r, uint8_t channel)
{
    if (channel > 125) return ESP_ERR_INVALID_ARG;
    gpio_set_level(r->ce, 0);
    return nw(r, 5, channel);
}
esp_err_t div_nrf_listen(div_nrf_t *r, const uint8_t addr[5], uint8_t width)
{
    if (!addr || width > 32) return ESP_ERR_INVALID_ARG;
    gpio_set_level(r->ce, 0);
    ESP_RETURN_ON_ERROR(nrf_cmd(r, 0x2a, addr, NULL, 5), TAG, "RX address");
    ESP_RETURN_ON_ERROR(nw(r, 0x11, width ? width : 32), TAG, "RX width");
    ESP_RETURN_ON_ERROR(nw(r, 0x1c, width ? 0 : 1), TAG, "DPL pipes");
    ESP_RETURN_ON_ERROR(nw(r, 0x1d, width ? 0 : 4), TAG, "DPL feature");
    ESP_RETURN_ON_ERROR(nw(r, 0, 0x0f), TAG, "PRX");
    gpio_set_level(r->ce, 1);
    esp_rom_delay_us(140);
    return ESP_OK;
}
esp_err_t div_nrf_receive(div_nrf_t *r, uint8_t *data, size_t *length)
{
    if (!data || !length || *length < 32) return ESP_ERR_INVALID_ARG;
    uint8_t fifo, feature, n;
    ESP_RETURN_ON_ERROR(nr(r, 0x17, &fifo), TAG, "FIFO");
    if (fifo & 1) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(nr(r, 0x1d, &feature), TAG, "FEATURE");
    if (feature & 4) ESP_RETURN_ON_ERROR(nrf_cmd(r, 0x60, NULL, &n, 1), TAG, "payload width");
    else ESP_RETURN_ON_ERROR(nr(r, 0x11, &n), TAG, "static width");
    if (!n || n > 32) { nrf_cmd(r, 0xe2, NULL, NULL, 0); return ESP_ERR_INVALID_SIZE; }
    ESP_RETURN_ON_ERROR(nrf_cmd(r, 0x61, NULL, data, n), TAG, "RX payload");
    *length = n;
    return nw(r, 7, 0x40);
}
esp_err_t div_nrf_send(div_nrf_t *r, const uint8_t address[5], const uint8_t *data, size_t n)
{
    if (!address || !data || !n || n > 32) return ESP_ERR_INVALID_ARG;
    gpio_set_level(r->ce, 0);
    ESP_RETURN_ON_ERROR(nw(r, 0, 0x0e), TAG, "PTX");
    ESP_RETURN_ON_ERROR(nrf_cmd(r, 0x30, address, NULL, 5), TAG, "TX address");
    ESP_RETURN_ON_ERROR(nw(r, 7, 0x70), TAG, "status");
    ESP_RETURN_ON_ERROR(nrf_cmd(r, 0xe1, NULL, NULL, 0), TAG, "flush TX");
    ESP_RETURN_ON_ERROR(nrf_cmd(r, 0xa0, data, NULL, n), TAG, "TX payload");
    gpio_set_level(r->ce, 1); esp_rom_delay_us(15); gpio_set_level(r->ce, 0);
    int64_t end = esp_timer_get_time() + 20000;
    do {
        uint8_t status;
        ESP_RETURN_ON_ERROR(nr(r, 7, &status), TAG, "TX status");
        if (status & 0x20) return nw(r, 7, 0x20);
        if (status & 0x10) { nw(r, 7, 0x10); return ESP_FAIL; }
        vTaskDelay(1);
    } while (esp_timer_get_time() < end);
    return ESP_ERR_TIMEOUT;
}
esp_err_t div_nrf_power_detect(div_nrf_t *r, bool *detected)
{
    if (!detected) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(nw(r, 0, 0x0f), TAG, "RPD mode");
    gpio_set_level(r->ce, 1); esp_rom_delay_us(180); gpio_set_level(r->ce, 0);
    uint8_t rpd;
    ESP_RETURN_ON_ERROR(nr(r, 9, &rpd), TAG, "RPD");
    *detected = (rpd & 1) != 0;
    return ESP_OK;
}
esp_err_t div_nrf_carrier(div_nrf_t *r, bool enabled)
{
    gpio_set_level(r->ce, 0);
    ESP_RETURN_ON_ERROR(nw(r, 0, 0x0e), TAG, "carrier mode");
    ESP_RETURN_ON_ERROR(nw(r, 6, enabled ? 0x96 : 0x06), TAG, "carrier RF");
    if (enabled) { gpio_set_level(r->ce, 1); esp_rom_delay_us(140); }
    return ESP_OK;
}
static esp_err_t cc_access(div_cc_t *r, uint8_t command, uint8_t *data, size_t len, bool read)
{
    uint8_t tx[65] = {command}, rx[65];
    if (!r || !r->spi || len > 64) return ESP_ERR_INVALID_ARG;
    if (!read && data) memcpy(tx + 1, data, len);
    esp_err_t err = exchange(r->spi, tx, rx, len + 1);
    if (err == ESP_OK && read && data) memcpy(data, rx + 1, len);
    return err;
}
static esp_err_t cw(div_cc_t *r, uint8_t reg, uint8_t value)
{ return cc_access(r, reg, &value, 1, false); }
static esp_err_t cs(div_cc_t *r, uint8_t cmd) { return cc_access(r, cmd, NULL, 0, false); }
esp_err_t div_cc_open(div_cc_t *r, double mhz)
{
    if (!r || !((mhz >= 300 && mhz <= 348) || (mhz >= 387 && mhz <= 464) ||
                (mhz >= 779 && mhz <= 928))) return ESP_ERR_INVALID_ARG;
    memset(r, 0, sizeof(*r));
    spi_device_interface_config_t config = {.clock_speed_hz = 1000000, .mode = 0,
        .spics_io_num = DIV_CC_CS, .queue_size = 1, .cs_ena_pretrans = 8};
    ESP_RETURN_ON_ERROR(spi_bus_add_device(DIV_RADIO_HOST, &config, &r->spi), TAG, "CC1101 SPI");
    esp_err_t err = cs(r, 0x30);
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t version = 0;
    if (err == ESP_OK) err = cc_access(r, 0xf1, &version, 1, true);
    if (err == ESP_OK && (version == 0 || version == 0xff)) err = ESP_ERR_NOT_FOUND;
    /* Asynchronous OOK. GDO0 is the serial input in TX; GDO2 is RX data.
       Receive paths never drive GDO0. Crystal frequency is 26 MHz. */
    static const uint8_t regs[][2] = {
        {0x00,0x0d},{0x02,0x0d},{0x07,0},{0x08,0x32},{0x0b,6},
        {0x10,0x67},{0x11,0x32},{0x12,0x30},{0x13,0x22},{0x14,0xf8},
        {0x15,0x15},{0x17,0},{0x18,0x18},{0x19,0x16},{0x1a,0x6c},
        {0x1b,0x43},{0x1c,0x40},{0x1d,0x91},{0x21,0x56},{0x22,0x11},
        {0x23,0xe9},{0x24,0x2a},{0x25,0},{0x26,0x1f},{0x2c,0x81},{0x2d,0x35},{0x2e,9}
    };
    for (size_t i = 0; err == ESP_OK && i < sizeof(regs)/sizeof(regs[0]); ++i) err = cw(r, regs[i][0], regs[i][1]);
    uint32_t freq = (uint32_t)(mhz * 65536.0 / 26.0 + 0.5);
    if (err == ESP_OK) err = cw(r, 0x0d, freq >> 16);
    if (err == ESP_OK) err = cw(r, 0x0e, freq >> 8);
    if (err == ESP_OK) err = cw(r, 0x0f, freq);
    uint8_t pa[8] = {0, mhz < 500 ? 0x60 : 0x50, 0,0,0,0,0,0};
    if (err == ESP_OK) err = cc_access(r, 0x7e, pa, 8, false);
    if (err == ESP_OK) err = cs(r, 0x33); /* SCAL */
    vTaskDelay(pdMS_TO_TICKS(3));
    if (err != ESP_OK) div_cc_close(r);
    return err;
}
esp_err_t div_cc_receive(div_cc_t *r)
{
    gpio_set_direction(DIV_CC_GDO0, GPIO_MODE_INPUT);
    gpio_set_direction(DIV_CC_GDO2, GPIO_MODE_INPUT);
    ESP_RETURN_ON_ERROR(cs(r, 0x36), TAG, "IDLE");
    return cs(r, 0x34);
}
esp_err_t div_cc_transmit(div_cc_t *r)
{
    ESP_RETURN_ON_ERROR(cs(r, 0x36), TAG, "IDLE");
    /* Switch the chip to TX before enabling the host output. */
    ESP_RETURN_ON_ERROR(cs(r, 0x35), TAG, "TX");
    esp_rom_delay_us(300);
    gpio_set_level(DIV_CC_GDO0, 0);
    return gpio_set_direction(DIV_CC_GDO0, GPIO_MODE_OUTPUT);
}
esp_err_t div_cc_rssi(div_cc_t *r, int *dbm)
{
    if (!dbm) return ESP_ERR_INVALID_ARG;
    uint8_t raw;
    ESP_RETURN_ON_ERROR(cc_access(r, 0xf4, &raw, 1, true), TAG, "RSSI");
    *dbm = (int8_t)raw / 2 - 74;
    return ESP_OK;
}
void div_cc_close(div_cc_t *r)
{
    if (!r || !r->spi) return;
    gpio_set_direction(DIV_CC_GDO0, GPIO_MODE_INPUT);
    cs(r, 0x36);
    cs(r, 0x39);
    spi_bus_remove_device(r->spi);
    r->spi = NULL;
}
