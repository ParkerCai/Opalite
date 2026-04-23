/*
  Parker Cai
  April, 2026
  Final Project: Opalite
  CS5330 - Pattern Recognition & Computer Vision

  jni_bridge.cpp
  Phase 3 edge-device bridge. Exposes three capability groups to
  com.opalite.edge.MainActivity so the Java capture loop can reuse the
  same free-space, sonar, and Brain C++ that the Windows app runs:

    - analyzeFreeSpaceNative: one JNI call per depth frame. Fills a
      pre-allocated float[8] with per-sector clearance scores, validity
      flags, center near-depth, and the suggested direction index.

    - sonar{Start,Stop,Update,SetVolume,SetEnabled}Native: lifecycle +
      per-frame target updates for the miniaudio-driven spatial-audio
      layer. One process-global Sonar instance.

    - askBrainNative: synchronous POST to Ollama with a JPEG-encoded
      color frame. Caller (Java side) must invoke from a worker thread.
*/

#include "free_space.h"
#include "sonar.h"
#include "brain_client.h"

#include <jni.h>
#include <android/log.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define OPALITE_LOG_TAG "opalite-edge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  OPALITE_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  OPALITE_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, OPALITE_LOG_TAG, __VA_ARGS__)

namespace {

  // One process-wide sonar. Created on first start, destroyed only at
  // library unload. Guarded so JNI lifecycle races (Activity stop during
  // a pending start) don't double-init miniaudio.
  std::unique_ptr<Sonar> g_sonar;
  std::mutex g_sonarMtx;

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* /*vm*/, void* /*reserved*/) {
  LOGI("libopaliteedge loaded");
  return JNI_VERSION_1_6;
}

// ---------------------------------------------------------------------------
// Free-space analysis
// ---------------------------------------------------------------------------

// Signature: void analyzeFreeSpaceNative(short[] depth, int w, int h,
//   float blockedThresholdM, float clearHorizonM, float[] out8)
//
// out8 layout (pre-allocated by caller, one per Activity):
//   [0] leftScore     (0..1; 0 blocked, 1 clear at horizon)
//   [1] centerScore
//   [2] rightScore
//   [3] leftValid     (0.0 / 1.0; false when insufficient depth samples)
//   [4] centerValid
//   [5] rightValid
//   [6] centerNearDepthM  (0 when center sector invalid)
//   [7] suggestedDir      (0=Left, 1=Center, 2=Right)
JNIEXPORT void JNICALL
Java_com_opalite_edge_MainActivity_analyzeFreeSpaceNative(
    JNIEnv* env, jclass /*clazz*/,
    jshortArray depth, jint w, jint h,
    jfloat blockedThresholdM, jfloat clearHorizonM,
    jfloatArray out8) {

  if (depth == nullptr || out8 == nullptr) return;
  if (w <= 0 || h <= 0) return;

  const jsize depthLen = env->GetArrayLength(depth);
  if (depthLen < static_cast<jsize>(w) * h) {
    LOGW("depth array too small: %d < %d*%d", depthLen, w, h);
    return;
  }

  // GetShortArrayRegion copies into a local buffer; cheaper than
  // GetShortArrayElements on large arrays because it skips the
  // "critical section" bookkeeping and pairs a single memcpy with
  // no need for a matching Release*Elements call.
  std::vector<int16_t> buf(static_cast<size_t>(w) * h);
  env->GetShortArrayRegion(depth, 0, w * h,
    reinterpret_cast<jshort*>(buf.data()));

  FreeSpaceConfig cfg;
  cfg.blockedThresholdM = blockedThresholdM;
  cfg.clearHorizonM = clearHorizonM;

  // Java shorts are signed; RealSense depth is unsigned mm, so reinterpret.
  const FreeSpaceResult fs = analyzeForwardPath(
    reinterpret_cast<const uint16_t*>(buf.data()), w, h, cfg);

  float out[8];
  out[0] = fs.left.score;
  out[1] = fs.center.score;
  out[2] = fs.right.score;
  out[3] = (fs.left.validPixels   >= cfg.minValidPixels) ? 1.0f : 0.0f;
  out[4] = (fs.center.validPixels >= cfg.minValidPixels) ? 1.0f : 0.0f;
  out[5] = (fs.right.validPixels  >= cfg.minValidPixels) ? 1.0f : 0.0f;
  out[6] = fs.center.nearDepthM;
  out[7] = static_cast<float>(static_cast<int>(fs.suggested));

  env->SetFloatArrayRegion(out8, 0, 8, out);
}

// ---------------------------------------------------------------------------
// Sonar lifecycle + per-frame targets
// ---------------------------------------------------------------------------

JNIEXPORT jboolean JNICALL
Java_com_opalite_edge_MainActivity_sonarStartNative(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  std::lock_guard<std::mutex> lock(g_sonarMtx);
  if (!g_sonar) g_sonar = std::make_unique<Sonar>();
  const bool ok = g_sonar->start();
  if (!ok) LOGE("Sonar::start failed");
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_opalite_edge_MainActivity_sonarStopNative(
    JNIEnv* /*env*/, jclass /*clazz*/) {
  std::lock_guard<std::mutex> lock(g_sonarMtx);
  if (g_sonar) g_sonar->stop();
}

JNIEXPORT void JNICALL
Java_com_opalite_edge_MainActivity_sonarSetVolumeNative(
    JNIEnv* /*env*/, jclass /*clazz*/, jfloat v01) {
  std::lock_guard<std::mutex> lock(g_sonarMtx);
  if (g_sonar) g_sonar->setVolume(v01);
}

JNIEXPORT void JNICALL
Java_com_opalite_edge_MainActivity_sonarSetEnabledNative(
    JNIEnv* /*env*/, jclass /*clazz*/, jboolean enabled) {
  std::lock_guard<std::mutex> lock(g_sonarMtx);
  if (g_sonar) g_sonar->setEnabled(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_opalite_edge_MainActivity_sonarUpdateNative(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jfloat leftScore,  jboolean leftValid,
    jfloat centerScore, jboolean centerValid,
    jfloat rightScore, jboolean rightValid) {
  // Lock-free setter chain on the Sonar side; still take g_sonarMtx
  // briefly to guard the unique_ptr against concurrent stop().
  std::lock_guard<std::mutex> lock(g_sonarMtx);
  if (!g_sonar) return;
  g_sonar->setLeft(leftScore,    leftValid   == JNI_TRUE);
  g_sonar->setCenter(centerScore, centerValid == JNI_TRUE);
  g_sonar->setRight(rightScore,   rightValid  == JNI_TRUE);
}

// ---------------------------------------------------------------------------
// Brain: synchronous POST to Ollama with a JPEG-encoded color frame.
// ---------------------------------------------------------------------------

// Signature: String askBrainNative(byte[] jpegBytes, String prompt, String host)
// Returns the Brain text response on success, or "ERROR: ..." on failure.
// Caller MUST invoke this on a worker thread (the UI thread will hang
// for the duration of the round-trip, ~500-2000 ms).
JNIEXPORT jstring JNICALL
Java_com_opalite_edge_MainActivity_askBrainNative(
    JNIEnv* env, jclass /*clazz*/,
    jbyteArray jpegBytes, jstring prompt, jstring host) {

  if (prompt == nullptr || host == nullptr) {
    return env->NewStringUTF("ERROR: null prompt/host");
  }

  const char* promptStr = env->GetStringUTFChars(prompt, nullptr);
  const char* hostStr   = env->GetStringUTFChars(host,   nullptr);
  const std::string promptCpp = promptStr ? promptStr : "";
  const std::string hostCpp   = hostStr   ? hostStr   : "";
  env->ReleaseStringUTFChars(prompt, promptStr);
  env->ReleaseStringUTFChars(host,   hostStr);

  std::vector<uint8_t> jpeg;
  if (jpegBytes != nullptr) {
    const jsize n = env->GetArrayLength(jpegBytes);
    jpeg.resize(static_cast<size_t>(n));
    env->GetByteArrayRegion(jpegBytes, 0, n,
      reinterpret_cast<jbyte*>(jpeg.data()));
  }

  BrainConfig cfg;
  cfg.host = hostCpp;

  const BrainResponse r = askOllama(promptCpp, jpeg, cfg);
  if (!r.ok) {
    const std::string err = std::string("ERROR: ") + r.error;
    LOGW("askBrainNative failed: %s", r.error.c_str());
    return env->NewStringUTF(err.c_str());
  }
  LOGI("askBrainNative ok, %.0f ms, %zu chars",
    r.roundtripMs, r.text.size());
  return env->NewStringUTF(r.text.c_str());
}

}  // extern "C"
