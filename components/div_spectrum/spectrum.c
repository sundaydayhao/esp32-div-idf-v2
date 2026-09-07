#include "div_spectrum.h"
#include <string.h>
#include <math.h>
#include <limits.h>

void div_spectrum_begin(div_spectrum_capture_t *s, uint64_t now, unsigned channel)
{
    uint32_t sequence=s->frame.sequence+1;
    memset(s,0,sizeof(*s));
    s->start_us=now; s->active=true;
    s->frame.channel=channel; s->frame.active=true; s->frame.sequence=sequence;
    s->frame.rssi=-127;
}
void div_spectrum_advance(div_spectrum_capture_t *s, uint64_t now)
{
    const unsigned period=DIV_SPECTRUM_SAMPLES*DIV_SPECTRUM_SAMPLE_US;
    if (!s->active || now<s->start_us+period) return;
    /* Publish one complete window. A UI refresh may be slower than the RX
       windows; retain the most recent complete window, including real silence. */
    uint64_t windows=(now-s->start_us)/period;
    if (windows==1) memcpy(s->frame.counts,s->counts,sizeof(s->counts));
    else memset(s->frame.counts,0,sizeof(s->frame.counts));
    s->frame.packets=s->total; s->frame.disconnects=s->disconnects;
    s->frame.rssi=windows==1 && s->count?(s->rssi_sum/(int32_t)s->count):-127;
    s->frame.sequence+=(uint32_t)windows;
    s->start_us+=windows*period;
    memset(s->counts,0,sizeof(s->counts)); s->count=0; s->rssi_sum=0;
}
void div_spectrum_receive(div_spectrum_capture_t *s, uint64_t now, int rssi, bool disconnect)
{
    if (!s->active || now<s->start_us) return;
    div_spectrum_advance(s,now);
    unsigned slot=(now-s->start_us)/DIV_SPECTRUM_SAMPLE_US;
    if (slot>=DIV_SPECTRUM_SAMPLES) return;
    if (s->counts[slot]<UINT16_MAX) ++s->counts[slot];
    ++s->total; ++s->count; s->rssi_sum+=rssi;
    if (disconnect) ++s->disconnects;
}
void div_spectrum_fft(const float samples[DIV_SPECTRUM_SAMPLES], float magnitude[DIV_SPECTRUM_BINS])
{
    const float pi=3.14159265358979323846f;
    float real[DIV_SPECTRUM_SAMPLES], imag[DIV_SPECTRUM_SAMPLES]={0}, mean=0;
    for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;++i) mean+=samples[i];
    mean/=DIV_SPECTRUM_SAMPLES;
    for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;++i)
        real[i]=(samples[i]-mean)*(0.54f-0.46f*cosf(2*pi*i/(DIV_SPECTRUM_SAMPLES-1)));
    for (unsigned i=1,j=0;i<DIV_SPECTRUM_SAMPLES;++i) {
        unsigned bit=DIV_SPECTRUM_SAMPLES>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) { float tmp=real[i]; real[i]=real[j]; real[j]=tmp; }
    }
    for (unsigned len=2;len<=DIV_SPECTRUM_SAMPLES;len<<=1) {
        float wr=cosf(-2*pi/len), wi=sinf(-2*pi/len);
        for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;i+=len) {
            float r=1, im=0;
            for (unsigned j=0;j<len/2;++j) {
                unsigned a=i+j, b=a+len/2;
                float vr=real[b]*r-imag[b]*im, vi=real[b]*im+imag[b]*r;
                real[b]=real[a]-vr; imag[b]=imag[a]-vi;
                real[a]+=vr; imag[a]+=vi;
                float next=r*wr-im*wi; im=r*wi+im*wr; r=next;
            }
        }
    }
    for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) magnitude[i]=sqrtf(real[i]*real[i]+imag[i]*imag[i]);
}
void div_spectrum_levels(const div_spectrum_frame_t *frame, uint8_t levels[DIV_SPECTRUM_BINS])
{
    float samples[DIV_SPECTRUM_SAMPLES], magnitude[DIV_SPECTRUM_BINS];
    unsigned cumulative=0;
    for (unsigned i=0;i<DIV_SPECTRUM_SAMPLES;++i) { cumulative+=frame->counts[i]; samples[i]=cumulative*300.0f; }
    if (!cumulative) { memset(levels,0,DIV_SPECTRUM_BINS); return; }
    div_spectrum_fft(samples,magnitude);
    float peak=1270;
    for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) if (magnitude[i]>peak) peak=magnitude[i];
    for (unsigned i=0;i<DIV_SPECTRUM_BINS;++i) {
        unsigned level=(unsigned)(magnitude[i]*127/peak);
        levels[i]=level>127?127:level;
    }
}
uint16_t div_spectrum_rgb565(unsigned k)
{
    if (k>127) k=127;
    unsigned r,g,b;
    if (k<32) { r=k/2; g=0; b=k; }
    else if (k<64) { r=k/2; g=0; b=63-k; }
    else if (k<96) { r=31; g=(k-64)*2; b=0; }
    else { r=31; g=63; b=k-96; }
    return (r<<11)|(g<<5)|b;
}
