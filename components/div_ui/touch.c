#include "div_touch.h"
#include <stdlib.h>
#include <string.h>

static bool near(div_touch_point_t a, div_touch_point_t b, int distance)
{ return abs(a.x-b.x)<=distance && abs(a.y-b.y)<=distance; }
void div_touch_block(div_touch_filter_t *filter)
{
    memset(filter,0,sizeof(*filter));
    filter->blocked=true;
}
bool div_touch_filter(div_touch_filter_t *f, bool valid, div_touch_point_t raw,
                      uint32_t now, div_touch_point_t *point)
{
    *point=f->last;
    if (!valid) {
        if (!f->releasing) { f->releasing=true; f->release_since=now; }
        f->tracking=false;
        if (now-f->release_since>=60) f->down=false;
        if (now-f->release_since>=120) f->blocked=false;
        return f->down && !f->blocked;
    }
    f->releasing=false;
    if (f->blocked) return false;
    /* Two agreeing readings separated in time confirm a press or a large move.
       One noisy conversion cannot move the pointer to another button. */
    if (!f->down || !near(raw,f->last,240)) {
        if (!f->tracking || !near(raw,f->candidate,100)) {
            f->tracking=true; f->since=now; f->candidate=raw;
        }
        if (now-f->since<30) return f->down;
        f->down=true;
    }
    f->tracking=false;
    f->last=raw;
    *point=raw;
    return true;
}
static int bound(int n, int max) { return n<0?0:n>max?max:n; }
bool div_touch_map(const div_touch_calibration_t *c, div_touch_point_t raw,
                   int width, int height, div_touch_point_t *point)
{
    if (width<2 || height<2 || c->x1<=c->x0 || c->y1<=c->y0) return false;
    if (c->swap) { int tmp=raw.x; raw.x=raw.y; raw.y=tmp; }
    int x=(raw.x-c->x0)*(width-1)/(c->x1-c->x0);
    int y=(raw.y-c->y0)*(height-1)/(c->y1-c->y0);
    if (c->invert_x) x=width-1-x;
    if (c->invert_y) y=height-1-y;
    point->x=bound(x,width-1); point->y=bound(y,height-1);
    return true;
}
bool div_touch_fit(const div_touch_point_t samples[4], int width, int height,
                   unsigned rotation, div_touch_calibration_t *cal)
{
    if (width<=41 || height<=41 || rotation>3) return false;
    div_touch_point_t native[4];
    for (unsigned i=0;i<4;++i) native[i]=samples[(i+rotation)%4];
    div_touch_calibration_t c={0};
    c.swap=abs(native[1].y-native[0].y)>abs(native[1].x-native[0].x);
    if (c.swap) for (unsigned i=0;i<4;++i) { int tmp=native[i].x; native[i].x=native[i].y; native[i].y=tmp; }
    int left=(native[0].x+native[3].x)/2, right=(native[1].x+native[2].x)/2;
    int top=(native[0].y+native[1].y)/2, bottom=(native[2].y+native[3].y)/2;
    int dx=abs(right-left), dy=abs(bottom-top);
    if (dx<1000 || dy<1000) return false;
    /* Reject a wrong corner, dragging, or unstable readings instead of saving
       a plausible range with a badly skewed hit map. */
    if (abs(native[0].x-native[3].x)>dx/12 || abs(native[1].x-native[2].x)>dx/12 ||
        abs(native[0].y-native[1].y)>dy/12 || abs(native[2].y-native[3].y)>dy/12) return false;
    c.invert_x=right<left; c.invert_y=bottom<top;
    int margin_x=dx*20/(width-41), margin_y=dy*20/(height-41);
    int low_x=(left<right?left:right)-margin_x, high_x=(left>right?left:right)+margin_x;
    int low_y=(top<bottom?top:bottom)-margin_y, high_y=(top>bottom?top:bottom)+margin_y;
    if (low_x<0 || low_y<0 || high_x>4095 || high_y>4095) return false;
    c.x0=low_x; c.x1=high_x; c.y0=low_y; c.y1=high_y;
    *cal=c;
    return true;
}
