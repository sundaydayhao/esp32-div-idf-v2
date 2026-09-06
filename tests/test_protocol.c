#include "div_protocol.h"
#include "pulse_profile.h"
#include "div_storage.h"
#include "minmea.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); exit(1); } } while (0)
static unsigned char stored[16384];
static size_t stored_length;
esp_err_t div_storage_write(const char *path,const void *data,size_t len)
{
    (void)path;
    if (len>sizeof(stored)) return ESP_ERR_INVALID_SIZE;
    memcpy(stored,data,len); stored_length=len; return ESP_OK;
}
esp_err_t div_storage_read(const char *path,void *data,size_t cap,size_t *len)
{
    (void)path;
    if (stored_length>cap) return ESP_ERR_INVALID_SIZE;
    memcpy(data,stored,stored_length); *len=stored_length; return ESP_OK;
}
static void test_hex(void)
{
    uint8_t data[6]; size_t n; char text[13];
    CHECK(div_hex_decode("01:23:45:67:89:ab",data,6,&n) && n==6 && data[5]==0xab);
    div_hex_encode(data,n,text,sizeof(text)); CHECK(!strcmp(text,"0123456789AB"));
    CHECK(!div_hex_decode("A B",data,6,&n));
    CHECK(!div_hex_decode("123",data,6,&n));
    CHECK(!div_hex_decode("GG",data,6,&n));
    CHECK(!div_hex_decode("00112233445566",data,6,&n));
    CHECK(div_hex_decode("",data,6,&n) && n==0);
    char tiny[2]={'x','x'}; div_hex_encode(data,6,tiny,2); CHECK(!tiny[0]);
}
static void test_wifi(void)
{
    uint8_t frame[64]={0x80};
    const uint8_t ies[]={0,3,'l','a','b',3,1,6,221,4,0,0x50,0xf2,4};
    memcpy(frame+36,ies,sizeof(ies)); frame[16]=0x12;
    div_wifi_ies_t parsed;
    CHECK(div_wifi_parse_ies(frame,36+sizeof(ies),&parsed));
    CHECK(!strcmp(parsed.ssid,"lab") && parsed.channel==6 && parsed.wps && parsed.bssid[0]==0x12);
    for (size_t n=0;n<36;++n) CHECK(!div_wifi_parse_ies(frame,n,&parsed));
    frame[37]=255; CHECK(!div_wifi_parse_ies(frame,50,&parsed));
    frame[37]=0; CHECK(div_wifi_parse_ies(frame,38,&parsed) && parsed.hidden);
    frame[0]=0x08; CHECK(!div_wifi_parse_ies(frame,38,&parsed));
    frame[0]=0x40; memcpy(frame+24,ies,sizeof(ies));
    CHECK(div_wifi_parse_ies(frame,24+sizeof(ies),&parsed) && !strcmp(parsed.ssid,"lab"));
}
static void test_dns(void)
{
    const uint8_t query[]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'l','a','b',0,0,1,0,1};
    uint8_t answer[64], bad[sizeof(query)], ip[]={192,168,4,1};
    size_t n=div_dns_answer(query,sizeof(query),answer,sizeof(answer),ip);
    CHECK(n==37 && answer[0]==0x12 && answer[1]==0x34 && answer[7]==1 && !memcmp(answer+n-4,ip,4));
    CHECK(!div_dns_answer(query,sizeof(query),answer,20,ip));
    for (size_t i=0;i<sizeof(query);++i) CHECK(!div_dns_answer(query,i,answer,sizeof(answer),ip));
    memcpy(bad,query,sizeof(bad)); bad[12]=0xc0; CHECK(!div_dns_answer(bad,sizeof(bad),answer,64,ip));
    memcpy(bad,query,sizeof(bad)); bad[18]=28;
    CHECK(div_dns_answer(bad,sizeof(bad),answer,64,ip)==sizeof(bad) && answer[7]==0);
    memcpy(bad,query,sizeof(bad)); bad[2]|=0x80; CHECK(!div_dns_answer(bad,sizeof(bad),answer,64,ip));
}
static void test_hid_access(void)
{
    uint8_t trailer[16]={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x07,0x80,0x69,0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t access[4],mod,key;
    CHECK(div_mifare_access(trailer,access) && access[0]==0 && access[1]==0 && access[2]==0 && access[3]==1);
    trailer[6]^=1; CHECK(!div_mifare_access(trailer,access));
    CHECK(div_hid_ascii('A',&mod,&key) && mod==2 && key==4);
    CHECK(div_hid_ascii('@',&mod,&key) && mod==2 && key==31);
    CHECK(div_hid_ascii('\\',&mod,&key) && mod==0 && key==49);
    CHECK(!div_hid_ascii(0,&mod,&key)); CHECK(!div_hid_ascii(0xff,&mod,&key));
    CHECK(div_hid_key("F12",&mod,&key) && key==69);
    CHECK(!div_hid_key("F13",&mod,&key));
    CHECK(div_hid_key("WIN",&mod,&key) && mod==8 && key==0);
    CHECK(div_hid_key("PGDN",&mod,&key) && key==78);
    const uint8_t findmy[]={6,0xff,0x4c,0,0x12,0x19,0};
    CHECK(div_ble_findmy(findmy,sizeof(findmy)));
    CHECK(!div_ble_findmy(findmy,sizeof(findmy)-1));
}
static void test_profiles(void)
{
    div_pulses_t p={0},read; uint32_t frequency;
    CHECK(div_nec_pulses(0x20df10ef,&p)==ESP_OK && p.carrier_hz==38000);
    CHECK(p.duration[0]==9000 && p.duration[1]==4500 && p.level[0]==1 && p.level[1]==0);
    CHECK(div_profile_save("capture.ir",&p,433920000)==ESP_OK);
    CHECK(div_profile_load("capture.ir",&read,&frequency)==ESP_OK);
    CHECK(frequency==433920000 && !memcmp(&p,&read,sizeof(p)));
    const char *raw="Filetype: Flipper SubGhz RAW File\nFrequency: 315000000\nRAW_Data: 350 -1050 1050 -350\n";
    div_storage_write("",raw,strlen(raw));
    CHECK(div_profile_load("",&read,&frequency)==ESP_OK && read.count==4 && frequency==315000000);
    const char *bad="RAW_Data: 350 -1050 nonsense -350\n";
    div_storage_write("",bad,strlen(bad)); CHECK(div_profile_load("",&read,&frequency)!=ESP_OK);
    const char *ir="type: raw\nfrequency: 38000\ndata: 9000 4500 560 560\n";
    div_storage_write("",ir,strlen(ir));
    CHECK(div_profile_load("",&read,&frequency)==ESP_OK && read.carrier_hz==38000 && read.level[1]==0);
    uint8_t legacy[46]={0x49,0x52,0x52,0x31,1,0,38,0,4,0};
    for (int i=0;i<4;++i) { legacy[38+i*2]=0x30; legacy[39+i*2]=2; }
    div_storage_write("",legacy,sizeof(legacy));
    CHECK(div_profile_load("",&read,&frequency)==ESP_OK && read.count==4 && read.duration[0]==560);
    legacy[8]=5; div_storage_write("",legacy,sizeof(legacy));
    CHECK(div_profile_load("",&read,&frequency)!=ESP_OK);
    memset(&p,0,sizeof(p)); CHECK(div_pulse_append(&p,70000,false)==ESP_OK && p.count==3);
    CHECK((uint32_t)p.duration[0]+p.duration[1]+p.duration[2]==70000);
    p.count=DIV_PULSE_MAX; CHECK(div_pulse_append(&p,1,true)==ESP_ERR_INVALID_SIZE);
}
static void test_gps(void)
{
    const char *line="$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    struct minmea_sentence_rmc rmc;
    CHECK(minmea_check(line,true) && minmea_parse_rmc(&rmc,line) && rmc.valid);
    CHECK(fabs(minmea_tocoord(&rmc.latitude)-48.1173)<0.0001);
    CHECK(fabs(minmea_tocoord(&rmc.longitude)-11.5166667)<0.0001);
    CHECK(!minmea_check("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00",true));
}
static void test_sequence(void)
{
    uint8_t seen[256]={0}; uint32_t state=1;
    for (int i=0;i<255;++i) {
        CHECK(state>0 && state<256 && !seen[state]);
        seen[state]=1; state=div_lfsr_next(8,state);
    }
    CHECK(state==1); CHECK(div_lfsr_next(7,1)==0);
}
int main(void)
{
    test_hex(); test_wifi(); test_dns(); test_hid_access(); test_profiles(); test_gps(); test_sequence();
    /* Exercise malformed external packet inputs over every bounded length. */
    uint32_t random=0x12345678; uint8_t frame[768],answer[800],ip[4]={127,0,0,1};
    for (unsigned round=0;round<10000;++round) {
        for (size_t i=0;i<sizeof(frame);++i) { random=random*1664525+1013904223; frame[i]=random>>24; }
        size_t n=random%sizeof(frame); div_wifi_ies_t ies;
        div_wifi_parse_ies(frame,n,&ies); div_ble_findmy(frame,n); div_dns_answer(frame,n,answer,sizeof(answer),ip);
    }
    printf("PASS: %u assertions and 10000 malformed-packet cases\n",checks);
    return 0;
}
