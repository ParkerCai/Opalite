/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  free_space.cpp
  Splits the depth frame's forward cone into left / center / right strips
  and reports per-sector clearance so the rest of the app can pick a
  suggested walking direction. Uses a low-percentile depth (robust to
  specle / holes) plus a minimum-support gate so isolated noisy pixels
  don't flip the guidance.
*/


#include "free_space.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

  SectorClearance scoreSector(const cv::Mat& depthMm16u,
    const cv::Rect& roi,
    const FreeSpaceConfig& cfg) {
    SectorClearance out{};
    out.roi = roi;
    if (roi.area() <= 0) return out;

    // Collect valid (non-zero) depths so we can sort / percentile them.
    const cv::Mat region = depthMm16u(roi);
    std::vector<uint16_t> vals;
    vals.reserve(static_cast<size_t>(roi.area()));
    for (int y = 0; y < region.rows; ++y) {
      const uint16_t* row = region.ptr<uint16_t>(y);
      for (int x = 0; x < region.cols; ++x) {
        const uint16_t v = row[x];
        if (v > 0) vals.push_back(v);
      }
    }
    out.validPixels = static_cast<int>(vals.size());

    // Minimum-support gate. Too few valid pixels -> treat as "no reading"
    // so speckle / holes can never flip the suggested direction.
    if (out.validPixels < cfg.minValidPixels) return out;

    const float pct = std::clamp(cfg.nearPercentile, 0.001f, 0.5f);
    int idx = static_cast<int>(pct * (vals.size() - 1));
    idx = std::clamp(idx, 0, static_cast<int>(vals.size()) - 1);
    std::nth_element(vals.begin(), vals.begin() + idx, vals.end());
    out.nearDepthM = static_cast<float>(vals[idx]) * 0.001f;

    const float span = std::max(0.01f,
      cfg.clearHorizonM - cfg.blockedThresholdM);
    out.score = std::clamp(
      (out.nearDepthM - cfg.blockedThresholdM) / span, 0.0f, 1.0f);
    out.blocked = out.nearDepthM > 0.0f
      && out.nearDepthM < cfg.blockedThresholdM;
    return out;
  }

}  // namespace

FreeSpaceResult analyzeForwardPath(const cv::Mat& depthMm16u,
  const FreeSpaceConfig& cfg) {
  FreeSpaceResult out{};
  if (depthMm16u.empty() || depthMm16u.type() != CV_16UC1) return out;

  const int W = depthMm16u.cols;
  const int H = depthMm16u.rows;

  // Clamp the configuration so the center beam always fits inside the
  // overall span, leaving at least a 1-pixel slice for each side.
  const float span = std::clamp(cfg.coneXFrac, 0.10f, 1.0f);
  const float beam = std::clamp(cfg.centerBeamFrac, 0.02f,
    std::max(0.04f, span - 0.04f));

  const int coneW = std::max(3, static_cast<int>(W * span));
  const int coneH = std::max(1, static_cast<int>(H * cfg.coneYFrac));
  const int coneX0 = (W - coneW) / 2;
  const int coneY0 = (H - coneH) / 2;

  // Narrow center beam, wide left / right sectors flanking it.
  const int beamW = std::clamp(static_cast<int>(W * beam), 2, coneW - 2);
  const int centerX = (W - beamW) / 2;
  const int leftX = coneX0;
  const int leftW = centerX - leftX;
  const int rightX = centerX + beamW;
  const int rightW = (coneX0 + coneW) - rightX;

  out.left = scoreSector(depthMm16u,
    cv::Rect(leftX, coneY0, leftW, coneH), cfg);
  out.center = scoreSector(depthMm16u,
    cv::Rect(centerX, coneY0, beamW, coneH), cfg);
  out.right = scoreSector(depthMm16u,
    cv::Rect(rightX, coneY0, rightW, coneH), cfg);

  // Sticky-center direction policy. If center has a trusted, unblocked
  // reading, stay on CENTER unless a side's near-depth is meaningfully
  // farther (by sideBiasM metres). When center is blocked or starved of
  // data, fall through to the best side.
  const bool centerUsable =
    out.center.validPixels >= cfg.minValidPixels;
  const bool centerOk = centerUsable && !out.center.blocked;
  const bool leftUsable =
    out.left.validPixels >= cfg.minValidPixels;
  const bool rightUsable =
    out.right.validPixels >= cfg.minValidPixels;

  if (centerOk) {
    const bool leftBeats = leftUsable
      && out.left.nearDepthM > out.center.nearDepthM + cfg.sideBiasM;
    const bool rightBeats = rightUsable
      && out.right.nearDepthM > out.center.nearDepthM + cfg.sideBiasM;
    if (leftBeats && rightBeats) {
      out.suggested = (out.left.nearDepthM >= out.right.nearDepthM)
        ? Direction::Left : Direction::Right;
    } else if (leftBeats) {
      out.suggested = Direction::Left;
    } else if (rightBeats) {
      out.suggested = Direction::Right;
    } else {
      out.suggested = Direction::Center;
    }
  } else if (leftUsable && rightUsable) {
    out.suggested = (out.left.nearDepthM >= out.right.nearDepthM)
      ? Direction::Left : Direction::Right;
  } else if (leftUsable) {
    out.suggested = Direction::Left;
  } else if (rightUsable) {
    out.suggested = Direction::Right;
  } else {
    out.suggested = Direction::Center;  // nothing to go on
  }

  out.nearestForwardM = out.center.nearDepthM;
  return out;
}
