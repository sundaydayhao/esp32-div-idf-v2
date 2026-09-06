#include "pulse_profile.h"
#include "div_storage.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
esp_err_t div_pulse_append(div_pulses_t *p, uint32_t duration, bool level)
{
    if (!duration || duration>1000000) return ESP_ERR_INVALID_ARG;
    while (duration) {
        if (p->count==DIV_PULSE_MAX) return ESP_ERR_INVALID_SIZE;
        uint16_t part=duration>32767 ? 32767 : duration;
        p->duration[p->count]=part; p->level[p->count++]=level; duration-=part;
    }
    return ESP_OK;
}
esp_err_t div_nec_pulses(uint32_t code, div_pulses_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    memset(p,0,sizeof(*p)); p->carrier_hz=38000;
    div_pulse_append(p,9000,true); div_pulse_append(p,4500,false);
    /* IRremoteESP8266 NEC values are represented in transmission bit order (MSB first). */
    for (int bit=31; bit>=0; --bit) {
        div_pulse_append(p,560,true); div_pulse_append(p,code & (1UL<<bit) ? 1690 : 560,false);
    }
    div_pulse_append(p,560,true); div_pulse_append(p,40000,false);
    return ESP_OK;
}
esp_err_t div_profile_save(const char *relative,const div_pulses_t *p,uint32_t frequency)
{
    if (!p || p->count<4 || p->count>DIV_PULSE_MAX) return ESP_ERR_INVALID_ARG;
    char *text=calloc(1,DIV_PULSE_MAX*8+192);
    if (!text) return ESP_ERR_NO_MEM;
    size_t cap=DIV_PULSE_MAX*8+192;
    int offset=snprintf(text,cap,"Filetype: ESP32-DIV RAW\nVersion: 1\nFrequency: %" PRIu32 "\nCarrier: %" PRIu32 "\nRAW_Data:",frequency,p->carrier_hz);
    for (unsigned i=0; i<p->count; ++i) offset+=snprintf(text+offset,cap-offset," %d",p->level[i] ? p->duration[i] : -(int)p->duration[i]);
    text[offset++]='\n';
    esp_err_t err=div_storage_write(relative,text,offset);
    free(text); return err;
}
static uint16_t le16(const uint8_t *p) { return p[0]|p[1]<<8; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p)|(uint32_t)le16(p+2)<<16; }
esp_err_t div_profile_load(const char *relative,div_pulses_t *p,uint32_t *frequency)
{
    if (!p || !frequency) return ESP_ERR_INVALID_ARG;
    char *text=calloc(1,16385);
    if (!text) return ESP_ERR_NO_MEM;
    size_t n;
    esp_err_t err=div_storage_read(relative,text,16384,&n);
    if (err!=ESP_OK) { free(text); return err; }
    memset(p,0,sizeof(*p)); *frequency=0;
    const uint8_t *bytes=(const uint8_t *)text;
    /* Original packed IrCaptureHeader (38 bytes), followed by raw microseconds. */
    if (n>=38 && le32(bytes)==0x31525249) {
        unsigned count=le16(bytes+8);
        if (le16(bytes+4)!=1 || n!=38+count*2 || count>DIV_PULSE_MAX) err=ESP_ERR_INVALID_SIZE;
        else {
            p->carrier_hz=le16(bytes+6)*1000;
            for (unsigned i=0; i<count && err==ESP_OK; ++i) err=div_pulse_append(p,le16(bytes+38+i*2),!(i&1));
        }
    } else {
        char *state, *line=strtok_r(text,"\r\n",&state);
        bool data_seen=false;
        while (line && err==ESP_OK) {
            if (!strncmp(line,"Frequency:",10)) *frequency=strtoul(line+10,NULL,10);
            else if (!strncmp(line,"Carrier:",8)) p->carrier_hz=strtoul(line+8,NULL,10);
            else if (!strncmp(line,"frequency:",10)) p->carrier_hz=strtoul(line+10,NULL,10);
            else if (!strncmp(line,"RAW_Data:",9) || !strncmp(line,"data:",5)) {
                bool alternating=!strncmp(line,"data:",5);
                char *cursor=strchr(line,':')+1;
                data_seen=true;
                unsigned index=0;
                while (*cursor && err==ESP_OK) {
                    while (*cursor==' ' || *cursor=='\t') ++cursor;
                    if (!*cursor) break;
                    char *end; errno=0; long value=strtol(cursor,&end,10);
                    if (errno || end==cursor || value==0 || value>1000000 || value < -1000000) { err=ESP_ERR_INVALID_ARG; break; }
                    err=div_pulse_append(p,labs(value),alternating ? !(index++&1) : value>0);
                    cursor=end;
                }
            }
            line=strtok_r(NULL,"\r\n",&state);
        }
        if (!data_seen) err=ESP_ERR_INVALID_RESPONSE;
    }
    if (p->count<4) err=ESP_ERR_INVALID_SIZE;
    if (p->carrier_hz && (p->carrier_hz<10000 || p->carrier_hz>100000)) err=ESP_ERR_INVALID_ARG;
    free(text); return err;
}
