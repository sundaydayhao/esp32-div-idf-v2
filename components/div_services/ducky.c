#include "div_service.h"
#include "div_storage.h"
#include "div_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static esp_err_t keystroke(uint8_t modifier, uint8_t key)
{
    uint8_t report[8] = {modifier, 0, key};
    esp_err_t err = div_ble_keyboard_report(report);
    if (err == ESP_OK) div_job_delay(12);
    memset(report, 0, sizeof(report));
    esp_err_t release = div_ble_keyboard_report(report);
    div_job_delay(12);
    return err == ESP_OK ? release : err;
}
static bool number(const char *s, unsigned *value, unsigned max)
{
    char *end;
    unsigned long n = strtoul(s, &end, 10);
    if (!*s || *end || *s == '-' || n > max) return false;
    *value = n;
    return true;
}
static esp_err_t execute(char *line, unsigned *default_delay)
{
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line || *line == '#' || !strncmp(line, "//", 2)) return ESP_OK;
    char *rest = strpbrk(line, " \t");
    if (rest) { *rest++ = 0; while (*rest == ' ' || *rest == '\t') ++rest; }
    else rest = line + strlen(line);
    for (char *p = line; *p; ++p) *p = toupper((unsigned char)*p);
    if (!strcmp(line, "REM")) return ESP_OK;
    if (!strcmp(line, "DELAY") || !strcmp(line, "WAIT_FOR") || !strcmp(line, "DEFAULT_DELAY") || !strcmp(line, "DEFAULTDELAY")) {
        unsigned delay;
        if (!number(rest, &delay, 60000)) return ESP_ERR_INVALID_ARG;
        if (!strcmp(line, "DEFAULT_DELAY") || !strcmp(line, "DEFAULTDELAY")) *default_delay = delay;
        else div_job_delay(delay);
        return ESP_OK;
    }
    if (!strcmp(line, "STRING") || !strcmp(line, "STRINGLN")) {
        for (unsigned char *p = (unsigned char *)rest; *p && !div_job_cancelled(); ++p) {
            uint8_t mod, key;
            if (!div_hid_ascii(*p, &mod, &key)) return ESP_ERR_NOT_SUPPORTED;
            esp_err_t err = keystroke(mod, key);
            if (err != ESP_OK) return err;
        }
        return !strcmp(line, "STRINGLN") && !div_job_cancelled() ? keystroke(0, 40) : ESP_OK;
    }
    uint8_t report[8] = {0};
    if ((!strcmp(line,"GUI") || !strcmp(line,"WIN") || !strcmp(line,"WINDOWS")) && !*rest)
        return keystroke(8,21); /* Original firmware defaults a bare GUI command to Win+R. */
    unsigned count = 0;
    char *token = line;
    char *state = NULL;
    do {
        uint8_t mod, key;
        if (!div_hid_key(token, &mod, &key)) return ESP_ERR_NOT_SUPPORTED;
        report[0] |= mod;
        if (key) {
            if (count == 6) return ESP_ERR_INVALID_SIZE;
            report[2 + count++] = key;
        }
        token = strtok_r(state ? NULL : rest, " \t", &state);
        if (token && strlen(token)>1) for (char *p=token;*p;++p) *p=toupper((unsigned char)*p);
    } while (token);
    esp_err_t err = div_ble_keyboard_report(report);
    div_job_delay(20);
    memset(report, 0, sizeof(report));
    esp_err_t release = div_ble_keyboard_report(report);
    return err == ESP_OK ? release : err;
}
esp_err_t div_ducky_run(const char *relative)
{
    if (!div_storage_ready()) return ESP_ERR_INVALID_STATE;
    char path[DIV_PATH_MAX];
    esp_err_t err = div_storage_path(relative, path);
    if (err != ESP_OK) return err;
    FILE *f = fopen(path, "r");
    if (!f) return ESP_ERR_NOT_FOUND;
    err = div_ble_keyboard_start();
    unsigned default_delay = 20, line_no = 0;
    char line[512];
    while (err == ESP_OK && !div_job_cancelled() && fgets(line, sizeof(line), f)) {
        ++line_no;
        if (!strchr(line, '\n') && !feof(f)) { err = ESP_ERR_INVALID_SIZE; break; }
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "REPEAT ", 7)) {
            unsigned repeats;
            if (!number(line + 7, &repeats, 10000)) { err = ESP_ERR_INVALID_ARG; break; }
            for (unsigned n = 0; n < repeats && err == ESP_OK && !div_job_cancelled(); ++n) {
                /* ESP32-DIV's REPEAT presses Enter; preserve its script semantics. */
                err = keystroke(0,40);
                div_job_delay(default_delay);
            }
        } else {
            err = execute(line, &default_delay);
            div_job_delay(default_delay);
        }
    }
    if (ferror(f)) err = ESP_FAIL;
    fclose(f);
    div_ble_keyboard_stop();
    div_report("Script line %u: %s", line_no, esp_err_to_name(err));
    return err;
}
