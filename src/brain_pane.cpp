/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  brain_pane.cpp
  Async Brain request state machine + ImGui pane rendering. Supports
  two modes:
    * Structured (default) - Ollama `format` field forces Gemma to emit
      {"main_object": "<noun>"}; the app composes the final sentence
      from that label plus the geometry layer's distance/direction.
      Geometry stays authoritative; Gemma only names the object.
    * Freeform - original path where Gemma writes the whole sentence,
      useful for prompt iteration and debugging.
  Editable prompts live in fixed-size char buffers so ImGui::InputText
  can mutate them in place.
*/


#include "brain_pane.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>

namespace {

constexpr const char* kDefaultStructuredPrompt =
  "Identify the single most prominent object directly in front of the "
  "camera. Respond with one word or a short noun phrase.";

constexpr const char* kDefaultFreeformPrompt =
  "in one short sentence, identify the main obstacle ahead and say "
  "whether the user should follow the suggested direction.";

void copyToBuf(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  const size_t n = std::min(cap - 1, std::strlen(src));
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

BrainPane::BrainPane() {
  copyToBuf(structuredBuf_, kPromptBufSize, kDefaultStructuredPrompt);
  copyToBuf(freeformBuf_, kPromptBufSize, kDefaultFreeformPrompt);

  // Brain round-trip log - separate from the Phase 1 latency.csv so
  // the per-frame pipeline timings and the per-query VLM timings don't
  // mix in one file.
  latencyCsv_.open("data/brain_latency.csv",
    std::ios::out | std::ios::trunc);
  if (latencyCsv_) {
    latencyCsv_ << "wall_ms,roundtrip_ms,ok,mode\n";
  } else {
    std::fprintf(stderr, "BrainPane: could not open data/brain_latency.csv\n");
  }
}

void BrainPane::pushLatency(double nowMs, double roundtripMs, bool ok,
  const char* mode) {
  latencyBuf_[latencyHead_] = roundtripMs;
  latencyHead_ = (latencyHead_ + 1) % kLatencyCap;
  if (latencyCount_ < kLatencyCap) ++latencyCount_;
  if (latencyCsv_) {
    latencyCsv_ << std::fixed << std::setprecision(3)
      << nowMs << "," << roundtripMs << ","
      << (ok ? 1 : 0) << "," << mode << "\n";
    latencyCsv_.flush();  // brain calls are rare; flush so a crash
                          // doesn't lose the last few samples.
  }
}

double BrainPane::medianLatencyMs() const {
  if (latencyCount_ == 0) return 0.0;
  std::array<double, kLatencyCap> sorted{};
  std::copy_n(latencyBuf_.begin(), latencyCount_, sorted.begin());
  std::sort(sorted.begin(), sorted.begin() + latencyCount_);
  return sorted[latencyCount_ / 2];
}

bool BrainPane::isPending() const {
  return state_ == State::Pending;
}

void BrainPane::fireStructured(const cv::Mat& colorBgr,
  const FreeSpaceResult& freeResult, double nowMs) {
  const cv::Mat frameCopy = colorBgr.clone();
  const BrainConfig cfgCopy = cfg_;
  const std::string instruction(structuredBuf_);
  fsSnapshot_ = freeResult;
  startMs_ = nowMs;
  state_ = State::Pending;
  structuredFuture_ = std::async(std::launch::async,
    [cfgCopy, instruction, frameCopy]() {
      return askOllamaStructured(instruction, frameCopy, cfgCopy);
    });
}

void BrainPane::fireFreeform(const cv::Mat& colorBgr,
  const FreeSpaceResult& freeResult, double nowMs) {
  const cv::Mat frameCopy = colorBgr.clone();
  const BrainConfig cfgCopy = cfg_;
  const std::string fullPrompt =
    buildGeometryPrompt(freeResult, std::string(freeformBuf_));
  fsSnapshot_ = freeResult;
  startMs_ = nowMs;
  state_ = State::Pending;
  freeformFuture_ = std::async(std::launch::async,
    [cfgCopy, fullPrompt, frameCopy]() {
      return askOllama(fullPrompt, frameCopy, cfgCopy);
    });
}

void BrainPane::render(double nowMs,
  const cv::Mat& colorBgr,
  const FreeSpaceResult& freeResult,
  bool spaceJustPressed) {
  // Poll the active worker.
  if (state_ == State::Pending) {
    if (mode_ == Mode::Structured && structuredFuture_.valid()
      && structuredFuture_.wait_for(std::chrono::seconds(0))
         == std::future_status::ready) {
      structuredResp_ = structuredFuture_.get();
      composedSentence_ = structuredResp_.ok
        ? composeSentence(fsSnapshot_, structuredResp_.mainObject)
        : std::string{};
      state_ = structuredResp_.ok ? State::Done : State::Error;
      pushLatency(nowMs, structuredResp_.roundtripMs,
        structuredResp_.ok, "structured");
      std::fprintf(stderr, "Brain[struct] ms=%.0f ok=%d obj='%s' raw='%s' err='%s'\n",
        structuredResp_.roundtripMs, structuredResp_.ok,
        structuredResp_.mainObject.c_str(),
        structuredResp_.raw.c_str(),
        structuredResp_.error.c_str());
      std::fflush(stderr);
    }
    else if (mode_ == Mode::Freeform && freeformFuture_.valid()
      && freeformFuture_.wait_for(std::chrono::seconds(0))
         == std::future_status::ready) {
      freeformResp_ = freeformFuture_.get();
      state_ = freeformResp_.ok ? State::Done : State::Error;
      pushLatency(nowMs, freeformResp_.roundtripMs,
        freeformResp_.ok, "freeform");
      std::fprintf(stderr, "Brain[free] ms=%.0f ok=%d len=%zu err='%s'\n",
        freeformResp_.roundtripMs, freeformResp_.ok,
        freeformResp_.text.size(), freeformResp_.error.c_str());
      std::fflush(stderr);
    }
  }

  const bool haveFrame = !colorBgr.empty();
  const bool canAsk = haveFrame && state_ != State::Pending;

  // Mode radio.
  int modeInt = (mode_ == Mode::Structured) ? 0 : 1;
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("model: %s  (SPACE to Ask)", cfg_.model.c_str());
  if (ImGui::RadioButton("Structured##bm", &modeInt, 0)) {
    mode_ = Mode::Structured;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Freeform##bm", &modeInt, 1)) {
    mode_ = Mode::Freeform;
  }

  // Prompt editor for the active mode. Multiline keeps a couple of
  // lines visible so longer instructions are easier to iterate on.
  char* activeBuf = (mode_ == Mode::Structured) ? structuredBuf_ : freeformBuf_;
  const float inputH = ImGui::GetTextLineHeight() * 3.0f;
  ImGui::InputTextMultiline("##prompt", activeBuf, kPromptBufSize,
    ImVec2(-1.0f, inputH));

  // Ask button + status.
  const bool askClicked = ImGui::Button("Ask")
    || (spaceJustPressed && canAsk);
  if (askClicked && canAsk) {
    if (mode_ == Mode::Structured) fireStructured(colorBgr, freeResult, nowMs);
    else                           fireFreeform(colorBgr, freeResult, nowMs);
  }
  ImGui::SameLine();

  if (!haveFrame) {
    ImGui::TextDisabled("waiting for first frame...");
  } else if (state_ == State::Idle) {
    ImGui::TextDisabled("idle");
  } else if (state_ == State::Pending) {
    const double elapsed = (nowMs - startMs_) / 1000.0;
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
      "requesting... %.1f s", elapsed);
  } else if (state_ == State::Done) {
    const double ms = (mode_ == Mode::Structured)
      ? structuredResp_.roundtripMs : freeformResp_.roundtripMs;
    const double med = medianLatencyMs();
    if (latencyCount_ > 1 && med > 0.0) {
      ImGui::TextDisabled("done (%.2f s, median %.2f s over %d)",
        ms / 1000.0, med / 1000.0, latencyCount_);
    } else {
      ImGui::TextDisabled("done (%.2f s)", ms / 1000.0);
    }
  } else {
    const double ms = (mode_ == Mode::Structured)
      ? structuredResp_.roundtripMs : freeformResp_.roundtripMs;
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
      "failed (%.0f ms)", ms);
  }

  ImGui::Separator();

  // Structured mode: big composed sentence on top, raw JSON muted below.
  // Freeform mode: raw text response in the scroll area.
  ImGui::BeginChild("brain_out", ImVec2(0, 0), true,
    ImGuiWindowFlags_HorizontalScrollbar);
  if (mode_ == Mode::Structured) {
    if (state_ == State::Done) {
      if (!composedSentence_.empty()) {
        ImGui::TextWrapped("%s", composedSentence_.c_str());
      } else {
        ImGui::TextDisabled("[no composed sentence]");
      }
      if (!structuredResp_.raw.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("raw: %s", structuredResp_.raw.c_str());
      }
    } else if (state_ == State::Error && !structuredResp_.error.empty()) {
      ImGui::TextWrapped("%s", structuredResp_.error.c_str());
      if (!structuredResp_.raw.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("raw: %s", structuredResp_.raw.c_str());
      }
    } else if (state_ == State::Pending) {
      ImGui::TextDisabled("(VLM forward pass running on worker thread)");
    } else {
      ImGui::TextDisabled(
        "Press Ask (or SPACE). Structured mode: Gemma names the "
        "object, the app composes the sentence from geometry.");
    }
  } else {
    if (state_ == State::Done) {
      if (freeformResp_.text.empty()) {
        ImGui::TextDisabled("[empty response]");
      } else {
        ImGui::TextWrapped("%s", freeformResp_.text.c_str());
      }
    } else if (state_ == State::Error && !freeformResp_.error.empty()) {
      ImGui::TextWrapped("%s", freeformResp_.error.c_str());
    } else if (state_ == State::Pending) {
      ImGui::TextDisabled("(VLM forward pass running on worker thread)");
    } else {
      ImGui::TextDisabled(
        "Press Ask (or SPACE). Freeform mode: Gemma writes the full "
        "sentence; useful for prompt iteration.");
    }
  }
  ImGui::EndChild();
}
