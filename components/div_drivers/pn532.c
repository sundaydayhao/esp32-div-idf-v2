#include "div_drivers.h"
#include "div_board.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
static i2c_master_dev_handle_t pn;
static esp_err_t wait_ready(uint32_t timeout)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout * 1000;
    do {
        uint8_t status = 0;
        esp_err_t err = i2c_master_receive(pn, &status, 1, 50);
        if (err != ESP_OK) return err;
        if (status & 1) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(5));
    } while (esp_timer_get_time() < end);
    return ESP_ERR_TIMEOUT;
}
esp_err_t div_pn532_command(const uint8_t *cmd, size_t n, uint8_t *out, size_t cap, size_t *received, uint32_t timeout)
{
    if (!pn || !cmd || !n || n > 250 || !out || !received || cap > 255) return ESP_ERR_INVALID_ARG;
    *received = 0;
    uint8_t tx[260] = {0,0,0xff,(uint8_t)(n+1),(uint8_t)(-(int)(n+1)),0xd4};
    uint8_t sum = 0xd4;
    for (size_t i=0; i<n; ++i) { tx[6+i] = cmd[i]; sum += cmd[i]; }
    tx[6+n] = (uint8_t)-sum;
    tx[7+n] = 0;
    esp_err_t err = i2c_master_transmit(pn, tx, n+8, 100);
    if (err != ESP_OK) return err;
    err = wait_ready(timeout);
    if (err != ESP_OK) return err;
    uint8_t ack[7];
    err = i2c_master_receive(pn, ack, sizeof(ack), 100);
    const uint8_t expected[] = {1,0,0,0xff,0,0xff,0};
    if (err != ESP_OK) return err;
    if (memcmp(ack, expected, sizeof(expected))) return ESP_ERR_INVALID_RESPONSE;
    err = wait_ready(timeout);
    if (err != ESP_OK) return err;
    uint8_t rx[264] = {0};
    /* The PN532 discards unread trailing bytes when the I2C read ends.
       Read a complete maximum normal frame, including the status byte. */
    err = i2c_master_receive(pn, rx, sizeof(rx), 100);
    if (err != ESP_OK) return err;
    if (rx[0] != 1 || rx[1] != 0 || rx[2] != 0 || rx[3] != 0xff ||
        (uint8_t)(rx[4] + rx[5]) || rx[4] < 2 || rx[6] != 0xd5 || rx[7] != (uint8_t)(cmd[0]+1))
        return ESP_ERR_INVALID_RESPONSE;
    size_t frame_len = rx[4];
    if (frame_len + 8 >= sizeof(rx) || frame_len - 2 > cap) return ESP_ERR_INVALID_SIZE;
    sum = 0;
    for (size_t i=0; i<=frame_len; ++i) sum += rx[6+i];
    if (sum || rx[7+frame_len]) return ESP_ERR_INVALID_CRC;
    *received = frame_len - 2;
    memcpy(out, rx+8, *received);
    return ESP_OK;
}
esp_err_t div_pn532_open(void)
{
#ifndef CONFIG_DIV_PN532_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (pn) return ESP_OK;
    esp_err_t err = i2c_master_probe(div_board_i2c(), 0x24, 100);
    if (err != ESP_OK) return err;
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24, .scl_speed_hz = 100000, .scl_wait_us = 10000};
    err = i2c_master_bus_add_device(div_board_i2c(), &cfg, &pn);
    if (err != ESP_OK) return err;
    uint8_t out[32]; size_t n;
    const uint8_t version[] = {2};
    err = div_pn532_command(version, sizeof(version), out, sizeof(out), &n, 1000);
    if (err == ESP_OK && (n < 4 || out[0] != 0x32)) err = ESP_ERR_NOT_FOUND;
    const uint8_t sam[] = {0x14,1,0x14,0};
    if (err == ESP_OK) err = div_pn532_command(sam, sizeof(sam), out, sizeof(out), &n, 1000);
    const uint8_t retries[] = {0x32,5,0xff,1,1};
    if (err == ESP_OK) err = div_pn532_command(retries, sizeof(retries), out, sizeof(out), &n, 1000);
    if (err != ESP_OK) div_pn532_close();
    return err;
#endif
}
esp_err_t div_pn532_poll(div_tag_t *tag)
{
    if (!tag) return ESP_ERR_INVALID_ARG;
    uint8_t out[48]; size_t n;
    const uint8_t cmd[] = {0x4a,1,0};
    esp_err_t err = div_pn532_command(cmd, sizeof(cmd), out, sizeof(out), &n, 400);
    if (err != ESP_OK) return err;
    if (!n || !out[0]) return ESP_ERR_NOT_FOUND;
    if (n < 6 || out[5] > 10 || n < (size_t)(6+out[5])) return ESP_ERR_INVALID_RESPONSE;
    memset(tag, 0, sizeof(*tag));
    tag->atqa = ((uint16_t)out[2] << 8) | out[3];
    tag->sak = out[4]; tag->uid_len = out[5]; memcpy(tag->uid, out+6, tag->uid_len);
    return ESP_OK;
}
esp_err_t div_pn532_auth(const div_tag_t *tag, uint8_t block, const uint8_t key[6])
{
    if (!tag || !key || tag->uid_len < 4) return ESP_ERR_INVALID_ARG;
    uint8_t cmd[14] = {0x40,1,0x60,block};
    memcpy(cmd+4,key,6); memcpy(cmd+10,tag->uid+tag->uid_len-4,4);
    uint8_t out[8]; size_t n;
    esp_err_t err=div_pn532_command(cmd,sizeof(cmd),out,sizeof(out),&n,500);
    return err != ESP_OK ? err : n && out[0] == 0 ? ESP_OK : ESP_FAIL;
}
esp_err_t div_pn532_read(uint8_t block, uint8_t out[16])
{
    uint8_t cmd[] = {0x40,1,0x30,block}, result[32]; size_t n;
    esp_err_t err=div_pn532_command(cmd,sizeof(cmd),result,sizeof(result),&n,500);
    if (err != ESP_OK) return err;
    if (n < 17 || result[0]) return ESP_FAIL;
    memcpy(out,result+1,16); return ESP_OK;
}
esp_err_t div_pn532_write(uint8_t block, const uint8_t data[16], bool ul)
{
    uint8_t cmd[20] = {0x40,1,ul ? 0xa2 : 0xa0,block}, out[8]; size_t n;
    memcpy(cmd+4,data,ul ? 4 : 16);
    esp_err_t err=div_pn532_command(cmd,ul ? 8 : 20,out,sizeof(out),&n,500);
    return err != ESP_OK ? err : n && out[0] == 0 ? ESP_OK : ESP_FAIL;
}
void div_pn532_close(void)
{
    if (pn) { i2c_master_bus_rm_device(pn); pn=NULL; }
}
