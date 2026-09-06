#include "div_service.h"
#include "div_board.h"
#include "div_storage.h"
#include "minmea.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#ifdef CONFIG_DIV_GPS_ENABLED
typedef struct {
    SemaphoreHandle_t lock,done;
    atomic_bool stop;
    double latitude,longitude,altitude,hdop;
    int satellites;
    bool valid,verbose;
    struct minmea_date date;
    struct minmea_time time;
    int64_t fix_time;
    FILE *file;
    unsigned records;
    bool write_error;
} gps_t;
static void sentence(gps_t *gps,const char *line)
{
    xSemaphoreTake(gps->lock,portMAX_DELAY);
    switch (minmea_sentence_id(line,true)) {
        case MINMEA_SENTENCE_RMC: {
            struct minmea_sentence_rmc rmc;
            if (minmea_parse_rmc(&rmc,line)) {
                gps->valid=rmc.valid;
                if (rmc.valid) {
                    gps->latitude=minmea_tocoord(&rmc.latitude); gps->longitude=minmea_tocoord(&rmc.longitude);
                    gps->time=rmc.time; gps->date=rmc.date; gps->fix_time=esp_timer_get_time();
                }
            }
            break;
        }
        case MINMEA_SENTENCE_GGA: {
            struct minmea_sentence_gga gga;
            if (minmea_parse_gga(&gga,line)) {
                gps->satellites=gga.satellites_tracked;
                gps->altitude=minmea_tofloat(&gga.altitude); gps->hdop=minmea_tofloat(&gga.hdop);
                if (!gga.fix_quality) gps->valid=false;
                if (gps->verbose) div_report("Fix %d sats %d HDOP %.1f altitude %.1f m",gga.fix_quality,gps->satellites,gps->hdop,gps->altitude);
            }
            break;
        }
        case MINMEA_SENTENCE_GSV: {
            struct minmea_sentence_gsv gsv;
            if (gps->verbose && minmea_parse_gsv(&gsv,line)) {
                div_report("%.2s satellites in view %d",line+1,gsv.total_sats);
                for (int i=0;i<4;++i) if (gsv.sats[i].nr)
                    div_report("PRN %d elevation %d azimuth %d SNR %.0f",gsv.sats[i].nr,gsv.sats[i].elevation,
                        gsv.sats[i].azimuth,minmea_tofloat(&gsv.sats[i].snr));
            }
            break;
        }
        default: break;
    }
    xSemaphoreGive(gps->lock);
}
static void receiver(void *arg)
{
    gps_t *gps=arg;
    char line[192]; unsigned used=0;
    while (!atomic_load(&gps->stop)) {
        uint8_t bytes[128];
        int n=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),pdMS_TO_TICKS(50));
        for (int i=0;i<n;++i) {
            if (bytes[i]=='$') used=0;
            if (bytes[i]=='\n') { line[used]=0; if (used) sentence(gps,line); used=0; }
            else if (bytes[i]!='\r') {
                if (used<sizeof(line)-1) line[used++]=bytes[i];
                else used=0;
            }
        }
    }
    xSemaphoreGive(gps->done);
    vTaskDelete(NULL);
}
static void observation(const div_observation_t *o,void *arg)
{
    gps_t *gps=arg;
    xSemaphoreTake(gps->lock,portMAX_DELAY);
    if (gps->valid && esp_timer_get_time()-gps->fix_time<5000000 &&
        isfinite(gps->latitude) && isfinite(gps->longitude) && gps->date.month>=1 && gps->date.month<=12) {
        char name[67]; unsigned pos=0;
        for (const char *p=o->name;*p && pos<64;++p) {
            if (*p=='"') name[pos++]='"';
            name[pos++]=(*p=='\r' || *p=='\n') ? ' ' : *p;
        }
        name[pos]=0;
        int year=gps->date.year;
        if (year<100) year+=year<80 ? 2000 : 1900;
        unsigned freq=o->ble ? 0 : o->channel==14 ? 2484 : 2407+o->channel*5;
        int wrote=fprintf(gps->file,"%02X:%02X:%02X:%02X:%02X:%02X,\"%s\",%s,%04d-%02d-%02d %02d:%02d:%02d,%u,%u,%d,%.7f,%.7f,%.1f,%.1f,%s\n",
            o->mac[0],o->mac[1],o->mac[2],o->mac[3],o->mac[4],o->mac[5],name,o->ble ? "[BLE]" : "[WIFI]",
            year,gps->date.month,gps->date.day,gps->time.hours,gps->time.minutes,gps->time.seconds,
            o->channel,freq,o->rssi,gps->latitude,gps->longitude,isfinite(gps->altitude) ? gps->altitude : 0,
            isfinite(gps->hdop) ? gps->hdop*5 : 0,o->ble ? "BLE" : "WIFI");
        if (wrote<0) gps->write_error=true;
        else ++gps->records;
    }
    xSemaphoreGive(gps->lock);
}
#endif
esp_err_t div_gps_job(const div_job_t *j)
{
#ifndef CONFIG_DIV_GPS_ENABLED
    (void)j;
    div_report("GPS is external to the supplied schematic. Enable GPS and set UART pins in menuconfig.");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!div_board_external_gpio(CONFIG_DIV_GPS_RX,false) || !div_board_external_gpio(CONFIG_DIV_GPS_TX,true) ||
        CONFIG_DIV_GPS_RX==CONFIG_DIV_GPS_TX) return ESP_ERR_INVALID_ARG;
    gps_t *gps=calloc(1,sizeof(*gps));
    if (!gps) return ESP_ERR_NO_MEM;
    gps->verbose=!strcmp(j->feature->id,"gps_satellites");
    gps->lock=xSemaphoreCreateMutex(); gps->done=xSemaphoreCreateBinary();
    if (!gps->lock || !gps->done) {
        if (gps->lock) vSemaphoreDelete(gps->lock);
        if (gps->done) vSemaphoreDelete(gps->done);
        free(gps); return ESP_ERR_NO_MEM;
    }
    uart_config_t config={.baud_rate=CONFIG_DIV_GPS_BAUD,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t err=uart_param_config(UART_NUM_1,&config);
    if (err==ESP_OK) err=uart_set_pin(UART_NUM_1,CONFIG_DIV_GPS_TX,CONFIG_DIV_GPS_RX,-1,-1);
    bool installed=false, started=false;
    if (err==ESP_OK) { err=uart_driver_install(UART_NUM_1,4096,0,0,NULL,0); installed=err==ESP_OK; }
    if (err==ESP_OK && !gps->verbose) {
        char path[DIV_PATH_MAX];
        err=div_storage_ready() ? div_storage_path(div_arg_string(j,"path","logs/wardrive.csv"),path) : ESP_ERR_INVALID_STATE;
        if (err==ESP_OK && !(gps->file=fopen(path,"a+"))) err=ESP_FAIL;
        if (gps->file) {
            fseek(gps->file,0,SEEK_END);
            if (!ftell(gps->file)) {
                if (fprintf(gps->file,"WigleWifi-1.6,appRelease=2.0,model=ESP32-DIV,release=2.0,device=wardrive,display=,board=ESP32-S3,brand=ESP32-DIV\n"
                    "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n")<0) err=ESP_FAIL;
            }
        }
    }
    if (err==ESP_OK) { started=xTaskCreate(receiver,"gps_rx",4096,gps,5,NULL)==pdPASS; if (!started) err=ESP_ERR_NO_MEM; }
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",60,1,86400)*1000000;
    while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
        if (!gps->verbose) {
            err=div_wifi_observe(observation,gps);
            if (err==ESP_OK && !div_job_cancelled()) err=div_ble_observe(2,observation,gps);
            if (gps->write_error || fflush(gps->file)) err=ESP_FAIL;
            div_report("Wardrive: %u observations with valid fixes",gps->records);
        }
        div_job_delay(1000);
    }
    atomic_store(&gps->stop,true);
    if (started) xSemaphoreTake(gps->done,portMAX_DELAY);
    if (gps->file && fclose(gps->file)) err=ESP_FAIL;
    if (installed) uart_driver_delete(UART_NUM_1);
    vSemaphoreDelete(gps->lock); vSemaphoreDelete(gps->done); free(gps);
    return err;
#endif
}
