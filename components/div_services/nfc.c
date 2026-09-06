#include "div_service.h"
#include "div_drivers.h"
#include "div_storage.h"
#include "div_protocol.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static unsigned sector_start(unsigned block) { return block<128 ? block&~3U : 128+((block-128)&~15U); }
static bool trailer(unsigned block) { return block<128 ? block%4==3 : (block-128)%16==15; }
static esp_err_t await_tag(div_tag_t *tag,unsigned seconds)
{
    int64_t end=esp_timer_get_time()+(int64_t)seconds*1000000;
    div_report("Present tag to the PN532");
    while (!div_job_cancelled() && esp_timer_get_time()<end) {
        esp_err_t err=div_pn532_poll(tag);
        if (err==ESP_OK) {
            char uid[21]; div_hex_encode(tag->uid,tag->uid_len,uid,sizeof(uid));
            div_report("UID %s ATQA %04X SAK %02X",uid,tag->atqa,tag->sak);
            return ESP_OK;
        }
        if (err!=ESP_ERR_NOT_FOUND && err!=ESP_ERR_TIMEOUT) return err;
        div_job_delay(50);
    }
    return ESP_ERR_TIMEOUT;
}
static esp_err_t rf_field(bool on)
{
    const uint8_t cmd[]={0x32,1,on ? 1 : 0}; uint8_t response[8]; size_t n;
    return div_pn532_command(cmd,sizeof(cmd),response,sizeof(response),&n,300);
}
static esp_err_t field_session(const div_job_t *j)
{
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",15,1,120)*1000000;
    esp_err_t err=ESP_OK;
    while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
        err=rf_field(false); div_job_delay(50);
        if (err==ESP_OK) err=rf_field(true);
        if (!strcmp(j->feature->id,"nfc_disrupt") && err==ESP_OK) {
            div_tag_t tag; esp_err_t poll=div_pn532_poll(&tag);
            div_report("Field cycle: %s",esp_err_to_name(poll));
        }
        div_job_delay(100);
    }
    rf_field(false); return err;
}
static esp_err_t emulate(const div_job_t *j)
{
    /* PN532 TgInitAsTarget: passive ISO14443-A, UID 08 12 34 56,
       followed by FeliCa parameters, NFCID3t, general/ historical bytes. */
    uint8_t init[38]={0x8c,1,0x04,0x00,0x12,0x34,0x56,0x20};
    init[26]=0x12; init[27]=0x34; init[28]=0x56;
    uint8_t response[255]; size_t n;
    div_report("Present device to the reader; target mode waiting");
    esp_err_t err=div_pn532_command(init,sizeof(init),response,sizeof(response),&n,1000);
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",15,1,120)*1000000;
    while (err==ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
        const uint8_t get[]={0x86};
        err=div_pn532_command(get,sizeof(get),response,sizeof(response),&n,500);
        if (err!=ESP_OK) break;
        if (!n || response[0]) { err=ESP_ERR_INVALID_RESPONSE; break; }
        div_report("Target received %u bytes",(unsigned)n-1);
        const uint8_t reply[]={0x8e,0x6a,0x82}; /* Unsupported application, never claim to emulate stored secrets. */
        err=div_pn532_command(reply,sizeof(reply),response,sizeof(response),&n,500);
    }
    return err;
}
static esp_err_t card_job(const div_job_t *j)
{
    const char *id=j->feature->id, *relative=div_arg_string(j,"path","nfc/card.bin");
    uint8_t key[6]; size_t n;
    if (!div_hex_decode(div_arg_string(j,"key","FFFFFFFFFFFF"),key,6,&n) || n!=6) return ESP_ERR_INVALID_ARG;
    if (!strcmp(id,"nfc_access")) {
        uint8_t *dump=malloc(4096); if (!dump) return ESP_ERR_NO_MEM;
        esp_err_t err=div_storage_read(relative,dump,4096,&n);
        if (err==ESP_OK && n%16) err=ESP_ERR_INVALID_SIZE;
        for (unsigned block=0;err==ESP_OK && block<n/16;++block) if (trailer(block)) {
            uint8_t access[4];
            if (!div_mifare_access(dump+block*16,access)) { div_report("Block %u: invalid access complements",block); continue; }
            div_report("Trailer %u C1C2C3: data %u %u %u; trailer %u",block,access[0],access[1],access[2],access[3]);
        }
        free(dump); return err;
    }
    div_tag_t tag;
    esp_err_t err=await_tag(&tag,div_arg_int(j,"seconds",15,1,120));
    if (err!=ESP_OK || !strcmp(id,"nfc_read")) return err;
    bool classic=(tag.sak&0x08)!=0, ul=tag.sak==0;
    if (!classic && !ul) return ESP_ERR_NOT_SUPPORTED;
    unsigned blocks=classic ? (tag.sak==0x18 ? 256 : tag.sak==0x09 ? 20 : 64) : 16;
    if (!strcmp(id,"nfc_dump")) {
        size_t size=classic ? blocks*16 : blocks*4;
        uint8_t *dump=calloc(1,size); if (!dump) return ESP_ERR_NO_MEM;
        unsigned failed=0;
        for (unsigned b=0;b<blocks && !div_job_cancelled();b+=classic ? 1 : 4) {
            if (classic && b==sector_start(b)) err=div_pn532_auth(&tag,b,key);
            if (err==ESP_OK) err=div_pn532_read(b,dump+b*(classic ? 16 : 4));
            if (err!=ESP_OK) { div_report("Read block %u: %s",b,esp_err_to_name(err)); ++failed; }
            else div_report("Read block %u",b);
        }
        /* A partial dump must never become an apparently valid clone source. */
        if (failed || div_job_cancelled()) err=ESP_FAIL;
        else err=div_storage_write(relative,dump,size);
        free(dump); return err;
    }
    if (!strcmp(id,"nfc_clone")) {
        uint8_t *dump=malloc(4096); if (!dump) return ESP_ERR_NO_MEM;
        err=div_storage_read(relative,dump,4096,&n);
        size_t expected=blocks*(classic ? 16 : 4);
        if (err==ESP_OK && n!=expected) err=ESP_ERR_INVALID_SIZE;
        unsigned authenticated=UINT32_MAX, written=0;
        for (unsigned b=classic ? 1 : 4;b<blocks && err==ESP_OK && !div_job_cancelled();++b) {
            if (classic && trailer(b)) continue;
            if (classic && sector_start(b)!=authenticated) {
                err=div_pn532_auth(&tag,b,key); authenticated=sector_start(b);
            }
            if (err==ESP_OK) err=div_pn532_write(b,dump+b*(classic ? 16 : 4),ul);
            if (err==ESP_OK) ++written;
        }
        div_report("Copied %u writable blocks/pages; UID and lock/trailer bytes preserved",written);
        free(dump); return err;
    }
    unsigned block=div_arg_int(j,"block",4,0,255);
    if (block>=blocks || (classic && (!block || trailer(block))) || (ul && block<4)) return ESP_ERR_INVALID_ARG;
    uint8_t data[16]={0};
    const char *hex=div_arg_string(j,"data","");
    if (*hex && (!div_hex_decode(hex,data,sizeof(data),&n) || n!=(ul ? 4 : 16))) return ESP_ERR_INVALID_ARG;
    if (classic) err=div_pn532_auth(&tag,block,key);
    if (err==ESP_OK) err=div_pn532_write(block,data,ul);
    return err;
}
esp_err_t div_nfc_job(const div_job_t *j)
{
    if (!strcmp(j->feature->id,"nfc_access")) return card_job(j);
    esp_err_t err=div_pn532_open();
    if (err!=ESP_OK) { div_report("PN532: %s (external I2C module must be enabled)",esp_err_to_name(err)); return err; }
    if (!strcmp(j->feature->id,"nfc_jam") || !strcmp(j->feature->id,"nfc_disrupt")) err=field_session(j);
    else if (!strcmp(j->feature->id,"nfc_emulate")) err=emulate(j);
    else err=card_job(j);
    rf_field(false); div_pn532_close(); return err;
}
