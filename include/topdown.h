/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  topdown.h
  Bird's-eye (X, Z) occupancy map built from an aligned depth frame.
  Each valid depth pixel is deprojected into metres using the camera
  intrinsics, then binned into a fixed-extent world grid with the camera
  at the bottom-centre looking "up" the image.
*/

#pragma once

#include <librealsense2/rs.h>
#include <opencv2/core.hpp>

struct TopDownConfig {
  float extentM = 5.0f;   // half side-to-side and full forward distance (m)
  float cellM = 0.02f;    // world metres per output pixel
  float minZM = 0.20f;    // ignore depth closer than this (m)
};

// Builds a grayscale-in-BGR top-down map (ready for updateTexture).
// Columns span [-extentM, +extentM] in camera-X; rows span [0, extentM]
// in camera-Z with Z=0 at the bottom row. Intensity is sqrt(count) so a
// few bright pixels don't wash out the rest of the scene.
cv::Mat buildTopDown(const cv::Mat& depthMm16u,
  const rs2_intrinsics& intr,
  const TopDownConfig& cfg);
