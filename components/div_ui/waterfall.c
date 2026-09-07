#include "div_waterfall.h"
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned width, height, row; uint8_t pixels[]; } waterfall_t;
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
    waterfall_t *state=lv_mem_alloc(sizeof(*state)+1024+(size_t)width*height);
    if (!state) return NULL;
    memset(state,0,sizeof(*state)+1024+(size_t)width*height);
    state->width=width; state->height=height;
    lv_obj_t *canvas=lv_canvas_create(parent);
    lv_obj_remove_style_all(canvas);
    lv_canvas_set_buffer(canvas,state->pixels,width,height,LV_IMG_CF_INDEXED_8BIT);
    lv_obj_set_size(canvas,width,height);
    lv_obj_set_user_data(canvas,state);
    lv_obj_clear_flag(canvas,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i=0;i<128;++i) lv_canvas_set_palette(canvas,i,color565(div_spectrum_rgb565(i)));
    lv_canvas_set_palette(canvas,128,color565(0x8410));
    memset(state->pixels+1024+52*width,128,width);
    lv_obj_add_event_cb(canvas,destroy,LV_EVENT_DELETE,state);
    return canvas;
}
void div_waterfall_update(lv_obj_t *canvas, const uint8_t levels[DIV_SPECTRUM_BINS])
{
    waterfall_t *s=lv_obj_get_user_data(canvas);
    uint8_t *pixels=s->pixels+1024;
    memset(pixels,0,s->width*50);
    unsigned row=53+s->row;
    for (unsigned x=0;x<s->width;++x) {
        /* Original display is mirrored around x=120, with the first 120 bins
           visible on each side. Keep this layout at other panel widths too. */
        unsigned bin=abs((int)x-(int)s->width/2)*120/(s->width/2);
        uint8_t level=levels[bin<128?bin:127];
        if (level>127) level=127;
        unsigned height=level*50/127;
        for (unsigned y=50-height;y<50;++y) pixels[y*s->width+x]=level;
        pixels[row*s->width+x]=level;
    }
    s->row=(s->row+1)%(s->height-53);
    lv_area_t area; lv_obj_get_coords(canvas,&area);
    lv_area_t graph={area.x1,area.y1,area.x2,area.y1+49};
    lv_area_t line={area.x1,area.y1+(lv_coord_t)row,area.x2,area.y1+(lv_coord_t)row};
    lv_obj_invalidate_area(canvas,&graph);
    lv_obj_invalidate_area(canvas,&line);
}
