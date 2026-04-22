/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  free_space.cpp
  Splits the depth frame's forward cone into left / center / right strips
  and reports per-sector clearance so the rest of the app can pick a
  suggested walking direction.
*/


#include "free_space.h"

#include <algorithm>

namespace {

  SectorClearance scoreSector(const cv::Mat& depthMm16u,
    const cv::Rect& roi,
    const FreeSpaceConfig& cfg) {
    SectorClearance out{};
    out.roi = roi;
    if (roi.area() <= 0) return out;

    const cv::Mat region = depthMm16u(roi);
    const cv::Mat validMask = region > 0;
    if (cv::countNonZero(validMask) == 0) return out;

    double dminMm = 0.0;
    cv::minMaxLoc(region, &dminMm, nullptr, nullptr, nullptr, validMask);
    out.minDepthM = static_cast<float>(dminMm) * 0.001f;

    const float span = std::max(0.01f,
      cfg.clearHorizonM - cfg.blockedThresholdM);
    out.score = std::clamp((out.minDepthM - cfg.blockedThresholdM) / span,
      0.0f, 1.0f);
    out.blocked = out.minDepthM > 0.0f
      && out.minDepthM < cfg.blockedThresholdM;
    return out;
  }

}  // namespace

FreeSpaceResult analyzeForwardPath(const cv::Mat& depthMm16u,
  const FreeSpaceConfig& cfg) {
  FreeSpaceResult out{};
  if (depthMm16u.empty() || depthMm16u.type() != CV_16UC1) return out;

  const int W = depthMm16u.cols;
  const int H = depthMm16u.rows;

  const int coneW = std::max(3, static_cast<int>(W * cfg.coneXFrac));
  const int coneH = std::max(1, static_cast<int>(H * cfg.coneYFrac));
  const int coneX0 = (W - coneW) / 2;
  const int coneY0 = (H - coneH) / 2;

  // Three equal vertical stripes inside the forward cone.
  const int stripeW = coneW / 3;
  const int leftX = coneX0;
  const int centerX = coneX0 + stripeW;
  const int rightX = coneX0 + 2 * stripeW;
  const int rightW = coneW - 2 * stripeW;  // absorb rounding remainder

  out.left = scoreSector(depthMm16u,
    cv::Rect(leftX, coneY0, stripeW, coneH), cfg);
  out.center = scoreSector(depthMm16u,
    cv::Rect(centerX, coneY0, stripeW, coneH), cfg);
  out.right = scoreSector(depthMm16u,
    cv::Rect(rightX, coneY0, rightW, coneH), cfg);

  // argmax(score) with tie-break preferring center, then the side with a
  // slightly higher raw depth.
  auto better = [](const SectorClearance& a, const SectorClearance& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.minDepthM > b.minDepthM;
  };
  out.suggested = Direction::Center;
  if (better(out.left, out.center)) out.suggested = Direction::Left;
  if (better(out.right,
    out.suggested == Direction::Left ? out.left : out.center)) {
    out.suggested = Direction::Right;
  }
  out.nearestForwardM = out.center.minDepthM;
  return out;
}
