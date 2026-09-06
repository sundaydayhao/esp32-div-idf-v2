#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    char ssid[33];
    uint8_t bssid[6], channel;
    bool hidden, wps;
} div_wifi_ies_t;
bool div_wifi_parse_ies(const uint8_t *frame, size_t len, div_wifi_ies_t *out);
bool div_hex_decode(const char *text, uint8_t *out, size_t capacity, size_t *length);
void div_hex_encode(const uint8_t *data, size_t len, char *out, size_t capacity);
size_t div_dns_answer(const uint8_t *query, size_t len, uint8_t *out, size_t cap, const uint8_t ip[4]);
bool div_mifare_access(const uint8_t trailer[16], uint8_t access[4]);
bool div_hid_ascii(unsigned char c, uint8_t *modifier, uint8_t *key);
bool div_hid_key(const char *name, uint8_t *modifier, uint8_t *key);
bool div_ble_findmy(const uint8_t *data, size_t length);
uint32_t div_lfsr_next(unsigned bits, uint32_t state);
