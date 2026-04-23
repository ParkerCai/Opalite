/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  brain_client.h
  Phase 2B semantic client. Posts a prompt (and, optionally, an already-
  JPEG-encoded color frame) to a local Ollama endpoint running a Gemma 4
  VLM and returns the response text plus round-trip latency.

  The core takes raw JPEG bytes so it compiles on Android NDK without
  OpenCV. Windows callers have cv::Mat-friendly convenience overloads
  compiled in when OPALITE_USE_OPENCV is defined; those just call
  cv::imencode and forward to the byte-vector core.
*/

#pragma once

#include "free_space.h"

#include <cstdint>
#include <string>
#include <vector>

struct BrainConfig {
  // Base URL of the Ollama endpoint. Phase 3 Android just changes this.
  std::string host = "http://localhost:11434";
  // Ollama model tag. Matches `ollama list` output.
  std::string model = "gemma4:e2b";
  // Seconds before the request is considered stalled.
  int timeoutSec = 60;
  // Generation options forwarded to Ollama.
  float temperature = 0.2f;
  // Token cap. Too small and Gemma 4 will occasionally burn tokens on
  // internal thinking and return an empty `response`; 200 is a safer
  // default for single-sentence captions.
  int numPredict = 200;
  // Keep the model loaded for this long between queries so the first
  // warm call doesn't pay cold-load latency.
  std::string keepAlive = "30m";
};

struct BrainResponse {
  bool ok = false;
  std::string text;              // empty unless ok
  std::string error;             // populated when !ok
  double roundtripMs = 0.0;      // wall-clock from POST to parsed response
};

// Schema-constrained variant of BrainResponse. Gemma is asked to emit
// exactly { "main_object": "<word-or-phrase>" } via Ollama's `format`
// field; the parsed object name is lifted into `mainObject` and the
// raw JSON string is preserved in `raw` for demo / debugging.
struct BrainStructuredResponse {
  bool ok = false;
  std::string mainObject;
  std::string raw;               // raw JSON body from Ollama (for the UI)
  std::string error;
  double roundtripMs = 0.0;
};

// Text-only prompt. Always available.
BrainResponse askOllama(const std::string& prompt,
  const BrainConfig& cfg);

// Byte-vector core. Caller hands in already-JPEG-encoded image bytes
// (e.g. from Android's Bitmap.compress(JPEG) on the phone). Portable;
// no OpenCV dependency.
BrainResponse askOllama(const std::string& prompt,
  const std::vector<uint8_t>& jpegBytes,
  const BrainConfig& cfg);

// Structured byte-vector core. Same as the cv::Mat overload but works
// from raw JPEG bytes.
BrainStructuredResponse askOllamaStructured(const std::string& instruction,
  const std::vector<uint8_t>& jpegBytes,
  const BrainConfig& cfg);

#ifdef OPALITE_USE_OPENCV
namespace cv { class Mat; }
// Windows convenience: accept a BGR cv::Mat, cv::imencode to JPEG, then
// forward to the byte-vector core above.
BrainResponse askOllama(const std::string& prompt,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg);
BrainStructuredResponse askOllamaStructured(const std::string& instruction,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg);
#endif

// Assemble a geometry-augmented prompt from the latest free-space state
// plus a user-facing question. Gemma receives sector distances and the
// suggested direction verbatim so it can reference them in the reply.
std::string buildGeometryPrompt(const FreeSpaceResult& fs,
  const std::string& question);

// Deterministic sentence generator. Location is derived from the
// closest free-space sector (depth-truth, not VLM guess). Gemma's only
// contribution is the object's name. Distance and direction come from
// the sensor so the sentence never contradicts the geometry layer.
std::string composeSentence(const FreeSpaceResult& fs,
  const std::string& mainObject);
