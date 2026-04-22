/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  realsense_stream.h
  RAII wrapper around an rs2::pipeline configured for a D435i.
  Step 5 opens aligned color + depth at 640x480 @ 30fps and exposes a
  colorized depth view for the preview pane.
*/

#pragma once

#include <librealsense2/rs.hpp>
#include <opencv2/core.hpp>
#include <string>

class RealSenseStream {
public:
  RealSenseStream();
  ~RealSenseStream();

  RealSenseStream(const RealSenseStream&) = delete;
  RealSenseStream& operator=(const RealSenseStream&) = delete;

  void start();
  void stop();

  // Non-blocking. Populates all three outputs with owning copies when a
  // new aligned frameset is available.
  //   colorBgrOut      CV_8UC3 BGR
  //   depthMm16uOut    CV_16UC1 depth in mm (0 = invalid)
  //   depthVizBgrOut   CV_8UC3 colorized depth, already in BGR order so
  //                    it flows through updateTexture unchanged
  bool poll(cv::Mat& colorBgrOut,
    cv::Mat& depthMm16uOut,
    cv::Mat& depthVizBgrOut);

  int colorWidth() const { return colorWidth_; }
  int colorHeight() const { return colorHeight_; }
  int depthWidth() const { return depthWidth_; }
  int depthHeight() const { return depthHeight_; }
  const std::string& usbType() const { return usbType_; }
  bool started() const { return started_; }

  // Intrinsics to deproject pixels of the *aligned* depth frame. Because
  // rs2::align(RS2_STREAM_COLOR) resamples depth onto the color sensor,
  // the color stream's intrinsics are the right ones to use.
  const rs2_intrinsics& depthIntrinsics() const { return alignedDepthIntr_; }

private:
  rs2::pipeline pipe_;
  rs2::config cfg_;
  rs2::align align_;
  bool started_ = false;
  int colorWidth_ = 0;
  int colorHeight_ = 0;
  int depthWidth_ = 0;
  int depthHeight_ = 0;
  std::string usbType_;
  rs2_intrinsics alignedDepthIntr_{};
};
