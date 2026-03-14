/**
 * VisionGuide — Main application orchestrator.
 * Connects camera, microphone, and Gemini Live API together.
 */
import { GeminiClient } from './gemini-client.js';
import { AudioRecorder } from './audio-recorder.js';
import { AudioStreamer } from './audio-streamer.js';
import { CameraManager } from './camera.js';

// ── System Prompt ──────────────────────────────────────────────
const SYSTEM_PROMPT = `You are VisionGuide, a real-time AI vision assistant for people who are blind or have low vision. You see the world through the user's phone camera and describe it to help them navigate safely.

RULES:
1. SAFETY FIRST: Always prioritize immediate hazards. Call out obstacles, stairs, curbs, traffic, and moving objects urgently.
2. BE CONCISE: Use short, clear descriptions. "Door ahead, 10 feet. Steps going down, 3 steps." Not paragraphs.
3. SPATIAL AWARENESS: Use clock positions or relative directions. "Person approaching from your right." "Sign above you at 12 o'clock."
4. READ TEXT: Automatically read signs, labels, menus, price tags, screens when visible.
5. CONTEXT MEMORY: Remember the environment context. If you already described something, don't repeat unless it changes.
6. NATURAL SPEECH: Speak naturally but clearly. You're a companion, not a robot.
7. PROACTIVE: Don't wait to be asked. If you see something important, say it immediately.
8. DESCRIBE ON REQUEST: When the user asks "what do you see?" give a comprehensive scene description.
9. NAVIGATION: Help with wayfinding. "You're approaching an intersection. Crosswalk is straight ahead."
10. PEOPLE: Describe people's general appearance and actions when relevant for navigation. "Someone is standing in the doorway."

Start by introducing yourself briefly: "VisionGuide connected. I can see through your camera. Point me anywhere and I'll describe what's ahead."

Voice: Calm, confident, clear. Like a trusted friend walking beside them.`;

// ── Gemini Config ──────────────────────────────────────────────
function getConfig() {
  return {
    model: 'models/gemini-2.0-flash-live-001',
    generationConfig: {
      temperature: 0.7,
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
const fpsSelect = document.getElementById('fps-select');

// ── App State ──────────────────────────────────────────────────
let client = null;
let recorder = null;
let streamer = null;
let camera = null;
let audioContext = null;
let cameraInterval = null;
let wakeLock = null;
let speakingCheckInterval = null;

// ── Helpers ────────────────────────────────────────────────────
function setStatus(state, text) {
  statusDot.className = 'status-dot ' + state;
  statusText.textContent = text;
}

function showError(msg) {
  errorText.textContent = msg;
  errorBanner.classList.add('visible');
  setTimeout(() => errorBanner.classList.remove('visible'), 6000);
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
  localStorage.setItem('vg_apiKey', apiKey);

  startBtn.disabled = true;
  startBtn.textContent = 'Connecting…';
  setStatus('connecting', 'Connecting…');

  try {
    // 1. Connect to Gemini
    client = new GeminiClient(apiKey, getConfig());

    client.onSetupComplete = () => {
      setStatus('connected', 'Connected — listening');
      vibrate(200);
      // Trigger initial greeting
      client.sendText('I just connected. Introduce yourself and tell me you can see through my camera.');
    };

    client.onAudio = (bytes) => {
      if (streamer) {
        if (!streamer.isInitialized) streamer.initialize();
        streamer.streamAudio(bytes);
      }
    };

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

    // Track speaking state for pulse animation
    speakingCheckInterval = setInterval(() => {
      if (streamer && streamer.isSpeaking) {
        pulseRing.classList.add('active');
        setStatus('speaking', 'Speaking…');
      } else {
        pulseRing.classList.remove('active');
        if (client && client.isConnected) {
          setStatus('connected', 'Listening…');
        }
      }
    }, 150);

    // 3. Camera
    camera = new CameraManager({
      width: 640,
      quality: 0.4,
      facingMode: 'environment'
    });
    await camera.initialize(cameraPreview);

    const fps = parseInt(fpsSelect.value) || 2;
    cameraInterval = setInterval(() => {
      if (client && client.isConnected && camera && camera.isInitialized) {
        const frame = camera.capture();
        client.sendImage(frame);
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

  } catch (err) {
    showError('Failed to start: ' + err.message);
    startBtn.disabled = false;
    startBtn.textContent = 'Start VisionGuide';
    setStatus('error', 'Error');
  }
}

// ── Stop Session ───────────────────────────────────────────────
function stopSession() {
  if (cameraInterval) { clearInterval(cameraInterval); cameraInterval = null; }
  if (speakingCheckInterval) { clearInterval(speakingCheckInterval); speakingCheckInterval = null; }
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
  startBtn.textContent = 'Start VisionGuide';
  setStatus('disconnected', 'Disconnected');
}

// ── Event Listeners ────────────────────────────────────────────
startBtn.addEventListener('click', startSession);
stopBtn.addEventListener('click', stopSession);

micBtn.addEventListener('click', () => {
  if (!recorder) return;
  const muted = recorder.toggleMute();
  micBtn.classList.toggle('muted', muted);
  micBtn.querySelector('.btn-label').textContent = muted ? 'Unmute' : 'Mute';
  micBtn.setAttribute('aria-label', muted ? 'Unmute microphone' : 'Mute microphone');
  vibrate(50);
});

switchCamBtn.addEventListener('click', async () => {
  if (!camera) return;
  if (cameraInterval) { clearInterval(cameraInterval); cameraInterval = null; }
  await camera.switchCamera(cameraPreview);
  const fps = parseInt(fpsSelect.value) || 2;
  cameraInterval = setInterval(() => {
    if (client && client.isConnected && camera && camera.isInitialized) {
      client.sendImage(camera.capture());
    }
  }, 1000 / fps);
  vibrate(50);
});

// Restore saved API key
const savedKey = localStorage.getItem('vg_apiKey');
if (savedKey) apiKeyInput.value = savedKey;

// Re-acquire wake lock on visibility change
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && client && client.isConnected) {
    acquireWakeLock();
  }
});
