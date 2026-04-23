/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  brain_client.cpp
  Thin wrapper over cpp-httplib + nlohmann/json for the Ollama
  /api/generate endpoint. Phase 2B-2 implements only the text-only path
  ("say hi" smoke); 2B-3 fills in the image overload with JPEG + base64.
*/


#include "brain_client.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

  struct Timer {
    std::chrono::steady_clock::time_point start{ std::chrono::steady_clock::now() };
    double elapsedMs() const {
      const auto now = std::chrono::steady_clock::now();
      return std::chrono::duration<double, std::milli>(now - start).count();
    }
  };

  // Minimal RFC-4648 base64 encoder. Ollama's /api/generate wants each
  // image as a plain base64 string (no data-URI prefix).
  std::string base64Encode(const std::vector<uint8_t>& data) {
    static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    int val = 0;
    int valb = -6;
    for (uint8_t c : data) {
      val = (val << 8) | c;
      valb += 8;
      while (valb >= 0) {
        out.push_back(kAlphabet[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    if (valb > -6) {
      out.push_back(kAlphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) out.push_back('=');
    return out;
  }

  // JPEG-encode a BGR8 Mat at a given quality and return the base64
  // string (empty on encode failure). Quality 70 is a sane default: ~5x
  // smaller than PNG, visually unchanged for the VLM.
  std::string jpegBase64(const cv::Mat& bgr, int quality) {
    std::vector<uint8_t> buf;
    const std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, quality };
    if (!cv::imencode(".jpg", bgr, buf, params)) return {};
    return base64Encode(buf);
  }

  BrainResponse postAndParse(const BrainConfig& cfg,
    const nlohmann::json& body) {
    BrainResponse out;
    Timer t;

    httplib::Client cli(cfg.host);
    cli.set_connection_timeout(cfg.timeoutSec, 0);
    cli.set_read_timeout(cfg.timeoutSec, 0);
    cli.set_write_timeout(cfg.timeoutSec, 0);

    auto res = cli.Post("/api/generate", body.dump(), "application/json");
    out.roundtripMs = t.elapsedMs();

    if (!res) {
      out.error = "no response (Ollama unreachable?)";
      return out;
    }
    if (res->status != 200) {
      out.error = "HTTP " + std::to_string(res->status) + ": " + res->body;
      return out;
    }

    try {
      const auto parsed = nlohmann::json::parse(res->body);
      out.text = parsed.value("response", "");
      out.ok = true;
    }
    catch (const std::exception& e) {
      out.error = std::string("JSON parse: ") + e.what();
    }
    return out;
  }

}  // namespace

BrainResponse askOllama(const std::string& prompt, const BrainConfig& cfg) {
  const nlohmann::json body = {
    {"model", cfg.model},
    {"prompt", prompt},
    {"stream", false},
    // Disable Gemma 4's internal "thinking" pass. With thinking on, the
    // model burns tokens reasoning privately and returns an empty
    // response field; turning it off makes the caption the actual output.
    {"think", false},
    {"keep_alive", cfg.keepAlive},
    {"options", {
      {"temperature", cfg.temperature},
      {"num_predict", cfg.numPredict},
    }},
  };
  return postAndParse(cfg, body);
}

BrainStructuredResponse askOllamaStructured(const std::string& instruction,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg) {
  BrainStructuredResponse out;
  Timer t;

  if (colorBgr.empty()) {
    out.error = "colorBgr is empty";
    return out;
  }
  const std::string b64 = jpegBase64(colorBgr, 70);
  if (b64.empty()) {
    out.error = "cv::imencode JPEG failed";
    return out;
  }

  // JSON Schema constrains Ollama's token generation so `response` is
  // guaranteed to parse into this exact shape. Location is NOT asked
  // of the VLM - the depth sensor tells us where the obstacle is far
  // more reliably than Gemma's eyeball of "which third of the frame".
  const nlohmann::json schema = {
    {"type", "object"},
    {"properties", {
      {"main_object", { {"type", "string"} }},
    }},
    {"required", nlohmann::json::array({"main_object"})},
  };

  const nlohmann::json body = {
    {"model", cfg.model},
    {"prompt", instruction},
    {"images", nlohmann::json::array({ b64 })},
    {"stream", false},
    {"think", false},
    {"keep_alive", cfg.keepAlive},
    {"format", schema},
    {"options", {
      {"temperature", cfg.temperature},
      {"num_predict", cfg.numPredict},
    }},
  };

  httplib::Client cli(cfg.host);
  cli.set_connection_timeout(cfg.timeoutSec, 0);
  cli.set_read_timeout(cfg.timeoutSec, 0);
  cli.set_write_timeout(cfg.timeoutSec, 0);

  auto res = cli.Post("/api/generate", body.dump(), "application/json");
  out.roundtripMs = t.elapsedMs();
  if (!res) {
    out.error = "no response (Ollama unreachable?)";
    return out;
  }
  if (res->status != 200) {
    out.error = "HTTP " + std::to_string(res->status) + ": " + res->body;
    return out;
  }

  try {
    const auto outer = nlohmann::json::parse(res->body);
    out.raw = outer.value("response", "");
    if (out.raw.empty()) {
      out.error = "Ollama returned empty response field";
      return out;
    }
    const auto inner = nlohmann::json::parse(out.raw);
    out.mainObject = inner.value("main_object", "");
    if (out.mainObject.empty()) {
      out.error = "parsed JSON missing main_object";
      return out;
    }
    out.ok = true;
  }
  catch (const std::exception& e) {
    out.error = std::string("JSON parse: ") + e.what();
  }
  return out;
}

std::string composeSentence(const FreeSpaceResult& fs,
  const std::string& mainObject) {
  auto capFirst = [](std::string s) {
    if (!s.empty()) {
      s[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(s[0])));
    }
    return s;
  };

  // Fall back to a neutral noun if Gemma returned nothing useful.
  std::string label = mainObject;
  if (label.empty()) label = "obstacle";
  const std::string capLabel = capFirst(label);

  // Derive location from the closest valid sector. The depth sensor is
  // the authority on "where the obstacle is" - far more reliable than
  // Gemma guessing which third of the frame the object's centroid sits
  // in. Ties are broken toward center (matches the sticky-center bias).
  auto sectorDepth = [](const SectorClearance& s) {
    return (s.validPixels > 0 && s.nearDepthM > 0.0f) ? s.nearDepthM : 1e9f;
  };
  const float dL = sectorDepth(fs.left);
  const float dC = sectorDepth(fs.center);
  const float dR = sectorDepth(fs.right);

  const char* posPhrase = "ahead";
  float posDist = fs.center.nearDepthM;
  // Prefer left/right only when clearly closer than center (25 cm margin).
  constexpr float kSideMargin = 0.25f;
  if (dL < dC - kSideMargin && dL <= dR) {
    posPhrase = "on the left";
    posDist = fs.left.nearDepthM;
  } else if (dR < dC - kSideMargin && dR < dL) {
    posPhrase = "on the right";
    posDist = fs.right.nearDepthM;
  }

  // Direction suffix from the sensor's sticky-center decision.
  const char* dirSuffix = "";
  if (fs.suggested == Direction::Left)       dirSuffix = ", go left";
  else if (fs.suggested == Direction::Right) dirSuffix = ", go right";

  // If everything looks open (no steer suggestion, nothing close),
  // short-circuit to the clean path-clear line.
  const bool openCenter =
    fs.suggested == Direction::Center && fs.center.nearDepthM > 2.0f;
  if (openCenter) {
    return "Path clear, continue straight.";
  }

  char buf[256];
  if (posDist > 0.0f) {
    std::snprintf(buf, sizeof(buf), "%s %s at %.2f m%s.",
      capLabel.c_str(), posPhrase, posDist, dirSuffix);
  } else {
    std::snprintf(buf, sizeof(buf), "%s %s%s.",
      capLabel.c_str(), posPhrase, dirSuffix);
  }
  return buf;
}

std::string buildGeometryPrompt(const FreeSpaceResult& fs,
  const std::string& question) {
  auto sectorLine = [](const char* name, const SectorClearance& s) {
    char buf[96];
    if (s.validPixels <= 0 || s.nearDepthM <= 0.0f) {
      std::snprintf(buf, sizeof(buf),
        "  %s sector:    no depth reading\n", name);
    } else {
      std::snprintf(buf, sizeof(buf),
        "  %s sector:    %.2f m  (%s)\n",
        name, s.nearDepthM, s.blocked ? "BLOCKED" : "clear");
    }
    return std::string(buf);
  };
  const char* dirName =
    fs.suggested == Direction::Left ? "LEFT"
    : fs.suggested == Direction::Right ? "RIGHT" : "CENTER";
  std::string p =
    "You are a calm navigation aide for a visually impaired user. "
    "The depth sensor reports:\n";
  p += sectorLine("left  ", fs.left);
  p += sectorLine("center", fs.center);
  p += sectorLine("right ", fs.right);
  p += "  suggested:      ";
  p += dirName;
  p += "\n\nGiven the photo below and these readings, ";
  p += question;
  return p;
}

BrainResponse askOllama(const std::string& prompt,
  const cv::Mat& colorBgr,
  const BrainConfig& cfg) {
  if (colorBgr.empty()) return askOllama(prompt, cfg);
  const std::string b64 = jpegBase64(colorBgr, 70);
  if (b64.empty()) {
    BrainResponse r;
    r.error = "cv::imencode JPEG failed";
    return r;
  }
  const nlohmann::json body = {
    {"model", cfg.model},
    {"prompt", prompt},
    {"images", nlohmann::json::array({ b64 })},
    {"stream", false},
    // Disable Gemma 4's internal "thinking" pass. With thinking on, the
    // model burns tokens reasoning privately and returns an empty
    // response field; turning it off makes the caption the actual output.
    {"think", false},
    {"keep_alive", cfg.keepAlive},
    {"options", {
      {"temperature", cfg.temperature},
      {"num_predict", cfg.numPredict},
    }},
  };
  return postAndParse(cfg, body);
}
