#pragma once
#include "div_ui_assets.h"

/* Category values match the service catalogue; these two are navigation only. */
enum { DIV_MENU_HOME = -1, DIV_MENU_MORE = 8 };
typedef void (*div_menu_action_t)(const char *action);
typedef struct { lv_color_t bg, fg, line, text, status, accent; } div_palette_t;
div_palette_t div_menu_palette(bool dark, unsigned accent);
void div_menu_show(lv_obj_t *parent, lv_group_t *group, int menu, unsigned page,
                   bool dark, unsigned accent, div_menu_action_t action);
void div_menu_focus(unsigned index);
unsigned div_menu_focused(void);
lv_obj_t *div_menu_status_create(lv_obj_t *parent, bool dark, unsigned accent);
void div_menu_status_update(lv_obj_t *bar, bool sd, int battery_percent, bool busy);
