#include "div_service.h"
#include "div_storage.h"
#include "div_protocol.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

void ble_store_config_init(void);
typedef struct { div_observation_t value; uint8_t data[31], length, type; } advertisement_t;
static QueueHandle_t scan_queue;
static atomic_bool synced, subscribed, encrypted;
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint dropped;
static bool initialized;
static uint8_t own_type, last_report[8], protocol_mode = 1;
static uint16_t report_handle, boot_handle;
static atomic_uint subscribed_handle;
static const uint8_t report_map[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,
    0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
};
enum { ATTR_INFO, ATTR_MAP, ATTR_REPORT, ATTR_PROTOCOL, ATTR_CONTROL, ATTR_REF, ATTR_OUTPUT, ATTR_OUT_REF };
static int hid_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)conn; (void)attr;
    intptr_t kind = (intptr_t)arg;
    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value; uint16_t len;
        if (OS_MBUF_PKTLEN(ctx->om) != 1 || ble_hs_mbuf_to_flat(ctx->om, &value, 1, &len))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (kind == ATTR_PROTOCOL) {
            if (value > 1) return BLE_ATT_ERR_UNLIKELY;
            protocol_mode = value;
        }
        return 0;
    }
    const uint8_t info[] = {0x11,0x01,0,2}, ref[] = {1,1}, outref[] = {1,2}, zero = 0;
    const void *data = &zero; size_t len = 1;
    switch (kind) {
        case ATTR_INFO: data = info; len = sizeof(info); break;
        case ATTR_MAP: data = report_map; len = sizeof(report_map); break;
        case ATTR_REPORT: data = last_report; len = sizeof(last_report); break;
        case ATTR_PROTOCOL: data = &protocol_mode; break;
        case ATTR_REF: data = ref; len = sizeof(ref); break;
        case ATTR_OUT_REF: data = outref; len = sizeof(outref); break;
        default: break;
    }
    return os_mbuf_append(ctx->om, data, len) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
#define HID_READ (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define HID_WRITE (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC)
static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=BLE_UUID16_DECLARE(0x2a4a), .access_cb=hid_access, .arg=(void *)ATTR_INFO, .flags=HID_READ},
        {.uuid=BLE_UUID16_DECLARE(0x2a4b), .access_cb=hid_access, .arg=(void *)ATTR_MAP, .flags=HID_READ},
        {.uuid=BLE_UUID16_DECLARE(0x2a4d), .access_cb=hid_access, .arg=(void *)ATTR_REPORT,
         .flags=HID_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle=&report_handle,
         .descriptors=(struct ble_gatt_dsc_def[]) {
             {.uuid=BLE_UUID16_DECLARE(0x2908), .att_flags=BLE_ATT_F_READ, .access_cb=hid_access, .arg=(void *)ATTR_REF}, {0}}},
        {.uuid=BLE_UUID16_DECLARE(0x2a4d), .access_cb=hid_access, .arg=(void *)ATTR_OUTPUT, .flags=HID_READ | HID_WRITE,
         .descriptors=(struct ble_gatt_dsc_def[]) {
             {.uuid=BLE_UUID16_DECLARE(0x2908), .att_flags=BLE_ATT_F_READ, .access_cb=hid_access, .arg=(void *)ATTR_OUT_REF}, {0}}},
        {.uuid=BLE_UUID16_DECLARE(0x2a4e), .access_cb=hid_access, .arg=(void *)ATTR_PROTOCOL, .flags=HID_READ | HID_WRITE},
        {.uuid=BLE_UUID16_DECLARE(0x2a4c), .access_cb=hid_access, .arg=(void *)ATTR_CONTROL, .flags=HID_WRITE},
        {.uuid=BLE_UUID16_DECLARE(0x2a22), .access_cb=hid_access, .arg=(void *)ATTR_REPORT,
         .flags=HID_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle=&boot_handle},
        {.uuid=BLE_UUID16_DECLARE(0x2a32), .access_cb=hid_access, .arg=(void *)ATTR_OUTPUT, .flags=HID_READ | HID_WRITE},
        {0}
    }
}, {0}};
static void sync_cb(void) { atomic_store(&synced, ble_hs_id_infer_auto(0, &own_type) == 0); }
static void reset_cb(int reason) { atomic_store(&synced, false); div_report("BLE reset %d", reason); }
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
static esp_err_t ensure_ble(void)
{
    if (!initialized) {
        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) return err;
        ble_hs_cfg.sync_cb = sync_cb;
        ble_hs_cfg.reset_cb = reset_cb;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_svc_gap_init();
        ble_svc_gatt_init();
        int rc = ble_gatts_count_cfg(services);
        if (!rc) rc = ble_gatts_add_svcs(services);
        if (rc) { nimble_port_deinit(); return ESP_FAIL; }
        ble_svc_gap_device_name_set("ESP32-DIV Keyboard");
        ble_svc_gap_device_appearance_set(0x03c1);
        ble_store_config_init();
        scan_queue = xQueueCreate(24, sizeof(advertisement_t));
        if (!scan_queue) { nimble_port_deinit(); return ESP_ERR_NO_MEM; }
        initialized = true;
        nimble_port_freertos_init(host_task);
    }
    int64_t end = esp_timer_get_time() + 5000000;
    while (!atomic_load(&synced) && !div_job_cancelled() && esp_timer_get_time() < end) div_job_delay(20);
    return atomic_load(&synced) ? ESP_OK : ESP_ERR_TIMEOUT;
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            advertisement_t a = {.value.ble=true, .value.rssi=event->disc.rssi, .type=event->disc.addr.type};
            for (unsigned i = 0; i < 6; ++i) a.value.mac[i] = event->disc.addr.val[5-i];
            a.length = event->disc.length_data > 31 ? 31 : event->disc.length_data;
            memcpy(a.data, event->disc.data, a.length);
            struct ble_hs_adv_fields fields;
            if (!ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) && fields.name) {
                size_t n = fields.name_len > 32 ? 32 : fields.name_len;
                memcpy(a.value.name, fields.name, n);
            }
            if (xQueueSend(scan_queue, &a, 0) != pdTRUE) atomic_fetch_add(&dropped, 1);
            break;
        }
        case BLE_GAP_EVENT_CONNECT:
            if (!event->connect.status) {
                atomic_store(&connection, event->connect.conn_handle);
                ble_gap_security_initiate(event->connect.conn_handle);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
            atomic_store(&subscribed, false); atomic_store(&encrypted, false);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == report_handle || event->subscribe.attr_handle == boot_handle) {
                atomic_store(&subscribed_handle, event->subscribe.attr_handle);
                atomic_store(&subscribed, event->subscribe.cur_notify != 0);
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE: {
            struct ble_gap_conn_desc desc;
            bool secure = !event->enc_change.status && !ble_gap_conn_find(event->enc_change.conn_handle, &desc) && desc.sec_state.encrypted;
            atomic_store(&encrypted, secure);
            break;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc desc;
            if (!ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc)) ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default: break;
    }
    return 0;
}
static esp_err_t advertise(const uint8_t *data, size_t n, bool keyboard)
{
    int rc = ble_gap_adv_set_data(data, n);
    struct ble_gap_adv_params params = {.conn_mode=keyboard ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON,
        .disc_mode=BLE_GAP_DISC_MODE_GEN, .itvl_min=160, .itvl_max=240};
    if (!rc) rc = ble_gap_adv_start(own_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) div_report("BLE advertising error %d", rc);
    return rc ? ESP_FAIL : ESP_OK;
}
esp_err_t div_ble_keyboard_start(void)
{
    esp_err_t err = ensure_ble();
    if (err != ESP_OK) return err;
    atomic_store(&subscribed, false); atomic_store(&encrypted, false);
    const uint8_t data[] = {2,1,6,3,3,0x12,0x18,3,0x19,0xc1,3,13,9,'D','I','V',' ','K','e','y','b','o','a','r','d'};
    err = advertise(data, sizeof(data), true);
    if (err != ESP_OK) return err;
    div_report("Pair DIV Keyboard on the host; waiting for encrypted HID subscription.");
    int64_t end = esp_timer_get_time() + 60000000;
    while ((!atomic_load(&subscribed) || !atomic_load(&encrypted)) &&
           !div_job_cancelled() && esp_timer_get_time() < end) div_job_delay(50);
    if (!atomic_load(&subscribed) || !atomic_load(&encrypted)) { div_ble_keyboard_stop(); return ESP_ERR_TIMEOUT; }
    div_job_delay(500);
    return ESP_OK;
}
esp_err_t div_ble_keyboard_report(const uint8_t report[8])
{
    if (!report || !atomic_load(&subscribed) || !atomic_load(&encrypted)) return ESP_ERR_INVALID_STATE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, 8);
    if (!om) return ESP_ERR_NO_MEM;
    memcpy(last_report, report, 8);
    int rc = ble_gatts_notify_custom(atomic_load(&connection), atomic_load(&subscribed_handle), om);
    return rc ? ESP_FAIL : ESP_OK;
}
void div_ble_keyboard_stop(void)
{
    if (!initialized) return;
    const uint8_t released[8] = {0};
    if (atomic_load(&subscribed)) { div_ble_keyboard_report(released); vTaskDelay(pdMS_TO_TICKS(30)); }
    ble_gap_adv_stop();
    uint16_t handle = atomic_load(&connection);
    if (handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        for (int i = 0; i < 40 && atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE; ++i) vTaskDelay(pdMS_TO_TICKS(25));
    }
    atomic_store(&subscribed, false);
}
static esp_err_t scan_ble(unsigned seconds, const div_job_t *job, div_observation_fn callback, void *context)
{
    esp_err_t err = ensure_ble();
    if (err != ESP_OK) return err;
    xQueueReset(scan_queue); atomic_store(&dropped, 0);
    struct ble_gap_disc_params params = {.passive=0, .itvl=160, .window=80, .filter_duplicates=0};
    int rc = ble_gap_disc(own_type, seconds * 1000, &params, gap_event, NULL);
    if (rc) return ESP_FAIL;
    struct seen { uint8_t mac[6], type; uint32_t count; int64_t last; char name[33]; } seen[64] = {0};
    unsigned used = 0;
    FILE *f = NULL;
    const char *relative = job ? div_arg_string(job, "path", "") : "";
    if (*relative) {
        char path[DIV_PATH_MAX];
        if (!div_storage_ready() || div_storage_path(relative, path) != ESP_OK || !(f=fopen(path, "w"))) {
            ble_gap_disc_cancel(); return ESP_FAIL;
        }
        fprintf(f, "MAC,Name,RSSI,Packets,LastSeenMilliseconds\n");
    }
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (!div_job_cancelled() && esp_timer_get_time() < end && err == ESP_OK) {
        advertisement_t a;
        if (xQueueReceive(scan_queue, &a, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        if (job && !strcmp(job->feature->id, "ble_airtag_rx") && !div_ble_findmy(a.data, a.length)) continue;
        if (job && !strcmp(job->feature->id, "ble_skimmer")) {
            if (!strstr(a.value.name, "HC-05") && !strstr(a.value.name, "HC-06") &&
                !strstr(a.value.name, "HMSoft") && !strstr(a.value.name, "Skimmer")) continue;
        }
        unsigned i;
        for (i = 0; i < used; ++i) if (seen[i].type == a.type && !memcmp(seen[i].mac, a.value.mac, 6)) break;
        if (i == used && used < 64) { memcpy(seen[i].mac, a.value.mac, 6); seen[i].type = a.type; ++used; }
        if (i >= 64) continue;
        ++seen[i].count;
        if (*a.value.name) strlcpy(seen[i].name, a.value.name, sizeof(seen[i].name));
        else strlcpy(a.value.name, seen[i].name, sizeof(a.value.name));
        int64_t now = esp_timer_get_time();
        if (seen[i].count == 1 || now - seen[i].last >= 1000000) {
            seen[i].last = now;
            if (callback) callback(&a.value, context);
            else {
                char mac[13]; div_hex_encode(a.value.mac, 6, mac, sizeof(mac));
                div_report("%s %d dBm # %" PRIu32 " %.32s", mac, a.value.rssi, seen[i].count, a.value.name);
                if (f) {
                    for (char *p = a.value.name; *p; ++p) if (*p == '"' || *p == '\r' || *p == '\n') *p = ' ';
                    if (fprintf(f, "%s,\"%s\",%d,%" PRIu32 ",%" PRId64 "\n", mac, a.value.name,
                        a.value.rssi, seen[i].count, now/1000) < 0) err = ESP_FAIL;
                }
            }
        }
    }
    ble_gap_disc_cancel();
    if (f && fclose(f)) err = ESP_FAIL;
    if (!callback) div_report("%u devices; queue drops %u", used, atomic_load(&dropped));
    return err;
}
esp_err_t div_ble_observe(unsigned seconds, div_observation_fn callback, void *context)
{ return callback && seconds && seconds <= 60 ? scan_ble(seconds, NULL, callback, context) : ESP_ERR_INVALID_ARG; }
esp_err_t div_ble_job(const div_job_t *job)
{
    const char *id = job->feature->id;
    if (!strcmp(id, "ble_ducky")) return div_ducky_run(div_arg_string(job, "path", "ducky/01_notepad_hello.txt"));
    if (!strcmp(id, "ble_scan") || !strcmp(id, "ble_sniff") || !strcmp(id, "ble_airtag_rx") || !strcmp(id, "ble_skimmer"))
        return scan_ble(div_arg_int(job, "seconds", 15, 1, 3600), job, NULL, NULL);
    esp_err_t err = ensure_ble();
    if (err != ESP_OK) return err;
    uint8_t data[31]; size_t n = 0;
    const char *hex = div_arg_string(job, "data", "");
    if (*hex) {
        if (!div_hex_decode(hex, data, sizeof(data), &n) || !n) return ESP_ERR_INVALID_ARG;
    } else {
        if (!strcmp(id,"ble_apple")) {
            /* Preserve ESP32-DIV's manufacturer test packet layout. */
            const uint8_t prefix[]={16,0xff,0x4c,0,0x0f,5,0xc1,0x27};
            memcpy(data,prefix,sizeof(prefix)); esp_fill_random(data+8,3);
            data[11]=0; data[12]=0; data[13]=0x10; esp_fill_random(data+14,3); n=17;
        } else if (!strcmp(id,"ble_airtag_tx")) {
            const uint8_t prefix[]={30,0xff,0x4c,0,7,0x19,5,0,0x55,0x55,0x55,0x55,0,0,0};
            memcpy(data,prefix,sizeof(prefix)); esp_fill_random(data+15,16); n=31;
        } else {
            const char *name = div_arg_string(job, "name", "DIV-Lab");
            size_t len = strlen(name);
            if (!len || len > 26) return ESP_ERR_INVALID_SIZE;
            data[0]=2; data[1]=1; data[2]=6; data[3]=len+1; data[4]=9;
            memcpy(data+5, name, len); n=len+5;
        }
    }
    err = advertise(data, n, false);
    if (err == ESP_OK) div_job_delay(div_arg_int(job, "seconds", 15, 1, 3600)*1000);
    ble_gap_adv_stop();
    return err;
}
