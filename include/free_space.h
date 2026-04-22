/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  free_space.h
  Three-sector forward free-space analyzer. Replaces the earlier single-
  ROI binary obstacle flag. Each sector (left / center / right) reports
  its nearest valid depth and a 0-1 clearance score; a suggested
  direction picks the most open sector, ties broken toward center.
*/

#pragma once

#include <opencv2/core.hpp>

struct FreeSpaceConfig {
  // Blocked below this distance regardless of clearance score.
  float blockedThresholdM = 0.5f;
  // Clamp point for full clearance: depth >= this maps to score 1.0.
  float clearHorizonM = 3.0f;
  // Horizontal portion of the image considered (centered, split in 3).
  float coneXFrac = 0.60f;
  // Vertical slice to sample (centered). Keeps ceiling / floor noise out.
  float coneYFrac = 0.50f;
  bool enabled = true;
};

struct SectorClearance {
  float minDepthM = 0.0f;  // 0 means no valid samples in the ROI
  float score = 0.0f;      // 0 = blocked at 0 m, 1 = clear at horizon
  bool blocked = false;    // minDepthM > 0 and < blockedThresholdM
  cv::Rect roi;            // pixel rectangle inside the depth image
};

enum class Direction { Left = 0, Center = 1, Right = 2 };

struct FreeSpaceResult {
  SectorClearance left;
  SectorClearance center;
  SectorClearance right;
  Direction suggested = Direction::Center;
  // Shortcut for "what's directly in front": the center sector's min
  // depth. 0 indicates no valid depth in the center band.
  float nearestForwardM = 0.0f;
};

FreeSpaceResult analyzeForwardPath(const cv::Mat& depthMm16u,
  const FreeSpaceConfig& cfg);
