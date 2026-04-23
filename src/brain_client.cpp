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

#include <chrono>
#include <cstdint>
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
