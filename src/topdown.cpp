/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  topdown.cpp
  Deprojects every valid depth pixel with rs2_deproject_pixel_to_point,
  drops the Y axis, and bins (X, Z) into a 2D occupancy grid. Result is
  a CV_8UC3 grayscale-as-BGR image ready for the GL uploader.
*/


#include "topdown.h"

#include <librealsense2/rsutil.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

cv::Mat buildTopDown(const cv::Mat& depthMm16u,
  const rs2_intrinsics& intr,
  const TopDownConfig& cfg) {
  if (depthMm16u.empty() || depthMm16u.type() != CV_16UC1) {
    return cv::Mat();
  }

  const int W = std::max(1, static_cast<int>((2.0f * cfg.extentM) / cfg.cellM));
  const int H = std::max(1, static_cast<int>(cfg.extentM / cfg.cellM));
  const float invCell = 1.0f / cfg.cellM;

  cv::Mat counts(H, W, CV_32SC1, cv::Scalar(0));

  for (int v = 0; v < depthMm16u.rows; ++v) {
    const uint16_t* drow = depthMm16u.ptr<uint16_t>(v);
    for (int u = 0; u < depthMm16u.cols; ++u) {
      const uint16_t dmm = drow[u];
      if (dmm == 0) continue;
      const float dM = dmm * 0.001f;
      if (dM < cfg.minZM || dM > cfg.extentM) continue;

      float pix[2] = { static_cast<float>(u), static_cast<float>(v) };
      float pt[3] = { 0.0f, 0.0f, 0.0f };
      rs2_deproject_pixel_to_point(pt, &intr, pix, dM);
      const float X = pt[0];
      const float Z = pt[2];
      if (Z <= 0.0f || Z > cfg.extentM) continue;
      if (X < -cfg.extentM || X > cfg.extentM) continue;

      const int col = static_cast<int>((X + cfg.extentM) * invCell);
      const int row = (H - 1) - static_cast<int>(Z * invCell);
      if (col < 0 || col >= W || row < 0 || row >= H) continue;
      counts.at<int32_t>(row, col) += 1;
    }
  }

  double maxCount = 0.0;
  cv::minMaxLoc(counts, nullptr, &maxCount);

  cv::Mat gray(H, W, CV_8UC1, cv::Scalar(0));
  if (maxCount > 0.0) {
    const float norm = 255.0f / std::sqrt(static_cast<float>(maxCount));
    for (int r = 0; r < H; ++r) {
      const int32_t* crow = counts.ptr<int32_t>(r);
      uint8_t* grow = gray.ptr<uint8_t>(r);
      for (int c = 0; c < W; ++c) {
        const int32_t n = crow[c];
        if (n <= 0) continue;
        const float v = std::sqrt(static_cast<float>(n)) * norm;
        grow[c] = static_cast<uint8_t>(std::min(255.0f, v));
      }
    }
  }

  // Mark camera origin with a small cross so orientation stays obvious
  // when the scene is mostly empty.
  const int camCol = W / 2;
  const int camRow = H - 1;
  cv::drawMarker(gray, cv::Point(camCol, camRow), cv::Scalar(255),
    cv::MARKER_TRIANGLE_UP, std::max(6, H / 40), 1);

  cv::Mat bgr;
  cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
  return bgr;
}
