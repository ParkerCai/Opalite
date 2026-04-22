/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  gl_textures.cpp
  cv::Mat -> OpenGL texture upload. Pattern adapted from Project 3's
  matToTexture but allocates the GL handle once and re-uploads via
  glTexImage2D each frame so a 30 Hz capture loop does not leak handles.
  Stays inside GL 1.1 so Windows opengl32 exports it all without a loader.
*/

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

// GL 1.2 enums - Windows's <GL/gl.h> stops at GL 1.1 so we define them locally.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif

#include <opencv2/imgproc.hpp>

#include "gl_textures.h"

GLuint createTexture() {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

void updateTexture(GLuint texId, const cv::Mat& bgr) {
  if (bgr.empty() || bgr.channels() != 3) return;

  // Upload with format=GL_BGR so the GPU does the byte swap; saves a CPU
  // cvtColor pass on every frame in the 30 Hz capture loop.
  glBindTexture(GL_TEXTURE_2D, texId);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
    bgr.cols, bgr.rows, 0,
    GL_BGR, GL_UNSIGNED_BYTE, bgr.data);
}
