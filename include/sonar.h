/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  sonar.h
  Phase 2A spatial-audio feedback layer. Owns one miniaudio playback
  device running on its own thread. UI thread writes target parameters
  via atomic setters; the audio callback reads them and synthesises the
  stereo buffer. This step (2A-2) writes silence; synthesis lands in
  steps 2A-3 and 2A-4.
*/

#pragma once

#include <atomic>

// Opaque forward declaration so consumers of sonar.h don't have to pull
// in miniaudio (which drags <windows.h> on Windows).
struct ma_device;

class Sonar {
public:
  Sonar();
  ~Sonar();
  Sonar(const Sonar&) = delete;
  Sonar& operator=(const Sonar&) = delete;

  // Brings up the audio device and starts the callback thread. Returns
  // true on success, false + logs to stderr otherwise. Safe to call
  // more than once.
  bool start();
  // Stops the callback thread and tears down the device.
  void stop();
  bool isRunning() const { return running_.load(); }

  void setEnabled(bool e) { enabled_.store(e); }
  bool isEnabled() const { return enabled_.load(); }

  void setVolume(float v01);
  float volume() const { return volume_.load(); }

  void setCarrierHz(float hz);
  float carrierHz() const { return carrierHz_.load(); }

  // Shape of the amp falloff as a sector clears: amp = (1 - score)^n.
  // n = 2 is quadratic (most sensitive), n = 4 quartic (Phase 2A default,
  // quiet at mid range), larger n -> near-silent until very close.
  void setFalloffExponent(float n);
  float falloffExponent() const { return falloffExponent_.load(); }

  // Per-sector target update. score is the 0..1 clearance from
  // FreeSpaceResult (1 = clear, 0 = blocked); `valid` is false when the
  // sector had insufficient depth support, in which case that sector
  // contributes silence. UI thread calls these once per geometry frame.
  void setLeft(float score, bool valid);
  void setCenter(float score, bool valid);
  void setRight(float score, bool valid);

  // Post-smoothing amplitudes (0..1) written by the audio thread once
  // per callback. UI reads these for the per-sector meters; values are
  // always the most recently rendered level, never targets.
  float leftAmp() const { return lastAmpL_.load(); }
  float centerAmp() const { return lastAmpC_.load(); }
  float rightAmp() const { return lastAmpR_.load(); }

private:
  ma_device* device_ = nullptr;
  std::atomic<bool> running_{ false };
  std::atomic<bool> enabled_{ true };
  std::atomic<float> volume_{ 0.5f };
  std::atomic<float> carrierHz_{ 110.0f };
  std::atomic<float> falloffExponent_{ 4.0f };

  // Per-sector targets written by UI thread, read by audio thread.
  std::atomic<float> leftScore_{ 1.0f };
  std::atomic<bool>  leftValid_{ false };
  std::atomic<float> centerScore_{ 1.0f };
  std::atomic<bool>  centerValid_{ false };
  std::atomic<float> rightScore_{ 1.0f };
  std::atomic<bool>  rightValid_{ false };

  // Audio-thread-only state (synth + per-sample smoothing).
  double phase_ = 0.0;           // carrier
  double envPhaseL_ = 0.0;       // pulse envelope per sector
  double envPhaseC_ = 0.0;
  double envPhaseR_ = 0.0;
  float smoothAmpL_ = 0.0f;      // lowpass-smoothed amplitudes
  float smoothAmpC_ = 0.0f;
  float smoothAmpR_ = 0.0f;
  float smoothPulseL_ = 0.5f;    // lowpass-smoothed pulse rates (Hz)
  float smoothPulseC_ = 0.5f;
  float smoothPulseR_ = 0.5f;
  float smoothCarrierHz_ = 110.0f;  // follows carrierHz_ via per-sample LP

  // Audio thread publishes the latest smoothed amplitudes here; UI
  // thread reads them for the per-sector meters.
  std::atomic<float> lastAmpL_{ 0.0f };
  std::atomic<float> lastAmpC_{ 0.0f };
  std::atomic<float> lastAmpR_{ 0.0f };

  // miniaudio calls this from its audio thread.
  static void onAudio(ma_device* device, void* output,
    const void* input, unsigned int frameCount);
};
