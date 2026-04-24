package com.opalite.edge;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.text.Editable;
import android.text.TextWatcher;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import com.intel.realsense.librealsense.Align;
import com.intel.realsense.librealsense.Config;
import com.intel.realsense.librealsense.DepthFrame;
import com.intel.realsense.librealsense.DeviceListener;
import com.intel.realsense.librealsense.Extension;
import com.intel.realsense.librealsense.Frame;
import com.intel.realsense.librealsense.FrameSet;
import com.intel.realsense.librealsense.HoleFillingFilter;
import com.intel.realsense.librealsense.Intrinsic;
import com.intel.realsense.librealsense.Pipeline;
import com.intel.realsense.librealsense.PipelineProfile;
import com.intel.realsense.librealsense.RsContext;
import com.intel.realsense.librealsense.StreamFormat;
import com.intel.realsense.librealsense.StreamType;
import com.intel.realsense.librealsense.VideoFrame;
import com.intel.realsense.librealsense.VideoStreamProfile;

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
    private static final int PERMISSIONS_REQUEST = 1003;
    // 848x480 matches the Windows build (D435i's widest-FoV native 16:9
    // depth mode). Depth gets aligned into color space per frame via the
    // Align filter below, so the two panes are pixel-for-pixel matched.
    private static final int STREAM_W = 848;
    private static final int STREAM_H = 480;
    private static final int STREAM_FPS = 30;
    private static final int FRAME_TIMEOUT_MS = 5000;
    private static final long ANNOUNCE_COOLDOWN_MS = 3000;
    private static final float BLOCKED_THRESHOLD_M = 0.8f;
    private static final float CLEAR_HORIZON_M = 3.0f;
    private static final String PREF_HOST = "brain_host";
    private static final long LONG_PRESS_THRESHOLD_MS = 400;

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
    private static native void sonarGetAmpsNative(float[] out3);
    private static native void sonarSetCarrierHzNative(float hz);
    private static native void sonarSetFalloffNative(float n);

    private static native String askBrainNative(byte[] jpegBytes,
        String prompt, String host);

    // --- UI ----------------------------------------------------------------

    private TextView statusText;
    private TextView freeLText;
    private TextView freeCText;
    private TextView freeRText;
    private TextView freeDirText;
    private TextView freeFwdText;
    private TextView logText;
    private Button describeButton;
    private Button debugToggle;
    private android.widget.LinearLayout debugPanel;
    private Switch speakSwitch;
    private EditText hostEdit;
    private android.widget.ImageView colorImage;
    private android.widget.ImageView depthImage;
    private android.widget.ImageView topDownImage;
    private android.widget.ProgressBar leftMeter;
    private android.widget.ProgressBar centerMeter;
    private android.widget.ProgressBar rightMeter;
    private Switch sonarOnSwitch;
    private SeekBar pitchSeek;
    private SeekBar falloffSeek;
    private Vibrator vibrator;

    // Overlay drawing scratch.
    private final Paint roiPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textShadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint wedgeFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint wedgeStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    // Last free-space snapshot published by the capture thread, read by the
    // UI thread to color the ROI overlay. Lock is cheap.
    private final Object scoresLock = new Object();
    private final float[] lastScores = new float[3];    // L, C, R
    private final boolean[] lastValid = new boolean[3]; // L, C, R
    private final float[] lastNearM = new float[3];     // L, C, R (metres)

    // Camera intrinsics captured once from the first depth frame. Intel's
    // Java SDK exposes Intrinsic but hides its fx/fy/cx/cy fields; see
    // extractIntrinsicsViaReflection below for the workaround.
    private static final class CamIntr {
        int w, h;
        float fx, fy, cx, cy;
    }
    private volatile CamIntr depthIntrinsics = null;

    // Top-down grid. 5m x 5m, 2.5 cm cell = 200 x 200 bins. Matches the
    // Windows topdown.cpp default so the two views are visually comparable.
    private static final float TD_EXTENT_M = 5.0f;
    private static final float TD_CELL_M   = 0.025f;
    private static final int   TD_BINS     = (int) (TD_EXTENT_M / TD_CELL_M);  // 200
    private Bitmap topDownBitmap = null;
    private int[] topDownPixels = null;
    private final int[] topDownHits = new int[TD_BINS * TD_BINS];

    // Reused buffer for sonar amp polling.
    private final float[] ampBuf = new float[3];
    private static final long SONAR_METER_MS = 50;  // 20 Hz UI updates
    private static final long VIEW_UPDATE_MS = 66;  // ~15 Hz image-view updates

    // Reused bitmaps + pixel buffers for the live image panes. All mutation
    // runs on the UI thread inside the view-update tick, so no extra lock.
    private Bitmap colorBitmap = null;
    private int[] colorPixels = null;
    private Bitmap depthBitmap = null;
    private int[] depthPixels = null;
    private final short[] depthSnap = new short[STREAM_W * STREAM_H];
    private final byte[] colorSnap = new byte[STREAM_W * STREAM_H * 3];
    private int depthSnapW = 0, depthSnapH = 0;
    private int colorSnapW = 0, colorSnapH = 0;
    private final Object depthLock = new Object();
    private short[] latestDepth = null;  // shared with processDepthFrame

    // --- Capture / state ---------------------------------------------------

    private final ExecutorService captureExec = Executors.newSingleThreadExecutor();
    private final ExecutorService brainExec = Executors.newSingleThreadExecutor();
    private Future<?> captureFuture;
    private volatile boolean keepStreaming = false;

    // Pre-allocated buffers to avoid GC pressure in the frame loop.
    private final short[] depthShorts = new short[STREAM_W * STREAM_H];
    private final byte[] depthBytes = new byte[STREAM_W * STREAM_H * 2];
    private final byte[] rgbBytes = new byte[STREAM_W * STREAM_H * 3];
    private final float[] freeOut = new float[10];

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

    // Speech recognition for the long-press "ask a free-form question" path.
    private SpeechRecognizer speechRecognizer;
    private boolean longPressActive = false;
    private boolean longPressRecording = false;
    private String lastRecognizedQuestion = null;
    private final Runnable startLongPressRecognition = this::beginVoiceCapture;
    // Default short-tap prompt. Long-press recording replaces this with
    // whatever the user says.
    // Gemma replies in the strict "<object>|<side>" format. Java composes
    // the final spoken sentence by combining the object with the real
    // geometry-derived distance from the depth camera, not Gemma's guess.
    private static final String SHORT_PRESS_PROMPT =
        "You are identifying the main object in front of a blind user. "
        + "Reply in EXACTLY this format and nothing else: "
        + "<short_object_description>|<side>\n"
        + "where <side> is one of: left, center, right.\n"
        + "The <short_object_description> is at most 4 words and includes "
        + "color if obvious. Do NOT include distance, do NOT add quotes, "
        + "do NOT explain, do NOT add any other text.\n"
        + "Good examples: white door|left   black sofa|center   "
        + "wooden desk|right";

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

        // Immersive fullscreen. Guarded so an OS-version mismatch on any
        // one of these APIs doesn't prevent the activity from starting.
        try {
            getWindow().setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController wic = getWindow().getInsetsController();
            if (wic != null) {
                wic.hide(android.view.WindowInsets.Type.statusBars()
                       | android.view.WindowInsets.Type.navigationBars());
                wic.setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode =
                android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        } catch (Throwable t) {
            Log.w(TAG, "immersive setup failed", t);
        }

        statusText = findViewById(R.id.statusText);
        freeLText = findViewById(R.id.freeLText);
        freeCText = findViewById(R.id.freeCText);
        freeRText = findViewById(R.id.freeRText);
        freeDirText = findViewById(R.id.freeDirText);
        freeFwdText = findViewById(R.id.freeFwdText);
        logText = findViewById(R.id.logText);
        describeButton = findViewById(R.id.describeButton);
        debugToggle = findViewById(R.id.debugToggle);
        debugPanel = findViewById(R.id.debugPanel);
        speakSwitch = findViewById(R.id.speakSwitch);
        hostEdit = findViewById(R.id.hostEdit);
        colorImage = findViewById(R.id.colorImage);
        depthImage = findViewById(R.id.depthImage);
        topDownImage = findViewById(R.id.topDownImage);
        leftMeter = findViewById(R.id.leftMeter);
        centerMeter = findViewById(R.id.centerMeter);
        rightMeter = findViewById(R.id.rightMeter);
        sonarOnSwitch = findViewById(R.id.sonarOnSwitch);
        pitchSeek = findViewById(R.id.pitchSeek);
        falloffSeek = findViewById(R.id.falloffSeek);
        vibrator = (Vibrator) getSystemService(VIBRATOR_SERVICE);

        sonarOnSwitch.setOnCheckedChangeListener((btn, checked) ->
            sonarSetEnabledNative(checked));

        // Pitch slider: seek [0,100] -> carrier [40, 1000] Hz. Default 10
        // in XML -> ~140 Hz, close to the miniaudio default of 110.
        pitchSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int p, boolean fromUser) {
                final float hz = 40.0f + (p / 100.0f) * 960.0f;
                sonarSetCarrierHzNative(hz);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        // Falloff slider: seek [0,100] -> exponent [1.5, 10]. Default 30 in
        // XML -> ~4.05, matching the Windows sonar's 4.0.
        falloffSeek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int p, boolean fromUser) {
                final float n = 1.5f + (p / 100.0f) * 8.5f;
                sonarSetFalloffNative(n);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });

        roiPaint.setStyle(Paint.Style.STROKE);
        roiPaint.setStrokeWidth(4.0f);

        // 640x480 color bitmap -> 28 px text is readable after the
        // ImageView shrinks it down.
        textPaint.setTextSize(28.0f);
        textPaint.setColor(0xFFFFFFFF);
        textPaint.setFakeBoldText(true);
        textShadowPaint.setTextSize(28.0f);
        textShadowPaint.setStyle(Paint.Style.STROKE);
        textShadowPaint.setStrokeWidth(4.0f);
        textShadowPaint.setColor(0xFF000000);
        textShadowPaint.setFakeBoldText(true);

        wedgeFillPaint.setStyle(Paint.Style.FILL);
        wedgeStrokePaint.setStyle(Paint.Style.STROKE);
        wedgeStrokePaint.setStrokeWidth(1.5f);

        startSonarMeterLoop();
        startViewUpdateLoop();

        // Tap = short-press quick description. Press-and-hold = voice
        // question. OnTouchListener gives us both DOWN and UP events so
        // we can route on release-duration.
        describeButton.setOnTouchListener(this::onDescribeTouch);

        debugToggle.setOnClickListener(v -> {
            final boolean showing = debugPanel.getVisibility() == android.view.View.VISIBLE;
            debugPanel.setVisibility(showing ? android.view.View.GONE : android.view.View.VISIBLE);
        });

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
        // Auto-retry the capture loop whenever librealsense reports a
        // device arrive/depart. Critical for the first launch: the USB
        // device-permission dialog is separate from the Camera/Mic
        // permissions and the user grants it AFTER onCreate returned,
        // so we'd otherwise sit stuck on "starting...".
        rsContext.setDevicesChangedCallback(new DeviceListener() {
            @Override
            public void onDeviceAttach() {
                log("device attached");
                uiHandler.post(MainActivity.this::attemptAutoStart);
            }
            @Override
            public void onDeviceDetach() {
                log("device detached");
                uiHandler.post(MainActivity.this::stopStreaming);
            }
        });

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

        maybeRequestAllPermissions();
        initSpeechRecognizer();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!sonarStartNative()) {
            log("sonar start failed");
        } else {
            // Seed native params from the current slider positions so the
            // UI and the audio thread agree from frame zero (instead of
            // the Sonar class's hardcoded defaults stepping on them).
            final SeekBar vol = findViewById(R.id.volumeSeek);
            sonarSetVolumeNative(vol.getProgress() / 100.0f);
            sonarSetCarrierHzNative(40.0f + (pitchSeek.getProgress() / 100.0f) * 960.0f);
            sonarSetFalloffNative(1.5f + (falloffSeek.getProgress() / 100.0f) * 8.5f);
            sonarSetEnabledNative(sonarOnSwitch.isChecked());
        }
        // Auto-start capture. If the RealSense isn't attached yet, the
        // pipeline open will throw and the status line shows the error;
        // re-plugging the camera fires USB_DEVICE_ATTACHED which re-launches
        // the activity, and this onResume will retry.
        attemptAutoStart();
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopStreaming();
        sonarStopNative();
    }

    @Override
    protected void onDestroy() {
        uiHandler.removeCallbacks(sonarMeterTick);
        uiHandler.removeCallbacks(viewUpdateTick);
        stopStreaming();
        captureExec.shutdownNow();
        brainExec.shutdownNow();
        if (tts != null) {
            tts.shutdown();
            tts = null;
        }
        if (speechRecognizer != null) {
            speechRecognizer.destroy();
            speechRecognizer = null;
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
        if (requestCode == PERMISSIONS_REQUEST) {
            boolean cameraGranted = false;
            boolean audioGranted = false;
            for (int i = 0; i < permissions.length; i++) {
                if (Manifest.permission.CAMERA.equals(permissions[i])) {
                    cameraGranted = grantResults[i] == PackageManager.PERMISSION_GRANTED;
                } else if (Manifest.permission.RECORD_AUDIO.equals(permissions[i])) {
                    audioGranted = grantResults[i] == PackageManager.PERMISSION_GRANTED;
                }
            }
            log("perms: camera=" + cameraGranted + " audio=" + audioGranted);
            if (cameraGranted) attemptAutoStart();
        }
    }

    // Request CAMERA + RECORD_AUDIO in one call so Android shows a single
    // combined dialog the user resolves once, instead of one dialog per
    // app launch. USB device permission is separate and prompted by the
    // OS the first time librealsense opens the D435i.
    private void maybeRequestAllPermissions() {
        java.util.ArrayList<String> needed = new java.util.ArrayList<>(2);
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            needed.add(Manifest.permission.CAMERA);
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            needed.add(Manifest.permission.RECORD_AUDIO);
        }
        if (!needed.isEmpty()) {
            requestPermissions(needed.toArray(new String[0]), PERMISSIONS_REQUEST);
        }
    }

    private void initSpeechRecognizer() {
        try {
            if (!SpeechRecognizer.isRecognitionAvailable(getApplicationContext())) {
                log("speech recognition unavailable on this device");
                return;
            }
            speechRecognizer = SpeechRecognizer.createSpeechRecognizer(getApplicationContext());
        } catch (Throwable t) {
            Log.w(TAG, "speech recognizer init failed", t);
            return;
        }
        speechRecognizer.setRecognitionListener(new RecognitionListener() {
            @Override public void onReadyForSpeech(Bundle params) {}
            @Override public void onBeginningOfSpeech() {}
            @Override public void onRmsChanged(float v) {}
            @Override public void onBufferReceived(byte[] b) {}
            @Override public void onEndOfSpeech() {}
            @Override
            public void onError(int error) {
                longPressRecording = false;
                log("voice error: " + error);
                uiHandler.post(() -> statusText.setText("voice error " + error));
            }
            @Override
            public void onResults(Bundle results) {
                longPressRecording = false;
                final java.util.ArrayList<String> matches =
                    results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                if (matches != null && !matches.isEmpty()) {
                    final String question = matches.get(0);
                    lastRecognizedQuestion = question;
                    log("voice: \"" + question + "\"");
                    if (tryHandleVoiceCommand(question)) {
                        statusText.setText("idle");
                    } else {
                        fireBrainWithQuestion(question);
                    }
                } else {
                    log("voice: no results");
                }
            }
            @Override public void onPartialResults(Bundle partial) {}
            @Override public void onEvent(int i, Bundle b) {}
        });
    }

    private boolean onDescribeTouch(android.view.View v, android.view.MotionEvent ev) {
        switch (ev.getAction()) {
            case android.view.MotionEvent.ACTION_DOWN:
                longPressActive = true;
                // Button's pressed-state drawable (depressed visual) only
                // fires automatically when a click reaches it; since our
                // OnTouchListener consumes the event, drive setPressed
                // ourselves so the user gets pressed feedback even when
                // holding through the long-press threshold.
                v.setPressed(true);
                buzz(30);
                uiHandler.postDelayed(startLongPressRecognition, LONG_PRESS_THRESHOLD_MS);
                return true;
            case android.view.MotionEvent.ACTION_UP:
            case android.view.MotionEvent.ACTION_CANCEL:
                v.setPressed(false);
                buzz(150);
                if (!longPressActive) return true;
                longPressActive = false;
                uiHandler.removeCallbacks(startLongPressRecognition);
                if (longPressRecording) {
                    // Flip the flag BEFORE stopListening so the pending
                    // onResults treats it as the final turn and does not
                    // restart the recognizer.
                    longPressRecording = false;
                    if (speechRecognizer != null) speechRecognizer.stopListening();
                    statusText.setText("transcribing...");
                } else {
                    onDescribeClick();
                }
                return true;
        }
        return false;
    }

    private void beginVoiceCapture() {
        if (!longPressActive) return;  // user already released
        if (speechRecognizer == null) {
            log("voice: recognizer not initialized");
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            log("voice: audio permission not granted");
            maybeRequestAllPermissions();
            return;
        }
        longPressRecording = true;
        statusText.setText("listening...");
        // Cancel any lingering state from the previous session so rapid
        // repeats do not trip ERROR_RECOGNIZER_BUSY.
        try { speechRecognizer.cancel(); } catch (Throwable ignored) {}
        final Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
            RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, getPackageName());
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
        intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_MINIMUM_LENGTH_MILLIS, 2000L);
        intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 2000L);
        speechRecognizer.startListening(intent);
    }

    // Sends the user's spoken question + latest color frame to Brain and
    // speaks the response. Reuses the existing brain path so latency CSV
    // and degraded-mode handling are shared.
    // Tries to match a recognized transcript against the app-control command
    // set. Returns true if a command was matched and executed. Matching is
    // intentionally substring-based on a small whitelist of exact phrases
    // so ambiguous questions like "is the sonar on?" don't misfire.
    private boolean tryHandleVoiceCommand(String text) {
        final String n = text.toLowerCase(Locale.US)
            .replaceAll("[.?!,]", "").trim();

        // Sonar off
        if (containsAny(n, "turn off sonar", "sonar off", "mute sonar",
                           "stop sonar", "disable sonar", "turn sonar off",
                           "silence sonar")) {
            applySonarEnable(false);
            return true;
        }
        // Sonar on
        if (containsAny(n, "turn on sonar", "sonar on", "unmute sonar",
                           "start sonar", "enable sonar", "turn sonar on")) {
            applySonarEnable(true);
            return true;
        }

        // Narration / TTS voice off
        if (containsAny(n, "turn off voice", "voice off", "stop talking",
                           "stop speaking", "mute voice", "disable voice",
                           "turn voice off", "don't speak", "stop narration",
                           "stop narrating", "be quiet")) {
            speakSwitch.setChecked(false);
            log("voice: narration off");
            return true;
        }
        // Narration / TTS voice on
        if (containsAny(n, "turn on voice", "voice on", "start talking",
                           "start speaking", "unmute voice", "enable voice",
                           "turn voice on", "speak to me", "start narration",
                           "start narrating")) {
            speakSwitch.setChecked(true);
            maybeSpeak("voice on");
            return true;
        }
        return false;
    }

    private void applySonarEnable(boolean enabled) {
        sonarOnSwitch.setChecked(enabled);
        sonarSetEnabledNative(enabled);
        log("voice: sonar " + (enabled ? "on" : "off"));
        maybeSpeak(enabled ? "sonar on" : "sonar off");
    }

    private static boolean containsAny(String text, String... phrases) {
        for (String p : phrases) {
            if (text.contains(p)) return true;
        }
        return false;
    }

    private void fireBrainWithQuestion(String question) {
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
        // Two routes: spatial/locator questions get the structured
        // <object>|<side> format and geometry-grounded sentence;
        // everything else gets a short free-form scene description.
        final boolean locator = isLocatorQuestion(question);
        final String prompt = locator
            ? "You are helping a blind user locate something. Look at the "
                + "image and answer their question.\n"
                + "Reply in EXACTLY this format and nothing else: "
                + "<short_object_description>|<side>\n"
                + "where <side> is one of: left, center, right.\n"
                + "If what they asked about is not visible in the image, "
                + "reply exactly: none|none\n"
                + "The <short_object_description> is at most 4 words and "
                + "includes color if obvious. Do NOT include distance, "
                + "do NOT add quotes, do NOT explain.\n"
                + "Good examples: white door|left   black sofa|center   "
                + "red chair|right   none|none\n"
                + "Question: \"" + question + "\""
            : "You are narrating a scene for a blind user. Answer their "
                + "question in ONE short sentence, under 15 words. "
                + "Name the main objects or describe the scene. "
                + "NEVER use: \"the image\", \"the photo\", \"this shows\", "
                + "\"I see\", \"I can see\", \"it appears\", \"there is\", "
                + "\"there are\", \"in the foreground\", \"in the background\", "
                + "\"the camera\". No preamble, no hedging, no explanation. "
                + "Just the sentence.\n"
                + "Good examples: \"Living room with a gray sofa, chair, "
                + "and coffee table.\"  \"Kitchen counter with a microwave "
                + "and fruit bowl.\"  \"Hallway with a closed door at the "
                + "end.\"\n"
                + "Question: \"" + question + "\"";
        // For locator queries, snapshot geometry so Java can compose a
        // grounded sentence using the sensor's real distance.
        final float[] nearsAtAsk = new float[3];
        final boolean[] validAtAsk = new boolean[3];
        synchronized (scoresLock) {
            nearsAtAsk[0] = lastNearM[0];
            nearsAtAsk[1] = lastNearM[1];
            nearsAtAsk[2] = lastNearM[2];
            validAtAsk[0] = lastValid[0];
            validAtAsk[1] = lastValid[1];
            validAtAsk[2] = lastValid[2];
        }
        log("brain: voice q (" + (locator ? "locator" : "describe")
            + ") -> " + host + " (" + jpeg.length + " B)");
        brainInFlight = true;
        uiHandler.post(() -> {
            describeButton.setEnabled(false);
            statusText.setText("requesting...");
        });
        brainExec.submit(() -> {
            final long t0 = SystemClock.elapsedRealtime();
            final String resp = askBrainNative(jpeg, prompt, host);
            final long dtMs = SystemClock.elapsedRealtime() - t0;
            final boolean ok = resp != null && !resp.startsWith("ERROR:");
            logBrainLatency(dtMs, ok);
            final String spoken;
            if (!ok) {
                spoken = "brain error";
            } else if (locator) {
                spoken = composeGroundedSentence(resp, nearsAtAsk, validAtAsk);
            } else {
                spoken = stripFillerPhrases(resp);
            }
            uiHandler.post(() -> {
                brainInFlight = false;
                describeButton.setEnabled(keepStreaming);
                statusText.setText(ok ? "done " + dtMs + "ms" : "brain error");
                log("brain: " + dtMs + "ms - " + resp);
                log("say: " + spoken);
                if (ok) maybeSpeak(spoken);
            });
        });
    }

    // Auto-start is called from onResume and onRequestPermissionsResult.
    // Safe to call multiple times; no-ops if already streaming or if the
    // camera permission hasn't been granted yet.
    private void attemptAutoStart() {
        if (keepStreaming) return;
        if (checkSelfPermission(Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) return;
        keepStreaming = true;
        describeButton.setEnabled(true);
        statusText.setText("starting...");
        captureFuture = captureExec.submit(this::runCaptureLoop);
    }

    private void stopStreaming() {
        keepStreaming = false;
        if (captureFuture != null) {
            captureFuture.cancel(true);
            captureFuture = null;
        }
        // Sonar silencing happens in the capture loop's finally block so
        // we're guaranteed it's the final writer; calling it from the UI
        // thread here would race with a last in-flight processDepthFrame.
        uiHandler.post(() -> {
            describeButton.setEnabled(false);
            statusText.setText("stopped");
        });
    }

    // --- Capture loop ------------------------------------------------------

    private void runCaptureLoop() {
        // Filter chain applied per frame:
        //   1. Align depth -> color so the two streams share the same FoV
        //      and pixel grid (fixes the ~20 deg depth-sensor FoV surplus).
        //   2. HoleFillingFilter to fill the parallax occlusion shadow
        //      next to close objects (the RGB camera sits ~5 cm right of
        //      the IR stereo module, so aligned depth has a sliver of
        //      no-data next to anything close).
        try (Pipeline pipe = new Pipeline();
             Config cfg = new Config();
             Align align = new Align(StreamType.COLOR);
             HoleFillingFilter holeFill = new HoleFillingFilter()) {
            cfg.enableStream(StreamType.DEPTH, -1, STREAM_W, STREAM_H, StreamFormat.Z16, STREAM_FPS);
            cfg.enableStream(StreamType.COLOR, -1, STREAM_W, STREAM_H, StreamFormat.RGB8, STREAM_FPS);

            try (PipelineProfile profile = pipe.start(cfg)) {
                uiHandler.post(() -> statusText.setText(
                    STREAM_W + "x" + STREAM_H + " @ " + STREAM_FPS + "fps"));

                long frameCount = 0;
                long lastLogMs = SystemClock.elapsedRealtime();

                while (keepStreaming && !Thread.currentThread().isInterrupted()) {
                    try (FrameSet raw = pipe.waitForFrames(FRAME_TIMEOUT_MS);
                         FrameSet aligned = raw.applyFilter(align);
                         FrameSet frames = aligned.applyFilter(holeFill)) {
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
                            String.format(Locale.US, "%.1f fps", fps)));
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
            // Friendlier banner than the raw exception so the user knows
            // what to do. Plug in or grant USB permission, and the
            // DeviceListener will retry automatically.
            final String banner = msg.toLowerCase(Locale.US).contains("no device")
                ? "waiting for RealSense..."
                : "error: " + msg;
            uiHandler.post(() -> {
                statusText.setText(banner);
                log("capture error: " + msg);
            });
        } finally {
            keepStreaming = false;
            // Capture thread is the sole per-frame sonar writer. Silence
            // all three sectors as our last act so the audio-thread amp
            // smoother ramps to zero (~60 ms) instead of holding the
            // last-seen hum indefinitely after Stop.
            sonarUpdateNative(0.0f, false, 0.0f, false, 0.0f, false);
            uiHandler.post(() -> {
                describeButton.setEnabled(false);
            });
        }
    }

    private void processDepthFrame(Frame depthF) {
        DepthFrame depth = depthF.as(Extension.DEPTH_FRAME);
        if (depth == null) return;
        ensureIntrinsics(depth);
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

        // Stash a snapshot of the raw depth for the Depth / Top-Down panes.
        synchronized (depthLock) {
            if (latestDepth == null || latestDepth.length != w * h) {
                latestDepth = new short[w * h];
            }
            System.arraycopy(depthShorts, 0, latestDepth, 0, w * h);
            depthSnapW = w;
            depthSnapH = h;
        }

        final float lScore = freeOut[0];
        final float cScore = freeOut[1];
        final float rScore = freeOut[2];
        final boolean lValid = freeOut[3] > 0.5f;
        final boolean cValid = freeOut[4] > 0.5f;
        final boolean rValid = freeOut[5] > 0.5f;
        final float lNearM = freeOut[6];
        final float cNearM = freeOut[7];
        final float rNearM = freeOut[8];
        final int sugg = (int) freeOut[9];

        sonarUpdateNative(lScore, lValid, cScore, cValid, rScore, rValid);

        // Publish for the Color-pane + Top-Down overlays.
        synchronized (scoresLock) {
            lastScores[0] = lScore; lastScores[1] = cScore; lastScores[2] = rScore;
            lastValid[0]  = lValid; lastValid[1]  = cValid; lastValid[2]  = rValid;
            lastNearM[0]  = lNearM; lastNearM[1]  = cNearM; lastNearM[2]  = rNearM;
        }

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
        final String lStr = String.format(Locale.US, "L %.2f", lScore);
        final String cStr = String.format(Locale.US, "C %.2f", cScore);
        final String rStr = String.format(Locale.US, "R %.2f", rScore);
        final String dStr = String.format(Locale.US, "dir %s", dirName);
        final String fStr = String.format(Locale.US, "fwd %.2fm", cNearM);
        uiHandler.post(() -> {
            freeLText.setText(lStr);
            freeCText.setText(cStr);
            freeRText.setText(rStr);
            freeDirText.setText(dStr);
            freeFwdText.setText(fStr);
        });
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
        final String prompt = SHORT_PRESS_PROMPT;
        final float[] nearsAtAsk = new float[3];
        final boolean[] validAtAsk = new boolean[3];
        synchronized (scoresLock) {
            nearsAtAsk[0] = lastNearM[0];
            nearsAtAsk[1] = lastNearM[1];
            nearsAtAsk[2] = lastNearM[2];
            validAtAsk[0] = lastValid[0];
            validAtAsk[1] = lastValid[1];
            validAtAsk[2] = lastValid[2];
        }
        log("brain: requesting (" + jpeg.length + " B jpeg) -> " + host);
        brainInFlight = true;
        describeButton.setEnabled(false);
        brainExec.submit(() -> {
            final long t0 = SystemClock.elapsedRealtime();
            final String resp = askBrainNative(jpeg, prompt, host);
            final long dtMs = SystemClock.elapsedRealtime() - t0;
            final boolean ok = resp != null && !resp.startsWith("ERROR:");
            logBrainLatency(dtMs, ok);
            final String spoken = ok
                ? composeGroundedSentence(resp, nearsAtAsk, validAtAsk)
                : "brain error";
            uiHandler.post(() -> {
                brainInFlight = false;
                describeButton.setEnabled(keepStreaming);
                log("brain: " + dtMs + " ms (median "
                    + String.format(Locale.US, "%.0f", brainLatencyMedianMs)
                    + " ms) - " + resp);
                log("say: " + spoken);
                if (ok) maybeSpeak(spoken);
            });
        });
    }

    // Parse Gemma's "<object>|<side>" reply and compose a grounded
    // sentence using the sensor's real distance at the chosen side.
    // Falls back gracefully if the reply is malformed.
    // True if the question asks WHERE something is (locator query).
    // These go through the structured <object>|<side> + grounded-distance
    // path. Everything else gets free-form scene description.
    private static boolean isLocatorQuestion(String q) {
        if (q == null) return false;
        final String n = " " + q.toLowerCase(Locale.US).trim() + " ";
        return n.contains(" where ")
            || n.contains("where's")
            || n.contains("where is")
            || n.contains("where are")
            || n.contains(" find ")
            || n.contains(" locate ")
            || n.contains(" point to ")
            || n.contains(" point out ")
            || n.contains(" which side ")
            || n.contains(" which direction ")
            || n.contains(" how far ");
    }

    // Defensive filler cleanup for free-form answers. Gemma usually
    // obeys the prompt but occasionally slips a preamble through.
    private static String stripFillerPhrases(String raw) {
        if (raw == null) return "";
        String s = raw.trim();
        // Strip surrounding quotes and stray code fences.
        if (s.startsWith("`")) s = s.replaceAll("`", "");
        if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2) {
            s = s.substring(1, s.length() - 1);
        }
        // Take the first sentence only.
        final int nl = s.indexOf('\n');
        if (nl >= 0) s = s.substring(0, nl).trim();
        final String[] leadings = {
            "the image shows ", "the image depicts ", "the image has ",
            "the photo shows ", "the photo depicts ",
            "this image shows ", "this photo shows ", "this shows ",
            "i can see ", "i see ", "it appears that ", "it appears ",
            "in the foreground ", "in the background ",
            "in front of the camera ", "the camera is showing ",
            "here we see ", "here is ", "here's "
        };
        boolean changed = true;
        while (changed) {
            changed = false;
            final String lower = s.toLowerCase(Locale.US);
            for (String lead : leadings) {
                if (lower.startsWith(lead)) {
                    s = s.substring(lead.length()).trim();
                    changed = true;
                    break;
                }
            }
        }
        if (!s.isEmpty()) {
            s = Character.toUpperCase(s.charAt(0)) + s.substring(1);
        }
        return s;
    }

    private static String composeGroundedSentence(String raw,
            float[] nearM, boolean[] valid) {
        if (raw == null) return "No answer.";
        String r = raw.trim();
        // Strip stray code fences / quotes that Gemma sometimes wraps.
        if (r.startsWith("`")) r = r.replaceAll("`", "");
        if (r.startsWith("\"") && r.endsWith("\"") && r.length() >= 2) {
            r = r.substring(1, r.length() - 1);
        }
        final int nl = r.indexOf('\n');
        if (nl >= 0) r = r.substring(0, nl).trim();
        final int bar = r.indexOf('|');
        if (bar < 0) return r.isEmpty() ? "No answer." : r;
        String object = r.substring(0, bar).trim();
        String side   = r.substring(bar + 1).trim().toLowerCase(Locale.US);
        // Strip trailing punctuation on side.
        while (!side.isEmpty()
                && !Character.isLetter(side.charAt(side.length() - 1))) {
            side = side.substring(0, side.length() - 1);
        }
        if (object.isEmpty() || object.equalsIgnoreCase("none")
                || side.equals("none") || side.equals("unknown")
                || side.isEmpty()) {
            return "Not visible.";
        }
        final String where;
        final float near;
        final boolean isValid;
        switch (side) {
            case "left":
                where = "on the left";
                near = nearM[0]; isValid = valid[0]; break;
            case "right":
                where = "on the right";
                near = nearM[2]; isValid = valid[2]; break;
            default:
                where = "in front";
                near = nearM[1]; isValid = valid[1]; break;
        }
        final String obj = Character.toUpperCase(object.charAt(0))
                + object.substring(1);
        if (isValid && near > 0.05f) {
            return String.format(Locale.US, "%s %s, %.1f meters.",
                obj, where, near);
        }
        return obj + " " + where + ".";
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

    // --- Sonar meters ------------------------------------------------------

    private final Runnable sonarMeterTick = new Runnable() {
        @Override
        public void run() {
            sonarGetAmpsNative(ampBuf);
            // amp is bounded by (1-score)^falloff * volume; at the default
            // volume 0.25 and falloff 4 it peaks near 0.25 when a sector is
            // fully blocked. Multiply by 4000 so a fully-blocked sector
            // fills the 1000-step ProgressBar roughly at default volume.
            final int lP = Math.min(1000, Math.max(0, (int) (ampBuf[0] * 4000.0f)));
            final int cP = Math.min(1000, Math.max(0, (int) (ampBuf[1] * 4000.0f)));
            final int rP = Math.min(1000, Math.max(0, (int) (ampBuf[2] * 4000.0f)));
            leftMeter.setProgress(lP);
            centerMeter.setProgress(cP);
            rightMeter.setProgress(rP);
            uiHandler.postDelayed(this, SONAR_METER_MS);
        }
    };

    private void startSonarMeterLoop() {
        uiHandler.postDelayed(sonarMeterTick, SONAR_METER_MS);
    }

    // --- Live image panes (Color + Depth) ----------------------------------

    private final Runnable viewUpdateTick = new Runnable() {
        @Override
        public void run() {
            updateColorPane();
            updateDepthPane();
            updateTopDownPane();
            uiHandler.postDelayed(this, VIEW_UPDATE_MS);
        }
    };

    private void startViewUpdateLoop() {
        uiHandler.postDelayed(viewUpdateTick, VIEW_UPDATE_MS);
    }

    private void updateColorPane() {
        // Copy the capture thread's latest RGB buffer into a local snapshot
        // so the pixel-pack loop below doesn't hold colorLock.
        final int w, h;
        synchronized (colorLock) {
            if (latestColorRgb == null) return;
            w = latestColorW;
            h = latestColorH;
            if (colorSnap.length < w * h * 3) return;
            System.arraycopy(latestColorRgb, 0, colorSnap, 0, w * h * 3);
            colorSnapW = w;
            colorSnapH = h;
        }
        if (colorPixels == null || colorPixels.length != w * h) {
            colorPixels = new int[w * h];
        }
        // RGB8 -> ARGB_8888. Capture pipeline is configured for RGB8, so
        // byte order is R,G,B per pixel.
        for (int i = 0, p = 0; i < colorPixels.length; i++, p += 3) {
            final int r = colorSnap[p]     & 0xFF;
            final int g = colorSnap[p + 1] & 0xFF;
            final int b = colorSnap[p + 2] & 0xFF;
            colorPixels[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
        if (colorBitmap == null || colorBitmap.getWidth() != w || colorBitmap.getHeight() != h) {
            colorBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        }
        colorBitmap.setPixels(colorPixels, 0, w, 0, 0, w, h);
        drawRoiOverlays(colorBitmap, w, h);
        colorImage.setImageBitmap(colorBitmap);
    }

    // Draws L/C/R ROI rectangles on the color frame in the same
    // red-yellow-green clearance color the Windows app uses. ROI geometry
    // matches the free_space analyzer defaults: horizontal cone 0.95 of
    // the frame width, center beam 0.25, vertical slice 0.50.
    private void drawRoiOverlays(Bitmap bmp, int w, int h) {
        final float f_coneX = 0.95f;
        final float f_beam  = 0.25f;
        final float f_coneY = 0.50f;
        final int coneW = Math.max(3, (int) (w * f_coneX));
        final int coneH = Math.max(1, (int) (h * f_coneY));
        final int coneX0 = (w - coneW) / 2;
        final int coneY0 = (h - coneH) / 2;
        final int beamW  = Math.max(2, Math.min(coneW - 2, (int) (w * f_beam)));
        final int centerX = (w - beamW) / 2;
        final int leftX  = coneX0;
        final int leftW  = centerX - leftX;
        final int rightX = centerX + beamW;
        final int rightW = (coneX0 + coneW) - rightX;

        float lS, cS, rS;
        boolean lV, cV, rV;
        float cNear;
        synchronized (scoresLock) {
            lS = lastScores[0]; cS = lastScores[1]; rS = lastScores[2];
            lV = lastValid[0];  cV = lastValid[1];  rV = lastValid[2];
            cNear = lastNearM[1];
        }

        final Canvas canvas = new Canvas(bmp);
        // Left, center, right sectors. Invalid sectors draw dim gray so you
        // can still see where they are but they don't scream "BLOCKED".
        drawOneRoi(canvas, leftX,   coneY0, leftW,  coneH, lS, lV);
        drawOneRoi(canvas, centerX, coneY0, beamW,  coneH, cS, cV);
        drawOneRoi(canvas, rightX,  coneY0, rightW, coneH, rS, rV);

        // Distance label above the center ROI. Black outline behind white
        // fill so it reads on any background. Matches the Windows overlay.
        if (cV && cNear > 0.0f) {
            final String label = String.format(Locale.US, "fwd %.2f m", cNear);
            final float tx = centerX + beamW * 0.5f;
            final float ty = Math.max(30.0f, (float) (coneY0 - 10));
            textPaint.setTextAlign(Paint.Align.CENTER);
            textShadowPaint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(label, tx, ty, textShadowPaint);
            canvas.drawText(label, tx, ty, textPaint);
        }
    }

    private void drawOneRoi(Canvas canvas, int x, int y, int rectW, int rectH,
                            float score, boolean valid) {
        final int color = valid ? scoreToColor(score) : 0x80808080;
        roiPaint.setColor(color);
        canvas.drawRect(x + 2, y + 2, x + rectW - 2, y + rectH - 2, roiPaint);
    }

    // score 0 -> red, 0.5 -> yellow, 1 -> green. Alpha 0xFF.
    private static int scoreToColor(float score) {
        final float s = Math.max(0.0f, Math.min(1.0f, score));
        int r, g;
        if (s < 0.5f) {
            r = 255;
            g = (int) (s * 2.0f * 255.0f);
        } else {
            r = (int) ((1.0f - s) * 2.0f * 255.0f);
            g = 255;
        }
        return 0xFF000000 | (r << 16) | (g << 8);
    }

    private void updateDepthPane() {
        // Stub for step 3 — will colormap depthSnap into depthPixels.
        final int w, h;
        synchronized (depthLock) {
            if (latestDepth == null) return;
            w = depthSnapW;
            h = depthSnapH;
            if (depthSnap.length < w * h) return;
            System.arraycopy(latestDepth, 0, depthSnap, 0, w * h);
        }
        if (depthPixels == null || depthPixels.length != w * h) {
            depthPixels = new int[w * h];
        }
        colormapDepth(depthSnap, w, h, depthPixels);
        if (depthBitmap == null || depthBitmap.getWidth() != w || depthBitmap.getHeight() != h) {
            depthBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        }
        depthBitmap.setPixels(depthPixels, 0, w, 0, 0, w, h);
        depthImage.setImageBitmap(depthBitmap);
    }

    // Pulls fx/fy/cx/cy from the RealSense Java SDK on the first depth
    // frame we see. Intel's Intrinsic class hides those fields without
    // getters, so fall back to reflection; if that breaks on a future
    // AAR, use the D435i @ 640x480 nominal values so top-down still draws.
    private void ensureIntrinsics(DepthFrame depth) {
        if (depthIntrinsics != null) return;
        try {
            VideoStreamProfile vsp = depth.getProfile().as(Extension.VIDEO_PROFILE);
            Intrinsic intr = vsp.getIntrinsic();
            Class<?> c = intr.getClass();
            java.lang.reflect.Field fFx = c.getDeclaredField("mFx"); fFx.setAccessible(true);
            java.lang.reflect.Field fFy = c.getDeclaredField("mFy"); fFy.setAccessible(true);
            java.lang.reflect.Field fPx = c.getDeclaredField("mPpx"); fPx.setAccessible(true);
            java.lang.reflect.Field fPy = c.getDeclaredField("mPpy"); fPy.setAccessible(true);
            CamIntr ci = new CamIntr();
            ci.w = intr.getWidth();
            ci.h = intr.getHeight();
            ci.fx = fFx.getFloat(intr);
            ci.fy = fFy.getFloat(intr);
            ci.cx = fPx.getFloat(intr);
            ci.cy = fPy.getFloat(intr);
            depthIntrinsics = ci;
            log(String.format(Locale.US,
                "intrinsics: %dx%d fx=%.1f fy=%.1f cx=%.1f cy=%.1f",
                ci.w, ci.h, ci.fx, ci.fy, ci.cx, ci.cy));
        } catch (Exception e) {
            Log.w(TAG, "intrinsics reflection failed, using D435i defaults", e);
            CamIntr ci = new CamIntr();
            ci.w = STREAM_W; ci.h = STREAM_H;
            ci.fx = 384.6f; ci.fy = 384.6f;
            ci.cx = STREAM_W / 2.0f;
            ci.cy = STREAM_H / 2.0f;
            depthIntrinsics = ci;
            log("intrinsics fallback: D435i @ " + STREAM_W + "x" + STREAM_H);
        }
    }

    private void updateTopDownPane() {
        final CamIntr intr = depthIntrinsics;
        if (intr == null) return;
        final int w, h;
        synchronized (depthLock) {
            if (latestDepth == null) return;
            w = depthSnapW;
            h = depthSnapH;
            if (depthSnap.length < w * h) return;
            System.arraycopy(latestDepth, 0, depthSnap, 0, w * h);
        }
        // Zero the hit counts for this frame.
        java.util.Arrays.fill(topDownHits, 0);

        // Grid spans X in [-TD_EXTENT_M/2, +TD_EXTENT_M/2] and Z in [0, TD_EXTENT_M].
        final float halfExtent = TD_EXTENT_M * 0.5f;
        final float invCell = 1.0f / TD_CELL_M;
        final float invFx = 1.0f / intr.fx;
        final float cx = intr.cx;
        // Subsample 2x on each axis: 4x fewer deprojections, still dense
        // enough for a clearly-readable top-down at 200x200 output.
        final int stride = 2;
        for (int v = 0; v < h; v += stride) {
            final int rowBase = v * w;
            for (int u = 0; u < w; u += stride) {
                final int dMm = depthSnap[rowBase + u] & 0xFFFF;
                if (dMm == 0) continue;
                final float dM = dMm * 0.001f;
                if (dM > TD_EXTENT_M) continue;
                final float X = (u - cx) * dM * invFx;
                if (X < -halfExtent || X > halfExtent) continue;
                final int col = (int) ((X + halfExtent) * invCell);
                final int row = (TD_BINS - 1) - (int) (dM * invCell);
                if (col < 0 || col >= TD_BINS || row < 0 || row >= TD_BINS) continue;
                topDownHits[row * TD_BINS + col]++;
            }
        }

        if (topDownPixels == null || topDownPixels.length != TD_BINS * TD_BINS) {
            topDownPixels = new int[TD_BINS * TD_BINS];
        }
        // Log-scale hit counts to 0..255 grayscale. `scale` controls how
        // quickly a cell saturates to white; 12 is a good visual default
        // for 2x subsampled 640x480 depth at 5m extent.
        final float invLog = 1.0f / (float) Math.log(12.0);
        for (int i = 0; i < topDownPixels.length; i++) {
            final int c = topDownHits[i];
            if (c == 0) { topDownPixels[i] = 0xFF000000; continue; }
            float t = (float) Math.log(1.0 + c) * invLog;
            if (t > 1.0f) t = 1.0f;
            final int g = (int) (t * 255.0f);
            topDownPixels[i] = 0xFF000000 | (g << 16) | (g << 8) | g;
        }
        // Camera position marker: red dot at bottom-center (2x2).
        final int camCol = TD_BINS / 2;
        final int camRow = TD_BINS - 2;
        for (int dr = 0; dr < 2; dr++) {
            for (int dc = -1; dc <= 0; dc++) {
                final int cc = camCol + dc;
                final int rr = camRow + dr;
                if (cc >= 0 && cc < TD_BINS && rr >= 0 && rr < TD_BINS) {
                    topDownPixels[rr * TD_BINS + cc] = 0xFFFF3030;
                }
            }
        }

        if (topDownBitmap == null) {
            topDownBitmap = Bitmap.createBitmap(TD_BINS, TD_BINS, Bitmap.Config.ARGB_8888);
        }
        topDownBitmap.setPixels(topDownPixels, 0, TD_BINS, 0, 0, TD_BINS, TD_BINS);
        drawTopDownWedges(topDownBitmap, intr, w, h);
        topDownImage.setImageBitmap(topDownBitmap);
    }

    // Three translucent wedges (L / C / R) drawn on top of the grayscale
    // occupancy, coloured by each sector's clearance. Wedge extends from
    // the camera out to the sector's near-depth (clamped to the blocked
    // threshold and the grid extent). Matches what the Windows top-down
    // pane shows.
    private void drawTopDownWedges(Bitmap bmp, CamIntr intr,
                                   int depthW, int depthH) {
        // ROI geometry in depth-image pixel space (matches drawRoiOverlays).
        final float f_coneX = 0.95f;
        final float f_beam  = 0.25f;
        final int coneW = Math.max(3, (int) (depthW * f_coneX));
        final int coneX0 = (depthW - coneW) / 2;
        final int beamW  = Math.max(2, Math.min(coneW - 2, (int) (depthW * f_beam)));
        final int centerX = (depthW - beamW) / 2;
        final int leftX  = coneX0;
        final int leftW  = centerX - leftX;
        final int rightX = centerX + beamW;
        final int rightW = (coneX0 + coneW) - rightX;

        float lS, cS, rS;
        boolean lV, cV, rV;
        float lNear, cNear, rNear;
        synchronized (scoresLock) {
            lS = lastScores[0]; cS = lastScores[1]; rS = lastScores[2];
            lV = lastValid[0];  cV = lastValid[1];  rV = lastValid[2];
            lNear = lastNearM[0]; cNear = lastNearM[1]; rNear = lastNearM[2];
        }

        final Canvas canvas = new Canvas(bmp);
        final float halfExtent = TD_EXTENT_M * 0.5f;
        final float invCell = 1.0f / TD_CELL_M;
        final float camCol = TD_BINS * 0.5f;
        final float camRow = TD_BINS - 1.0f;

        drawOneWedge(canvas, intr, halfExtent, invCell, camCol, camRow,
            leftX,  leftX + leftW - 1,   lNear, lS, lV);
        drawOneWedge(canvas, intr, halfExtent, invCell, camCol, camRow,
            centerX, centerX + beamW - 1, cNear, cS, cV);
        drawOneWedge(canvas, intr, halfExtent, invCell, camCol, camRow,
            rightX, rightX + rightW - 1, rNear, rS, rV);
    }

    private void drawOneWedge(Canvas canvas, CamIntr intr,
                              float halfExtent, float invCell,
                              float camCol, float camRow,
                              int uLeft, int uRight,
                              float nearM, float score, boolean valid) {
        if (!valid) return;
        // Clamp wedge distance: short wedges from the blocked threshold
        // stay visible even when the sector reports a closer-than-blocked
        // reading; long wedges saturate at the grid extent.
        float Z = Math.max(BLOCKED_THRESHOLD_M, Math.min(TD_EXTENT_M, nearM));
        if (Z <= 0.0f) return;
        final float invFx = 1.0f / intr.fx;
        final float X_L = (uLeft  - intr.cx) * Z * invFx;
        final float X_R = (uRight - intr.cx) * Z * invFx;
        final float colL = (X_L + halfExtent) * invCell;
        final float colR = (X_R + halfExtent) * invCell;
        final float row  = (TD_BINS - 1) - Z * invCell;

        final int baseColor = scoreToColor(score);
        final int fillColor = (baseColor & 0x00FFFFFF) | 0x60000000;

        android.graphics.Path path = new android.graphics.Path();
        path.moveTo(camCol, camRow);
        path.lineTo(colL, row);
        path.lineTo(colR, row);
        path.close();
        wedgeFillPaint.setColor(fillColor);
        wedgeStrokePaint.setColor(baseColor);
        canvas.drawPath(path, wedgeFillPaint);
        canvas.drawPath(path, wedgeStrokePaint);
    }

    // Depth (mm, unsigned 16) -> ARGB_8888 turbo-ish colormap. Close = warm
    // (red), far = cool (blue), zero = black. 3000 mm horizon matches the
    // CLEAR_HORIZON_M clamp used by the free-space analyzer.
    private static void colormapDepth(short[] depth, int w, int h, int[] out) {
        final int horizonMm = 3000;
        for (int i = 0; i < out.length; i++) {
            final int d = depth[i] & 0xFFFF;  // sign-extend guard
            if (d == 0) {
                out[i] = 0xFF000000;  // opaque black for no-return pixels
                continue;
            }
            final int clamp = d > horizonMm ? horizonMm : d;
            // t in [0,1]: 0 near, 1 far
            final float t = clamp / (float) horizonMm;
            // Three-stop ramp: red (near) -> yellow -> green -> cyan -> blue (far)
            int r, g, b;
            if (t < 0.25f) {
                final float u = t / 0.25f;      // 0..1 red -> yellow
                r = 255; g = (int) (255 * u); b = 0;
            } else if (t < 0.5f) {
                final float u = (t - 0.25f) / 0.25f;  // yellow -> green
                r = (int) (255 * (1.0f - u)); g = 255; b = 0;
            } else if (t < 0.75f) {
                final float u = (t - 0.5f) / 0.25f;   // green -> cyan
                r = 0; g = 255; b = (int) (255 * u);
            } else {
                final float u = (t - 0.75f) / 0.25f;  // cyan -> blue
                r = 0; g = (int) (255 * (1.0f - u)); b = 255;
            }
            out[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    private void log(String line) {
        Log.i(TAG, line);
        uiHandler.post(() -> {
            final CharSequence prev = logText.getText();
            logText.setText(prev + "\n" + line);
        });
    }

    private void buzz(long ms) {
        if (vibrator == null || !vibrator.hasVibrator()) return;
        try {
            vibrator.vibrate(VibrationEffect.createOneShot(
                ms, VibrationEffect.DEFAULT_AMPLITUDE));
        } catch (Throwable t) {
            Log.w(TAG, "vibrate failed", t);
        }
    }
}
