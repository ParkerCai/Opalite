/**
 * Opalite — Main application orchestrator.
 * Connects camera, microphone, and Gemini Live API together.
 */
import { GeminiClient } from './gemini-client.js';
import { AudioRecorder } from './audio-recorder.js';
import { AudioStreamer } from './audio-streamer.js';
import { CameraManager } from './camera.js';

// ── System Prompt ──────────────────────────────────────────────
const SYSTEM_PROMPT = `You are Opalite, a vision assistant helping a user navigate the world through their phone camera. You receive continuous camera frames.

YOUR JOB: Proactively narrate what you see as it changes. Do not wait to be asked. You are the user's eyes.

WHEN TO SPEAK:
- Always describe what is in the current image when prompted
- Hazard detected: stairs, curb, obstacle, traffic, wet floor — say it urgently, start with "Alert."
- Visible text: read signs, labels, doors, menus automatically
- NEVER say "no changes", "nothing changed", "no significant changes", or anything about changes. Just describe what you see or stay completely silent.

HOW TO SPEAK:
- Short. Max 2 sentences per update.
- Objects and positions only. No artistic language.
- Use: ahead, left, right, close, far
- For hazards, start with "Careful:" or "Watch out:"
- Speak fast. The user needs quick information.

NEVER: invent objects not visible, describe the phone/hands, use background/foreground terms, repeat yourself.

IMPORTANT: When you detect a hazard (stairs, curb, obstacle, traffic, wet floor, drop, edge), say the word "ALERT" before your description. Example: "Alert. Stairs ahead, going down."
This helps the app trigger a vibration warning for the user.`;

// ── Gemini Config ──────────────────────────────────────────────
function getConfig() {
  return {
    model: 'models/gemini-2.5-flash-native-audio-latest',
    generationConfig: {
      temperature: 0.3,
      responseModalities: ['AUDIO'],
      speechConfig: {
        voiceConfig: {
          prebuiltVoiceConfig: { voiceName: 'Kore' }
        }
      }
    },
    systemInstruction: { parts: [{ text: SYSTEM_PROMPT }] },
    tools: {},
    safetySettings: [
      { category: 'HARM_CATEGORY_HARASSMENT', threshold: 'BLOCK_NONE' },
      { category: 'HARM_CATEGORY_DANGEROUS_CONTENT', threshold: 'BLOCK_NONE' },
      { category: 'HARM_CATEGORY_SEXUALLY_EXPLICIT', threshold: 'BLOCK_NONE' },
      { category: 'HARM_CATEGORY_HATE_SPEECH', threshold: 'BLOCK_NONE' },
      { category: 'HARM_CATEGORY_CIVIC_INTEGRITY', threshold: 'BLOCK_NONE' }
    ]
  };
}

// ── DOM Elements ───────────────────────────────────────────────
const setupScreen = document.getElementById('setup-screen');
const sessionScreen = document.getElementById('session-screen');
const apiKeyInput = document.getElementById('api-key');
const startBtn = document.getElementById('start-btn');
const stopBtn = document.getElementById('stop-btn');
const micBtn = document.getElementById('mic-btn');
const switchCamBtn = document.getElementById('switch-cam-btn');
const statusDot = document.getElementById('status-dot');
const statusText = document.getElementById('status-text');
const cameraPreview = document.getElementById('camera-preview');
const pulseRing = document.getElementById('pulse-ring');
const errorBanner = document.getElementById('error-banner');
const errorText = document.getElementById('error-text');
const FPS = 1; // Fixed at 1 FPS per Google's recommendation

// ── App State ──────────────────────────────────────────────────
let client = null;
let recorder = null;
let streamer = null;
let camera = null;
let audioContext = null;
let cameraInterval = null;
let wakeLock = null;
let speakingCheckInterval = null;
let nudgeInterval = null;
let lastAISpokeAt = 0;
let lastFrameSentAt = 0;

// ── Helpers ────────────────────────────────────────────────────
function setStatus(state, text) {
  statusDot.className = 'status-dot ' + state;
  statusText.textContent = text;
  // Announce to TalkBack/screen readers via aria-live region
  const announce = document.getElementById('a11y-announce');
  if (announce) announce.textContent = text;
}

function showError(msg) {
  errorText.textContent = msg;
  errorBanner.classList.add('visible');
  setTimeout(() => errorBanner.classList.remove('visible'), 15000);
}

function vibrate(pattern) {
  if (navigator.vibrate) navigator.vibrate(pattern);
}

async function acquireWakeLock() {
  try {
    if ('wakeLock' in navigator) {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    }
  } catch (_) {}
}

function releaseWakeLock() {
  if (wakeLock) { wakeLock.release(); wakeLock = null; }
}

// ── Start Session ──────────────────────────────────────────────
async function startSession() {
  const apiKey = apiKeyInput.value.trim();
  if (!apiKey) {
    showError('Please enter your Gemini API key');
    return;
  }
  localStorage.setItem('oe_apiKey', apiKey);

  startBtn.disabled = true;
  startBtn.textContent = 'Connecting…';
  setStatus('connecting', 'Connecting…');

  try {
    // 1. Connect to Gemini
    client = new GeminiClient(apiKey, getConfig());

    client.onSetupComplete = () => {
      setStatus('connected', 'Connected — starting camera…');
      vibrate(200);
      // Don't send any text yet — wait for camera frames to arrive first
      // The AI will start describing once it receives real frames
    };

    // Flag to send greeting after first frame
    let greetingSent = false;

    let firstAudioChunk = true;
    client.onAudio = (bytes) => {
      if (!streamer) return;
      if (!streamer.isInitialized) {
        streamer.initialize();
      }
      if (firstAudioChunk) {
        vibrate(30);
        firstAudioChunk = false;
      }
      streamer.streamAudio(bytes);
      lastAISpokeAt = Date.now();
      setStatus('speaking', 'Speaking…');
    };

    // Hazard detection: when AI sends text containing hazard words, vibrate strongly
    client.onText = (text) => {
      const lower = text.toLowerCase();
      const hazardWords = ['careful', 'watch out', 'hazard', 'stairs', 'step', 'curb',
        'obstacle', 'traffic', 'car', 'vehicle', 'hole', 'wet', 'drop', 'edge', 'stop'];
      if (hazardWords.some(w => lower.includes(w))) {
        vibrate([100, 50, 100, 50, 100]); // strong triple-pulse for hazard
      }
    };

    client.onTurnComplete = () => {
      firstAudioChunk = true;
      setStatus('connected', 'Listening…');
    };

    // Auto-narration: every 4 seconds of silence, nudge the model
    lastAISpokeAt = Date.now();
    nudgeInterval = setInterval(() => {
      if (!client || !client.isConnected) return;
      if (Date.now() - lastAISpokeAt > 4000) {
        if (camera && camera.isInitialized) {
          const frame = camera.capture();
          if (frame) {
            client.sendImageWithText(frame, 'Describe what you see.');
            lastAISpokeAt = Date.now();
          }
        }
      }
    }, 2000);

    client.onText = (text) => {
      console.log('Gemini text:', text);
    };

    client.onInterrupted = () => {
      if (streamer) { streamer.stop(); streamer.isInitialized = false; }
    };

    client.onTurnComplete = () => {
      setStatus('connected', 'Listening…');
    };

    client.onError = (msg) => {
      showError(msg);
      setStatus('error', 'Error');
    };

    await client.connect();

    // 2. Audio context + streamer
    audioContext = new AudioContext();
    streamer = new AudioStreamer(audioContext);
    streamer.initialize();

    // Track speaking state for pulse animation + latency display
    speakingCheckInterval = setInterval(() => {
      if (streamer && streamer.isSpeaking) {
        pulseRing.classList.add('active');
        // Don't overwrite latency display
        if (!statusText.textContent.includes('ms')) {
          setStatus('speaking', 'Speaking…');
        }
      } else {
        pulseRing.classList.remove('active');
        if (client && client.isConnected) {
          setStatus('connected', 'Listening…');
        }
      }
    }, 150);

    // 3. Camera
    camera = new CameraManager({
      width: 768,
      quality: 0.6,
      facingMode: 'environment'
    });
    await camera.initialize(cameraPreview);

    const fps = FPS;
    cameraInterval = setInterval(() => {
      if (client && client.isConnected && camera && camera.isInitialized) {
        const frame = camera.capture();
        if (!frame) return; // video not ready yet
        client.sendImage(frame);
        lastFrameSentAt = Date.now();

        // Don't auto-trigger on startup — let user tap or speak first
        if (!greetingSent) {
          greetingSent = true;
          setStatus('connected', 'Tap screen or speak');
        }
      }
    }, 1000 / fps);

    // 4. Microphone
    recorder = new AudioRecorder();
    await recorder.start((base64audio) => {
      if (client && client.isConnected) {
        client.sendAudio(base64audio);
      }
    });

    // 5. Wake lock
    await acquireWakeLock();

    // Show session screen
    setupScreen.classList.add('hidden');
    sessionScreen.classList.remove('hidden');

    // No fullscreen API — causes layout issues on some Android devices

  } catch (err) {
    showError('Failed to start: ' + err.message);
    startBtn.disabled = false;
    startBtn.textContent = 'Start';
    setStatus('error', 'Error');
  }
}

// ── Stop Session ───────────────────────────────────────────────
function stopSession() {
  if (cameraInterval) { clearInterval(cameraInterval); cameraInterval = null; }
  if (speakingCheckInterval) { clearInterval(speakingCheckInterval); speakingCheckInterval = null; }
  if (nudgeInterval) { clearInterval(nudgeInterval); nudgeInterval = null; }
  if (recorder) { recorder.stop(); recorder = null; }
  if (streamer) { streamer.stop(); streamer = null; }
  if (camera) { camera.dispose(); camera = null; }
  if (client) { client.disconnect(); client = null; }
  if (audioContext) { audioContext.close(); audioContext = null; }
  releaseWakeLock();
  pulseRing.classList.remove('active');

  sessionScreen.classList.add('hidden');
  setupScreen.classList.remove('hidden');
  startBtn.disabled = false;
  startBtn.textContent = 'Start Opalite';
  setStatus('disconnected', 'Disconnected');
}

// ── Event Listeners ────────────────────────────────────────────
startBtn.addEventListener('click', startSession);

// End button → show confirmation modal
stopBtn.addEventListener('click', () => {
  document.getElementById('exit-modal').classList.remove('hidden');
});
document.getElementById('exit-cancel').addEventListener('click', () => {
  document.getElementById('exit-modal').classList.add('hidden');
});
document.getElementById('exit-confirm').addEventListener('click', () => {
  document.getElementById('exit-modal').classList.add('hidden');
  stopSession();
});

micBtn.addEventListener('click', () => {
  if (!recorder) return;
  const muted = recorder.toggleMute();
  micBtn.classList.toggle('muted', muted);
  micBtn.querySelector('.btn-label').textContent = muted ? 'Unmute' : 'Mute';
  micBtn.setAttribute('aria-label', muted ? 'Unmute microphone' : 'Mute microphone');
  vibrate(50);
});

// Tap = capture fresh frame + send with text in one turn.
// This forces the model to look at THIS specific image.
document.getElementById('tap-zone').addEventListener('click', (e) => {
  e.preventDefault();
  if (!client || !client.isConnected) return;
  if (!camera || !camera.isInitialized) return;
  vibrate([50, 30, 50]);
  const frame = camera.capture();
  if (frame) {
    client.sendImageWithText(frame, 'Describe this image.');
  }
});

switchCamBtn.addEventListener('click', async () => {
  if (!camera) return;
  if (cameraInterval) { clearInterval(cameraInterval); cameraInterval = null; }
  await camera.switchCamera(cameraPreview);
  const fps = FPS;
  cameraInterval = setInterval(() => {
    if (client && client.isConnected && camera && camera.isInitialized) {
      client.sendImage(camera.capture());
    }
  }, 1000 / fps);
  vibrate(50);
});

// Restore saved API key (config.local.js > localStorage)
const savedKey = window.OPALITE_API_KEY || localStorage.getItem('oe_apiKey');
if (savedKey && savedKey !== 'PASTE_YOUR_KEY_HERE') apiKeyInput.value = savedKey;

// Re-acquire wake lock on visibility change
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && client && client.isConnected) {
    acquireWakeLock();
  }
});
