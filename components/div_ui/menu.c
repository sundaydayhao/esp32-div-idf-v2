#include "div_menu.h"
#include <string.h>
#include <stdio.h>

typedef struct { const char *label, *action; const lv_img_dsc_t *icon; } item_t;
#define ITEM(label, action, icon) {label, action, &div_icon_##icon}
static const item_t home[] = {
    ITEM("WiFi", "@wifi", wifi), ITEM("2.4GHz", "@nrf", jammer),
    ITEM("More", "@more", down_dots), ITEM("Settings", "system_settings", setting),
    ITEM("Bluetooth", "@ble", spoofer), ITEM("SubGHz", "@sub", analyzer),
    ITEM("Tools", "@tools", stat), ITEM("About", "system_about", question)
};
static const item_t wifi[] = {
    ITEM("Packet Monitor", "wifi_monitor", wifi), ITEM("Beacon Spammer", "wifi_beacon", antenna),
    ITEM("WiFi Deauther", "wifi_deauth", wifi_jammer), ITEM("Probe Request Flood", "wifi_probe", Skull_3),
    ITEM("Deauth Detector", "wifi_detect", eye2), ITEM("WiFi Scanner", "wifi_scan", jammer),
    ITEM("Captive Portal", "wifi_portal", bash), ITEM("Hidden SSID Revealer", "wifi_hidden", eye_blind),
    ITEM("WPS Scanner", "wifi_wps", key), ITEM("ARP Scanner", "wifi_arp", list),
    ITEM("Karma Attack", "wifi_karma", devil)
};
static const item_t ble[] = {
    ITEM("BLE Jammer", "ble_jam", ble_jammer), ITEM("BLE Spoofer", "ble_spoof", spoofer),
    ITEM("Sour Apple", "ble_apple", apple), ITEM("AirTag Spoofer", "ble_airtag_tx", tags),
    ITEM("AirTag Sniffer", "ble_airtag_rx", magnifying_glass), ITEM("Sniffer", "ble_sniff", analyzer),
    ITEM("BLE Scanner", "ble_scan", graph), ITEM("BLE Rubber Ducky", "ble_ducky", rubber_ducky),
    ITEM("Skimmer Detect", "ble_skimmer", Wireless_4)
};
static const item_t nrf[] = {
    ITEM("Scanner", "nrf_scan", scanner), ITEM("Proto Kill", "nrf_protokill", kill),
    ITEM("ESB Sniffer", "nrf_sniff", analyzer), ITEM("ESB Replay", "nrf_replay", follow),
    ITEM("MouseJack Scan", "nrf_mouse_scan", magnifying_glass), ITEM("MouseJack Inject", "nrf_mouse_inject", key)
};
static const item_t sub[] = {
    ITEM("Replay Attack", "sub_capture", antenna), ITEM("SubGHz Jammer", "sub_jam", no_signal),
    ITEM("De Bruijn / Brute", "sub_brute", graph_self_loop), ITEM("Jamming Detector", "sub_detect", Voice_Id),
    ITEM("Saved Profile", "sub_replay", list)
};
static const item_t tools[] = {
    ITEM("Serial Monitor", "system_serial", bash), ITEM("Update Firmware", "system_update", follow),
    ITEM("Touch Calibrate", "system_calibrate", undo), ITEM("SD File Manager", "system_files", sdcard)
};
static const item_t more[] = {
    ITEM("IR Remote", "@ir", led), ITEM("RFID/NFC", "@nfc", rfid_chip),
    ITEM("GPS", "@gps", satellite), ITEM("Main Menu", "@home", go_back)
};
static const item_t ir[] = {
    ITEM("IR Record", "ir_record", led), ITEM("IR Saved Profiles", "ir_saved", list),
    ITEM("Universal IR Controller", "ir_universal", remote_control)
};
static const item_t nfc[] = {
    ITEM("Card Reader", "nfc_read", magnifying_glass), ITEM("Card Clone", "nfc_clone", follow),
    ITEM("Erase", "nfc_erase", recycle), ITEM("Dump", "nfc_dump", dot_matrix),
    ITEM("Decode Access", "nfc_access", key), ITEM("Jam Reader", "nfc_jam", kill),
    ITEM("Tag Disrupt", "nfc_disrupt", flash), ITEM("Disrupt Emulate", "nfc_emulate", devil)
};
static const item_t gps[] = {
    ITEM("Wardriver", "gps_wardrive", satellite), ITEM("Satellite Scanner", "gps_satellites", satellite_dish)
};
static const item_t back = ITEM("Back", "@back", go_back);
static const item_t main_menu = ITEM("Main Menu", "@home", go_back);
static const item_t next_page = ITEM("Next Page", "@page", navigate_right);
static const item_t prev_page = ITEM("Prev Page", "@page", navigate_left);
static lv_obj_t *buttons[10];
static unsigned button_count;
static int active_menu;
static lv_group_t *active_group;
static div_menu_action_t activate;
static div_palette_t palette;

/* Expand the original RGB565 constants without changing their quantisation. */
static lv_color_t rgb565(uint16_t c)
{
    unsigned r=(c>>11)&31, g=(c>>5)&63, b=c&31;
    return lv_color_make((r<<3)|(r>>2), (g<<2)|(g>>4), (b<<3)|(b>>2));
}
div_palette_t div_menu_palette(bool dark, unsigned accent)
{
    static const uint16_t accents[]={0xfbe4,0xb721,0xf800,0x07ff,0xf81f,0xffe0,0xffff};
    return (div_palette_t){rgb565(dark?0x20e4:0xf7de), rgb565(dark?0x3166:0xe73c),
        rgb565(0x8410), rgb565(dark?0xffff:0), rgb565(dark?0x4208:0xc618),
        rgb565(accents[accent<7?accent:0])};
}
static lv_obj_t *icon(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y, lv_color_t color)
{
    lv_obj_t *obj=lv_img_create(parent);
    lv_img_set_src(obj,src);
    lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_img_recolor(obj,color,0);
    lv_obj_set_style_img_recolor_opa(obj,LV_OPA_COVER,0);
    return obj;
}
unsigned div_menu_focused(void)
{
    lv_obj_t *focus=lv_group_get_focused(active_group);
    for (unsigned i=0;i<button_count;++i) if (buttons[i]==focus) return i;
    return 0;
}
void div_menu_focus(unsigned index)
{
    if (index<button_count) lv_group_focus_obj(buttons[index]);
}
static void item_event(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    lv_obj_t *obj=lv_event_get_target(event);
    const item_t *item=lv_event_get_user_data(event);
    if (code==LV_EVENT_CLICKED) { if (activate) activate(item->action); return; }
    if (code==LV_EVENT_KEY) {
        uint32_t key=lv_event_get_key(event);
        if (key==LV_KEY_ESC) { if (activate) activate("@back"); return; }
        int delta=0;
        if (key==LV_KEY_UP) delta=active_menu==DIV_MENU_MORE ? -2 : -1;
        if (key==LV_KEY_DOWN) delta=active_menu==DIV_MENU_MORE ? 2 : 1;
        if (key==LV_KEY_LEFT) delta=active_menu==DIV_MENU_HOME ? -4 : -1;
        if (key==LV_KEY_RIGHT) delta=active_menu==DIV_MENU_HOME ? 4 : 1;
        if (delta && button_count) div_menu_focus((div_menu_focused()+button_count+delta)%button_count);
    }
    if (code==LV_EVENT_FOCUSED || code==LV_EVENT_DEFOCUSED) {
        bool grid=active_menu==DIV_MENU_HOME || active_menu==DIV_MENU_MORE;
        lv_color_t color=grid || code==LV_EVENT_FOCUSED ? palette.accent : palette.text;
        for (uint32_t i=0;i<lv_obj_get_child_cnt(obj);++i) {
            lv_obj_t *child=lv_obj_get_child(obj,i);
            if (lv_obj_check_type(child,&lv_img_class)) lv_obj_set_style_img_recolor(child,color,0);
        }
    }
}
static lv_obj_t *item_button(lv_obj_t *parent, const item_t *item, int x, int y, int w, int h, bool tile, bool footer, bool right)
{
    lv_obj_t *obj=lv_btn_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_text_font(obj,&div_font_16,0);
    lv_obj_set_style_text_color(obj,palette.text,0);
    lv_obj_set_style_text_color(obj,palette.accent,LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(obj,palette.accent,LV_STATE_PRESSED);
    if (tile) {
        lv_obj_set_style_bg_color(obj,palette.fg,0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
        lv_obj_set_style_radius(obj,5,0);
        lv_obj_set_style_border_width(obj,1,0);
        lv_obj_set_style_border_color(obj,palette.line,0);
        lv_obj_set_style_border_color(obj,palette.accent,LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(obj,palette.accent,LV_STATE_PRESSED);
    }
    lv_obj_t *text=lv_label_create(obj);
    lv_label_set_text_fmt(text,"%s%s",(!tile && !footer)?"| ":"",item->label);
    if (tile) {
        lv_obj_align(text,LV_ALIGN_TOP_MID,0,h/2-1);
        if (item==&home[2]) {
            icon(obj,&div_icon_led,(w-56)/2-1,9,palette.accent);
            icon(obj,&div_icon_satellite,(w-56)/2+19,9,palette.accent);
            icon(obj,&div_icon_down_dots,(w-56)/2+39,9,palette.accent);
        } else icon(obj,item->icon,(w-16)/2-1,9,palette.accent);
    } else {
        lv_obj_set_pos(text,30,footer?6:0);
        if (right) lv_obj_align(text,LV_ALIGN_TOP_RIGHT,-30,6);
        icon(obj,item->icon,right?w-26:10,footer?6:0,palette.text);
    }
    lv_obj_add_event_cb(obj,item_event,LV_EVENT_ALL,(void *)item);
    lv_group_add_obj(active_group,obj);
    buttons[button_count++]=obj;
    return obj;
}
void div_menu_show(lv_obj_t *parent, lv_group_t *group, int menu, unsigned page,
                   bool dark, unsigned accent, div_menu_action_t action)
{
    active_group=group; active_menu=menu; activate=action; button_count=0;
    palette=div_menu_palette(dark,accent);
    lv_obj_update_layout(parent);
    int x=lv_obj_get_x(parent), y=lv_obj_get_y(parent);
    int w=lv_obj_get_width(parent), h=lv_obj_get_height(parent);
    lv_obj_remove_style_all(parent);
    lv_obj_set_pos(parent,x,y); lv_obj_set_size(parent,w,h);
    lv_obj_set_style_bg_color(parent,palette.bg,0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    if (menu==DIV_MENU_HOME || menu==DIV_MENU_MORE) {
        const item_t *items=menu==DIV_MENU_HOME ? home : more;
        unsigned count=menu==DIV_MENU_HOME ? 8 : 4;
        for (unsigned i=0;i<count;++i) {
            int col=menu==DIV_MENU_HOME?i/4:i%2, row=menu==DIV_MENU_HOME?i%4:i/2;
            item_button(parent,&items[i],10+col*w/2,10+row*h/4,w/2-20,h/4-15,true,false,false);
        }
    } else {
        const item_t *items=NULL; unsigned count=0;
#define MENU(id, array) case id: items=array; count=sizeof(array)/sizeof(array[0]); break
        switch (menu) {
            MENU(0,wifi); MENU(1,ble); MENU(2,nrf); MENU(3,sub);
            MENU(4,ir); MENU(5,nfc); MENU(6,gps); MENU(7,tools);
            default: break;
        }
#undef MENU
        bool paged=menu==0 || menu==1;
        unsigned first=paged && page ? 8 : 0;
        unsigned end=paged && !page ? 8 : count;
        for (unsigned i=first;i<end;++i)
            item_button(parent,&items[i],0,10+(i-first)*(h-30)/9,w,28,false,false,false);
        if (paged) {
            item_button(parent,&main_menu,0,h-30,w/2,28,false,true,false);
            item_button(parent,page?&prev_page:&next_page,w/2,h-30,w-w/2,28,false,true,true);
        } else item_button(parent,&back,0,h-30,w,28,false,true,false);
    }
    div_menu_focus(0);
    if (button_count) lv_event_send(buttons[0],LV_EVENT_FOCUSED,NULL);
}
lv_obj_t *div_menu_status_create(lv_obj_t *parent, bool dark, unsigned accent)
{
    div_palette_t p=div_menu_palette(dark,accent);
    lv_obj_t *bar=lv_obj_create(parent);
    lv_obj_remove_style_all(bar); lv_obj_clear_flag(bar,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar,LV_PCT(100),20);
    lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0); lv_obj_set_style_bg_color(bar,p.status,0);
    lv_obj_set_style_border_width(bar,1,0); lv_obj_set_style_border_side(bar,LV_BORDER_SIDE_BOTTOM,0);
    lv_obj_set_style_border_color(bar,lv_color_white(),0);
    lv_obj_t *battery=lv_obj_create(bar);
    lv_obj_remove_style_all(battery); lv_obj_set_pos(battery,7,4); lv_obj_set_size(battery,22,10);
    lv_obj_set_style_border_width(battery,1,0); lv_obj_set_style_border_color(battery,p.text,0);
    lv_obj_t *tip=lv_obj_create(bar);
    lv_obj_remove_style_all(tip); lv_obj_set_pos(tip,29,7); lv_obj_set_size(tip,2,4);
    lv_obj_set_style_bg_color(tip,p.text,0); lv_obj_set_style_bg_opa(tip,LV_OPA_COVER,0);
    lv_obj_t *percent=lv_label_create(bar); lv_label_set_text(percent,"--%");
    lv_obj_set_pos(percent,37,6); lv_obj_set_style_text_font(percent,&div_font_8,0);
    lv_obj_set_style_text_color(percent,p.text,0);
    lv_obj_t *run=lv_label_create(bar); lv_label_set_text(run,"");
    lv_obj_set_pos(run,70,6); lv_obj_set_style_text_font(run,&div_font_8,0);
    lv_obj_set_style_text_color(run,p.accent,0);
    lv_obj_t *ble_icon=icon(bar,&div_icon_spoofer,0,2,p.line);
    lv_obj_align(ble_icon,LV_ALIGN_TOP_RIGHT,-69,2);
    for (int i=0;i<4;++i) {
        lv_obj_t *signal=lv_obj_create(bar); lv_obj_remove_style_all(signal);
        lv_obj_set_size(signal,4,3*(i+1)); lv_obj_align(signal,LV_ALIGN_TOP_RIGHT,-59+6*i,15-3*(i+1));
        lv_obj_set_style_bg_opa(signal,LV_OPA_COVER,0); lv_obj_set_style_bg_color(signal,p.line,0);
    }
    lv_obj_t *temp=icon(bar,&div_icon_temp,0,2,p.line); lv_obj_align(temp,LV_ALIGN_TOP_RIGHT,-23,2);
    lv_obj_t *sd=icon(bar,&div_icon_nullsdcard,0,2,lv_color_hex(0xff0000));
    lv_obj_align(sd,LV_ALIGN_TOP_RIGHT,-4,2);
    return bar;
}
void div_menu_status_update(lv_obj_t *bar, bool sd, int battery_percent, bool busy)
{
    lv_obj_t *percent=lv_obj_get_child(bar,2), *run=lv_obj_get_child(bar,3), *sd_icon=lv_obj_get_child(bar,-1);
    if (battery_percent<0) lv_label_set_text(percent,"--%");
    else lv_label_set_text_fmt(percent,"%d%%",battery_percent);
    lv_label_set_text(run,busy?"RUN":"");
    lv_img_set_src(sd_icon,sd?&div_icon_sdcard:&div_icon_nullsdcard);
    lv_obj_set_style_img_recolor(sd_icon,lv_color_hex(sd?0x00ff00:0xff0000),0);
}
