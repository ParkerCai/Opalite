package com.opalite.edge;

import android.Manifest;
import android.app.Activity;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.text.Editable;
import android.text.TextWatcher;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.speech.tts.TextToSpeech;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import com.intel.realsense.librealsense.Config;
import com.intel.realsense.librealsense.DepthFrame;
import com.intel.realsense.librealsense.Extension;
import com.intel.realsense.librealsense.Frame;
import com.intel.realsense.librealsense.FrameSet;
import com.intel.realsense.librealsense.Pipeline;
import com.intel.realsense.librealsense.PipelineProfile;
import com.intel.realsense.librealsense.RsContext;
import com.intel.realsense.librealsense.StreamFormat;
import com.intel.realsense.librealsense.StreamType;
import com.intel.realsense.librealsense.VideoFrame;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/*
  Phase 3 edge-device UI. Opens the librealsense Java pipeline against a
  D435i plugged in over USB-OTG and pumps every depth frame through the
  NDK side (analyzeFreeSpaceNative) which drives the sonar audio layer
  and TTS threshold alerts. A Describe button JPEG-encodes the latest
  color frame and fires askBrainNative on a worker thread.
*/
public class MainActivity extends Activity {

    private static final String TAG = "opalite-edge";
    private static final int CAMERA_PERMISSION_REQUEST = 1001;
    private static final int STREAM_W = 640;
    private static final int STREAM_H = 480;
    private static final int STREAM_FPS = 30;
    private static final int FRAME_TIMEOUT_MS = 5000;
    private static final long ANNOUNCE_COOLDOWN_MS = 3000;
    private static final float BLOCKED_THRESHOLD_M = 0.8f;
    private static final float CLEAR_HORIZON_M = 3.0f;
    private static final String PREF_HOST = "brain_host";

    // --- JNI surface -------------------------------------------------------

    static {
        System.loadLibrary("opaliteedge");
    }

    private static native void analyzeFreeSpaceNative(short[] depth, int w, int h,
        float blockedThresholdM, float clearHorizonM, float[] out8);

    private static native boolean sonarStartNative();
    private static native void sonarStopNative();
    private static native void sonarSetVolumeNative(float v01);
    private static native void sonarSetEnabledNative(boolean enabled);
    private static native void sonarUpdateNative(
        float lScore, boolean lValid,
        float cScore, boolean cValid,
        float rScore, boolean rValid);

    private static native String askBrainNative(byte[] jpegBytes,
        String prompt, String host);

    // --- UI ----------------------------------------------------------------

    private TextView statusText;
    private TextView freeSpaceText;
    private TextView logText;
    private Button startButton;
    private Button stopButton;
    private Button describeButton;
    private Switch speakSwitch;
    private EditText hostEdit;

    // --- Capture / state ---------------------------------------------------

    private final ExecutorService captureExec = Executors.newSingleThreadExecutor();
    private final ExecutorService brainExec = Executors.newSingleThreadExecutor();
    private Future<?> captureFuture;
    private volatile boolean keepStreaming = false;

    // Pre-allocated buffers to avoid GC pressure in the frame loop.
    private final short[] depthShorts = new short[STREAM_W * STREAM_H];
    private final byte[] depthBytes = new byte[STREAM_W * STREAM_H * 2];
    private final byte[] rgbBytes = new byte[STREAM_W * STREAM_H * 3];
    private final float[] freeOut = new float[8];

    // Latest color frame snapshot for the Describe button. Swapped atomically
    // under lock because the capture thread writes and the UI thread reads.
    private final Object colorLock = new Object();
    private byte[] latestColorRgb = null;   // size = w*h*3 when valid
    private int latestColorW = 0;
    private int latestColorH = 0;

    // TTS + announcement cooldown.
    private TextToSpeech tts;
    private volatile boolean ttsReady = false;
    private long lastAnnouncementAtMs = 0;
    private boolean lastWasBlocked = false;
    private boolean brainInFlight = false;

    private RsContext rsContext;

    // Rolling CSV of Brain round-trip latencies. Written to the app's
    // external files dir so it's reachable with `adb pull` without any
    // runtime storage permissions. Columns match the desktop build's
    // data/brain_latency.csv so the report can merge both datasets.
    private PrintWriter brainLatencyCsv;
    private double brainLatencyMedianMs = 0.0;
    private final java.util.ArrayList<Double> brainLatencyWindow = new java.util.ArrayList<>(32);

    private final Handler uiHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        statusText = findViewById(R.id.statusText);
        freeSpaceText = findViewById(R.id.freeSpaceText);
        logText = findViewById(R.id.logText);
        startButton = findViewById(R.id.startButton);
        stopButton = findViewById(R.id.stopButton);
        describeButton = findViewById(R.id.describeButton);
        speakSwitch = findViewById(R.id.speakSwitch);
        hostEdit = findViewById(R.id.hostEdit);

        startButton.setOnClickListener(v -> onStartClick());
        stopButton.setOnClickListener(v -> onStopClick());
        describeButton.setOnClickListener(v -> onDescribeClick());

        // Restore the last Brain host the user typed; persist every edit.
        // Falls back to whatever's baked into the layout XML on first launch.
        final SharedPreferences prefs = getPreferences(MODE_PRIVATE);
        final String savedHost = prefs.getString(PREF_HOST, null);
        if (savedHost != null && !savedHost.isEmpty()) {
            hostEdit.setText(savedHost);
        }
        hostEdit.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override
            public void afterTextChanged(Editable s) {
                prefs.edit().putString(PREF_HOST, s.toString()).apply();
            }
        });

        SeekBar vol = findViewById(R.id.volumeSeek);
        vol.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int p, boolean fromUser) {
                sonarSetVolumeNative(p / 100.0f);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        // Required by librealsense's JNI to resolve classloader lookups.
        RsContext.init(getApplicationContext());
        rsContext = new RsContext();

        openBrainLatencyCsv();

        tts = new TextToSpeech(getApplicationContext(), status -> {
            if (status == TextToSpeech.SUCCESS) {
                tts.setLanguage(Locale.US);
                ttsReady = true;
                log("TTS ready");
            } else {
                log("TTS init failed: " + status);
            }
        });

        maybeRequestCameraPermission();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!sonarStartNative()) {
            log("sonar start failed");
        } else {
            sonarSetVolumeNative(0.25f);
            log("sonar running");
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        sonarStopNative();
    }

    @Override
    protected void onDestroy() {
        stopStreaming();
        captureExec.shutdownNow();
        brainExec.shutdownNow();
        if (tts != null) {
            tts.shutdown();
            tts = null;
        }
        if (rsContext != null) {
            rsContext.close();
            rsContext = null;
        }
        if (brainLatencyCsv != null) {
            brainLatencyCsv.close();
            brainLatencyCsv = null;
        }
        super.onDestroy();
    }

    // --- Brain latency logging ---------------------------------------------

    private void openBrainLatencyCsv() {
        try {
            final File dir = getExternalFilesDir(null);
            if (dir == null) {
                log("brain csv: no external files dir");
                return;
            }
            final File csv = new File(dir, "brain_latency.csv");
            final boolean needHeader = !csv.exists() || csv.length() == 0;
            brainLatencyCsv = new PrintWriter(new FileWriter(csv, true), true);
            if (needHeader) {
                // Column schema matches the desktop build's data/brain_latency.csv
                // so a report script can concat both datasets.
                brainLatencyCsv.println("wall_ms,roundtrip_ms,ok,mode");
            }
            log("brain csv: " + csv.getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "openBrainLatencyCsv failed", e);
            brainLatencyCsv = null;
        }
    }

    private void logBrainLatency(long roundtripMs, boolean ok) {
        // Rolling median across the last 30 queries (same window as the
        // desktop BrainPane's kLatencyCap = 30). Done on the brain worker
        // thread since that's where the Describe callback lives.
        synchronized (brainLatencyWindow) {
            brainLatencyWindow.add((double) roundtripMs);
            if (brainLatencyWindow.size() > 30) {
                brainLatencyWindow.remove(0);
            }
            final java.util.ArrayList<Double> sorted = new java.util.ArrayList<>(brainLatencyWindow);
            java.util.Collections.sort(sorted);
            final int n = sorted.size();
            brainLatencyMedianMs = (n % 2 == 1)
                ? sorted.get(n / 2)
                : 0.5 * (sorted.get(n / 2 - 1) + sorted.get(n / 2));
        }
        if (brainLatencyCsv != null) {
            brainLatencyCsv.printf(Locale.US, "%d,%d,%d,freeform%n",
                System.currentTimeMillis(), roundtripMs, ok ? 1 : 0);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == CAMERA_PERMISSION_REQUEST) {
            final boolean granted = grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            log(granted ? "camera permission granted" : "camera permission DENIED");
        }
    }

    private void maybeRequestCameraPermission() {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.CAMERA }, CAMERA_PERMISSION_REQUEST);
        }
    }

    private void onStartClick() {
        if (keepStreaming) return;
        keepStreaming = true;
        startButton.setEnabled(false);
        stopButton.setEnabled(true);
        describeButton.setEnabled(true);
        statusText.setText("Status: starting pipeline...");
        captureFuture = captureExec.submit(this::runCaptureLoop);
    }

    private void onStopClick() {
        stopStreaming();
    }

    private void stopStreaming() {
        keepStreaming = false;
        if (captureFuture != null) {
            captureFuture.cancel(true);
            captureFuture = null;
        }
        // Mute the sonar targets so the amp smoother ramps to silence
        // instead of holding the last-seen hum indefinitely. Sectors go
        // "invalid" so the audio callback treats them as no-data and
        // emits no signal regardless of the last score.
        sonarUpdateNative(0.0f, false, 0.0f, false, 0.0f, false);
        uiHandler.post(() -> {
            startButton.setEnabled(true);
            stopButton.setEnabled(false);
            describeButton.setEnabled(false);
            statusText.setText("Status: stopped");
        });
    }

    // --- Capture loop ------------------------------------------------------

    private void runCaptureLoop() {
        try (Pipeline pipe = new Pipeline(); Config cfg = new Config()) {
            cfg.enableStream(StreamType.DEPTH, -1, STREAM_W, STREAM_H, StreamFormat.Z16, STREAM_FPS);
            cfg.enableStream(StreamType.COLOR, -1, STREAM_W, STREAM_H, StreamFormat.RGB8, STREAM_FPS);

            try (PipelineProfile profile = pipe.start(cfg)) {
                uiHandler.post(() -> statusText.setText("Status: streaming "
                    + STREAM_W + "x" + STREAM_H + " @ " + STREAM_FPS + " fps"));

                long frameCount = 0;
                long lastLogMs = SystemClock.elapsedRealtime();

                while (keepStreaming && !Thread.currentThread().isInterrupted()) {
                    try (FrameSet frames = pipe.waitForFrames(FRAME_TIMEOUT_MS)) {
                        Frame depthF = frames.first(StreamType.DEPTH);
                        Frame colorF = frames.first(StreamType.COLOR);
                        try {
                            if (depthF != null) {
                                processDepthFrame(depthF);
                            }
                            if (colorF != null) {
                                snapshotColorFrame(colorF);
                            }
                        } finally {
                            if (depthF != null) depthF.close();
                            if (colorF != null) colorF.close();
                        }
                    }

                    frameCount++;
                    final long nowMs = SystemClock.elapsedRealtime();
                    if (nowMs - lastLogMs >= 2000) {
                        final long fc = frameCount;
                        final double fps = frameCount * 1000.0 / (nowMs - lastLogMs);
                        uiHandler.post(() -> statusText.setText(
                            String.format(Locale.US, "Status: streaming, %d frames, %.1f fps", fc, fps)));
                        lastLogMs = nowMs;
                        frameCount = 0;
                    }
                }
            } finally {
                try { pipe.stop(); } catch (Exception ignored) {}
            }
        } catch (Exception e) {
            Log.e(TAG, "capture loop error", e);
            final String msg = e.getMessage() != null ? e.getMessage() : e.toString();
            uiHandler.post(() -> {
                statusText.setText("Status: error - " + msg);
                log("capture error: " + msg);
            });
        } finally {
            keepStreaming = false;
            uiHandler.post(() -> {
                startButton.setEnabled(true);
                stopButton.setEnabled(false);
                describeButton.setEnabled(false);
            });
        }
    }

    private void processDepthFrame(Frame depthF) {
        DepthFrame depth = depthF.as(Extension.DEPTH_FRAME);
        if (depth == null) return;
        final int w = depth.getWidth();
        final int h = depth.getHeight();
        if (w * h * 2 > depthBytes.length) return;  // buffer too small

        depth.getData(depthBytes);
        // Z16 is little-endian unsigned 16 on all ARM Android targets.
        ByteBuffer.wrap(depthBytes, 0, w * h * 2)
            .order(ByteOrder.LITTLE_ENDIAN)
            .asShortBuffer()
            .get(depthShorts, 0, w * h);

        analyzeFreeSpaceNative(depthShorts, w, h,
            BLOCKED_THRESHOLD_M, CLEAR_HORIZON_M, freeOut);

        final float lScore = freeOut[0];
        final float cScore = freeOut[1];
        final float rScore = freeOut[2];
        final boolean lValid = freeOut[3] > 0.5f;
        final boolean cValid = freeOut[4] > 0.5f;
        final boolean rValid = freeOut[5] > 0.5f;
        final float cNearM = freeOut[6];
        final int sugg = (int) freeOut[7];

        sonarUpdateNative(lScore, lValid, cScore, cValid, rScore, rValid);

        // Clear / BLOCKED cooldown-gated TTS. Speak only when we cross
        // from clear -> blocked AND enough time has elapsed since the
        // last announcement, so we don't yell continuously.
        final boolean blockedNow = cValid && cNearM > 0.0f && cNearM < BLOCKED_THRESHOLD_M;
        final long nowMs = SystemClock.elapsedRealtime();
        if (blockedNow && !lastWasBlocked
                && nowMs - lastAnnouncementAtMs >= ANNOUNCE_COOLDOWN_MS) {
            lastAnnouncementAtMs = nowMs;
            final String line = String.format(Locale.US, "Blocked at %.1f meters", cNearM);
            maybeSpeak(line);
        }
        lastWasBlocked = blockedNow;

        final String dirName = sugg == 0 ? "LEFT" : sugg == 2 ? "RIGHT" : "CENTER";
        uiHandler.post(() -> freeSpaceText.setText(String.format(Locale.US,
            "L %.2f  C %.2f  R %.2f    dir %s    fwd %.2fm",
            lScore, cScore, rScore, dirName, cNearM)));
    }

    private void snapshotColorFrame(Frame colorF) {
        VideoFrame color = colorF.as(Extension.VIDEO_FRAME);
        if (color == null) return;
        final int w = color.getWidth();
        final int h = color.getHeight();
        if (w * h * 3 > rgbBytes.length) return;

        color.getData(rgbBytes);
        synchronized (colorLock) {
            if (latestColorRgb == null || latestColorRgb.length != w * h * 3) {
                latestColorRgb = new byte[w * h * 3];
            }
            System.arraycopy(rgbBytes, 0, latestColorRgb, 0, w * h * 3);
            latestColorW = w;
            latestColorH = h;
        }
    }

    // --- Brain / Describe --------------------------------------------------

    private void onDescribeClick() {
        if (brainInFlight) {
            log("brain: request already in flight");
            return;
        }
        final byte[] jpeg;
        synchronized (colorLock) {
            if (latestColorRgb == null) {
                log("brain: no color frame yet");
                return;
            }
            jpeg = rgbToJpeg(latestColorRgb, latestColorW, latestColorH, 70);
        }
        if (jpeg == null) {
            log("brain: JPEG encode failed");
            return;
        }
        final String host = hostEdit.getText().toString().trim();
        // Keep the sentence object-first. Explicitly forbid Gemma's default
        // "the camera is..." framing, which adds no information for a
        // visually-impaired listener.
        final String prompt =
            "You are narrating for a visually impaired user. Name the main "
            + "object or scene ahead in one short sentence, starting with "
            + "the object itself (e.g. \"A sofa with a blue throw pillow.\"). "
            + "Do not mention \"the camera\", \"the image\", \"the photo\", "
            + "or \"in front of\". Do not describe what the camera sees; "
            + "describe what is there.";
        log("brain: requesting (" + jpeg.length + " B jpeg) -> " + host);
        brainInFlight = true;
        describeButton.setEnabled(false);
        brainExec.submit(() -> {
            final long t0 = SystemClock.elapsedRealtime();
            final String resp = askBrainNative(jpeg, prompt, host);
            final long dtMs = SystemClock.elapsedRealtime() - t0;
            final boolean ok = resp != null && !resp.startsWith("ERROR:");
            logBrainLatency(dtMs, ok);
            uiHandler.post(() -> {
                brainInFlight = false;
                describeButton.setEnabled(keepStreaming);
                log("brain: " + dtMs + " ms (median "
                    + String.format(Locale.US, "%.0f", brainLatencyMedianMs)
                    + " ms) - " + resp);
                if (ok) {
                    maybeSpeak(resp);
                }
            });
        });
    }

    // BGR would match the Windows build, but the pipeline is configured for
    // RGB8 (matches the original test app), so encode RGB here. Gemma doesn't
    // care about channel order at JPEG quality 70.
    private static byte[] rgbToJpeg(byte[] rgb, int w, int h, int quality) {
        try {
            int[] pixels = new int[w * h];
            for (int i = 0, p = 0; i < pixels.length; i++, p += 3) {
                final int r = rgb[p]     & 0xFF;
                final int g = rgb[p + 1] & 0xFF;
                final int b = rgb[p + 2] & 0xFF;
                pixels[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
            Bitmap bmp = Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888);
            ByteArrayOutputStream out = new ByteArrayOutputStream(64 * 1024);
            bmp.compress(Bitmap.CompressFormat.JPEG, quality, out);
            bmp.recycle();
            return out.toByteArray();
        } catch (Exception e) {
            Log.e(TAG, "rgbToJpeg failed", e);
            return null;
        }
    }

    private void maybeSpeak(String text) {
        if (!ttsReady || tts == null) return;
        if (speakSwitch == null || !speakSwitch.isChecked()) return;
        tts.speak(text, TextToSpeech.QUEUE_FLUSH, null, "utt-" + SystemClock.elapsedRealtime());
    }

    private void log(String line) {
        Log.i(TAG, line);
        uiHandler.post(() -> {
            final CharSequence prev = logText.getText();
            logText.setText(prev + "\n" + line);
        });
    }
}
