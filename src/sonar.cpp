/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  sonar.cpp
  Phase 2A step 2: audio device + callback skeleton. The callback
  currently writes silence; later steps fill it with per-sector stereo
  synthesis driven by FreeSpaceResult. This is the only translation
  unit that defines MINIAUDIO_IMPLEMENTATION, so miniaudio's private
  backend code (WASAPI on Windows, ALSA / PulseAudio / OpenSL on
  other platforms) is compiled exactly once.
*/


#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "sonar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

Sonar::Sonar() = default;

Sonar::~Sonar() {
  stop();
  delete device_;
  device_ = nullptr;
}

bool Sonar::start() {
  if (running_.load()) return true;
  if (!device_) device_ = new ma_device{};

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 48000;
  cfg.dataCallback = &Sonar::onAudio;
  cfg.pUserData = this;

  if (ma_device_init(nullptr, &cfg, device_) != MA_SUCCESS) {
    std::fprintf(stderr, "Sonar: ma_device_init failed\n");
    delete device_;
    device_ = nullptr;
    return false;
  }
  if (ma_device_start(device_) != MA_SUCCESS) {
    std::fprintf(stderr, "Sonar: ma_device_start failed\n");
    ma_device_uninit(device_);
    delete device_;
    device_ = nullptr;
    return false;
  }
  running_.store(true);
  return true;
}

void Sonar::stop() {
  if (!running_.load()) return;
  if (device_) ma_device_uninit(device_);
  running_.store(false);
}

void Sonar::setVolume(float v01) {
  volume_.store(std::clamp(v01, 0.0f, 1.0f));
}

void Sonar::setCarrierHz(float hz) {
  carrierHz_.store(std::clamp(hz, 40.0f, 1000.0f));
}

void Sonar::setFalloffExponent(float n) {
  falloffExponent_.store(std::clamp(n, 1.5f, 10.0f));
}

void Sonar::setLeft(float score, bool valid) {
  leftScore_.store(std::clamp(score, 0.0f, 1.0f));
  leftValid_.store(valid);
}

void Sonar::setCenter(float score, bool valid) {
  centerScore_.store(std::clamp(score, 0.0f, 1.0f));
  centerValid_.store(valid);
}

void Sonar::setRight(float score, bool valid) {
  rightScore_.store(std::clamp(score, 0.0f, 1.0f));
  rightValid_.store(valid);
}

void Sonar::onAudio(ma_device* device, void* output,
  const void* /*input*/, unsigned int frameCount) {
  Sonar* self = static_cast<Sonar*>(device->pUserData);
  const size_t byteCount = static_cast<size_t>(frameCount) * 2u * sizeof(float);

  if (!self) {
    std::memset(output, 0, byteCount);
    return;
  }

  // Step 2A-4: three sectors, pulse envelope, stereo pan, per-sample
  // lowpass smoothing. 110 Hz carrier (low hum, reproducible on most
  // output devices, clear of AC-mains harmonics).
  // Step 2A-5 addition: `enabled` is folded into a gain multiplier so
  // toggling it fades via the same amp smoother (~60 ms ramp) - no
  // startup thunk, no click when muting.
  const bool enabled = self->enabled_.load();
  const float vol = enabled ? self->volume_.load() : 0.0f;
  const double sr = static_cast<double>(device->sampleRate);
  const float targetCarrier = self->carrierHz_.load();
  constexpr double kTwoPi = 6.28318530717958647692;

  // Derive per-sector amp + pulseHz targets. Insufficient-data sectors
  // go silent regardless of score.
  const float falloff = self->falloffExponent_.load();
  auto readTarget = [vol, falloff](std::atomic<float>& scoreA,
                          std::atomic<bool>& validA,
                          float& outAmp, float& outPulse) {
    const bool valid = validA.load();
    const float score = std::clamp(scoreA.load(), 0.0f, 1.0f);
    // amp = (1 - score)^falloff. Higher exponent = steeper onset near
    // the blocked threshold, so mid-range clearances stay quiet.
    // std::pow is called once per sector per callback, not per sample.
    const float x = 1.0f - score;
    const float curved = std::pow(x, falloff);
    outAmp = valid ? curved * vol : 0.0f;
    outPulse = 0.5f + x * 5.0f;  // 0.5..5.5 Hz - still linear
  };

  float tgtAmpL, tgtPulseL;
  float tgtAmpC, tgtPulseC;
  float tgtAmpR, tgtPulseR;
  readTarget(self->leftScore_,   self->leftValid_,   tgtAmpL, tgtPulseL);
  readTarget(self->centerScore_, self->centerValid_, tgtAmpC, tgtPulseC);
  readTarget(self->rightScore_,  self->rightValid_,  tgtAmpR, tgtPulseR);

  // alpha = 1 - exp(-1 / (tc_seconds * sample_rate))
  constexpr double kAmpTc = 0.060;    // 60 ms
  constexpr double kPulseTc = 0.080;  // 80 ms
  const float alphaAmp =
    static_cast<float>(1.0 - std::exp(-1.0 / (kAmpTc * sr)));
  const float alphaPulse =
    static_cast<float>(1.0 - std::exp(-1.0 / (kPulseTc * sr)));

  float* out = static_cast<float*>(output);
  for (unsigned int i = 0; i < frameCount; ++i) {
    // Smooth targets towards current values (one-pole IIR per sample).
    self->smoothAmpL_   += alphaAmp   * (tgtAmpL   - self->smoothAmpL_);
    self->smoothAmpC_   += alphaAmp   * (tgtAmpC   - self->smoothAmpC_);
    self->smoothAmpR_   += alphaAmp   * (tgtAmpR   - self->smoothAmpR_);
    self->smoothPulseL_ += alphaPulse * (tgtPulseL - self->smoothPulseL_);
    self->smoothPulseC_ += alphaPulse * (tgtPulseC - self->smoothPulseC_);
    self->smoothPulseR_ += alphaPulse * (tgtPulseR - self->smoothPulseR_);

    // Shared carrier. Smooth the carrier frequency too so slider drags
    // don't snap phase / introduce clicks.
    self->smoothCarrierHz_ +=
      alphaPulse * (targetCarrier - self->smoothCarrierHz_);
    const double carrier = std::sin(kTwoPi * self->phase_);
    self->phase_ += static_cast<double>(self->smoothCarrierHz_) / sr;
    if (self->phase_ >= 1.0) self->phase_ -= 1.0;

    // Per-sector pulse envelopes (0..1), each at its own pulseHz.
    const double envL = 0.5 * (1.0 + std::sin(kTwoPi * self->envPhaseL_));
    const double envC = 0.5 * (1.0 + std::sin(kTwoPi * self->envPhaseC_));
    const double envR = 0.5 * (1.0 + std::sin(kTwoPi * self->envPhaseR_));
    self->envPhaseL_ += self->smoothPulseL_ / sr;
    self->envPhaseC_ += self->smoothPulseC_ / sr;
    self->envPhaseR_ += self->smoothPulseR_ / sr;
    if (self->envPhaseL_ >= 1.0) self->envPhaseL_ -= 1.0;
    if (self->envPhaseC_ >= 1.0) self->envPhaseC_ -= 1.0;
    if (self->envPhaseR_ >= 1.0) self->envPhaseR_ -= 1.0;

    const float sL = static_cast<float>(self->smoothAmpL_ * envL * carrier);
    const float sC = static_cast<float>(self->smoothAmpC_ * envC * carrier);
    const float sR = static_cast<float>(self->smoothAmpR_ * envR * carrier);

    // Pan: L sector -> L channel only, R sector -> R only, C sector
    // equal in both at 0.5 gain so center isn't perceived as twice as
    // loud as a side at the same score.
    out[i * 2 + 0] = sL + 0.5f * sC;
    out[i * 2 + 1] = sR + 0.5f * sC;
  }

  // Publish the latest smoothed amplitudes for the UI meters.
  self->lastAmpL_.store(self->smoothAmpL_);
  self->lastAmpC_.store(self->smoothAmpC_);
  self->lastAmpR_.store(self->smoothAmpR_);
}
