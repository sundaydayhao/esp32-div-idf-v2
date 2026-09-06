#include "div_protocol.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool div_hex_decode(const char *text, uint8_t *out, size_t cap, size_t *length)
{
    if (!text || !out || !length) return false;
    *length = 0;
    int high = -1;
    for (; *text; ++text) {
        if (*text == ':' || *text == '-' || isspace((unsigned char)*text)) {
            if (high >= 0) return false;
            continue;
        }
        int n = hexval(*text);
        if (n < 0) return false;
        if (high < 0) high = n;
        else {
            if (*length == cap) return false;
            out[(*length)++] = (uint8_t)((high << 4) | n);
            high = -1;
        }
    }
    return high < 0;
}
void div_hex_encode(const uint8_t *data, size_t len, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!out || !cap) return;
    size_t n = 0;
    for (size_t i = 0; i < len && n + 2 < cap; ++i) {
        out[n++] = hex[data[i] >> 4];
        out[n++] = hex[data[i] & 15];
    }
    out[n] = 0;
}
bool div_wifi_parse_ies(const uint8_t *frame, size_t len, div_wifi_ies_t *out)
{
    if (!frame || !out || len < 24 || (frame[0] & 0x0c)) return false;
    uint8_t subtype = frame[0] & 0xf0;
    size_t offset;
    if (subtype == 0x80 || subtype == 0x50) offset = 36;
    else if (subtype == 0x40) offset = 24;
    else if (subtype == 0x00) offset = 28;
    else if (subtype == 0x20) offset = 34;
    else return false;
    if (offset > len) return false;
    memset(out, 0, sizeof(*out));
    memcpy(out->bssid, frame + 16, 6);
    bool ssid_seen = false;
    while (offset < len) {
        if (len - offset < 2) return false;
        unsigned tag = frame[offset++], n = frame[offset++];
        if (n > len - offset) return false;
        const uint8_t *p = frame + offset;
        if (tag == 0) {
            if (n > 32) return false;
            if (!ssid_seen) {
                memcpy(out->ssid, p, n);
                out->ssid[n] = 0;
                out->hidden = !n || !p[0];
                ssid_seen = true;
            }
        } else if (tag == 3 && n == 1) out->channel = *p;
        else if (tag == 221 && n >= 4 && !memcmp(p, "\x00\x50\xf2\x04", 4)) out->wps = true;
        offset += n;
    }
    return ssid_seen;
}
size_t div_dns_answer(const uint8_t *q, size_t len, uint8_t *out, size_t cap, const uint8_t ip[4])
{
    if (!q || !out || !ip || len < 17 || len > cap || cap - len < 16 || (q[2] & 0xf8) ||
        q[4] != 0 || q[5] != 1) return 0;
    size_t p = 12;
    while (p < len && q[p]) {
        size_t n = q[p++];
        if (n > 63 || n > len - p) return 0; /* Reject compression in questions. */
        p += n;
    }
    if (p >= len || len - p < 5) return 0;
    ++p;
    bool ipv4 = q[p] == 0 && q[p+1] == 1 && q[p+2] == 0 && q[p+3] == 1;
    p += 4;
    memcpy(out, q, p);
    out[2] = 0x81; out[3] = 0x80;
    out[6] = 0; out[7] = ipv4 ? 1 : 0;
    memset(out + 8, 0, 4);
    if (!ipv4) return p;
    const uint8_t rr[12] = {0xc0,0x0c,0,1,0,1,0,0,0,30,0,4};
    memcpy(out + p, rr, sizeof(rr));
    memcpy(out + p + 12, ip, 4);
    return p + 16;
}
bool div_mifare_access(const uint8_t t[16], uint8_t out[4])
{
    if (!t || !out) return false;
    uint8_t c1 = t[7] >> 4, c2 = t[8] & 15, c3 = t[8] >> 4;
    if ((t[6] & 15) != ((~c1) & 15) || (t[6] >> 4) != ((~c2) & 15) ||
        (t[7] & 15) != ((~c3) & 15)) return false;
    for (int i = 0; i < 4; ++i) out[i] = (((c1 >> i) & 1) << 2) |
        (((c2 >> i) & 1) << 1) | ((c3 >> i) & 1);
    return true;
}
bool div_hid_ascii(unsigned char c, uint8_t *mod, uint8_t *key)
{
    if (!mod || !key) return false;
    *mod = 0; *key = 0;
    if (c >= 'a' && c <= 'z') *key = c - 'a' + 4;
    else if (c >= 'A' && c <= 'Z') { *mod = 2; *key = c - 'A' + 4; }
    else if (c >= '1' && c <= '9') *key = c - '1' + 30;
    else if (c == '0') *key = 39;
    else {
        const char *plain = "\n\x1b\b\t -=[]\\#;'\x60,./";
        const uint8_t codes[] = {40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56};
        const char *shift = "!@#$%^&*()_+{}|:\"~<>?";
        const uint8_t shifted[] = {30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
        const char *p = c ? strchr(plain, c) : NULL;
        if (p) *key = codes[p - plain];
        else if (c && (p = strchr(shift, c))) { *key = shifted[p - shift]; *mod = 2; }
        else return false;
    }
    return true;
}
bool div_hid_key(const char *name, uint8_t *mod, uint8_t *key)
{
    if (!name || !mod || !key) return false;
    *mod = 0; *key = 0;
    struct item { const char *name; uint8_t code, mod; };
    static const struct item keys[] = {
        {"ENTER",40,0},{"RETURN",40,0},{"ESC",41,0},{"ESCAPE",41,0},{"BACKSPACE",42,0},
        {"TAB",43,0},{"SPACE",44,0},{"CAPSLOCK",57,0},{"PRINTSCREEN",70,0},{"SCROLLLOCK",71,0},
        {"PAUSE",72,0},{"INSERT",73,0},{"HOME",74,0},{"PAGEUP",75,0},{"DELETE",76,0},
        {"END",77,0},{"PAGEDOWN",78,0},{"PGDN",78,0},{"PGUP",75,0},{"RIGHT",79,0},{"RIGHTARROW",79,0},
        {"LEFT",80,0},{"LEFTARROW",80,0},{"DOWN",81,0},{"DOWNARROW",81,0},
        {"UP",82,0},{"UPARROW",82,0},{"NUMLOCK",83,0},{"MENU",101,0},{"APP",101,0},
        {"CTRL",0,1},{"CONTROL",0,1},{"SHIFT",0,2},{"ALT",0,4},{"GUI",0,8},{"WINDOWS",0,8},{"WIN",0,8},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (!strcmp(name, keys[i].name)) { *mod = keys[i].mod; *key = keys[i].code; return true; }
    if (name[0] == 'F' && name[1]) {
        char *end;
        long n = strtol(name + 1, &end, 10);
        if (!*end && n >= 1 && n <= 12) { *key = 57 + n; return true; }
    }
    return name[0] && !name[1] && div_hid_ascii(name[0], mod, key);
}
bool div_ble_findmy(const uint8_t *data, size_t len)
{
    if (!data) return false;
    size_t p = 0;
    while (p < len) {
        unsigned n = data[p++];
        if (!n) break;
        if (n > len - p) return false;
        if (n >= 5 && data[p] == 0xff && data[p+1] == 0x4c &&
            data[p+2] == 0 && data[p+3] == 0x12 && data[p+4] == 0x19) return true;
        if (n >= 8 && data[p] == 0xff && data[p+1] == 0x4c && data[p+2] == 0 &&
            data[p+3] == 7 && data[p+4] == 0x19 && data[p+6] == 0 &&
            (data[p+7] == 0x55 || data[p+7] == 0x30)) return true;
        p += n;
    }
    return false;
}
uint32_t div_lfsr_next(unsigned bits,uint32_t state)
{
    /* Same Fibonacci feedback taps as ESP32-DIV's De Bruijn mode. */
    uint32_t taps;
    switch (bits) {
        case 8: taps=0xb8; break;
        case 10: taps=0x240; break;
        case 12: taps=0xca0; break;
        case 16: taps=0xb400; break;
        case 18: taps=0x20400; break;
        case 20: taps=0x90000; break;
        case 24: taps=0xe10000; break;
        default: return 0;
    }
    uint32_t value=state&taps,feedback=0;
    while (value) { feedback^=value&1; value>>=1; }
    return ((state<<1)|feedback)&((1UL<<bits)-1);
}
