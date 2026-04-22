/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  obstacle.h
  Dead-simple forward-cone obstacle detector. Looks at a centered rectangle
  of the aligned depth frame and flags whenever the closest valid depth in
  that region is inside a user-set threshold. No segmentation or tracking
  yet - that's Phase 2.
*/

#pragma once

#include <opencv2/core.hpp>

struct ObstacleConfig {
  // 0.5 m default: "imminent collision - stop now" range, comparable to a
  // short navigation-stack inflation radius. Blind-assistant early warning
  // is typically 1.0-1.5 m; the slider exposes both regimes.
  float thresholdM = 0.5f;
  float coneXFrac = 0.30f;  // ROI width as fraction of image width
  float coneYFrac = 0.50f;  // ROI height as fraction of image height
  bool enabled = true;
};

struct ObstacleResult {
  bool present = false;
  float minDepthM = 0.0f;
  cv::Rect roi;  // pixel rectangle inside the depth image
};

ObstacleResult detectForwardObstacle(const cv::Mat& depthMm16u,
  const ObstacleConfig& cfg);
