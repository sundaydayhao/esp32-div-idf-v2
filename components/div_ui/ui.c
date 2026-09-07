#include "div_ui.h"
#include "div_board.h"
#include "div_storage.h"
#include "div_service.h"
#include "div_menu.h"
#include "div_touch.h"
#include "div_waterfall.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

static spi_device_handle_t touch;
static lv_disp_t *display;
static lv_obj_t *body, *title, *status, *keyboard, *output, *form, *controls;
static lv_group_t *group;
static lv_obj_t *inputs[12];
static const div_feature_t *selected;
static int category = -1;
static unsigned menu_page, menu_focus[10];
static div_settings_t settings;
static uint32_t after;
static char log_text[4096];
static lv_obj_t *packet_graph, *packet_title;
static uint32_t packet_sequence;
static bool packet_config;
static char packet_args[DIV_JOB_TEXT_MAX]="{\"seconds\":0,\"channel\":1}";
static atomic_bool calibration_requested;
static atomic_bool back_requested;
static bool calibrating, was_pressed, cal_pending;
static unsigned corner;
static div_touch_point_t cal_samples[4], cal_sample, cal_anchor;
static div_touch_calibration_t cal_fit;
static div_touch_filter_t touch_filter;
static unsigned cal_count;
static uint32_t cal_sum_x, cal_sum_y;
static lv_obj_t *cross, *cal_label, *cal_screen;
static int64_t cal_deadline, pressed_since;
static int screen_w(void) { return lv_disp_get_hor_res(display); }
static int screen_h(void) { return lv_disp_get_ver_res(display); }
static lv_color_t accent_color(uint8_t preset)
{
    return div_menu_palette(settings.dark,preset).accent;
}

static bool adc_read(uint8_t command, uint16_t *value)
{
    spi_transaction_t transaction = {.length=24, .flags=SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .tx_data={command,0,0}};
    if (spi_device_polling_transmit(touch, &transaction) != ESP_OK) return false;
    *value = ((transaction.rx_data[1] << 8 | transaction.rx_data[2]) >> 3) & 4095;
    return true;
}
static bool touch_raw(uint16_t *x, uint16_t *y)
{
    uint16_t z1, z2, discard, xs[5], ys[5];
    bool ok=false;
    /* ESP-IDF SPI bus acquisition only accepts portMAX_DELAY. The LCD DMA
       completes via its ISR before this short group of touch transactions. */
    if (!touch || spi_device_acquire_bus(touch,portMAX_DELAY)!=ESP_OK) return false;
    /* Keep the ADC on while sampling. Discard the first conversion after
       switching axes, then confirm pressure again before accepting the packet. */
    if (!adc_read(0xb1,&z1) || !adc_read(0xc1,&z2)) goto done;
    unsigned threshold=touch_filter.down ? 150 : 300;
    if (!z1 || z1==4095 || !z2 || z1+4095-z2<threshold) goto done;
    if (!adc_read(0xd1,&discard)) goto done;
    for (unsigned i=0;i<5;++i) if (!adc_read(0xd1,&xs[i])) goto done;
    if (!adc_read(0x91,&discard)) goto done;
    for (unsigned i=0;i<5;++i) if (!adc_read(0x91,&ys[i])) goto done;
    if (!adc_read(0xb1,&z1) || !adc_read(0xc1,&z2)) goto done;
    if (!z1 || z1==4095 || !z2 || z1+4095-z2<threshold) goto done;
    for (unsigned i=0; i<5; ++i) for (unsigned j=i+1; j<5; ++j) {
        if (xs[j]<xs[i]) { uint16_t v=xs[i]; xs[i]=xs[j]; xs[j]=v; }
        if (ys[j]<ys[i]) { uint16_t v=ys[i]; ys[i]=ys[j]; ys[j]=v; }
    }
    if (xs[2]<32 || xs[2]>4063 || ys[2]<32 || ys[2]>4063 || xs[3]-xs[1]>120 || ys[3]-ys[1]>120) goto done;
    *x=xs[2]; *y=ys[2]; ok=true;
done:
    adc_read(0x90,&discard); /* Power down / restore PENIRQ. */
    spi_device_release_bus(touch);
    return ok;
}
static int clamp(int value, int max) { return value < 0 ? 0 : value > max ? max : value; }
static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    uint16_t x=0, y=0;
    bool valid=touch_raw(&x,&y);
    div_touch_point_t raw;
    bool pressed=div_touch_filter(&touch_filter,valid,(div_touch_point_t){x,y},esp_timer_get_time()/1000,&raw);
    if (calibrating) {
        if (pressed && !cal_pending) {
            if (!was_pressed || abs(raw.x-cal_anchor.x)>100 || abs(raw.y-cal_anchor.y)>100) {
                pressed_since=esp_timer_get_time(); cal_count=0; cal_sum_x=cal_sum_y=0; cal_anchor=raw;
            }
            if (valid && cal_count<64) { cal_sum_x+=raw.x; cal_sum_y+=raw.y; ++cal_count; }
        }
        if (!pressed && was_pressed && !cal_pending && cal_count>=5 && esp_timer_get_time()-pressed_since>=120000) {
            cal_sample=(div_touch_point_t){cal_sum_x/cal_count,cal_sum_y/cal_count};
            cal_pending=true;
        }
        data->state=LV_INDEV_STATE_REL;
        was_pressed=pressed;
        return;
    }
    if (!settings.touch_calibrated) pressed=false;
    data->state=pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (!pressed) return;
    /* LVGL rotates pointer coordinates itself. Supply native panel coordinates. */
    div_touch_calibration_t cal={settings.touch_x0,settings.touch_x1,settings.touch_y0,settings.touch_y1,
        settings.touch_swap,settings.touch_invert_x,settings.touch_invert_y};
    div_touch_point_t point;
    if (div_touch_map(&cal,raw,DIV_LCD_WIDTH,DIV_LCD_HEIGHT,&point)) {
        data->point.x=point.x; data->point.y=point.y;
    } else data->state=LV_INDEV_STATE_REL;
}
static void key_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static uint8_t previous, stable;
    static bool aux_down;
    static bool aux_long;
    static int64_t aux_since;
    static int64_t changed;
    uint8_t pressed=0;
    div_board_buttons(&pressed);
    int64_t now=esp_timer_get_time();
    if (pressed != previous) { previous=pressed; changed=now; }
    if (now-changed >= 25000) stable=pressed;
    bool aux_pressed=(stable & (1<<DIV_BUTTON_AUX))!=0;
    if (aux_pressed && !aux_down) {
        atomic_store(&back_requested,true); aux_since=now; aux_long=false;
    }
    if (aux_pressed && !aux_long && now-aux_since>2000000) {
        atomic_store(&calibration_requested,true); aux_long=true;
    }
    aux_down=aux_pressed;
    data->state=stable ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (stable & (1<<DIV_BUTTON_UP)) data->key=selected ? LV_KEY_PREV : LV_KEY_UP;
    else if (stable & (1<<DIV_BUTTON_DOWN)) data->key=selected ? LV_KEY_NEXT : LV_KEY_DOWN;
    else if (stable & (1<<DIV_BUTTON_LEFT)) data->key=LV_KEY_LEFT;
    else if (stable & (1<<DIV_BUTTON_RIGHT)) data->key=LV_KEY_RIGHT;
    else if (stable & (1<<DIV_BUTTON_OK)) data->key=LV_KEY_ENTER;
    else if (aux_pressed) { data->key=0; data->state=LV_INDEV_STATE_REL; }
}
static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *object=lv_label_create(parent);
    lv_label_set_text(object,text);
    lv_obj_set_width(object,LV_PCT(100));
    return object;
}
static void beep_off(lv_timer_t *timer) { (void)timer; div_board_beep(false); }
static void click_feedback(lv_event_t *event)
{
    (void)event;
    if (div_board_beep(true)==ESP_OK) {
        lv_timer_t *timer=lv_timer_create(beep_off,30,NULL);
        if (timer) lv_timer_set_repeat_count(timer,1);
        else div_board_beep(false);
    }
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *arg)
{
    lv_obj_t *object=lv_btn_create(parent);
    lv_obj_add_flag(object,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_size(object,LV_PCT(100),30);
    div_palette_t palette=div_menu_palette(settings.dark,settings.accent);
    lv_obj_set_style_bg_color(object,palette.fg,0);
    lv_obj_set_style_text_color(object,palette.text,0);
    lv_obj_set_style_text_color(object,palette.accent,LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(object,1,0);
    lv_obj_set_style_border_color(object,palette.line,0);
    lv_obj_set_style_border_color(object,palette.accent,LV_STATE_FOCUSED);
    lv_obj_set_style_radius(object,4,0); lv_obj_set_style_shadow_width(object,0,0);
    lv_obj_set_style_pad_all(object,2,0);
    lv_obj_t *caption=lv_label_create(object);
    lv_label_set_text(caption,text);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object,click_feedback,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(object,callback,LV_EVENT_CLICKED,arg);
    lv_group_add_obj(group,object);
    return object;
}
static void clear_body(void)
{
    /* A page change must consume the whole touch, including release bounce. */
    div_touch_block(&touch_filter);
    lv_group_remove_all_objs(group);
    if (keyboard) { lv_obj_del(keyboard); keyboard=NULL; }
    lv_obj_clean(body);
    packet_graph=NULL; packet_title=NULL;
    lv_obj_clean(controls);
    lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(body);
    div_palette_t palette=div_menu_palette(settings.dark,settings.accent);
    lv_obj_set_style_bg_color(body,palette.bg,0);
    lv_obj_set_style_bg_opa(body,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(body,palette.text,0);
    lv_obj_set_style_text_font(body,&div_font_16,0);
    lv_obj_set_style_text_color(title,palette.accent,0);
    lv_obj_set_style_bg_color(lv_scr_act(),palette.bg,0);
    output=NULL; form=NULL; memset(inputs,0,sizeof(inputs));
}
static void show_home(void);
static void show_category(int value);
static void show_feature(const div_feature_t *feature);
static void show_packet_monitor(bool start);
static void back_event(lv_event_t *event)
{
    (void)event;
    if (calibrating) { calibrating=false; div_report("Calibration cancelled"); }
    div_service_stop();
    if (selected) { selected=NULL; show_category(category); }
    else if (category==DIV_IR || category==DIV_NFC || category==DIV_GPS) show_category(DIV_MENU_MORE);
    else show_home();
}
static void keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event)==LV_EVENT_READY || lv_event_get_code(event)==LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keyboard,NULL);
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(body,screen_h()-88);
    }
}
static void edit_event(lv_event_t *event)
{
    lv_obj_t *input=lv_event_get_target(event);
    if (!keyboard) {
        keyboard=lv_keyboard_create(lv_scr_act());
        lv_obj_set_height(keyboard,screen_h()/2);
        lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_ALL,NULL);
    }
    lv_keyboard_set_textarea(keyboard,input);
    lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(body,screen_h()/2-40);
    lv_obj_scroll_to_view(input,LV_ANIM_ON);
}
static void stop_event(lv_event_t *event) { (void)event; div_service_stop(); }
static void run_event(lv_event_t *event)
{
    (void)event;
    if (!selected) return;
    cJSON *args=cJSON_CreateObject();
    if (!args) return;
    bool ok=true;
    for (unsigned i=0; selected->fields[i].key && i<12; ++i)
        if (!cJSON_AddStringToObject(args,selected->fields[i].key,lv_textarea_get_text(inputs[i]))) ok=false;
    char *json=ok ? cJSON_PrintUnformatted(args) : NULL;
    esp_err_t err=json ? div_service_start(selected->id,json) : ESP_ERR_NO_MEM;
    if (err==ESP_OK && !strcmp(selected->id,"wifi_monitor")) strlcpy(packet_args,json,sizeof(packet_args));
    cJSON_free(json); cJSON_Delete(args);
    if (err!=ESP_OK) div_report("Cannot start: %s",esp_err_to_name(err));
    else {
        if (!strcmp(selected->id,"wifi_monitor")) { clear_body(); show_packet_monitor(false); return; }
        log_text[0]=0;
        if (output) lv_label_set_text(output,"Starting...");
        if (form) lv_obj_add_flag(form,LV_OBJ_FLAG_HIDDEN);
        if (keyboard) { lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN); lv_obj_set_height(body,screen_h()-88); }
    }
}
static void form_event(lv_event_t *event)
{ (void)event; if (form) { lv_obj_clear_flag(form,LV_OBJ_FLAG_HIDDEN); lv_obj_scroll_to_view(form,LV_ANIM_ON); } }
static void packet_run(lv_event_t *event)
{
    (void)event;
    esp_err_t err=div_service_start("wifi_monitor",packet_args);
    if (err!=ESP_OK) {
        div_report("Monitor start: %s",esp_err_to_name(err));
        if (packet_title) lv_label_set_text(packet_title,"Busy - retry Run");
    }
}
static void packet_parameters(lv_event_t *event)
{
    (void)event;
    div_service_stop(); packet_config=true; show_feature(selected); packet_config=false;
}
static void packet_channel(lv_event_t *event)
{ div_wifi_monitor_channel((intptr_t)lv_event_get_user_data(event)); }
static void packet_key(lv_event_t *event)
{
    uint32_t key=lv_event_get_key(event);
    if (key==LV_KEY_LEFT) div_wifi_monitor_channel(-1);
    if (key==LV_KEY_RIGHT) div_wifi_monitor_channel(1);
}
static void show_packet_monitor(bool start)
{
    lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(body,0,20); lv_obj_set_size(body,screen_w(),screen_h()-20);
    lv_obj_clear_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    packet_title=lv_label_create(body); lv_obj_set_pos(packet_title,8,5);
    lv_obj_set_style_text_font(packet_title,&div_font_8,0);
    lv_label_set_text(packet_title,"Packet Monitor");
    lv_obj_t *minus=button(body,"-",packet_channel,(void *)(intptr_t)-1);
    lv_obj_t *plus=button(body,"+",packet_channel,(void *)(intptr_t)1);
    lv_obj_set_size(minus,28,18); lv_obj_set_pos(minus,screen_w()-62,0);
    lv_obj_set_size(plus,28,18); lv_obj_set_pos(plus,screen_w()-30,0);
    packet_graph=div_waterfall_create(body,screen_w(),screen_h()-74);
    if (packet_graph) lv_obj_set_pos(packet_graph,0,18);
    else {
        lv_obj_t *message=label(body,"Not enough display memory"); lv_obj_set_pos(message,8,40);
    }
    packet_sequence=0;
    lv_obj_clear_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *buttons[]={button(controls,"Run",packet_run,NULL),button(controls,"Stop",stop_event,NULL),
        button(controls,"Config",packet_parameters,NULL),button(controls,"Back",back_event,NULL)};
    for (unsigned i=0;i<4;++i) {
        lv_obj_set_width(buttons[i],screen_w()/4-4); lv_obj_set_pos(buttons[i],2+i*screen_w()/4,2);
        lv_obj_add_event_cb(buttons[i],packet_key,LV_EVENT_KEY,NULL);
    }
    lv_obj_add_event_cb(minus,packet_key,LV_EVENT_KEY,NULL); lv_obj_add_event_cb(plus,packet_key,LV_EVENT_KEY,NULL);
    lv_group_focus_obj(buttons[3]);
    if (start && packet_graph) packet_run(NULL);
    ESP_LOGI("div.ui","Packet Monitor waterfall opened");
}
static void update_packet(lv_timer_t *timer)
{
    (void)timer;
    if (!packet_graph || !packet_title) return;
    div_spectrum_frame_t frame;
    bool active=div_wifi_monitor_frame(&frame);
    if (active && frame.sequence!=packet_sequence) {
        uint8_t levels[DIV_SPECTRUM_BINS];
        div_spectrum_levels(&frame,levels); div_waterfall_update(packet_graph,levels);
        packet_sequence=frame.sequence;
    }
    if (active) lv_label_set_text_fmt(packet_title,"Ch:%u  Packet:%lu",frame.channel,(unsigned long)frame.packets);
    else lv_label_set_text(packet_title,div_service_busy()?"Starting...":"Stopped - tap Run");
}
static lv_obj_t *positioned_label(lv_obj_t *parent, const char *text, int x, int y, bool small)
{
    lv_obj_t *obj=lv_label_create(parent); lv_label_set_text(obj,text); lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_font(obj,small?&div_font_8:&div_font_16,0);
    return obj;
}
static void show_about(void)
{
    lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(body,0,20); lv_obj_set_size(body,screen_w(),screen_h()-20);
    lv_obj_t *page=lv_btn_create(body); lv_obj_remove_style_all(page);
    lv_obj_set_size(page,screen_w(),screen_h()-20);
    lv_obj_clear_flag(page,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page,back_event,LV_EVENT_CLICKED,NULL); lv_group_add_obj(group,page);
    lv_obj_t *name=positioned_label(page,"ESP32-DIV",16,20,false);
    lv_obj_set_style_text_color(name,accent_color(settings.accent),0);
    positioned_label(page,"by CiferTech - C / ESP-IDF",16,40,true);
    const char *labels[]={"Board","Mail","GitHub","Web","Firmware"};
    const char *values[]={DIV_BOARD_NAME,"cifertech@gmail.com","github.com/cifertech","CiferTech.net","C UI / native LCD"};
    for (unsigned i=0;i<5;++i) {
        positioned_label(page,labels[i],16,76+i*22,true);
        positioned_label(page,values[i],80,76+i*22,true);
    }
    positioned_label(page,"SELECT / tap to go back",16,screen_h()-40,true);
}
static void save_settings(lv_event_t *event)
{
    (void)event;
    div_settings_t next; div_settings_load(&next);
    next.brightness=lv_slider_get_value(inputs[0]);
    next.dark=lv_obj_has_state(inputs[1],LV_STATE_CHECKED);
    next.led_enabled=lv_obj_has_state(inputs[2],LV_STATE_CHECKED);
    next.background_scan=lv_obj_has_state(inputs[3],LV_STATE_CHECKED);
    next.accent=lv_dropdown_get_selected(inputs[4]);
    esp_err_t err=div_settings_save(&next);
    if (err==ESP_OK) {
        div_board_brightness(next.brightness);
        div_board_led(0,next.led_enabled?8:0,next.led_enabled?16:0);
        settings=next;
        lv_theme_default_init(display,accent_color(settings.accent),lv_palette_main(LV_PALETTE_CYAN),settings.dark,&div_font_16);
        lv_obj_del(status); status=div_menu_status_create(lv_scr_act(),settings.dark,settings.accent);
        show_home();
    } else if (output) lv_label_set_text_fmt(output,"Save failed: %s",esp_err_to_name(err));
}
static void show_settings(void)
{
    lv_label_set_text(title,"Settings"); lv_obj_clear_flag(title,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(body,0,48); lv_obj_set_size(body,screen_w(),screen_h()-88);
    positioned_label(body,"Brightness",10,0,false);
    inputs[0]=lv_slider_create(body); lv_obj_set_pos(inputs[0],18,32); lv_obj_set_size(inputs[0],screen_w()-36,10);
    lv_slider_set_range(inputs[0],0,255); lv_slider_set_value(inputs[0],settings.brightness,LV_ANIM_OFF);
    lv_group_add_obj(group,inputs[0]);
    const char *names[]={"Dark theme","LED enabled","Background scan"};
    bool enabled[]={settings.dark,settings.led_enabled,settings.background_scan};
    for (unsigned i=0;i<3;++i) {
        positioned_label(body,names[i],10,62+34*i,false);
        inputs[i+1]=lv_switch_create(body); lv_obj_set_pos(inputs[i+1],screen_w()-54,60+34*i);
        lv_obj_set_size(inputs[i+1],44,22);
        if (enabled[i]) lv_obj_add_state(inputs[i+1],LV_STATE_CHECKED);
        lv_group_add_obj(group,inputs[i+1]);
    }
    positioned_label(body,"Accent",10,170,false);
    inputs[4]=lv_dropdown_create(body); lv_obj_set_pos(inputs[4],80,164); lv_obj_set_size(inputs[4],screen_w()-90,30);
    lv_dropdown_set_options(inputs[4],"Orange\nGreen\nRed\nCyan\nPurple\nYellow\nWhite");
    lv_dropdown_set_selected(inputs[4],settings.accent); lv_group_add_obj(group,inputs[4]);
    output=positioned_label(body,"",10,205,true);
    lv_obj_clear_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *save=button(controls,"Save",save_settings,NULL), *back=button(controls,"Back",back_event,NULL);
    lv_obj_set_width(save,screen_w()/2-8); lv_obj_set_pos(save,4,2);
    lv_obj_set_width(back,screen_w()/2-8); lv_obj_set_pos(back,screen_w()/2+4,2);
}
static void show_feature(const div_feature_t *feature)
{
    selected=feature;
    if (!strcmp(selected->id,"system_calibrate")) { atomic_store(&calibration_requested,true); return; }
    clear_body();
    if (!strcmp(selected->id,"wifi_monitor") && !packet_config) { show_packet_monitor(true); return; }
    if (!strcmp(selected->id,"system_about")) { show_about(); return; }
    if (!strcmp(selected->id,"system_settings")) { show_settings(); return; }
    lv_obj_clear_flag(title,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(title,selected->name);
    lv_obj_set_pos(body,0,48); lv_obj_set_size(body,screen_w(),screen_h()-88);
    lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(body,8,0); lv_obj_set_style_pad_row(body,6,0);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    form=lv_obj_create(body);
    lv_obj_remove_style_all(form);
    lv_obj_set_width(form,LV_PCT(100)); lv_obj_set_height(form,LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(form,4,0);
    lv_obj_set_flex_flow(form,LV_FLEX_FLOW_COLUMN);
    div_settings_t saved; div_settings_load(&saved);
    for (unsigned i=0; selected->fields[i].key && i<12; ++i) {
        const div_field_t *field=&selected->fields[i];
        label(form,field->label);
        inputs[i]=lv_textarea_create(form);
        lv_obj_set_size(inputs[i],LV_PCT(100),30);
        div_palette_t palette=div_menu_palette(settings.dark,settings.accent);
        lv_obj_set_style_bg_color(inputs[i],palette.fg,0);
        lv_obj_set_style_text_color(inputs[i],palette.text,0);
        lv_obj_set_style_border_color(inputs[i],palette.line,0);
        lv_obj_set_style_pad_all(inputs[i],5,0);
        lv_textarea_set_one_line(inputs[i],true); lv_textarea_set_max_length(inputs[i],256);
        lv_textarea_set_text(inputs[i],field->value);
        if (!strcmp(field->key,"password")) lv_textarea_set_password_mode(inputs[i],true);
        if (!strcmp(selected->id,"system_settings")) {
            int value=!strcmp(field->key,"brightness") ? saved.brightness : !strcmp(field->key,"dark") ? saved.dark :
                !strcmp(field->key,"led") ? saved.led_enabled : !strcmp(field->key,"accent") ? saved.accent : saved.background_scan;
            char number[8]; snprintf(number,sizeof(number),"%d",value); lv_textarea_set_text(inputs[i],number);
        }
        lv_obj_add_event_cb(inputs[i],edit_event,LV_EVENT_CLICKED,NULL);
        lv_group_add_obj(group,inputs[i]);
    }
    lv_obj_clear_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *run=button(controls,"Run",run_event,NULL);
    lv_obj_t *stop=button(controls,"Stop",stop_event,NULL);
    lv_obj_t *params=button(controls,"Config",form_event,NULL);
    lv_obj_t *back=button(controls,"Back",back_event,NULL);
    lv_obj_t *buttons[]={run,stop,params,back};
    for (unsigned i=0;i<4;++i) { lv_obj_set_width(buttons[i],screen_w()/4-4); lv_obj_set_pos(buttons[i],2+i*screen_w()/4,2); }
    log_text[0]=0;
    output=label(body,selected->description);
    lv_label_set_long_mode(output,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(output,&div_font_8,0);
}
static void menu_action(const char *action)
{
    click_feedback(NULL);
    menu_focus[category+1]=div_menu_focused();
    if (!strcmp(action,"@page")) { menu_page^=1; menu_focus[category+1]=0; show_category(category); return; }
    if (!strcmp(action,"@back")) { back_event(NULL); return; }
    const char *menus[]={"@wifi","@ble","@nrf","@sub","@ir","@nfc","@gps","@tools","@more"};
    if (!strcmp(action,"@home")) { show_home(); return; }
    for (unsigned i=0;i<sizeof(menus)/sizeof(menus[0]);++i) {
        if (!strcmp(action,menus[i])) { menu_page=0; show_category(i); return; }
    }
    const div_feature_t *feature=div_feature_find(action);
    if (feature) show_feature(feature);
}
static void show_category(int value)
{
    clear_body(); category=value; selected=NULL;
    lv_obj_add_flag(title,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(body,0,20); lv_obj_set_size(body,screen_w(),screen_h()-20);
    lv_obj_update_layout(body);
    div_menu_show(body,group,category,menu_page,settings.dark,settings.accent,menu_action);
    div_menu_focus(menu_focus[category+1]);
}
static void show_home(void)
{
    menu_page=0;
    show_category(DIV_MENU_HOME);
}
static void position_cross(void)
{
    const bool right=corner==1 || corner==2, bottom=corner>=2;
    int x=corner==4?screen_w()/2:right?screen_w()-21:20;
    int y=corner==4?screen_h()/2:bottom?screen_h()-21:20;
    lv_obj_set_pos(cross,x-10,y-10);
    lv_label_set_text_fmt(cal_label,"Touch calibration %u / 5\n\nHold the cross, then lift.\n\nAUX: Cancel",corner+1);
    lv_obj_align(cal_label,LV_ALIGN_CENTER,0,corner==4?75:0);
    cal_count=0; was_pressed=false; cal_pending=false;
    div_touch_block(&touch_filter);
    cal_deadline=esp_timer_get_time()+180000000;
}
static void begin_calibration(void)
{
    if (cal_screen) { lv_obj_del(cal_screen); cal_screen=NULL; cross=NULL; }
    clear_body(); calibrating=true; corner=0; was_pressed=false; pressed_since=0;
    cal_screen=lv_obj_create(lv_scr_act()); lv_obj_remove_style_all(cal_screen);
    lv_obj_set_size(cal_screen,screen_w(),screen_h()); lv_obj_clear_flag(cal_screen,LV_OBJ_FLAG_SCROLLABLE);
    div_palette_t palette=div_menu_palette(settings.dark,settings.accent);
    lv_obj_set_style_bg_opa(cal_screen,LV_OPA_COVER,0); lv_obj_set_style_bg_color(cal_screen,palette.bg,0);
    lv_obj_set_style_text_color(cal_screen,palette.text,0);
    cal_label=label(cal_screen,""); lv_obj_set_width(cal_label,screen_w()-40);
    lv_obj_set_style_text_align(cal_label,LV_TEXT_ALIGN_CENTER,0);
    cross=lv_obj_create(cal_screen); lv_obj_remove_style_all(cross); lv_obj_set_size(cross,21,21);
    for (unsigned i=0;i<2;++i) {
        lv_obj_t *line=lv_obj_create(cross); lv_obj_remove_style_all(line);
        lv_obj_set_size(line,i?1:21,i?21:1); lv_obj_set_pos(line,i?10:0,i?0:10);
        lv_obj_set_style_bg_color(line,palette.accent,0); lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
    }
    position_cross();
    ESP_LOGI("div.touch","Calibration started: touch TL, TR, BR, BL, then centre; hold each target and lift");
}
static esp_err_t request_calibration(void)
{
    if (!touch || !display) return ESP_ERR_NOT_SUPPORTED;
    atomic_store(&calibration_requested,true);
    return ESP_OK;
}
static bool finish_calibration(void)
{
    div_touch_point_t center;
    if (!div_touch_map(&cal_fit,cal_sample,DIV_LCD_WIDTH,DIV_LCD_HEIGHT,&center) ||
        abs(center.x-DIV_LCD_WIDTH/2)>12 || abs(center.y-DIV_LCD_HEIGHT/2)>12) return false;
    div_settings_t next=settings;
    next.touch_swap=cal_fit.swap; next.touch_invert_x=cal_fit.invert_x; next.touch_invert_y=cal_fit.invert_y;
    next.touch_x0=cal_fit.x0; next.touch_x1=cal_fit.x1; next.touch_y0=cal_fit.y0; next.touch_y1=cal_fit.y1;
    next.touch_calibrated=1;
    if (div_settings_save(&next)!=ESP_OK) return false;
    settings=next;
    ESP_LOGI("div.touch","Calibration saved: x=%u..%u y=%u..%u swap=%u invert=%u/%u centre=%d,%d",
        next.touch_x0,next.touch_x1,next.touch_y0,next.touch_y1,next.touch_swap,next.touch_invert_x,next.touch_invert_y,center.x,center.y);
    return true;
}
static void update(lv_timer_t *timer)
{
    (void)timer;
    if (atomic_exchange(&back_requested,false)) back_event(NULL);
    if (atomic_exchange(&calibration_requested,false)) begin_calibration();
    if (calibrating) {
        if (cal_pending) {
            ESP_LOGI("div.touch","Target %u: raw=%d,%d (%u samples)",corner+1,cal_sample.x,cal_sample.y,cal_count);
            bool ok=true;
            if (corner<4) {
                cal_samples[corner++]=cal_sample;
                if (corner==4) ok=div_touch_fit(cal_samples,DIV_LCD_WIDTH,DIV_LCD_HEIGHT,CONFIG_DIV_ROTATION,&cal_fit);
            } else {
                ok=finish_calibration();
                if (ok) { calibrating=false; show_home(); }
            }
            if (calibrating) {
                if (!ok) { corner=0; ESP_LOGW("div.touch","Calibration check failed; restarting targets"); }
                position_cross();
            }
        }
        if (calibrating && esp_timer_get_time()>cal_deadline) { calibrating=false; div_report("Calibration timeout; hold AUX for 2 seconds to retry"); show_home(); }
    }
    if (!calibrating && cal_screen) { lv_obj_del(cal_screen); cal_screen=NULL; cross=NULL; }
    div_line_t lines[8];
    size_t n=div_report_read(&after,lines,8);
    for (size_t i=0;i<n;++i) {
        size_t used=strlen(log_text), extra=strlen(lines[i].text)+1;
        if (used+extra>=sizeof(log_text)) {
            size_t remove=sizeof(log_text)/2;
            while (remove<used && log_text[remove]!='\n') ++remove;
            if (remove<used) ++remove;
            memmove(log_text,log_text+remove,used-remove+1);
        }
        strlcat(log_text,lines[i].text,sizeof(log_text)); strlcat(log_text,"\n",sizeof(log_text));
    }
    if (n && output) lv_label_set_text(output,log_text);
    if (!calibrating) {
        div_settings_t saved;
        if (div_settings_load(&saved)==ESP_OK && (saved.dark!=settings.dark || saved.accent!=settings.accent)) {
            settings=saved;
            lv_theme_default_init(display,accent_color(saved.accent),lv_palette_main(LV_PALETTE_CYAN),saved.dark,&div_font_16);
            lv_obj_del(status); status=div_menu_status_create(lv_scr_act(),settings.dark,settings.accent);
            if (!selected) show_category(category);
        }
        settings=saved;
    }
    int millivolts, percent=-1;
    if (div_board_battery_mv(&millivolts)==ESP_OK) percent=clamp((millivolts-3300)*100/900,100);
    div_menu_status_update(status,div_storage_ready(),percent,div_service_busy());
}
esp_err_t div_ui_init(void)
{
    div_settings_load(&settings);
    esp_lcd_panel_io_handle_t io=NULL;
    esp_lcd_panel_handle_t panel=NULL;
    esp_lcd_panel_io_spi_config_t io_config={.dc_gpio_num=DIV_LCD_DC, .cs_gpio_num=DIV_LCD_CS,
        .pclk_hz=26000000, .lcd_cmd_bits=8, .lcd_param_bits=8, .spi_mode=0, .trans_queue_depth=4};
    esp_err_t err=esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DIV_LCD_HOST,&io_config,&io);
    if (err!=ESP_OK) return err;
    esp_lcd_panel_dev_config_t panel_config={.reset_gpio_num=-1, .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR, .bits_per_pixel=16};
#ifdef CONFIG_DIV_PANEL_ST7796
    err=esp_lcd_new_panel_st7796(io,&panel_config,&panel);
#else
    err=esp_lcd_new_panel_ili9341(io,&panel_config,&panel);
#endif
    if (err==ESP_OK) err=esp_lcd_panel_reset(panel);
    if (err==ESP_OK) err=esp_lcd_panel_init(panel);
    if (err==ESP_OK) err=esp_lcd_panel_disp_on_off(panel,true);
    if (err!=ESP_OK) { if(panel) esp_lcd_panel_del(panel); esp_lcd_panel_io_del(io); return err; }
    lvgl_port_cfg_t cfg=ESP_LVGL_PORT_INIT_CONFIG();
    cfg.task_max_sleep_ms=20;
    cfg.task_stack=8192;
    err=lvgl_port_init(&cfg);
    if (err!=ESP_OK) { esp_lcd_panel_del(panel); esp_lcd_panel_io_del(io); return err; }
    lvgl_port_display_cfg_t disp={.io_handle=io,.panel_handle=panel,.buffer_size=DIV_LCD_WIDTH*24,
        .double_buffer=false,.hres=DIV_LCD_WIDTH,.vres=DIV_LCD_HEIGHT,
        .rotation.mirror_x=DIV_LCD_NATIVE_MIRROR_X,.flags.buff_dma=true};
    display=lvgl_port_add_disp(&disp);
    if (!display) return ESP_ERR_NO_MEM;
    spi_device_interface_config_t touch_config={.clock_speed_hz=1000000,.mode=0,.spics_io_num=DIV_TOUCH_CS,.queue_size=1};
    err=spi_bus_add_device(DIV_LCD_HOST,&touch_config,&touch);
    if (err!=ESP_OK) div_report("Touch SPI: %s",esp_err_to_name(err));
    lvgl_port_lock(0);
    lv_disp_set_rotation(display,(lv_disp_rot_t)CONFIG_DIV_ROTATION);
    lv_theme_default_init(display,accent_color(settings.accent),lv_palette_main(LV_PALETTE_CYAN),settings.dark,&div_font_16);
    group=lv_group_create();
    lv_group_set_default(group);
    static lv_indev_drv_t pointer, keys;
    lv_indev_drv_init(&pointer); pointer.type=LV_INDEV_TYPE_POINTER; pointer.read_cb=touch_read; pointer.disp=display;
    if (touch) lv_indev_drv_register(&pointer);
    lv_indev_drv_init(&keys); keys.type=LV_INDEV_TYPE_KEYPAD; keys.read_cb=key_read; keys.disp=display;
    lv_indev_t *keypad=lv_indev_drv_register(&keys); lv_indev_set_group(keypad,group);
    div_palette_t palette=div_menu_palette(settings.dark,settings.accent);
    lv_obj_set_style_bg_color(lv_scr_act(),palette.bg,0);
    lv_obj_set_style_text_font(lv_scr_act(),&div_font_16,0);
    title=label(lv_scr_act(),DIV_BOARD_NAME); lv_obj_set_pos(title,8,25);
    lv_obj_set_style_text_color(title,palette.accent,0);
    body=lv_obj_create(lv_scr_act());
    controls=lv_obj_create(lv_scr_act()); lv_obj_remove_style_all(controls);
    lv_obj_set_pos(controls,0,screen_h()-36); lv_obj_set_size(controls,screen_w(),36);
    status=div_menu_status_create(lv_scr_act(),settings.dark,settings.accent);
    show_home(); lv_timer_create(update,150,NULL);
    lv_timer_create(update_packet,60,NULL);
    if (touch && !settings.touch_calibrated) atomic_store(&calibration_requested,true);
    div_service_set_calibrator(request_calibration);
    ESP_LOGI("div.ui","%s: %dx%d, rotation %u, native mirror X=%d; original menu assets",
        DIV_BOARD_NAME,screen_w(),screen_h(),CONFIG_DIV_ROTATION,DIV_LCD_NATIVE_MIRROR_X);
    lvgl_port_unlock();
    div_board_led(0,settings.led_enabled ? 8 : 0,settings.led_enabled ? 16 : 0);
    return div_board_brightness(settings.brightness);
}
