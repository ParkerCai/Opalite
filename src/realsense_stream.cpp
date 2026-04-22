/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  realsense_stream.cpp
  Owns the rs2::pipeline and exposes live frames to the rest of Opalite.
  Step 5 delivers aligned color + depth at 640x480 @ 30 fps plus a
  colorized depth view (already converted to BGR for the uploader).
*/


#include "realsense_stream.h"

#include <opencv2/imgproc.hpp>

namespace {
  // 848x480 is the D435i's native ~16:9 depth mode (widest FoV without
  // doubling the USB bandwidth cost of 1280x720). Color is driven at the
  // same resolution so rs2::align is a straight per-pixel mapping.
  constexpr int COLOR_WIDTH = 848;
  constexpr int COLOR_HEIGHT = 480;
  constexpr int COLOR_FPS = 30;
  constexpr int DEPTH_WIDTH = 848;
  constexpr int DEPTH_HEIGHT = 480;
  constexpr int DEPTH_FPS = 30;
}  // namespace

RealSenseStream::RealSenseStream()
  : align_(RS2_STREAM_COLOR) {
  cfg_.enable_stream(RS2_STREAM_COLOR, COLOR_WIDTH, COLOR_HEIGHT,
    RS2_FORMAT_BGR8, COLOR_FPS);
  cfg_.enable_stream(RS2_STREAM_DEPTH, DEPTH_WIDTH, DEPTH_HEIGHT,
    RS2_FORMAT_Z16, DEPTH_FPS);
}

RealSenseStream::~RealSenseStream() {
  if (started_) stop();
}

void RealSenseStream::start() {
  if (started_) return;

  rs2::pipeline_profile profile = pipe_.start(cfg_);

  rs2::device dev = profile.get_device();
  usbType_ = dev.supports(RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR)
    ? dev.get_info(RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR)
    : std::string{ "unknown" };

  auto colorProfile = profile.get_stream(RS2_STREAM_COLOR)
    .as<rs2::video_stream_profile>();
  colorWidth_ = colorProfile.width();
  colorHeight_ = colorProfile.height();
  alignedDepthIntr_ = colorProfile.get_intrinsics();

  auto depthProfile = profile.get_stream(RS2_STREAM_DEPTH)
    .as<rs2::video_stream_profile>();
  depthWidth_ = depthProfile.width();
  depthHeight_ = depthProfile.height();

  started_ = true;
}

void RealSenseStream::stop() {
  if (!started_) return;
  pipe_.stop();
  started_ = false;
}

bool RealSenseStream::poll(cv::Mat& colorBgrOut,
  cv::Mat& depthMm16uOut,
  cv::Mat& depthVizBgrOut) {
  if (!started_) return false;

  rs2::frameset fs;
  if (!pipe_.poll_for_frames(&fs)) return false;

  // Align depth onto color so (u, v) indexes both identically.
  rs2::frameset aligned = align_.process(fs);
  rs2::video_frame color = aligned.get_color_frame();
  rs2::depth_frame depth = aligned.get_depth_frame();
  if (!color || !depth) return false;

  {
    const int w = color.get_width();
    const int h = color.get_height();
    cv::Mat wrapped(h, w, CV_8UC3, const_cast<void*>(color.get_data()),
      cv::Mat::AUTO_STEP);
    colorBgrOut = wrapped.clone();
  }

  {
    const int w = depth.get_width();
    const int h = depth.get_height();
    cv::Mat wrapped(h, w, CV_16UC1, const_cast<void*>(depth.get_data()),
      cv::Mat::AUTO_STEP);
    depthMm16uOut = wrapped.clone();
  }

  // Custom colorization: close = warm (red/yellow), far = cool (blue).
  // We invert the depth before running COLORMAP_JET so the default
  // rs2::colorizer direction (close=blue, far=red) is reversed. Invalid
  // (zero) pixels are forced to black so they stand out from valid depth.
  {
    constexpr float kMinMm = 300.0f;   // 0.30 m
    constexpr float kMaxMm = 4000.0f;  // 4.00 m
    cv::Mat mm32;
    depthMm16uOut.convertTo(mm32, CV_32FC1);
    cv::Mat valid = depthMm16uOut > 0;
    cv::Mat clamped;
    cv::max(mm32, kMinMm, clamped);
    cv::min(clamped, kMaxMm, clamped);
    cv::Mat inv;
    cv::subtract(cv::Scalar(kMaxMm), clamped, inv);
    cv::Mat u8;
    inv.convertTo(u8, CV_8UC1, 255.0 / (kMaxMm - kMinMm));
    cv::applyColorMap(u8, depthVizBgrOut, cv::COLORMAP_JET);
    depthVizBgrOut.setTo(cv::Scalar(0, 0, 0), ~valid);
  }

  return true;
}
