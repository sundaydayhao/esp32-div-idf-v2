#include "div_service.h"
#include "div_drivers.h"
#include "div_storage.h"
#include "div_protocol.h"
#include "div_board.h"
#include "pulse_profile.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static esp_err_t nrf_job(const div_job_t *j)
{
    div_nrf_t r={0};
    esp_err_t err=div_nrf_open(div_arg_int(j,"radio",1,1,3),&r);
    if (err!=ESP_OK) return err;
    const char *id=j->feature->id;
    uint8_t channel=div_arg_int(j,"channel",40,0,125), address[5]; size_t n;
    if (!div_hex_decode(div_arg_string(j,"address","E7E7E7E7E7"),address,5,&n) || n!=5) { div_nrf_close(&r); return ESP_ERR_INVALID_ARG; }
    err=div_nrf_channel(&r,channel);
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",15,1,3600)*1000000;
    FILE *file=NULL;
    if (!strcmp(id,"nrf_scan")) {
        unsigned counts[126]={0}, rounds=0;
        while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
            for (unsigned ch=0;ch<126 && err==ESP_OK && !div_job_cancelled();++ch) {
                err=div_nrf_channel(&r,ch); bool detected=false;
                if (err==ESP_OK) err=div_nrf_power_detect(&r,&detected);
                counts[ch]+=detected;
            }
            if (++rounds%10==0) {
                unsigned peak=0;
                for (unsigned ch=1;ch<126;++ch) if (counts[ch]>counts[peak]) peak=ch;
                div_report("Peak %u MHz: %u / %u samples",2400+peak,counts[peak],rounds);
            }
            div_job_delay(1);
        }
        for (unsigned ch=0;ch<126;++ch) div_report("%u MHz %u/%u",2400+ch,counts[ch],rounds);
    } else if (!strcmp(id,"ble_jam") || !strcmp(id,"nrf_protokill")) {
        if (err==ESP_OK) err=div_nrf_carrier(&r,true);
        if (err==ESP_OK) div_job_delay(div_arg_int(j,"seconds",15,1,3600)*1000);
    } else if (!strcmp(id,"nrf_mouse_inject")) {
        uint8_t payload[32];
        if (!div_hex_decode(div_arg_string(j,"payload",""),payload,32,&n) || !n) err=ESP_ERR_INVALID_ARG;
        if (err==ESP_OK) err=div_nrf_send(&r,address,payload,n);
    } else {
        char path[DIV_PATH_MAX];
        const char *relative=div_arg_string(j,"path","esb/capture.bin");
        if (!div_storage_ready()) err=ESP_ERR_INVALID_STATE;
        if (err==ESP_OK) err=div_storage_path(relative,path);
        bool replay=!strcmp(id,"nrf_replay");
        if (err==ESP_OK && !(file=fopen(path,replay ? "rb" : "wb"))) err=ESP_ERR_NOT_FOUND;
        const uint8_t magic[8]={'D','I','V','E','S','B',1,0};
        uint8_t header[8];
        if (err==ESP_OK && replay) {
            if (fread(header,1,8,file)!=8 || memcmp(header,magic,8)) err=ESP_ERR_INVALID_RESPONSE;
        } else if (err==ESP_OK) {
            if (fwrite(magic,1,8,file)!=8) err=ESP_FAIL;
            else err=div_nrf_listen(&r,address,div_arg_int(j,"width",0,0,32));
        }
        unsigned total=0;
        while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
            uint8_t record[39]={channel,0};
            memcpy(record+2,address,5);
            if (replay) {
                size_t got=fread(record,1,sizeof(record),file);
                if (!got && feof(file)) break;
                if (got!=sizeof(record) || record[0]>125 || !record[1] || record[1]>32) { err=ESP_ERR_INVALID_RESPONSE; break; }
                err=div_nrf_channel(&r,record[0]);
                if (err==ESP_OK) err=div_nrf_send(&r,record+2,record+7,record[1]);
                div_job_delay(20);
            } else {
                n=32; err=div_nrf_receive(&r,record+7,&n);
                if (err==ESP_ERR_NOT_FOUND) { err=ESP_OK; div_job_delay(2); continue; }
                if (err!=ESP_OK) break;
                record[1]=n;
                if (fwrite(record,1,sizeof(record),file)!=sizeof(record)) { err=ESP_FAIL; break; }
                char hex[65]; div_hex_encode(record+7,n,hex,sizeof(hex));
                div_report("ch %u len %u %s",channel,(unsigned)n,hex);
            }
            ++total;
        }
        div_report("%u ESB records",total);
    }
    if (file && fclose(file)) err=ESP_FAIL;
    div_nrf_close(&r); return err;
}
static esp_err_t fixed_code(div_pulses_t *p,uint32_t value,unsigned bits,unsigned pulse)
{
    memset(p,0,sizeof(*p));
    for (int bit=(int)bits-1;bit>=0;--bit) {
        bool one=value & (1UL<<bit);
        div_pulse_append(p,pulse*(one ? 3 : 1),true);
        div_pulse_append(p,pulse*(one ? 1 : 3),false);
    }
    div_pulse_append(p,pulse,true);
    return div_pulse_append(p,pulse*31,false);
}
static esp_err_t sub_job(const div_job_t *j)
{
    div_cc_t radio={0};
    double mhz=div_arg_double(j,"frequency",433.92,300,928);
    div_pulses_t *p=calloc(1,sizeof(*p));
    if (!p) return ESP_ERR_NO_MEM;
    const char *id=j->feature->id, *path=div_arg_string(j,"path","subghz/capture.sub");
    uint32_t frequency=0;
    esp_err_t err=ESP_OK;
    if (!strcmp(id,"sub_replay")) {
        err=div_profile_load(path,p,&frequency);
        if (frequency) mhz=frequency/1000000.0;
        p->carrier_hz=0;
    }
    if (err==ESP_OK) err=div_cc_open(&radio,mhz);
    if (err!=ESP_OK) { free(p); return err; }
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",15,1,3600)*1000000;
    if (!strcmp(id,"sub_capture")) {
        err=div_cc_receive(&radio);
        if (err==ESP_OK) err=div_pulse_capture(DIV_CC_GDO2,false,div_arg_int(j,"seconds",15,1,120)*1000,p,div_job_cancelled);
        if (err==ESP_OK) err=div_profile_save(path,p,(uint32_t)(mhz*1000000+0.5));
        if (err==ESP_OK) div_report("Saved %" PRIu32 " pulses at %.3f MHz",p->count,mhz);
    } else if (!strcmp(id,"sub_detect")) {
        err=div_cc_receive(&radio);
        unsigned sustained=0;
        while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
            int rssi; err=div_cc_rssi(&radio,&rssi);
            if (err==ESP_OK) {
                sustained=rssi>div_arg_int(j,"threshold",-60,-110,0) ? sustained+1 : 0;
                div_report("%.3f MHz %d dBm%s",mhz,rssi,sustained>=8 ? " sustained activity" : "");
            }
            div_job_delay(250);
        }
    } else {
        err=div_cc_transmit(&radio);
        if (!strcmp(id,"sub_jam")) {
            if (err==ESP_OK) { gpio_set_level(DIV_CC_GDO0,1); div_job_delay(div_arg_int(j,"seconds",15,1,3600)*1000); }
        } else if (!strcmp(id,"sub_brute")) {
            unsigned bits=div_arg_int(j,"bits",12,1,24), pulse=div_arg_int(j,"pulse",350,50,1000);
            if (!strcmp(div_arg_string(j,"mode","brute"),"debruijn")) {
                uint32_t state=1;
                if (!div_lfsr_next(bits,state)) err=ESP_ERR_INVALID_ARG;
                for (uint32_t i=0;i<(1UL<<bits)-1 && err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end;++i) {
                    gpio_set_level(DIV_CC_GDO0,state&1);
                    esp_rom_delay_us(pulse);
                    state=div_lfsr_next(bits,state);
                    if (!(i&255)) div_job_delay(1);
                }
                gpio_set_level(DIV_CC_GDO0,0);
            } else {
            uint32_t first=div_arg_int(j,"start",0,0,(1UL<<bits)-1);
            unsigned count=div_arg_int(j,"count",1,1,1000000);
            for (uint32_t code=first;code<(1UL<<bits) && code-first<count && err==ESP_OK &&
                 !div_job_cancelled() && esp_timer_get_time()<end;++code) {
                err=fixed_code(p,code,bits,pulse);
                if (err==ESP_OK) err=div_pulse_send(DIV_CC_GDO0,p);
                if (!(code%32)) div_report("Fixed code %" PRIu32 " / %u bits",code,bits);
                div_job_delay(1);
            }
            }
        } else if (!strcmp(id,"sub_replay")) {
            unsigned count=div_arg_int(j,"count",1,1,1000);
            for (unsigned i=0;i<count && err==ESP_OK && !div_job_cancelled();++i) {
                err=div_pulse_send(DIV_CC_GDO0,p); div_job_delay(10);
            }
        } else err=ESP_ERR_NOT_SUPPORTED;
    }
    div_cc_close(&radio); free(p); return err;
}
esp_err_t div_rf_job(const div_job_t *job)
{ return job->feature->category==DIV_NRF ? nrf_job(job) : sub_job(job); }
