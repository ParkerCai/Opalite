/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  obstacle.cpp
  Forward-cone ROI minimum-depth check. Valid (non-zero) depth pixels in
  the ROI are scanned for the minimum distance; if that minimum falls
  under ObstacleConfig::thresholdM the result is marked present.
*/


#include "obstacle.h"

#include <algorithm>

ObstacleResult detectForwardObstacle(const cv::Mat& depthMm16u,
  const ObstacleConfig& cfg) {
  ObstacleResult out{};
  if (depthMm16u.empty() || depthMm16u.type() != CV_16UC1) return out;

  const int W = depthMm16u.cols;
  const int H = depthMm16u.rows;
  const int roiW = std::max(1,
    static_cast<int>(static_cast<float>(W) * cfg.coneXFrac));
  const int roiH = std::max(1,
    static_cast<int>(static_cast<float>(H) * cfg.coneYFrac));
  out.roi = cv::Rect((W - roiW) / 2, (H - roiH) / 2, roiW, roiH);

  const cv::Mat region = depthMm16u(out.roi);
  const cv::Mat validMask = region > 0;
  if (cv::countNonZero(validMask) == 0) return out;

  double dminMm = 0.0;
  cv::minMaxLoc(region, &dminMm, nullptr, nullptr, nullptr, validMask);
  out.minDepthM = static_cast<float>(dminMm) * 0.001f;
  out.present = out.minDepthM > 0.0f && out.minDepthM < cfg.thresholdM;
  return out;
}
