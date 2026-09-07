#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
#include "div_spectrum.h"
#define DIV_JOB_TEXT_MAX 1536
typedef enum { DIV_WIFI, DIV_BLE, DIV_NRF, DIV_SUB, DIV_IR, DIV_NFC, DIV_GPS, DIV_SYSTEM } div_category_t;
typedef struct { const char *key, *label, *value; } div_field_t;
typedef struct {
    const char *id, *name, *description;
    div_category_t category;
    const div_field_t *fields;
    bool writes;
} div_feature_t;
typedef struct { const div_feature_t *feature; const cJSON *args; } div_job_t;
typedef struct { uint32_t seq; char text[192]; } div_line_t;
const div_feature_t *div_features(size_t *count);
const div_feature_t *div_feature_find(const char *id);
const char *div_category_name(div_category_t category);
esp_err_t div_service_init(void);
esp_err_t div_console_init(void);
typedef esp_err_t (*div_calibrate_fn)(void);
void div_service_set_calibrator(div_calibrate_fn callback);
esp_err_t div_service_calibrate(void);
esp_err_t div_service_start(const char *id, const char *json);
void div_service_stop(void);
bool div_service_busy(void);
bool div_job_cancelled(void);
bool div_job_delay(uint32_t ms);
void div_report(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
size_t div_report_read(uint32_t *after, div_line_t *lines, size_t cap);
const char *div_arg_string(const div_job_t *job, const char *name, const char *fallback);
int div_arg_int(const div_job_t *job, const char *name, int fallback, int low, int high);
double div_arg_double(const div_job_t *job, const char *name, double fallback, double low, double high);
esp_err_t div_wifi_job(const div_job_t *job);
bool div_wifi_monitor_frame(div_spectrum_frame_t *frame);
void div_wifi_monitor_channel(int delta);
esp_err_t div_ble_job(const div_job_t *job);
esp_err_t div_rf_job(const div_job_t *job);
esp_err_t div_ir_job(const div_job_t *job);
esp_err_t div_nfc_job(const div_job_t *job);
esp_err_t div_gps_job(const div_job_t *job);
esp_err_t div_system_job(const div_job_t *job);
esp_err_t div_ducky_run(const char *path);
esp_err_t div_ble_keyboard_start(void);
esp_err_t div_ble_keyboard_report(const uint8_t report[8]);
void div_ble_keyboard_stop(void);
typedef struct {
    uint8_t mac[6], channel;
    int8_t rssi;
    char name[33];
    bool ble;
} div_observation_t;
typedef void (*div_observation_fn)(const div_observation_t *observation, void *context);
esp_err_t div_wifi_observe(div_observation_fn callback, void *context);
esp_err_t div_ble_observe(unsigned seconds, div_observation_fn callback, void *context);
