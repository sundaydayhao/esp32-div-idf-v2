#include "div_storage.h"
#include "div_board.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_check.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
static const char *TAG = "storage";
static sdmmc_card_t *card;
static nvs_handle_t nvs;
_Static_assert(sizeof(div_settings_t)==24,"Version 2 NVS migration requires the original 24-byte settings layout");
bool div_storage_ready(void) { return card != NULL; }
esp_err_t div_storage_path(const char *relative, char out[DIV_PATH_MAX])
{
    if (!relative || !out) return ESP_ERR_INVALID_ARG;
    while (*relative == '/') ++relative;
    /* FAT path components, never permit traversal or Windows drive syntax. */
    if (strchr(relative, '\\') || strchr(relative, ':')) return ESP_ERR_INVALID_ARG;
    const char *p = relative;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
            return ESP_ERR_INVALID_ARG;
        if (!end) break;
        p = end + 1;
    }
    int n = snprintf(out, DIV_PATH_MAX, "%s/%s", DIV_SD_ROOT, relative);
    return n >= 0 && n < DIV_PATH_MAX ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
esp_err_t div_storage_mkdir(const char *relative)
{
    if (!card) return ESP_ERR_INVALID_STATE;
    char path[DIV_PATH_MAX];
    ESP_RETURN_ON_ERROR(div_storage_path(relative, path), TAG, "path");
    return mkdir(path, 0775) == 0 || errno == EEXIST ? ESP_OK : ESP_FAIL;
}
esp_err_t div_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    /* Never erase all NVS implicitly; credentials/calibration need recovery. */
    ESP_RETURN_ON_ERROR(err, TAG, "NVS init");
    ESP_RETURN_ON_ERROR(nvs_open("div", NVS_READWRITE, &nvs), TAG, "NVS namespace");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = DIV_RADIO_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = DIV_SD_CS;
    slot.host_id = DIV_RADIO_HOST;
    /* No card-detect signal is present in the supplied TFT schematics. */
    esp_vfs_fat_sdmmc_mount_config_t mount = {.format_if_mount_failed = false,
        .max_files = 8, .allocation_unit_size = 16 * 1024};
    err = esp_vfs_fat_sdspi_mount(DIV_SD_ROOT, &host, &slot, &mount, &card);
    if (err != ESP_OK) {
        card = NULL;
        ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(err));
        return ESP_OK;
    }
    const char *dirs[] = {"config", "captures", "logs", "ir", "subghz", "nfc", "ducky", "esb"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        ESP_RETURN_ON_ERROR(div_storage_mkdir(dirs[i]), TAG, "SD directory");
    return ESP_OK;
}
esp_err_t div_settings_load(div_settings_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (div_settings_t){.version = 3, .brightness = 160, .dark = 1,
        .touch_x0 = 280, .touch_x1 = 3850, .touch_y0 = 320, .touch_y1 = 3750,
        .board_id = DIV_BOARD_ID};
    div_settings_t saved;
    size_t size = sizeof(saved);
    esp_err_t err = nvs_get_blob(nvs, "settings", &saved, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Import user preferences from the Arduino firmware; never reuse
           another board's touch calibration after the schematic changed. */
        char json[2048]; size_t length;
        if (div_storage_read("config/settings.json",json,sizeof(json)-1,&length)==ESP_OK) {
            json[length]=0;
            cJSON *doc=cJSON_Parse(json);
            const cJSON *p=cJSON_GetObjectItemCaseSensitive(doc,"brightness");
            if (cJSON_IsNumber(p) && p->valueint>=0 && p->valueint<=255) out->brightness=p->valueint;
            p=cJSON_GetObjectItemCaseSensitive(doc,"theme");
            if (cJSON_IsNumber(p)) out->dark=p->valueint==0;
            p=cJSON_GetObjectItemCaseSensitive(doc,"accentColor");
            if (cJSON_IsNumber(p) && p->valueint>=0 && p->valueint<7) out->accent=p->valueint;
            out->led_enabled=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc,"neopixelEnabled"));
            out->background_scan=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc,"autoWifiScan")) ||
                cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc,"autoBleScan"));
            cJSON_Delete(doc);
            /* Only NVS here: preserve the old JSON until the user saves. */
            esp_err_t imported=nvs_set_blob(nvs,"settings",out,sizeof(*out));
            if (imported==ESP_OK) imported=nvs_commit(nvs);
            if (imported!=ESP_OK) return imported;
        }
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    if (size != sizeof(saved) || (saved.version != 2 && saved.version != 3) || saved.board_id != DIV_BOARD_ID || saved.accent>6 ||
        saved.touch_x1 <= saved.touch_x0 || saved.touch_y1 <= saved.touch_y0 ||
        saved.touch_x1 > 4095 || saved.touch_y1 > 4095) return ESP_ERR_INVALID_VERSION;
    if (saved.version==2) {
        /* Version 2 did not distinguish guessed ranges from real calibration.
           The new byte occupies old struct padding; do not interpret that byte. */
        saved.version=3;
        saved.touch_calibrated=0;
    }
    if (saved.touch_calibrated>1) return ESP_ERR_INVALID_VERSION;
    *out = saved;
    return ESP_OK;
}
esp_err_t div_settings_save(const div_settings_t *s)
{
    if (!s || s->version != 3 || s->board_id != DIV_BOARD_ID || s->accent>6 || s->touch_calibrated>1 ||
        s->touch_x0 >= s->touch_x1 || s->touch_y0 >= s->touch_y1 ||
        s->touch_x1 > 4095 || s->touch_y1 > 4095) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(nvs_set_blob(nvs, "settings", s, sizeof(*s)), TAG, "save settings");
    ESP_RETURN_ON_ERROR(nvs_commit(nvs),TAG,"commit settings");
    if (card) {
        char json[512];
        int length=snprintf(json,sizeof(json),
            "{\"board\":\"ESP32_DIV_V%d\",\"brightness\":%u,\"theme\":%u,\"accentColor\":%u,"
            "\"neopixelEnabled\":%s,\"autoWifiScan\":%s,\"autoBleScan\":%s,"
            "\"touch\":{\"xMin\":%u,\"xMax\":%u,\"yMin\":%u,\"yMax\":%u,\"swap\":%u,\"invertX\":%u,\"invertY\":%u,\"calibrated\":%u}}\n",
            DIV_BOARD_ID,s->brightness,!s->dark,s->accent,s->led_enabled ? "true" : "false",
            s->background_scan ? "true" : "false",s->background_scan ? "true" : "false",
            s->touch_x0,s->touch_x1,s->touch_y0,s->touch_y1,s->touch_swap,s->touch_invert_x,s->touch_invert_y,s->touch_calibrated);
        if (length<0 || length>=sizeof(json)) return ESP_ERR_INVALID_SIZE;
        esp_err_t backup=div_storage_write("config/settings.json",json,length);
        if (backup!=ESP_OK) ESP_LOGW(TAG,"NVS saved; SD settings backup failed: %s",esp_err_to_name(backup));
    }
    return ESP_OK;
}
esp_err_t div_storage_write(const char *relative, const void *data, size_t len)
{
    if (!card || (!data && len)) return ESP_ERR_INVALID_STATE;
    char path[DIV_PATH_MAX], tmp[DIV_PATH_MAX + 8];
    ESP_RETURN_ON_ERROR(div_storage_path(relative, path), TAG, "path");
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return ESP_FAIL;
    bool ok = fwrite(data, 1, len, f) == len;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { unlink(tmp); return ESP_FAIL; }
    /* FAT rename does not replace destinations; preserve the previous file
       on failed writes, and use a backup during the replacement. */
    char backup[DIV_PATH_MAX + 8];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    unlink(backup);
    bool existed = access(path, F_OK) == 0;
    if (existed && rename(path, backup) != 0) { unlink(tmp); return ESP_FAIL; }
    if (rename(tmp, path) != 0) {
        if (existed) rename(backup, path);
        return ESP_FAIL;
    }
    if (existed) unlink(backup);
    return ESP_OK;
}
esp_err_t div_storage_read(const char *relative, void *data, size_t cap, size_t *len)
{
    if (!card || !data || !len) return ESP_ERR_INVALID_ARG;
    char path[DIV_PATH_MAX];
    ESP_RETURN_ON_ERROR(div_storage_path(relative, path), TAG, "path");
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    *len = fread(data, 1, cap, f);
    bool too_big = fgetc(f) != EOF;
    bool bad = ferror(f);
    fclose(f);
    return bad ? ESP_FAIL : too_big ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
esp_err_t div_storage_ota(const char *relative)
{
    if (!card) return ESP_ERR_INVALID_STATE;
    char path[DIV_PATH_MAX];
    ESP_RETURN_ON_ERROR(div_storage_path(relative, path), TAG, "OTA path");
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) { fclose(f); return ESP_ERR_NOT_FOUND; }
    struct stat st;
    if (fstat(fileno(f), &st) || st.st_size <= 0 || (size_t)st.st_size > partition->size) {
        fclose(f); return ESP_ERR_INVALID_SIZE;
    }
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(partition, st.st_size, &handle);
    if (err != ESP_OK) { fclose(f); return err; }
    uint8_t block[2048];
    size_t n;
    while ((n = fread(block, 1, sizeof(block), f)) > 0) {
        err = esp_ota_write(handle, block, n);
        if (err != ESP_OK) break;
    }
    if (ferror(f)) err = ESP_FAIL;
    fclose(f);
    if (err != ESP_OK) { esp_ota_abort(handle); return err; }
    /* esp_ota_end validates image checksum, target chip, and image format. */
    err = esp_ota_end(handle);
    if (err != ESP_OK) return err;
    return esp_ota_set_boot_partition(partition);
}
