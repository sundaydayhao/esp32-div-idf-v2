#include "div_service.h"
#include "div_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
typedef struct { const div_feature_t *feature; char args[DIV_JOB_TEXT_MAX]; } request_t;
static QueueHandle_t requests;
static SemaphoreHandle_t report_lock;
static atomic_bool busy, cancelled;
static div_line_t report_lines[64];
static uint32_t sequence;
static void count_observation(const div_observation_t *observation,void *context)
{
    (void)observation;
    ++*(unsigned *)context;
}
static div_calibrate_fn calibrator;
void div_service_set_calibrator(div_calibrate_fn callback) { calibrator = callback; }
esp_err_t div_service_calibrate(void) { return calibrator ? calibrator() : ESP_ERR_NOT_SUPPORTED; }
void div_report(const char *fmt, ...)
{
    char text[192];
    va_list ap;
    va_start(ap, fmt); vsnprintf(text, sizeof(text), fmt, ap); va_end(ap);
    printf("%s\n", text);
    if (!report_lock || xSemaphoreTake(report_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    uint32_t seq = ++sequence;
    report_lines[seq % 64].seq = seq;
    strlcpy(report_lines[seq % 64].text, text, sizeof(text));
    xSemaphoreGive(report_lock);
}
size_t div_report_read(uint32_t *after, div_line_t *out, size_t cap)
{
    if (!after || !out || !report_lock || xSemaphoreTake(report_lock, 0) != pdTRUE) return 0;
    uint32_t first = *after + 1;
    if (sequence > 63 && first < sequence - 63) first = sequence - 63;
    size_t n = 0;
    for (uint32_t i = first; i <= sequence && n < cap; ++i) {
        out[n++] = report_lines[i % 64];
        *after = i;
    }
    xSemaphoreGive(report_lock);
    return n;
}
bool div_service_busy(void) { return atomic_load(&busy); }
bool div_job_cancelled(void) { return atomic_load(&cancelled); }
void div_service_stop(void) { atomic_store(&cancelled, true); }
bool div_job_delay(uint32_t ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    do {
        if (div_job_cancelled()) return false;
        uint32_t remain = (uint32_t)((end - esp_timer_get_time() + 999) / 1000);
        if (esp_timer_get_time() >= end) break;
        vTaskDelay(pdMS_TO_TICKS(remain > 20 ? 20 : remain) + 1);
    } while (esp_timer_get_time() < end);
    return !div_job_cancelled();
}
const char *div_arg_string(const div_job_t *job, const char *name, const char *fallback)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(job->args, name);
    return cJSON_IsString(p) ? p->valuestring : fallback;
}
double div_arg_double(const div_job_t *job, const char *name, double fallback, double low, double high)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(job->args, name);
    double value = fallback;
    if (cJSON_IsNumber(p)) value = p->valuedouble;
    else if (cJSON_IsString(p)) {
        char *end;
        value = strtod(p->valuestring, &end);
        if (end == p->valuestring || *end) return fallback;
    }
    return isfinite(value) && value >= low && value <= high ? value : fallback;
}
int div_arg_int(const div_job_t *j, const char *n, int d, int lo, int hi)
{ return (int)div_arg_double(j, n, d, lo, hi); }
static void worker(void *arg)
{
    (void)arg;
    request_t request;
    int64_t next_background=esp_timer_get_time()+30000000;
    for (;;) {
        if (xQueueReceive(requests, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
            if (esp_timer_get_time()<next_background) continue;
            next_background=esp_timer_get_time()+60000000;
            div_settings_t settings;
            div_settings_load(&settings);
            bool expected=false;
            if (!settings.background_scan || !atomic_compare_exchange_strong(&busy,&expected,true)) continue;
            atomic_store(&cancelled,false);
            unsigned wifi=0,ble=0;
            esp_err_t scan_error=div_wifi_observe(count_observation,&wifi);
            if (!div_job_cancelled()) div_ble_observe(2,count_observation,&ble);
            if (!div_job_cancelled()) div_report("Background: %u APs, %u BLE observations (%s)",wifi,ble,esp_err_to_name(scan_error));
            atomic_store(&busy,false);
            continue;
        }
        cJSON *args = cJSON_Parse(request.args);
        div_job_t job = {.feature = request.feature, .args = args};
        div_report("Starting: %s", job.feature->name);
        esp_err_t err;
        if (!args) err = ESP_ERR_NO_MEM;
        else if (div_job_cancelled()) err = ESP_ERR_INVALID_STATE;
        else switch (job.feature->category) {
            case DIV_WIFI: err = div_wifi_job(&job); break;
            case DIV_BLE: err = div_ble_job(&job); break;
            case DIV_NRF: case DIV_SUB: err = div_rf_job(&job); break;
            case DIV_IR: err = div_ir_job(&job); break;
            case DIV_NFC: err = div_nfc_job(&job); break;
            case DIV_GPS: err = div_gps_job(&job); break;
            default: err = div_system_job(&job); break;
        }
        cJSON_Delete(args);
        div_report("%s: %s", div_job_cancelled() ? "Stopped" : "Finished", esp_err_to_name(err));
        atomic_store(&busy, false);
    }
}
esp_err_t div_service_init(void)
{
    report_lock = xSemaphoreCreateMutex();
    requests = xQueueCreate(1, sizeof(request_t));
    if (!report_lock || !requests) return ESP_ERR_NO_MEM;
    return xTaskCreate(worker, "div_worker", 12288, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t div_service_start(const char *id, const char *json)
{
    const div_feature_t *f = div_feature_find(id);
    if (!f || !json || strlen(json) >= DIV_JOB_TEXT_MAX) return ESP_ERR_INVALID_ARG;
    cJSON *parsed = cJSON_Parse(json);
    bool valid = cJSON_IsObject(parsed);
    cJSON_Delete(parsed);
    if (!valid) return ESP_ERR_INVALID_ARG;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&busy, &expected, true)) return ESP_ERR_INVALID_STATE;
    atomic_store(&cancelled, false);
    request_t request = {.feature = f};
    strlcpy(request.args, json, sizeof(request.args));
    if (xQueueSend(requests, &request, 0) != pdTRUE) {
        atomic_store(&busy, false); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
