#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DIV_SPECTRUM_SAMPLES 256
#define DIV_SPECTRUM_BINS 128
#define DIV_SPECTRUM_SAMPLE_US 200

typedef struct {
    uint32_t sequence, packets, disconnects, dropped;
    uint16_t counts[DIV_SPECTRUM_SAMPLES];
    uint8_t channel;
    int8_t rssi;
    bool active;
} div_spectrum_frame_t;
typedef struct {
    uint64_t start_us;
    uint32_t total, disconnects, count;
    int32_t rssi_sum;
    uint16_t counts[DIV_SPECTRUM_SAMPLES];
    div_spectrum_frame_t frame;
    bool active;
} div_spectrum_capture_t;

void div_spectrum_begin(div_spectrum_capture_t *capture, uint64_t now, unsigned channel);
void div_spectrum_advance(div_spectrum_capture_t *capture, uint64_t now);
void div_spectrum_receive(div_spectrum_capture_t *capture, uint64_t now, int rssi, bool disconnect);
/* Same source as the original monitor: FFT of packet-count changes over time.
   This is not an RF/SDR frequency spectrum. */
void div_spectrum_fft(const float samples[DIV_SPECTRUM_SAMPLES], float magnitude[DIV_SPECTRUM_BINS]);
void div_spectrum_levels(const div_spectrum_frame_t *frame, uint8_t levels[DIV_SPECTRUM_BINS]);
uint16_t div_spectrum_rgb565(unsigned level);
