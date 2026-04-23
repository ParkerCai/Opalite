/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  main.cpp
  GLFW + Dear ImGui shell on an OpenGL 3.3 core context, hosting the
  Opalite preview panes. Step 4 adds a live RealSense D435i color stream
  into a "Color" ImGui window next to the Controls panel.
*/

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <librealsense2/rs.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <print>
#include <string>

#include "free_space.h"
#include "gl_textures.h"
#include "realsense_stream.h"
#include "sonar.h"
#include "topdown.h"

#include <librealsense2/rsutil.h>
#include <opencv2/imgproc.hpp>

namespace {

  constexpr const char* GLSL_VERSION = "#version 330 core";

  void glfwErrorCallback(int err, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", err, desc);
  }

  // Exponential moving average of the camera's effective frame rate.
  struct FpsMeter {
    double lastMs = 0.0;
    double ewma = 0.0;

    void tick(double nowMs) {
      if (lastMs > 0.0) {
        const double dt = nowMs - lastMs;
        if (dt > 0.0) {
          const double inst = 1000.0 / dt;
          ewma = (ewma == 0.0) ? inst : 0.9 * ewma + 0.1 * inst;
        }
      }
      lastMs = nowMs;
    }
  };

  // Ring buffer of the last N per-frame pipeline latencies (time from
  // poll-success to end-of-render in milliseconds). Sorted copies give
  // median / p95 for the HUD; raw samples stream into data/latency.csv.
  struct LatencyMeter {
    static constexpr int kCapacity = 120;
    std::array<double, kCapacity> buf{};
    int head = 0;
    int count = 0;

    void push(double ms) {
      buf[head] = ms;
      head = (head + 1) % kCapacity;
      if (count < kCapacity) ++count;
    }

    double percentile(double p) const {
      if (count == 0) return 0.0;
      std::array<double, kCapacity> sorted{};
      std::copy_n(buf.begin(), count, sorted.begin());
      std::sort(sorted.begin(), sorted.begin() + count);
      int idx = static_cast<int>(p * (count - 1));
      idx = std::clamp(idx, 0, count - 1);
      return sorted[idx];
    }

    double minVal() const {
      if (count == 0) return 0.0;
      return *std::min_element(buf.begin(), buf.begin() + count);
    }
  };

}  // namespace

int main() {
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
    std::fprintf(stderr, "glfwInit failed\n");
    return 1;
  }

  // Match the OS content-scale so fonts and window chrome stay readable on
  // high-DPI displays (pattern lifted from Project 3 / or2d_gui.cpp).
  float xscale = 1.0f, yscale = 1.0f;
  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    glfwGetMonitorContentScale(mon, &xscale, &yscale);
  }
  const float uiScale = xscale;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  // Three stacked rows (Top-Down, Color+Depth, Controls) need enough
  // vertical room so the Controls pane isn't cropped on first launch.
  const int winInitW = static_cast<int>(1500.0f * uiScale);
  const int winInitH = static_cast<int>(1300.0f * uiScale);
  GLFWwindow* window = glfwCreateWindow(winInitW, winInitH, "Opalite",
    nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "glfwCreateWindow failed\n");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->AddFontDefault();
  io.FontGlobalScale = uiScale;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes(uiScale);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(GLSL_VERSION);

  RealSenseStream camera;
  try {
    camera.start();
  }
  catch (const rs2::error& e) {
    std::fprintf(stderr, "RealSense start failed: %s (%s)\n",
      e.what(), e.get_failed_function().c_str());
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  std::println("Opalite Phase 1 (step 5): camera color {}x{} depth {}x{}, USB={}",
    camera.colorWidth(), camera.colorHeight(),
    camera.depthWidth(), camera.depthHeight(), camera.usbType());

  const GLuint colorTex = createTexture();
  const GLuint depthTex = createTexture();
  const GLuint topdownTex = createTexture();
  cv::Mat colorBgr;
  cv::Mat depthMm16u;
  cv::Mat depthVizBgr;
  cv::Mat topdownBgr;
  FpsMeter cameraFps;
  double depthMinM = 0.0;
  double depthMaxM = 0.0;
  TopDownConfig topdownCfg;
  FreeSpaceConfig freeCfg;
  FreeSpaceResult freeResult;

  Sonar sonar;
  if (!sonar.start()) {
    std::fprintf(stderr, "Sonar: audio unavailable, continuing without feedback\n");
  }

  const std::filesystem::path saveDir = "data/saved_frames";
  {
    std::error_code ec;
    std::filesystem::create_directories(saveDir, ec);
    if (ec) {
      std::fprintf(stderr, "Warning: could not create %s: %s\n",
        saveDir.string().c_str(), ec.message().c_str());
    }
  }
  std::string lastSavedLabel;

  // Latency trace: pipeline time from capture-poll success to end-of-
  // render, logged per fresh frame. Header written only on first call.
  LatencyMeter latency;
  std::ofstream latencyCsv("data/latency.csv", std::ios::out | std::ios::trunc);
  if (latencyCsv) {
    latencyCsv << "wall_ms,latency_ms\n";
  } else {
    std::fprintf(stderr, "Warning: could not open data/latency.csv\n");
  }
  double pendingGrabMs = -1.0;  // -1 = no new frame to time this render

  bool shouldClose = false;

  while (!glfwWindowShouldClose(window) && !shouldClose) {
    glfwPollEvents();

    // Q or ESC closes the window (works from anywhere in the app since
    // GLFW key state is checked here rather than routed through ImGui).
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ||
      glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      shouldClose = true;
    }

    // M toggles sonar mute. Edge-detect so holding the key doesn't
    // flip-flop the state every frame.
    static bool mWasDown = false;
    const bool mDown = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
    if (mDown && !mWasDown) sonar.setEnabled(!sonar.isEnabled());
    mWasDown = mDown;

    if (camera.poll(colorBgr, depthMm16u, depthVizBgr)) {
      pendingGrabMs = glfwGetTime() * 1000.0;
      cameraFps.tick(pendingGrabMs);

      // Min / max over valid (non-zero) depth pixels, converted to metres.
      cv::Mat validMask = depthMm16u > 0;
      if (cv::countNonZero(validMask) > 0) {
        double dminMm = 0.0, dmaxMm = 0.0;
        cv::minMaxLoc(depthMm16u, &dminMm, &dmaxMm, nullptr, nullptr, validMask);
        depthMinM = dminMm * 0.001;
        depthMaxM = dmaxMm * 0.001;
      }

      topdownBgr = buildTopDown(depthMm16u, camera.depthIntrinsics(), topdownCfg);

      freeResult = freeCfg.enabled
        ? analyzeForwardPath(depthMm16u, freeCfg)
        : FreeSpaceResult{};

      // Feed all three sectors' current clearances to the sonar layer.
      // Insufficient-support sectors are silenced at the source.
      const int minValid = freeCfg.minValidPixels;
      sonar.setLeft  (freeResult.left.score,
                      freeResult.left.validPixels   >= minValid);
      sonar.setCenter(freeResult.center.score,
                      freeResult.center.validPixels >= minValid);
      sonar.setRight (freeResult.right.score,
                      freeResult.right.validPixels  >= minValid);

      // Clearance -> BGR: interpolate red (blocked) through yellow to
      // green (clear). Cheap, communicates severity without a colormap.
      auto clearanceColor = [](const SectorClearance& sec) -> cv::Scalar {
        const float s = sec.score;  // 0..1
        const float r = (s < 0.5f) ? 255.0f : (1.0f - s) * 2.0f * 255.0f;
        const float g = (s < 0.5f) ? s * 2.0f * 255.0f : 255.0f;
        return cv::Scalar(0.0, g, r);  // BGR
      };

      if (freeCfg.enabled) {
        const int thickness = std::max(2, static_cast<int>(2 * uiScale));
        const SectorClearance* sectors[3] = {
          &freeResult.left, &freeResult.center, &freeResult.right
        };

        for (const SectorClearance* sec : sectors) {
          cv::rectangle(colorBgr, sec->roi, clearanceColor(*sec), thickness);
        }

        // Label only the center sector with the distance (readable).
        if (freeResult.center.nearDepthM > 0.0f) {
          char label[64];
          std::snprintf(label, sizeof(label), "fwd %.2f m",
            freeResult.center.nearDepthM);
          const cv::Point org(freeResult.center.roi.x,
            std::max(20, freeResult.center.roi.y - 10));
          cv::putText(colorBgr, label, org, cv::FONT_HERSHEY_SIMPLEX,
            0.8, clearanceColor(freeResult.center), 2, cv::LINE_AA);
        }

        // Three sector wedges on the top-down map - same color scheme.
        if (!topdownBgr.empty()) {
          const rs2_intrinsics& intr = camera.depthIntrinsics();
          const int srcH = depthMm16u.rows;
          const int vCenter = srcH / 2;
          const int tdW = topdownBgr.cols;
          const int tdH = topdownBgr.rows;
          const float invCell = 1.0f / topdownCfg.cellM;
          auto toCell = [&](float X, float Z) -> cv::Point {
            int col = static_cast<int>((X + topdownCfg.extentM) * invCell);
            int row = (tdH - 1) - static_cast<int>(Z * invCell);
            col = std::clamp(col, 0, tdW - 1);
            row = std::clamp(row, 0, tdH - 1);
            return cv::Point(col, row);
          };
          const cv::Point cam(tdW / 2, tdH - 1);

          for (const SectorClearance* sec : sectors) {
            const cv::Scalar color = clearanceColor(*sec);
            const int uLeft = sec->roi.x;
            const int uRight = sec->roi.x + sec->roi.width - 1;
            const float wedgeZ = std::min(
              std::max(sec->nearDepthM, freeCfg.blockedThresholdM),
              topdownCfg.extentM);
            float pixL[2] = { static_cast<float>(uLeft),
                              static_cast<float>(vCenter) };
            float pixR[2] = { static_cast<float>(uRight),
                              static_cast<float>(vCenter) };
            float ptL[3] = { 0, 0, 0 };
            float ptR[3] = { 0, 0, 0 };
            rs2_deproject_pixel_to_point(ptL, &intr, pixL, wedgeZ);
            rs2_deproject_pixel_to_point(ptR, &intr, pixR, wedgeZ);
            const cv::Point l = toCell(ptL[0], ptL[2]);
            const cv::Point r = toCell(ptR[0], ptR[2]);
            cv::line(topdownBgr, cam, l, color, 1);
            cv::line(topdownBgr, cam, r, color, 1);
            cv::line(topdownBgr, l, r, color, 1);
          }
        }
      }

      updateTexture(colorTex, colorBgr);
      updateTexture(depthTex, depthVizBgr);
      updateTexture(topdownTex, topdownBgr);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Fixed three-pane layout: Color | Depth across the top, Controls
    // spanning the bottom. Recomputed every frame so it follows window
    // resizes without needing ImGui docking.
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    const float winW = static_cast<float>(fbw);
    const float winH = static_cast<float>(fbh);
    const float halfW = winW * 0.5f;

    // Layout: Top-Down on top (full width), Color + Depth in the middle
    // row (half width each), Controls on the bottom spanning full width.
    const float srcW = (camera.colorWidth() > 0)
      ? static_cast<float>(camera.colorWidth()) : 16.0f;
    const float srcH = (camera.colorHeight() > 0)
      ? static_cast<float>(camera.colorHeight()) : 9.0f;
    const float tdW = (topdownBgr.cols > 0)
      ? static_cast<float>(topdownBgr.cols) : 2.0f;
    const float tdH = (topdownBgr.rows > 0)
      ? static_cast<float>(topdownBgr.rows) : 1.0f;
    const float paneChromeH = 36.0f * uiScale;
    const float minControlsH = 200.0f * uiScale;

    const float middleRowH = halfW * (srcH / srcW) + paneChromeH;
    // Top-down shares the Color/Depth row height and stretches the
    // occupancy map to the full pane width (user prefers a wide banner
    // over an aspect-fit image with dead space on the sides).
    const float topdownH = middleRowH;
    const float middleTop = topdownH;
    const float controlsTop = middleTop + middleRowH;
    const float controlsH = std::max(minControlsH, winH - controlsTop);
    (void)tdW; (void)tdH;  // unused now that we stretch the top-down image
    constexpr ImGuiWindowFlags kFixedFlags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    const auto colorHandle = reinterpret_cast<ImTextureID>(
      static_cast<intptr_t>(colorTex));
    const auto depthHandle = reinterpret_cast<ImTextureID>(
      static_cast<intptr_t>(depthTex));
    const auto topdownHandle = reinterpret_cast<ImTextureID>(
      static_cast<intptr_t>(topdownTex));

    auto fitImageSize = [](float srcW, float srcH, ImVec2 avail) -> ImVec2 {
      if (srcW <= 0.0f || srcH <= 0.0f) return ImVec2(0.0f, 0.0f);
      const float srcAspect = srcW / srcH;
      const float availAspect = avail.x / avail.y;
      if (availAspect > srcAspect) {
        return ImVec2(avail.y * srcAspect, avail.y);
      }
      return ImVec2(avail.x, avail.x / srcAspect);
      };

    // Top-Down pane (full-width top row). Image stretches to fill the
    // entire content region - aspect is intentionally ignored so the
    // occupancy map reads as a banner across the top.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, topdownH), ImGuiCond_Always);
    ImGui::Begin("Top-Down", nullptr, kFixedFlags);
    if (!topdownBgr.empty()) {
      ImGui::Image(topdownHandle, ImGui::GetContentRegionAvail());
    }
    else {
      ImGui::Text("waiting for top-down...");
    }
    ImGui::End();

    // Color pane (middle-left).
    ImGui::SetNextWindowPos(ImVec2(0.0f, middleTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(halfW, middleRowH), ImGuiCond_Always);
    ImGui::Begin("Color", nullptr, kFixedFlags);
    if (camera.colorWidth() > 0) {
      ImGui::Image(colorHandle,
        fitImageSize(static_cast<float>(camera.colorWidth()),
          static_cast<float>(camera.colorHeight()),
          ImGui::GetContentRegionAvail()));
    }
    else {
      ImGui::Text("waiting for first color frame...");
    }
    ImGui::End();

    // Depth pane (middle-right).
    ImGui::SetNextWindowPos(ImVec2(halfW, middleTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW - halfW, middleRowH), ImGuiCond_Always);
    ImGui::Begin("Depth", nullptr, kFixedFlags);
    if (camera.depthWidth() > 0) {
      ImGui::Image(depthHandle,
        fitImageSize(static_cast<float>(camera.depthWidth()),
          static_cast<float>(camera.depthHeight()),
          ImGui::GetContentRegionAvail()));
    }
    else {
      ImGui::Text("waiting for first depth frame...");
    }
    ImGui::End();

    // Controls, spanning the bottom.
    ImGui::SetNextWindowPos(ImVec2(0.0f, controlsTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, controlsH), ImGuiCond_Always);
    ImGui::Begin("Controls", nullptr, kFixedFlags);
    ImGui::Columns(3, "controls_cols", false);
    ImGui::Text("Render: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("Camera: %.1f FPS", cameraFps.ewma);
    ImGui::Text("Latency: med %.1f ms  p95 %.1f ms",
      latency.percentile(0.5), latency.percentile(0.95));
    ImGui::Text("USB:    %s", camera.usbType().c_str());
    if (ImGui::Button("Quit")) shouldClose = true;
    ImGui::SameLine();
    if (ImGui::Button("Save frame") && !colorBgr.empty() && !depthMm16u.empty()) {
      const auto now = std::chrono::system_clock::now();
      const std::time_t t = std::chrono::system_clock::to_time_t(now);
      std::tm tm{};
      localtime_s(&tm, &t);
      char stamp[32];
      std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
      const std::filesystem::path colorPath = saveDir / (std::string(stamp) + "_color.png");
      const std::filesystem::path depthPath = saveDir / (std::string(stamp) + "_depth.png");
      const bool okColor = cv::imwrite(colorPath.string(), colorBgr);
      const bool okDepth = cv::imwrite(depthPath.string(), depthMm16u);
      lastSavedLabel = (okColor && okDepth)
        ? ("Saved " + std::string(stamp))
        : "Save failed - see stderr";
      if (!okColor) std::fprintf(stderr, "Failed to write %s\n",
        colorPath.string().c_str());
      if (!okDepth) std::fprintf(stderr, "Failed to write %s\n",
        depthPath.string().c_str());
    }
    if (!lastSavedLabel.empty()) {
      ImGui::Text("%s", lastSavedLabel.c_str());
    }
    ImGui::NextColumn();
    ImGui::Text("Color:  %d x %d", camera.colorWidth(), camera.colorHeight());
    ImGui::Text("Depth:  %d x %d", camera.depthWidth(), camera.depthHeight());
    ImGui::Text("Range:  %.2f m - %.2f m", depthMinM, depthMaxM);
    ImGui::Separator();
    ImGui::Text("Free space");
    ImGui::Checkbox("enabled##free", &freeCfg.enabled);
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("blocked (m)", &freeCfg.blockedThresholdM,
      0.3f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("horizon (m)", &freeCfg.clearHorizonM,
      1.0f, 6.0f, "%.1f");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("span", &freeCfg.coneXFrac, 0.30f, 1.00f, "%.2f");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("beam", &freeCfg.centerBeamFrac, 0.05f, 0.50f, "%.2f");
    if (freeCfg.centerBeamFrac > freeCfg.coneXFrac - 0.04f) {
      freeCfg.centerBeamFrac = std::max(0.05f, freeCfg.coneXFrac - 0.04f);
    }
    if (freeCfg.enabled) {
      auto sectorText = [](const char* name, const SectorClearance& s) {
        const ImVec4 col = s.blocked
          ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
          : ImVec4(0.3f + 0.7f * (1.0f - s.score),
                   0.3f + 0.7f * s.score, 0.3f, 1.0f);
        ImGui::TextColored(col, "%s  %.2f m  score %.2f%s",
          name, s.nearDepthM, s.score, s.blocked ? "  [BLOCKED]" : "");
      };
      sectorText("L", freeResult.left);
      sectorText("C", freeResult.center);
      sectorText("R", freeResult.right);
      const char* dirName =
        freeResult.suggested == Direction::Left ? "LEFT"
        : freeResult.suggested == Direction::Right ? "RIGHT" : "CENTER";
      ImGui::Text("Forward: %.2f m   Suggested: %s",
        freeResult.nearestForwardM, dirName);
    } else {
      ImGui::TextDisabled("(analyzer off)");
    }
    ImGui::Separator();
    ImGui::Text("Sonar  (M to mute)");
    {
      bool sonarOn = sonar.isEnabled();
      if (ImGui::Checkbox("enabled##sonar", &sonarOn)) sonar.setEnabled(sonarOn);
      float sonarVol = sonar.volume();
      ImGui::SetNextItemWidth(200.0f * uiScale);
      if (ImGui::SliderFloat("volume", &sonarVol, 0.0f, 1.0f, "%.2f")) {
        sonar.setVolume(sonarVol);
      }
      float sonarHz = sonar.carrierHz();
      ImGui::SetNextItemWidth(200.0f * uiScale);
      if (ImGui::SliderFloat("pitch (Hz)", &sonarHz, 50.0f, 600.0f, "%.0f")) {
        sonar.setCarrierHz(sonarHz);
      }
      float sonarFalloff = sonar.falloffExponent();
      ImGui::SetNextItemWidth(200.0f * uiScale);
      if (ImGui::SliderFloat("falloff", &sonarFalloff, 2.0f, 8.0f, "%.1f")) {
        sonar.setFalloffExponent(sonarFalloff);
      }
      // Per-sector meters - read the audio thread's most recent smoothed
      // amplitudes so the bars match what's actually playing.
      const ImVec2 meterSize(140.0f * uiScale, 6.0f * uiScale);
      ImGui::Text("L");
      ImGui::SameLine();
      ImGui::ProgressBar(sonar.leftAmp(),  meterSize, "");
      ImGui::Text("C");
      ImGui::SameLine();
      ImGui::ProgressBar(sonar.centerAmp(), meterSize, "");
      ImGui::Text("R");
      ImGui::SameLine();
      ImGui::ProgressBar(sonar.rightAmp(), meterSize, "");
      ImGui::TextDisabled(sonar.isRunning() ? "(audio device up)" : "(no audio device)");
    }
    ImGui::NextColumn();
    ImGui::Text("Top-down");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("extent (m)", &topdownCfg.extentM, 2.0f, 10.0f, "%.1f");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("cell (m)", &topdownCfg.cellM, 0.01f, 0.10f, "%.3f");
    ImGui::SetNextItemWidth(200.0f * uiScale);
    ImGui::SliderFloat("min Z (m)", &topdownCfg.minZM, 0.10f, 1.00f, "%.2f");
    ImGui::Columns(1);
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

    // Close the pipeline latency measurement only on frames where we
    // actually had a new capture; idle passes through the loop aren't
    // useful data points.
    if (pendingGrabMs > 0.0) {
      const double endMs = glfwGetTime() * 1000.0;
      const double latMs = endMs - pendingGrabMs;
      latency.push(latMs);
      if (latencyCsv) {
        latencyCsv << std::fixed << std::setprecision(3)
          << endMs << "," << latMs << "\n";
      }
      pendingGrabMs = -1.0;
    }
  }

  const GLuint textures[] = { colorTex, depthTex, topdownTex };
  glDeleteTextures(3, textures);
  camera.stop();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
