/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  free_space.h
  Three-sector forward free-space analyzer. Each sector (left / center /
  right) reports a robust near-depth (low-percentile, not raw minimum) and
  a 0-1 clearance score; a suggested direction picks the most open sector
  with ties broken toward center. Percentile + minimum-support guard the
  reading against single-pixel speckle and depth-map holes.
*/

#pragma once

#include <opencv2/core.hpp>

struct FreeSpaceConfig {
  // Blocked below this distance regardless of clearance score.
  float blockedThresholdM = 0.5f;
  // Clamp point for full clearance: depth >= this maps to score 1.0.
  float clearHorizonM = 3.0f;
  // Total horizontal span of image width used for the three sectors.
  // Default 0.95 covers most of the ~69 deg horizontal FoV.
  float coneXFrac = 0.95f;
  // Width of the narrow center "forward hazard" beam as a fraction of the
  // image width. The left / right sectors split what's left of the span.
  // Default 0.25 (~22 deg) for a readable beam on the demo display.
  float centerBeamFrac = 0.25f;
  // Vertical slice to sample (centered). Keeps ceiling / floor noise out.
  float coneYFrac = 0.50f;
  // Percentile used as the sector's "near depth". 0.05 = 5th percentile,
  // i.e. the value where 5 % of valid pixels are closer. Smaller values
  // approach raw min (noisier); larger values grow more conservative.
  float nearPercentile = 0.05f;
  // Minimum count of valid (non-zero) depth pixels in a sector ROI before
  // the reading is trusted. Below this the sector is "insufficient data"
  // and never flagged blocked - prevents speckle / holes from flipping
  // the suggested direction.
  int minValidPixels = 200;
  // Sticky-center bias: when the center sector is usable and not
  // blocked, stay on CENTER unless a side's near-depth beats center's by
  // at least this many metres. Keeps guidance calm instead of jittering
  // between directions over tiny depth differences.
  float sideBiasM = 0.25f;
  bool enabled = true;
};

struct SectorClearance {
  float nearDepthM = 0.0f;  // 0 means insufficient valid samples in the ROI
  float score = 0.0f;       // 0 = blocked at 0 m, 1 = clear at horizon
  bool blocked = false;     // nearDepthM > 0 and < blockedThresholdM
  int validPixels = 0;      // count of non-zero depth samples in the ROI
  cv::Rect roi;             // pixel rectangle inside the depth image
};

enum class Direction { Left = 0, Center = 1, Right = 2 };

struct FreeSpaceResult {
  SectorClearance left;
  SectorClearance center;
  SectorClearance right;
  Direction suggested = Direction::Center;
  // Shortcut for "what's directly in front": the center sector's near-
  // depth percentile. 0 indicates the center sector had insufficient data.
  float nearestForwardM = 0.0f;
};

FreeSpaceResult analyzeForwardPath(const cv::Mat& depthMm16u,
  const FreeSpaceConfig& cfg);
