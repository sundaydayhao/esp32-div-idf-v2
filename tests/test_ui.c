/* Exercise the same C/LVGL menu renderer used on the board. No radio hardware. */
#include "div_menu.h"
#include "div_touch.h"
#include "div_waterfall.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void spectrum_tests(void)
{
    div_spectrum_capture_t capture={0};
    const uint64_t start=1000, period=DIV_SPECTRUM_SAMPLES*DIV_SPECTRUM_SAMPLE_US;
    div_spectrum_begin(&capture,start,6);
    div_spectrum_receive(&capture,start,-40,false);
    div_spectrum_receive(&capture,start+199,-60,true);
    div_spectrum_receive(&capture,start+200,-50,false);
    div_spectrum_receive(&capture,start+period-1,-70,false);
    div_spectrum_advance(&capture,start+period);
    assert(capture.frame.counts[0]==2 && capture.frame.counts[1]==1 && capture.frame.counts[255]==1);
    assert(capture.frame.packets==4 && capture.frame.disconnects==1 && capture.frame.rssi==-55);
    uint8_t levels[DIV_SPECTRUM_BINS];
    div_spectrum_levels(&capture.frame,levels);
    unsigned nonzero=0;
    for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) { assert(levels[i]<=127); nonzero+=levels[i]>0; }
    assert(nonzero>0);
    div_spectrum_receive(&capture,start+period,-80,false); /* Exact window boundary. */
    assert(capture.counts[0]==1 && capture.frame.packets==4);
    div_spectrum_advance(&capture,start+4*period); /* Missed windows must show silence. */
    div_spectrum_levels(&capture.frame,levels);
    for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) assert(levels[i]==0);
    assert(capture.frame.packets==5 && capture.frame.rssi==-127);
    uint32_t sequence=capture.frame.sequence;
    div_spectrum_begin(&capture,start+4*period,11);
    assert(capture.frame.channel==11 && capture.frame.sequence>sequence && capture.frame.packets==0);
    capture.active=false;
    div_spectrum_receive(&capture,start+4*period,-40,false);
    assert(capture.total==0);

    /* Compare the radix-2 result with an independent direct DFT, including DC
       removal and a Hamming window. A known tone must peak at its true bin. */
    float input[DIV_SPECTRUM_SAMPLES], magnitude[DIV_SPECTRUM_BINS];
    const double pi=3.14159265358979323846;
    double mean=0;
    for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;++i) {
        input[i]=5+2*sinf(2*pi*17*i/256)+0.3f*cosf(2*pi*43*i/256); mean+=input[i];
    }
    mean/=DIV_SPECTRUM_SAMPLES;
    div_spectrum_fft(input,magnitude);
    unsigned peak=0;
    for (unsigned k=0;k<DIV_SPECTRUM_BINS;++k) {
        double real=0, imag=0;
        for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;++i) {
            double sample=(input[i]-mean)*(0.54-0.46*cos(2*pi*i/255));
            real+=sample*cos(2*pi*k*i/256); imag-=sample*sin(2*pi*k*i/256);
        }
        assert(fabs(magnitude[k]-sqrt(real*real+imag*imag))<0.002);
        if (magnitude[k]>magnitude[peak]) peak=k;
    }
    assert(peak==17);
    assert(div_spectrum_rgb565(0)==0 && div_spectrum_rgb565(64)==0xf800 && div_spectrum_rgb565(127)==0xffff);
}

static lv_color_t pixels[240*320], buffer[240*24];
static const char *last_action;
static unsigned click_count;
static div_touch_filter_t filter;
static bool raw_pressed;
static div_touch_point_t raw_point;
static const div_touch_calibration_t calibration={300,3800,300,3800,true,true,true};
static void pointer_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    div_touch_point_t raw, point;
    bool pressed=div_touch_filter(&filter,raw_pressed,raw_point,lv_tick_get(),&raw);
    data->state=pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
    if (pressed) {
        assert(div_touch_map(&calibration,raw,240,320,&point));
        data->point.x=point.x; data->point.y=point.y;
    }
}
static void touch_time(unsigned milliseconds)
{
    for (unsigned t=0;t<milliseconds;t+=10) { lv_tick_inc(10); lv_timer_handler(); }
}
static void touch_tests(void)
{
    /* COM26 V3 measurements: top edge has HIGH Y ADC values. These real
       asymmetric samples catch the old uncalibrated, non-inverted Y mapping. */
    const div_touch_point_t measured[]={{477,3644},{3620,3609},{3598,362},{476,387}};
    div_touch_calibration_t measured_fit; div_touch_point_t center;
    assert(div_touch_fit(measured,240,320,0,&measured_fit));
    assert(!measured_fit.swap && !measured_fit.invert_x && measured_fit.invert_y);
    assert(div_touch_map(&measured_fit,(div_touch_point_t){2065,1958},240,320,&center));
    assert(abs(center.x-120)<=5 && abs(center.y-160)<=5);
    /* Fit every wiring orientation and display rotation, not only a symmetric
       centre point which cannot reveal swapped / inverted axes. */
    for (unsigned rotate=0;rotate<4;++rotate) for (unsigned wiring=0;wiring<8;++wiring) {
        div_touch_point_t points[4];
        const div_touch_point_t corners[]={{20,20},{219,20},{219,299},{20,299}};
        for (unsigned i=0;i<4;++i) {
            div_touch_point_t p=corners[i];
            int x=300+p.x*3500/239, y=300+p.y*3500/319;
            if (wiring&1) x=4100-x;
            if (wiring&2) y=4100-y;
            points[(i+rotate)%4]=(wiring&4)?(div_touch_point_t){y,x}:(div_touch_point_t){x,y};
        }
        div_touch_calibration_t fit;
        assert(div_touch_fit(points,240,320,rotate,&fit));
        for (unsigned i=0;i<4;++i) {
            div_touch_point_t mapped;
            assert(div_touch_map(&fit,points[(i+rotate)%4],240,320,&mapped));
            assert(abs(mapped.x-corners[i].x)<=2 && abs(mapped.y-corners[i].y)<=2);
        }
        points[0]=points[1];
        assert(!div_touch_fit(points,240,320,rotate,&fit));
    }
    div_touch_filter_t f={0}; div_touch_point_t p, a={1000,1200}, noise={3300,400};
    assert(!div_touch_filter(&f,true,a,0,&p));
    assert(div_touch_filter(&f,true,a,30,&p));
    assert(div_touch_filter(&f,true,noise,40,&p) && p.x==1000 && p.y==1200);
    assert(div_touch_filter(&f,false,a,50,&p));
    assert(div_touch_filter(&f,true,a,70,&p));
    assert(div_touch_filter(&f,false,a,80,&p));
    assert(!div_touch_filter(&f,false,a,140,&p));
    div_touch_block(&f);
    assert(!div_touch_filter(&f,true,a,160,&p));
    assert(!div_touch_filter(&f,false,a,180,&p));
    assert(!div_touch_filter(&f,true,a,200,&p)); /* Release bounce on new page. */
    assert(!div_touch_filter(&f,false,a,220,&p));
    assert(!div_touch_filter(&f,false,a,340,&p));
    assert(!div_touch_filter(&f,true,a,360,&p));
    assert(div_touch_filter(&f,true,a,390,&p));
}
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color)
{
    for (int y=area->y1;y<=area->y2;++y)
        for (int x=area->x1;x<=area->x2;++x) pixels[y*240+x]=*color++;
    lv_disp_flush_ready(driver);
}
static void action(const char *value) { last_action=value; ++click_count; }
static void snapshot(const char *name)
{
    lv_refr_now(NULL);
    char path[200]; snprintf(path,sizeof(path),"build/ui-preview/%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n240 320\n255\n");
    for (unsigned i=0;i<240*320;++i) {
        lv_color32_t c; c.full=lv_color_to32(pixels[i]);
        uint8_t rgb[]={c.ch.red,c.ch.green,c.ch.blue}; fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(void)
{
    touch_tests();
    spectrum_tests();
    lv_init();
    static lv_disp_draw_buf_t draw; lv_disp_draw_buf_init(&draw,buffer,NULL,240*24);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res=240; driver.ver_res=320; driver.draw_buf=&draw; driver.flush_cb=flush;
    lv_disp_t *display=lv_disp_drv_register(&driver);
    lv_theme_default_init(display,lv_color_hex(0xff8000),lv_color_hex(0xff8000),true,&div_font_16);
    lv_obj_t *body=lv_obj_create(lv_scr_act()); lv_obj_set_pos(body,0,20); lv_obj_set_size(body,240,300);
    lv_obj_t *bar=div_menu_status_create(lv_scr_act(),true,0);
    div_menu_status_update(bar,true,-1,false);
    lv_group_t *group=lv_group_create(); lv_group_set_default(group);
    static lv_indev_drv_t pointer; lv_indev_drv_init(&pointer);
    pointer.type=LV_INDEV_TYPE_POINTER; pointer.read_cb=pointer_read; pointer.disp=display;
    lv_indev_drv_register(&pointer);
    lv_obj_update_layout(body);
    div_menu_show(body,group,-1,0,true,0,action); lv_obj_update_layout(body);
    assert(lv_obj_get_child_cnt(body)==8);
    const int positions[8][2]={{10,30},{10,105},{10,180},{10,255},{130,30},{130,105},{130,180},{130,255}};
    for (unsigned i=0;i<8;++i) {
        lv_obj_t *button=lv_obj_get_child(body,i); lv_area_t a; lv_obj_get_coords(button,&a);
        assert(a.x1==positions[i][0] && a.y1==positions[i][1]);
        assert(lv_obj_get_width(button)==100 && lv_obj_get_height(button)==60);
    }
    uint32_t key=LV_KEY_RIGHT;
    lv_event_send(lv_group_get_focused(group),LV_EVENT_KEY,&key); assert(div_menu_focused()==4);
    lv_event_send(lv_group_get_focused(group),LV_EVENT_CLICKED,NULL); assert(!strcmp(last_action,"@ble"));
    div_menu_focus(0); snapshot("home");
    const char *expected[]={"@wifi","@nrf","@more","system_settings","@ble","@sub","@tools","system_about"};
    for (unsigned i=0;i<8;++i) {
        int x=positions[i][0]+50, y=positions[i][1]+30;
        raw_point=(div_touch_point_t){3800-y*3500/319,3800-x*3500/239};
        unsigned before=click_count;
        raw_pressed=true; touch_time(150);
        raw_pressed=false; touch_time(160);
        assert(click_count==before+1 && !strcmp(last_action,expected[i]));
    }
    /* A single press stays on its original tile even if the pointer slides. */
    raw_point=(div_touch_point_t){3800-285*3500/319,3800-180*3500/239};
    unsigned before=click_count;
    raw_pressed=true; touch_time(100);
    raw_point=(div_touch_point_t){3800-60*3500/319,3800-60*3500/239}; touch_time(100);
    raw_pressed=false; touch_time(160);
    assert(click_count==before+1 && !strcmp(last_action,"system_about"));
    for (int menu=0;menu<=8;++menu) {
        unsigned pages=menu<2?2:1;
        for (unsigned page=0;page<pages;++page) {
            lv_group_remove_all_objs(group); lv_obj_clean(body);
            div_menu_show(body,group,menu,page,true,0,action); lv_obj_update_layout(body);
            char name[32]; snprintf(name,sizeof(name),"menu-%d-%u",menu,page); snapshot(name);
            for (uint32_t i=0;i<lv_obj_get_child_cnt(body);++i) {
                lv_obj_t *button=lv_obj_get_child(body,i); lv_area_t a; lv_obj_get_coords(button,&a);
                assert(a.x1>=0 && a.x2<240 && a.y1>=20 && a.y2<320);
                lv_event_send(button,LV_EVENT_CLICKED,NULL); assert(last_action && *last_action);
            }
            if (menu<2) {
                assert(!strcmp(last_action,"@page"));
                lv_obj_t *footer=lv_obj_get_child(body,-1); lv_area_t a; lv_obj_get_coords(footer,&a);
                assert(a.y1==290);
            }
        }
    }
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&div_font_16,&glyph,'W',0) && glyph.adv_w==10);
    assert(lv_font_get_glyph_dsc(&div_font_16,&glyph,'i',0) && glyph.adv_w==4);
    lv_group_remove_all_objs(group); lv_obj_clean(body); lv_obj_remove_style_all(body);
    lv_obj_set_pos(body,0,20); lv_obj_set_size(body,240,300);
    lv_obj_t *canvas=div_waterfall_create(body,240,246); assert(canvas);
    lv_obj_set_pos(canvas,0,18); lv_obj_update_layout(body);
    uint8_t levels[DIV_SPECTRUM_BINS]; memset(levels,127,sizeof(levels));
    div_waterfall_update(canvas,levels); lv_refr_now(NULL);
    assert(lv_color_to32(pixels[91*240+120])==0xffffffff); /* First waterfall row. */
    memset(levels,0,sizeof(levels)); div_waterfall_update(canvas,levels); lv_refr_now(NULL);
    assert(lv_color_to32(pixels[91*240+120])==0xffffffff); /* History persists. */
    assert((lv_color_to32(pixels[92*240+120])&0xffffff)==0); /* Silent row is black. */
    assert((lv_color_to32(pixels[87*240+120])&0xffffff)==0); /* Spectrum clears on silence. */
    /* Synthetic pattern checks cyclic history wrap, every palette segment and
       partial redraws using the actual 96 KiB LVGL allocator. */
    for (unsigned row=0;row<246-53;++row) {
        for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) levels[i]=(i+row)%128;
        div_waterfall_update(canvas,levels); lv_refr_now(NULL);
    }
    snapshot("waterfall-test-pattern");
    lv_obj_del(canvas); lv_refr_now(NULL);
    assert(lv_mem_test()==LV_RES_OK);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    assert(memory.free_biggest_size>65000); /* Canvas buffer and decoder released. */
    puts("Waterfall: timestamp windows, silence, channel reset, FFT vs DFT, palette, history wrap, partial redraw and buffer release passed.");
    puts("UI: 32 calibration orientations, noise/release filter, real pointer hits on all 8 tiles including About, press lock, grid/pages/keypad/fonts passed.");
    return 0;
}
