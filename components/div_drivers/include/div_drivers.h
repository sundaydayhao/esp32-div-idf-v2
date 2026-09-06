#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "driver/spi_master.h"
typedef struct { spi_device_handle_t spi; int ce; } div_nrf_t;
esp_err_t div_nrf_open(unsigned index, div_nrf_t *radio);
void div_nrf_close(div_nrf_t *radio);
esp_err_t div_nrf_channel(div_nrf_t *radio, uint8_t channel);
esp_err_t div_nrf_listen(div_nrf_t *radio, const uint8_t address[5], uint8_t width);
esp_err_t div_nrf_receive(div_nrf_t *radio, uint8_t *data, size_t *length);
esp_err_t div_nrf_send(div_nrf_t *radio, const uint8_t address[5], const uint8_t *data, size_t length);
esp_err_t div_nrf_power_detect(div_nrf_t *radio, bool *detected);
esp_err_t div_nrf_carrier(div_nrf_t *radio, bool enabled);
typedef struct { spi_device_handle_t spi; } div_cc_t;
esp_err_t div_cc_open(div_cc_t *radio, double mhz);
esp_err_t div_cc_receive(div_cc_t *radio);
esp_err_t div_cc_transmit(div_cc_t *radio);
esp_err_t div_cc_rssi(div_cc_t *radio, int *dbm);
void div_cc_close(div_cc_t *radio);
#define DIV_PULSE_MAX 1024
typedef struct {
    uint32_t carrier_hz, count;
    uint16_t duration[DIV_PULSE_MAX];
    uint8_t level[DIV_PULSE_MAX];
} div_pulses_t;
typedef bool (*div_cancel_fn)(void);
esp_err_t div_pulse_capture(int gpio, bool invert, uint32_t timeout_ms, div_pulses_t *out, div_cancel_fn cancel);
esp_err_t div_pulse_send(int gpio, const div_pulses_t *pulses);
typedef struct { uint8_t uid[10], uid_len, sak; uint16_t atqa; } div_tag_t;
esp_err_t div_pn532_open(void);
esp_err_t div_pn532_command(const uint8_t *cmd, size_t len, uint8_t *out, size_t cap, size_t *received, uint32_t timeout_ms);
esp_err_t div_pn532_poll(div_tag_t *tag);
esp_err_t div_pn532_auth(const div_tag_t *tag, uint8_t block, const uint8_t key[6]);
esp_err_t div_pn532_read(uint8_t block, uint8_t out[16]);
esp_err_t div_pn532_write(uint8_t block, const uint8_t data[16], bool ultralight);
void div_pn532_close(void);
