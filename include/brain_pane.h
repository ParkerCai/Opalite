/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  brain_pane.h
  Owns the asynchronous Brain request state and draws the Brain ImGui
  window contents. Keeps main.cpp out of the business of managing the
  std::async future, the idle/pending/done/error state machine, and the
  UI layout for the caption pane.
*/

#pragma once

#include "brain_client.h"
#include "free_space.h"

#include <opencv2/core.hpp>

#include <array>
#include <fstream>
#include <future>
#include <string>

class BrainPane {
public:
  enum class Mode { Structured, Freeform };

  BrainPane();

  // Draws the full contents of the Brain ImGui window. The caller is
  // responsible for ImGui::Begin("Brain", ...) / ImGui::End() around
  // this call so window placement stays in main.cpp's layout pass.
  //
  // Polls the worker future each call; fires a new request when the
  // user clicks Ask or when `spaceJustPressed` is true (edge-detected
  // by the caller). colorBgr and freeResult are snapshotted into the
  // worker's closure, so the caller may continue mutating them.
  void render(double nowMs,
    const cv::Mat& colorBgr,
    const FreeSpaceResult& freeResult,
    bool spaceJustPressed);

  bool isPending() const;

  BrainConfig& config() { return cfg_; }
  const BrainConfig& config() const { return cfg_; }

private:
  enum class State { Idle, Pending, Done, Error };

  void fireStructured(const cv::Mat& colorBgr,
    const FreeSpaceResult& freeResult, double nowMs);
  void fireFreeform(const cv::Mat& colorBgr,
    const FreeSpaceResult& freeResult, double nowMs);

  Mode mode_ = Mode::Structured;
  State state_ = State::Idle;

  // Only one of these holds a valid running request at a time.
  std::future<BrainResponse> freeformFuture_;
  std::future<BrainStructuredResponse> structuredFuture_;

  BrainResponse freeformResp_;
  BrainStructuredResponse structuredResp_;

  // Snapshotted free-space at dispatch time. The composed sentence is
  // built from THIS (not the live freeResult) so the object name and
  // geometry stay consistent with the image Gemma actually saw.
  FreeSpaceResult fsSnapshot_;
  std::string composedSentence_;

  double startMs_ = 0.0;
  BrainConfig cfg_;

  // Editable prompts - separate buffers per mode so toggling doesn't
  // stomp what you typed. Fixed size to keep ImGui::InputText happy.
  static constexpr int kPromptBufSize = 512;
  char structuredBuf_[kPromptBufSize]{};
  char freeformBuf_[kPromptBufSize]{};

  // Ring buffer of per-request round-trip latencies + data/brain_latency.csv
  // append stream. Mirrors the Phase 1 pipeline-latency HUD pattern.
  static constexpr int kLatencyCap = 30;
  std::array<double, kLatencyCap> latencyBuf_{};
  int latencyHead_ = 0;
  int latencyCount_ = 0;
  std::ofstream latencyCsv_;

  void pushLatency(double nowMs, double roundtripMs, bool ok,
    const char* mode);
  double medianLatencyMs() const;
};
