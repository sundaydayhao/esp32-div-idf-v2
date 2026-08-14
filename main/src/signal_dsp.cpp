#include "signal_dsp.h"

#include <math.h>
#include <div_string.h>

#include "esp_dsp.h"
#include "esp_log.h"
#include "shared.h"

static const char* TAG = "signal_dsp";
static bool s_fftReady = false;

static bool signalDspEnsureReady(size_t samples) {
  if (s_fftReady) {
    return true;
  }

  esp_err_t err = dsps_fft2r_init_fc32(nullptr, samples);
  if (err != ESP_OK && err != ESP_ERR_DSP_REINITIALIZED) {
    ESP_LOGW(TAG, "FFT init failed: %s", esp_err_to_name(err));
    return false;
  }
  s_fftReady = true;
  return true;
}

bool signalDspComputeMagnitude(const double* realIn, double* realOut, size_t samples) {
  static float work[WIFI_SPECTRUM_FFT_N * 2];

  if (realIn == nullptr || realOut == nullptr || samples == 0 ||
      samples > WIFI_SPECTRUM_FFT_N) {
    return false;
  }
  if (!signalDspEnsureReady(samples)) {
    return false;
  }

  const float denom = (samples > 1) ? (float)(samples - 1) : 1.0f;
  for (size_t i = 0; i < samples; ++i) {
    const float phase = 2.0f * (float)M_PI * (float)i / denom;
    const float window = 0.54f - 0.46f * cosf(phase);
    work[i * 2] = (float)realIn[i] * window;
    work[i * 2 + 1] = 0.0f;
  }

  dsps_fft2r_fc32(work, samples);
  dsps_bit_rev_fc32(work, samples);
  dsps_cplx2reC_fc32(work, samples);

  for (size_t i = 0; i < samples / 2; ++i) {
    const float re = work[i * 2];
    const float im = work[i * 2 + 1];
    realOut[i] = sqrtf(re * re + im * im);
  }
  for (size_t i = samples / 2; i < samples; ++i) {
    realOut[i] = 0.0;
  }

  return true;
}
