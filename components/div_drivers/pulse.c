#include "div_drivers.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
static bool rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *ctx)
{
    (void)channel;
    BaseType_t wake = pdFALSE;
    size_t count = event->num_symbols;
    xQueueSendFromISR((QueueHandle_t)ctx, &count, &wake);
    return wake == pdTRUE;
}
esp_err_t div_pulse_capture(int pin, bool invert, uint32_t timeout, div_pulses_t *out, div_cancel_fn cancel)
{
    if (!out || !timeout) return ESP_ERR_INVALID_ARG;
    rmt_symbol_word_t *buffer = calloc(DIV_PULSE_MAX / 2, sizeof(*buffer));
    QueueHandle_t queue = xQueueCreate(1, sizeof(size_t));
    if (!buffer || !queue) { free(buffer); if (queue) vQueueDelete(queue); return ESP_ERR_NO_MEM; }
    rmt_channel_handle_t rx = NULL;
    rmt_rx_channel_config_t cfg = {.gpio_num = pin, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, .mem_block_symbols = 64, .flags.invert_in = invert};
    esp_err_t err = rmt_new_rx_channel(&cfg, &rx);
    bool enabled = false;
    if (err == ESP_OK) {
        rmt_rx_event_callbacks_t callbacks = {.on_recv_done = rx_done};
        err = rmt_rx_register_event_callbacks(rx, &callbacks, queue);
    }
    if (err == ESP_OK) { err = rmt_enable(rx); enabled = err == ESP_OK; }
    if (err == ESP_OK) {
        rmt_receive_config_t capture = {.signal_range_min_ns = 10000, .signal_range_max_ns = 20000000};
        err = rmt_receive(rx, buffer, sizeof(*buffer) * DIV_PULSE_MAX / 2, &capture);
    }
    size_t count = 0;
    int64_t end = esp_timer_get_time() + (int64_t)timeout * 1000;
    if (err == ESP_OK) {
        err = ESP_ERR_TIMEOUT;
        do {
            if (xQueueReceive(queue, &count, pdMS_TO_TICKS(20)) == pdTRUE) { err = ESP_OK; break; }
            if (cancel && cancel()) break;
        } while (esp_timer_get_time() < end);
    }
    if (enabled) rmt_disable(rx);
    if (rx) rmt_del_channel(rx);
    vQueueDelete(queue);
    if (err == ESP_OK) {
        memset(out, 0, sizeof(*out));
        if (count >= DIV_PULSE_MAX / 2) err = ESP_ERR_INVALID_SIZE; /* Never replay truncated captures. */
        else for (size_t i = 0; i < count; ++i) {
            if (buffer[i].duration0) {
                out->duration[out->count] = buffer[i].duration0;
                out->level[out->count++] = buffer[i].level0;
            }
            if (buffer[i].duration1) {
                out->duration[out->count] = buffer[i].duration1;
                out->level[out->count++] = buffer[i].level1;
            }
        }
        if (out->count < 4) err = ESP_ERR_INVALID_SIZE;
    }
    free(buffer);
    return err;
}
esp_err_t div_pulse_send(int pin, const div_pulses_t *p)
{
    if (!p || !p->count || p->count > DIV_PULSE_MAX ||
        (p->carrier_hz && (p->carrier_hz < 10000 || p->carrier_hz > 100000))) return ESP_ERR_INVALID_ARG;
    size_t n = (p->count + 1) / 2;
    rmt_symbol_word_t *symbols = calloc(n, sizeof(*symbols));
    if (!symbols) return ESP_ERR_NO_MEM;
    uint64_t total = 0;
    for (size_t i = 0; i < p->count; ++i) {
        if (!p->duration[i] || p->duration[i] > 32767 || p->level[i] > 1) {
            free(symbols); return ESP_ERR_INVALID_ARG;
        }
        total += p->duration[i];
        if (i & 1) { symbols[i/2].duration1 = p->duration[i]; symbols[i/2].level1 = p->level[i]; }
        else { symbols[i/2].duration0 = p->duration[i]; symbols[i/2].level0 = p->level[i]; }
    }
    if (total > 10000000) { free(symbols); return ESP_ERR_INVALID_SIZE; }
    rmt_channel_handle_t tx = NULL;
    rmt_encoder_handle_t encoder = NULL;
    rmt_tx_channel_config_t cfg = {.gpio_num = pin, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, .mem_block_symbols = 64, .trans_queue_depth = 1};
    esp_err_t err = rmt_new_tx_channel(&cfg, &tx);
    if (err == ESP_OK && p->carrier_hz) {
        rmt_carrier_config_t carrier = {.frequency_hz = p->carrier_hz, .duty_cycle = 0.33f};
        err = rmt_apply_carrier(tx, &carrier);
    }
    rmt_copy_encoder_config_t enc = {};
    if (err == ESP_OK) err = rmt_new_copy_encoder(&enc, &encoder);
    bool enabled = false;
    if (err == ESP_OK) { err = rmt_enable(tx); enabled = err == ESP_OK; }
    if (err == ESP_OK) {
        rmt_transmit_config_t transmit = {.loop_count = 0, .flags.eot_level = 0};
        err = rmt_transmit(tx, encoder, symbols, n * sizeof(*symbols), &transmit);
        if (err == ESP_OK) err = rmt_tx_wait_all_done(tx, (int)(total / 1000 + 500));
    }
    if (enabled) rmt_disable(tx);
    if (encoder) rmt_del_encoder(encoder);
    if (tx) rmt_del_channel(tx);
    gpio_set_level(pin, 0);
    free(symbols);
    return err;
}
