/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  gl_textures.h
  OpenGL texture helpers for displaying cv::Mat frames inside ImGui.
  All helpers require a current GL context and only use GL 1.1 symbols,
  so no loader (GLAD / GLEW) is needed in Phase 1.
*/

#pragma once

#include <opencv2/core.hpp>

using GLuint = unsigned int;

// Allocate a GL_TEXTURE_2D with linear filtering and clamp-to-edge wrap.
GLuint createTexture();

// Upload an 8UC3 BGR cv::Mat to the given texture. Converts BGR->RGB
// internally. Single-channel callers should expand to BGR first.
void updateTexture(GLuint texId, const cv::Mat& bgr);
