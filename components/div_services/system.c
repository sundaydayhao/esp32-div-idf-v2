#include "div_service.h"
#include "div_storage.h"
#include "div_board.h"
#include "esp_console.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static esp_err_t files(const div_job_t *j)
{
    if (!div_storage_ready()) return ESP_ERR_INVALID_STATE;
    char path[DIV_PATH_MAX], destination[DIV_PATH_MAX];
    const char *rel = div_arg_string(j, "path", "");
    esp_err_t err = div_storage_path(rel, path);
    if (err != ESP_OK) return err;
    const char *action = div_arg_string(j, "action", "list");
    if (!strcmp(action, "list")) {
        DIR *dir = opendir(path);
        if (!dir) return ESP_ERR_NOT_FOUND;
        struct dirent *entry;
        while (!div_job_cancelled() && (entry = readdir(dir)))
            div_report("%s%s", entry->d_name, entry->d_type == DT_DIR ? "/" : "");
        closedir(dir);
        return ESP_OK;
    }
    if (!strcmp(action, "read")) {
        FILE *f = fopen(path, "rb");
        if (!f) return ESP_ERR_NOT_FOUND;
        char line[160];
        while (!div_job_cancelled() && fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            div_report("%s", line);
        }
        bool bad = ferror(f);
        fclose(f);
        return bad ? ESP_FAIL : ESP_OK;
    }
    if (!strcmp(action, "mkdir")) return div_storage_mkdir(rel);
    if (!*rel || !strcmp(rel, "/")) return ESP_ERR_INVALID_ARG;
    if (!strcmp(action, "delete")) {
        struct stat st;
        if (stat(path, &st)) return ESP_ERR_NOT_FOUND;
        return (S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path)) == 0 ? ESP_OK : ESP_FAIL;
    }
    if (!strcmp(action, "rename")) {
        const char *dest = div_arg_string(j, "destination", "");
        if (!*dest) return ESP_ERR_INVALID_ARG;
        err = div_storage_path(dest, destination);
        if (err != ESP_OK) return err;
        if (!access(destination, F_OK)) return ESP_ERR_INVALID_STATE;
        return rename(path, destination) == 0 ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t serial(const div_job_t *j)
{
#ifndef CONFIG_DIV_SERIAL_ENABLED
    (void)j;
    div_report("Enable external UART and select its pins in menuconfig.");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!div_board_external_gpio(CONFIG_DIV_SERIAL_RX, false) ||
        !div_board_external_gpio(CONFIG_DIV_SERIAL_TX, true) ||
        CONFIG_DIV_SERIAL_RX == CONFIG_DIV_SERIAL_TX) return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_DIV_GPS_ENABLED
    if (CONFIG_DIV_SERIAL_RX == CONFIG_DIV_GPS_RX || CONFIG_DIV_SERIAL_TX == CONFIG_DIV_GPS_RX ||
        (CONFIG_DIV_GPS_TX >= 0 && (CONFIG_DIV_SERIAL_RX == CONFIG_DIV_GPS_TX ||
                                  CONFIG_DIV_SERIAL_TX == CONFIG_DIV_GPS_TX))) return ESP_ERR_INVALID_ARG;
#endif
    uart_config_t config = {.baud_rate = div_arg_int(j, "baud", 115200, 1200, 921600),
        .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
    esp_err_t err = uart_param_config(UART_NUM_2, &config);
    if (err == ESP_OK) err = uart_set_pin(UART_NUM_2, CONFIG_DIV_SERIAL_TX, CONFIG_DIV_SERIAL_RX, -1, -1);
    if (err != ESP_OK) return err;
    err = uart_driver_install(UART_NUM_2, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    const char *text = div_arg_string(j, "text", "");
    if (*text && CONFIG_DIV_SERIAL_TX < 0) err = ESP_ERR_NOT_SUPPORTED;
    else if (*text && uart_write_bytes(UART_NUM_2, text, strlen(text)) < 0) err = ESP_FAIL;
    int64_t end = esp_timer_get_time() + (int64_t)div_arg_int(j, "seconds", 30, 1, 3600) * 1000000;
    while (err == ESP_OK && !div_job_cancelled() && esp_timer_get_time() < end) {
        char buffer[128];
        int n = uart_read_bytes(UART_NUM_2, buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(50));
        if (n < 0) { err = ESP_FAIL; break; }
        if (n) { buffer[n] = 0; div_report("%s", buffer); }
    }
    uart_driver_delete(UART_NUM_2);
    return err;
#endif
}

esp_err_t div_system_job(const div_job_t *j)
{
    const char *id = j->feature->id;
    if (!strcmp(id, "system_files")) return files(j);
    if (!strcmp(id, "system_serial")) return serial(j);
    if (!strcmp(id, "system_calibrate")) return div_service_calibrate();
    if (!strcmp(id, "system_update")) {
        esp_err_t err = div_storage_ota(div_arg_string(j, "path", "firmware.bin"));
        if (err == ESP_OK) div_report("Image validated and selected. Restart device to apply.");
        return err;
    }
    if (!strcmp(id, "system_settings")) {
        div_settings_t s;
        div_settings_load(&s);
        s.brightness = div_arg_int(j, "brightness", s.brightness, 0, 255);
        s.dark = div_arg_int(j, "dark", s.dark, 0, 1);
        s.led_enabled = div_arg_int(j, "led", s.led_enabled, 0, 1);
        s.background_scan = div_arg_int(j, "background", s.background_scan, 0, 1);
        s.accent = div_arg_int(j, "accent", s.accent, 0, 6);
        esp_err_t err = div_settings_save(&s);
        if (err == ESP_OK) err = div_board_brightness(s.brightness);
        div_board_led(0, s.led_enabled ? 8 : 0, s.led_enabled ? 16 : 0);
        div_report("Settings saved; brightness %u, theme %s", s.brightness, s.dark ? "dark" : "light");
        return err;
    }
    if (strcmp(id, "system_about")) return ESP_ERR_NOT_SUPPORTED;
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const esp_app_desc_t *app = esp_app_get_description();
    div_report("%s / %s %s / IDF %s", DIV_BOARD_NAME, app->project_name, app->version, esp_get_idf_version());
    div_report("Cores %u; free heap %" PRIu32 "; minimum %" PRIu32, chip.cores,
        esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    div_report("SD %s; uptime %" PRId64 " s", div_storage_ready() ? "mounted" : "absent", esp_timer_get_time()/1000000);
    int mv;
    if (div_board_battery_mv(&mv) == ESP_OK) div_report("Battery %d mV", mv);
    return ESP_OK;
}

static int cmd_list(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t n;
    const div_feature_t *all = div_features(&n);
    for (size_t i = 0; i < n; ++i) printf("%-20s %s / %s\n", all[i].id, div_category_name(all[i].category), all[i].name);
    return 0;
}
static int cmd_run(int argc, char **argv)
{
    if (argc < 2 || argc > 3) { printf("run <feature> [\"{\\\"key\\\":\\\"value\\\"}\"]\n"); return 1; }
    esp_err_t err = div_service_start(argv[1], argc == 3 ? argv[2] : "{}");
    printf("%s\n", esp_err_to_name(err));
    return err != ESP_OK;
}
static int cmd_stop(int argc, char **argv) { (void)argc; (void)argv; div_service_stop(); return 0; }
static int cmd_describe(int argc, char **argv)
{
    if (argc != 2) return 1;
    const div_feature_t *f = div_feature_find(argv[1]);
    if (!f) return 1;
    printf("%s\n", f->description);
    for (const div_field_t *p = f->fields; p->key; ++p) printf("%s = %s (%s)\n", p->key, p->value, p->label);
    return 0;
}
esp_err_t div_console_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "div>";
    cfg.max_cmdline_length = DIV_JOB_TEXT_MAX + 128;
    cfg.task_stack_size = 6144;
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart, &cfg, &repl);
    if (err != ESP_OK) return err;
    const esp_console_cmd_t commands[] = {
        {.command="features", .help="List features", .func=cmd_list},
        {.command="describe", .help="Show feature parameters", .func=cmd_describe},
        {.command="run", .help="Start a feature with optional JSON parameters", .func=cmd_run},
        {.command="stop", .help="Stop the current feature", .func=cmd_stop}
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        err = esp_console_cmd_register(&commands[i]);
        if (err != ESP_OK) { repl->del(repl); return err; }
    }
    return esp_console_start_repl(repl);
}
