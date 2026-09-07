#include "div_waterfall.h"
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned width, height, row, stride, colors; uint8_t pixels[]; } waterfall_t;
static lv_color_t color565(uint16_t c)
{
    unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    return lv_color_make((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2));
}
static void destroy(lv_event_t *event)
{ lv_mem_free(lv_event_get_user_data(event)); }
lv_obj_t *div_waterfall_create(lv_obj_t *parent, int width, int height)
{
    if (width<2 || height<55) return NULL;
    /* The V4 portrait buffer would require 130 KiB at 8 bpp, exceeding the
       96 KiB LVGL pool. Use a packed palette there, keeping full spatial
       resolution and leaving the ESP heap available to the Wi-Fi driver. */
    bool packed=(size_t)width*height>60000;
    unsigned colors=packed?16:256, stride=packed?(width+1)/2:width;
    size_t bytes=sizeof(waterfall_t)+colors*sizeof(lv_color32_t)+(size_t)stride*height;
    waterfall_t *state=lv_mem_alloc(bytes);
    if (!state) return NULL;
    memset(state,0,bytes);
    state->width=width; state->height=height; state->stride=stride; state->colors=colors;
    lv_obj_t *canvas=lv_canvas_create(parent);
    lv_obj_remove_style_all(canvas);
    lv_canvas_set_buffer(canvas,state->pixels,width,height,packed?LV_IMG_CF_INDEXED_4BIT:LV_IMG_CF_INDEXED_8BIT);
    lv_obj_set_size(canvas,width,height);
    lv_obj_set_user_data(canvas,state);
    lv_obj_clear_flag(canvas,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    unsigned divider=packed?15:128;
    for (unsigned i=0;i<divider;++i)
        lv_canvas_set_palette(canvas,i,color565(div_spectrum_rgb565(i*127/(divider-1))));
    lv_canvas_set_palette(canvas,divider,color565(0x8410));
    memset(state->pixels+colors*sizeof(lv_color32_t)+52*stride,packed?0xff:128,stride);
    lv_obj_add_event_cb(canvas,destroy,LV_EVENT_DELETE,state);
    return canvas;
}
static void set_pixel(waterfall_t *s, uint8_t *pixels, unsigned x, unsigned y, uint8_t color)
{
    if (s->colors==256) pixels[y*s->stride+x]=color;
    else {
        uint8_t *p=&pixels[y*s->stride+x/2];
        if (x&1) *p=(*p&0xf0)|color;
        else *p=(*p&0x0f)|(color<<4);
    }
}
void div_waterfall_update(lv_obj_t *canvas, const uint8_t levels[DIV_SPECTRUM_BINS])
{
    waterfall_t *s=lv_obj_get_user_data(canvas);
    uint8_t *pixels=s->pixels+s->colors*sizeof(lv_color32_t);
    memset(pixels,0,s->stride*50);
    unsigned row=53+s->row;
    for (unsigned x=0;x<s->width;++x) {
        /* Original display is mirrored around x=120, with the first 120 bins
           visible on each side. Keep this layout at other panel widths too. */
        unsigned bin=abs((int)x-(int)s->width/2)*120/(s->width/2);
        uint8_t level=levels[bin<128?bin:127];
        if (level>127) level=127;
        uint8_t color=s->colors==16?(level*14+63)/127:level;
        unsigned height=level*50/127;
        for (unsigned y=50-height;y<50;++y) set_pixel(s,pixels,x,y,color);
        set_pixel(s,pixels,x,row,color);
    }
    s->row=(s->row+1)%(s->height-53);
    lv_area_t area; lv_obj_get_coords(canvas,&area);
    lv_area_t graph={area.x1,area.y1,area.x2,area.y1+49};
    lv_area_t line={area.x1,area.y1+(lv_coord_t)row,area.x2,area.y1+(lv_coord_t)row};
    lv_obj_invalidate_area(canvas,&graph);
    lv_obj_invalidate_area(canvas,&line);
}
