#include "div_ui.h"
#include "div_board.h"
#include "div_storage.h"
#include "div_service.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

static spi_device_handle_t touch;
static lv_disp_t *display;
static lv_obj_t *body, *title, *status, *keyboard, *output, *form;
static lv_group_t *group;
static lv_obj_t *inputs[12];
static const div_feature_t *selected;
static int category = -1;
static div_settings_t settings;
static uint32_t after;
static char log_text[4096];
static atomic_bool calibration_requested;
static bool calibrating, was_pressed;
static unsigned corner;
static uint16_t sample_x[4], sample_y[4], raw_x, raw_y;
static lv_obj_t *cross, *cal_label;
static int64_t cal_deadline, pressed_since;
static int screen_w(void) { return lv_disp_get_hor_res(display); }
static int screen_h(void) { return lv_disp_get_ver_res(display); }
static lv_color_t accent_color(uint8_t preset)
{
    static const uint32_t colors[]={0xff8800,0x48c050,0xe74444,0x00c8dd,0xa060df,0xf4ca40,0xffffff};
    return lv_color_hex(colors[preset<7 ? preset : 0]);
}

static bool adc_read(uint8_t command, uint16_t *value)
{
    spi_transaction_t transaction = {.length=24, .flags=SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .tx_data={command,0,0}};
    if (spi_device_transmit(touch, &transaction) != ESP_OK) return false;
    *value = ((transaction.rx_data[1] << 8 | transaction.rx_data[2]) >> 3) & 4095;
    return true;
}
static bool touch_raw(uint16_t *x, uint16_t *y)
{
    uint16_t z1, z2, xs[3], ys[3];
    if (!touch || !adc_read(0xb0, &z1) || !adc_read(0xc0, &z2)) return false;
    if ((!z1 && !z2) || (z1 == 4095 && z2 == 4095) || z1 + 4095 - z2 < 350) return false;
    for (unsigned i = 0; i < 3; ++i) if (!adc_read(0xd0, &xs[i]) || !adc_read(0x90, &ys[i])) return false;
    /* Median rejects isolated noisy samples on the shared display bus. */
    for (unsigned i=0; i<3; ++i) for (unsigned j=i+1; j<3; ++j) {
        if (xs[j]<xs[i]) { uint16_t v=xs[i]; xs[i]=xs[j]; xs[j]=v; }
        if (ys[j]<ys[i]) { uint16_t v=ys[i]; ys[i]=ys[j]; ys[j]=v; }
    }
    *x=xs[1]; *y=ys[1];
    return true;
}
static int clamp(int value, int max) { return value < 0 ? 0 : value > max ? max : value; }
static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    uint16_t x, y;
    bool pressed = touch_raw(&x, &y);
    if (pressed) { raw_x=x; raw_y=y; }
    if (calibrating) {
        if (pressed && !was_pressed) pressed_since=esp_timer_get_time();
        data->state=LV_INDEV_STATE_REL;
        was_pressed=pressed;
        return;
    }
    data->state=pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (!pressed) return;
    if (settings.touch_swap) { uint16_t v=x; x=y; y=v; }
    /* LVGL rotates pointer coordinates itself. Supply native panel coordinates. */
    int px=((int)x-settings.touch_x0)*(DIV_LCD_WIDTH-1)/(settings.touch_x1-settings.touch_x0);
    int py=((int)y-settings.touch_y0)*(DIV_LCD_HEIGHT-1)/(settings.touch_y1-settings.touch_y0);
    if (settings.touch_invert_x) px=DIV_LCD_WIDTH-1-px;
    if (settings.touch_invert_y) py=DIV_LCD_HEIGHT-1-py;
    data->point.x=clamp(px, DIV_LCD_WIDTH-1); data->point.y=clamp(py, DIV_LCD_HEIGHT-1);
}
static void key_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static uint8_t previous, stable;
    static int64_t changed;
    uint8_t pressed=0;
    div_board_buttons(&pressed);
    int64_t now=esp_timer_get_time();
    if (pressed != previous) { previous=pressed; changed=now; }
    if (now-changed >= 25000) stable=pressed;
    data->state=stable ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (stable & (1<<DIV_BUTTON_UP)) data->key=LV_KEY_PREV;
    else if (stable & (1<<DIV_BUTTON_DOWN)) data->key=LV_KEY_NEXT;
    else if (stable & (1<<DIV_BUTTON_LEFT)) data->key=LV_KEY_LEFT;
    else if (stable & (1<<DIV_BUTTON_RIGHT)) data->key=LV_KEY_RIGHT;
    else if (stable & (1<<DIV_BUTTON_OK)) data->key=LV_KEY_ENTER;
    else if (stable & (1<<DIV_BUTTON_AUX)) { data->key=LV_KEY_ESC; div_service_stop(); }
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
    lv_obj_set_size(object,LV_PCT(100),40);
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
    lv_group_remove_all_objs(group);
    if (keyboard) { lv_obj_del(keyboard); keyboard=NULL; }
    lv_obj_clean(body);
    output=NULL; form=NULL; memset(inputs,0,sizeof(inputs));
}
static void show_home(void);
static void show_category(int value);
static void back_event(lv_event_t *event)
{
    (void)event;
    if (calibrating) { calibrating=false; div_report("Calibration cancelled"); }
    div_service_stop();
    if (selected) { selected=NULL; show_category(category); }
    else show_home();
}
static void keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event)==LV_EVENT_READY || lv_event_get_code(event)==LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keyboard,NULL);
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(body,screen_h()-76);
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
    cJSON_free(json); cJSON_Delete(args);
    if (err!=ESP_OK) div_report("Cannot start: %s",esp_err_to_name(err));
    else {
        log_text[0]=0;
        if (output) lv_label_set_text(output,"Starting...");
        if (form) lv_obj_add_flag(form,LV_OBJ_FLAG_HIDDEN);
        if (keyboard) { lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN); lv_obj_set_height(body,screen_h()-76); }
    }
}
static void form_event(lv_event_t *event)
{ (void)event; if (form) { lv_obj_clear_flag(form,LV_OBJ_FLAG_HIDDEN); lv_obj_scroll_to_view(form,LV_ANIM_ON); } }
static void feature_event(lv_event_t *event)
{
    selected=lv_event_get_user_data(event);
    clear_body();
    lv_label_set_text(title,selected->name);
    label(body,selected->description);
    form=lv_obj_create(body);
    lv_obj_set_width(form,LV_PCT(100)); lv_obj_set_height(form,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(form,LV_FLEX_FLOW_COLUMN);
    div_settings_t saved; div_settings_load(&saved);
    for (unsigned i=0; selected->fields[i].key && i<12; ++i) {
        const div_field_t *field=&selected->fields[i];
        label(form,field->label);
        inputs[i]=lv_textarea_create(form);
        lv_obj_set_width(inputs[i],LV_PCT(100));
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
    button(body,"Run",run_event,NULL);
    button(body,"Stop",stop_event,NULL);
    button(body,"Parameters",form_event,NULL);
    button(body,"Back",back_event,NULL);
    output=label(body,"");
    lv_label_set_long_mode(output,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(output,&lv_font_montserrat_14,0);
}
static void show_category(int value)
{
    clear_body(); category=value; selected=NULL;
    lv_label_set_text(title,div_category_name(value));
    size_t n; const div_feature_t *all=div_features(&n);
    for (size_t i=0; i<n; ++i) {
        bool in_category=all[i].category==(div_category_t)value;
        if (value==DIV_BLE && !strcmp(all[i].id,"ble_jam")) in_category=true;
        if (value==DIV_NRF && !strcmp(all[i].id,"ble_jam")) in_category=false;
        if (in_category) button(body,all[i].name,feature_event,(void *)&all[i]);
    }
    button(body,"Back",back_event,NULL);
}
static void category_event(lv_event_t *event) { show_category((intptr_t)lv_event_get_user_data(event)); }
static void show_home(void)
{
    clear_body(); category=-1; selected=NULL;
    lv_label_set_text(title,DIV_BOARD_NAME);
    for (int i=0;i<8;++i) button(body,div_category_name(i),category_event,(void *)(intptr_t)i);
}
static void position_cross(void)
{
    const bool right=corner==1 || corner==2, bottom=corner>=2;
    lv_obj_set_pos(cross,right ? screen_w()-34 : 14,bottom ? screen_h()-34 : 14);
    lv_label_set_text_fmt(cal_label,"Touch and release target %u / 4",corner+1);
}
static void begin_calibration(void)
{
    clear_body(); calibrating=true; corner=0; was_pressed=false; pressed_since=0;
    cal_deadline=esp_timer_get_time()+60000000;
    cal_label=label(body,"Touch calibration");
    button(body,"Cancel",back_event,NULL);
    cross=lv_label_create(lv_scr_act()); lv_label_set_text(cross,"+");
    lv_obj_set_style_text_color(cross,lv_palette_main(LV_PALETTE_RED),0);
    position_cross();
}
static esp_err_t request_calibration(void)
{
    if (!touch || !display) return ESP_ERR_NOT_SUPPORTED;
    atomic_store(&calibration_requested,true);
    return ESP_OK;
}
static bool finish_calibration(void)
{
    /* Reorder screen corners into native-panel TL, TR, BR, BL before fitting. */
    uint16_t native_x[4],native_y[4];
    for (unsigned i=0;i<4;++i) {
        unsigned original=(i+CONFIG_DIV_ROTATION)%4;
        native_x[i]=sample_x[original]; native_y[i]=sample_y[original];
    }
    memcpy(sample_x,native_x,sizeof(sample_x)); memcpy(sample_y,native_y,sizeof(sample_y));
    int dx=abs((int)sample_x[1]-sample_x[0]), dy=abs((int)sample_y[1]-sample_y[0]);
    div_settings_t next=settings;
    next.touch_swap=dy>dx;
    uint16_t *x=next.touch_swap ? sample_y : sample_x;
    uint16_t *y=next.touch_swap ? sample_x : sample_y;
    int left=(x[0]+x[3])/2, right=(x[1]+x[2])/2;
    int top=(y[0]+y[1])/2, bottom=(y[2]+y[3])/2;
    if (abs(right-left)<1000 || abs(bottom-top)<1000) return false;
    next.touch_invert_x=right<left; next.touch_invert_y=bottom<top;
    int lo_x=left<right ? left : right, hi_x=left>right ? left : right;
    int lo_y=top<bottom ? top : bottom, hi_y=top>bottom ? top : bottom;
    int margin_x=(hi_x-lo_x)*24/(DIV_LCD_WIDTH-49), margin_y=(hi_y-lo_y)*24/(DIV_LCD_HEIGHT-49);
    next.touch_x0=clamp(lo_x-margin_x,4095); next.touch_x1=clamp(hi_x+margin_x,4095);
    next.touch_y0=clamp(lo_y-margin_y,4095); next.touch_y1=clamp(hi_y+margin_y,4095);
    if (div_settings_save(&next)!=ESP_OK) return false;
    settings=next;
    return true;
}
static void update(lv_timer_t *timer)
{
    (void)timer;
    if (atomic_exchange(&calibration_requested,false)) begin_calibration();
    if (calibrating) {
        static bool had_touch;
        if (was_pressed && esp_timer_get_time()-pressed_since>60000) { sample_x[corner]=raw_x; sample_y[corner]=raw_y; had_touch=true; }
        if (!was_pressed && had_touch) {
            had_touch=false;
            if (++corner==4) {
                div_report("Touch calibration %s",finish_calibration() ? "saved" : "failed: retry with accurate targets");
                calibrating=false; show_home();
            } else position_cross();
        }
        if (esp_timer_get_time()>cal_deadline) { calibrating=false; had_touch=false; div_report("Calibration timeout"); show_home(); }
    }
    if (!calibrating && cross) { lv_obj_del(cross); cross=NULL; }
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
            lv_theme_default_init(display,accent_color(saved.accent),lv_palette_main(LV_PALETTE_CYAN),saved.dark,LV_FONT_DEFAULT);
        }
        settings=saved;
    }
    lv_label_set_text_fmt(status,"%s  SD:%s  %lu KB",div_service_busy() ? "Running" : "Ready",
        div_storage_ready() ? "OK" : "--",(unsigned long)esp_get_free_heap_size()/1024);
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
    err=lvgl_port_init(&cfg);
    if (err!=ESP_OK) { esp_lcd_panel_del(panel); esp_lcd_panel_io_del(io); return err; }
    lvgl_port_display_cfg_t disp={.io_handle=io,.panel_handle=panel,.buffer_size=DIV_LCD_WIDTH*24,
        .double_buffer=false,.hres=DIV_LCD_WIDTH,.vres=DIV_LCD_HEIGHT,.flags.buff_dma=true};
    display=lvgl_port_add_disp(&disp);
    if (!display) return ESP_ERR_NO_MEM;
    spi_device_interface_config_t touch_config={.clock_speed_hz=2000000,.mode=0,.spics_io_num=DIV_TOUCH_CS,.queue_size=1};
    err=spi_bus_add_device(DIV_LCD_HOST,&touch_config,&touch);
    if (err!=ESP_OK) div_report("Touch SPI: %s",esp_err_to_name(err));
    lvgl_port_lock(0);
    lv_disp_set_rotation(display,(lv_disp_rot_t)CONFIG_DIV_ROTATION);
    lv_theme_default_init(display,accent_color(settings.accent),lv_palette_main(LV_PALETTE_CYAN),settings.dark,LV_FONT_DEFAULT);
    group=lv_group_create();
    lv_group_set_default(group);
    static lv_indev_drv_t pointer, keys;
    lv_indev_drv_init(&pointer); pointer.type=LV_INDEV_TYPE_POINTER; pointer.read_cb=touch_read; pointer.disp=display;
    if (touch) lv_indev_drv_register(&pointer);
    lv_indev_drv_init(&keys); keys.type=LV_INDEV_TYPE_KEYPAD; keys.read_cb=key_read; keys.disp=display;
    lv_indev_t *keypad=lv_indev_drv_register(&keys); lv_indev_set_group(keypad,group);
    title=label(lv_scr_act(),DIV_BOARD_NAME); lv_obj_set_pos(title,8,8);
    body=lv_obj_create(lv_scr_act()); lv_obj_set_size(body,screen_w(),screen_h()-76); lv_obj_set_pos(body,0,36);
    lv_obj_set_style_pad_all(body,8,0); lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    status=label(lv_scr_act(),"Starting"); lv_obj_set_pos(status,4,screen_h()-28);
    show_home(); lv_timer_create(update,150,NULL);
    div_service_set_calibrator(request_calibration);
    lvgl_port_unlock();
    div_board_led(0,settings.led_enabled ? 8 : 0,settings.led_enabled ? 16 : 0);
    return div_board_brightness(settings.brightness);
}
