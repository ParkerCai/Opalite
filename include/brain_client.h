/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  brain_client.h
  Phase 2B semantic client. Posts a prompt (and, later, a JPEG-encoded
  color frame) to a local Ollama endpoint running a Gemma 4 VLM, then
  returns the response text and round-trip latency. Sync API for now;
  step 2B-5 wraps it in std::async for the UI thread.
*/

#pragma once

#include "free_space.h"

#include <opencv2/core.hpp>

#include <string>

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

// Sync POST to <host>/api/generate. Returns when Ollama has fully
// produced the non-streaming response or when the timeout fires. Phase
// 2B-2 uses the text-only overload; step 2B-3 adds the image path.
BrainResponse askOllama(const std::string& prompt,
  const BrainConfig& cfg);

// Overload with a color frame (BGR8). Phase 2B-3 implementation.
BrainResponse askOllama(const std::string& prompt,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg);

// Structured variant: asks Gemma for a single object name via a JSON
// schema (`format` field). Returns a parsed BrainStructuredResponse,
// keeping the raw JSON string for the debug readout.
BrainStructuredResponse askOllamaStructured(const std::string& instruction,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg);

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
