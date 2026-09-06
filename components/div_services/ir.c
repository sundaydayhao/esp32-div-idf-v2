#include "div_service.h"
#include "div_board.h"
#include "pulse_profile.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
esp_err_t div_ir_job(const div_job_t *j)
{
#ifndef CONFIG_DIV_IR_ENABLED
    (void)j;
    div_report("GPIO14/21 belong to NRF24 #3. Select IR wiring in menuconfig after rewiring.");
    return ESP_ERR_NOT_SUPPORTED;
#else
    div_pulses_t *p=calloc(1,sizeof(*p));
    if (!p) return ESP_ERR_NO_MEM;
    const char *path=div_arg_string(j,"path","ir/capture.ir");
    esp_err_t err;
    if (!strcmp(j->feature->id,"ir_record")) {
        div_report("Point the remote at the IR receiver and press a key.");
        err=div_pulse_capture(DIV_IR_RX,true,div_arg_int(j,"seconds",15,1,120)*1000,p,div_job_cancelled);
        if (err==ESP_OK) {
            p->carrier_hz=div_arg_int(j,"carrier",38000,10000,100000);
            err=div_profile_save(path,p,0);
            if (err==ESP_OK) div_report("Saved %" PRIu32 " pulses to %s",p->count,path);
        }
    } else {
        const char *code=div_arg_string(j,"code","");
        uint32_t frequency;
        if (*code) {
            char *end; unsigned long long value=strtoull(code,&end,16);
            err=(*end || end==code || value>UINT32_MAX) ? ESP_ERR_INVALID_ARG : div_nec_pulses(value,p);
        } else err=div_profile_load(path,p,&frequency);
        if (err==ESP_OK && !p->carrier_hz) p->carrier_hz=div_arg_int(j,"carrier",38000,10000,100000);
        unsigned count=div_arg_int(j,"count",1,1,1000);
        for (unsigned i=0; i<count && err==ESP_OK && !div_job_cancelled(); ++i) {
            err=div_pulse_send(DIV_IR_TX,p); div_job_delay(40);
        }
    }
    free(p); return err;
#endif
}
