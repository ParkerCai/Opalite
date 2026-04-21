/**
 * Opalite — Main application orchestrator.
 * Connects camera, microphone, Gemini Live API, optional CV priors,
 * local backend mode, and lightweight session logging for evaluation.
 */
import { GeminiClient } from './gemini-client.js';
import { LocalClient } from './local-client.js';
import { AudioRecorder } from './audio-recorder.js';
import { AudioStreamer } from './audio-streamer.js';
import { CameraManager } from './camera.js';
import { CVEnhancementPipeline } from './cv/enhancement-pipeline.js';
import { SessionLogger } from './evaluation/session-logger.js';

// ── System Prompt ──────────────────────────────────────────────
const SYSTEM_PROMPT = `You are a navigation assistant. A user is walking and you see through their phone camera. Help them move safely.

When the user asks a question, answer it based on what you see. Keep answers under 10 words when possible. Be direct.

Only speak when spoken to, unless you see immediate danger (cars, stairs, drops, obstacles in the path). For danger, interrupt and warn immediately.

Good answers: "Clear path ahead." "Door on your left." "Sign says Exit." "Bench about 5 feet to the right."
Bad answers: "I can see a beautiful pathway stretching ahead with trees on both sides." Too long, too descriptive.

Never describe colors, textures, materials, or aesthetics. Never say "I can see" or "it appears." Just state facts. Never mention the phone or camera.`;

const FPS = 1; // Fixed at 1 FPS per Google's recommendation
const CV_CONFIG = window.OPALITE_CV || { enabled: false };
const EVAL_CONFIG = window.OPALITE_EVAL || { enabled: true };
const LOCAL_CONFIG = window.OPALITE_LOCAL || {};

// ── Gemini Config ──────────────────────────────────────────────
function getGeminiConfig() {
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

function buildDefaultLocalWsUrl() {
  const configuredUrl = LOCAL_CONFIG.wsUrl;
  if (configuredUrl) {
    if (configuredUrl.startsWith('ws://') || configuredUrl.startsWith('wss://')) {
      return configuredUrl;
    }
    if (configuredUrl.startsWith('/')) {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      return `${protocol}//${window.location.host}${configuredUrl}`;
    }
  }

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  if (!window.location.host) {
    return 'ws://localhost:8787/ws';
  }

  if (!window.location.port || window.location.port === '8787') {
    return `${protocol}//${window.location.host}/ws`;
  }

  return `${protocol}//${window.location.hostname}:8787/ws`;
}

function getSelectedMode() {
  return modeSelect.value === 'local' ? 'local' : 'gemini';
}

function applyModeUI() {
  const mode = getSelectedMode();
  const isGemini = mode === 'gemini';

  apiKeyGroup.classList.toggle('hidden', !isGemini);
  localEndpointGroup.classList.toggle('hidden', isGemini);
  startBtn.textContent = isGemini ? 'Start with Gemini' : 'Start Opalite Local';
  setupProviderLabel.textContent = isGemini ? 'Powered by Gemini Live API' : 'Powered by Opalite Local backend';
  modeHint.textContent = isGemini
    ? 'Gemini Live streams directly from the browser to Google\'s Live API.'
    : 'Local mode keeps the current frontend, streams frames and mic audio to a local WebSocket backend, and currently works best for tap-to-describe.';
}

// ── DOM Elements ───────────────────────────────────────────────
const setupScreen = document.getElementById('setup-screen');
const sessionScreen = document.getElementById('session-screen');
const modeSelect = document.getElementById('mode-select');
const apiKeyGroup = document.getElementById('api-key-group');
const apiKeyInput = document.getElementById('api-key');
const localEndpointGroup = document.getElementById('local-endpoint-group');
const localEndpointInput = document.getElementById('local-endpoint');
const modeHint = document.getElementById('mode-hint');
const setupProviderLabel = document.getElementById('setup-provider-label');
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

// ── App State ──────────────────────────────────────────────────
let client = null;
let recorder = null;
let streamer = null;
let camera = null;
let audioContext = null;
let cameraInterval = null;
let wakeLock = null;
let speakingCheckInterval = null;
let lastFrameSentAt = 0;
let enhancementPipeline = null;
let latestCVAnalysis = null;
let lastCVContextAt = 0;
let logger = null;
let lastStatusSignature = '';
let currentMode = 'gemini';
let currentLocalWsUrl = buildDefaultLocalWsUrl();

// ── Helpers ────────────────────────────────────────────────────
function setStatus(state, text) {
  statusDot.className = 'status-dot ' + state;
  statusText.textContent = text;
  const announce = document.getElementById('a11y-announce');
  if (announce) announce.textContent = text;

  const signature = `${state}:${text}`;
  if (signature !== lastStatusSignature) {
    lastStatusSignature = signature;
    logger?.log('status', { state, text });
  }
}

function showError(msg) {
  errorText.textContent = msg;
  errorBanner.classList.add('visible');
  setTimeout(() => errorBanner.classList.remove('visible'), 15000);
  logger?.log('error', { message: msg });
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
  if (wakeLock) {
    wakeLock.release();
    wakeLock = null;
  }
}

async function maybeRunCV(frame, source = 'stream') {
  if (!enhancementPipeline?.shouldAnalyze()) return latestCVAnalysis;

  const previousSummary = latestCVAnalysis?.summaryText || '';

  try {
    const analysis = await enhancementPipeline.analyze(frame);
    if (!analysis) return latestCVAnalysis;

    latestCVAnalysis = analysis;
    logger?.log('cv_analysis', {
      source,
      summaryText: analysis.summaryText,
      scene: analysis.scene?.scene || null,
      obstacleRisk: analysis.depth?.obstacleRisk || null,
      detections: analysis.detections?.labels || []
    });

    if (analysis.depth?.obstacleRisk === 'high') {
      vibrate([120, 60, 120, 60, 120]);
    }

    const hasNewSummary = analysis.summaryText && analysis.summaryText !== previousSummary;
    const enoughTimePassed = Date.now() - lastCVContextAt > 4000;
    if (client?.isConnected && CV_CONFIG.sendContextNotes !== false && hasNewSummary && enoughTimePassed) {
      client.sendText(`Navigation priors: ${analysis.summaryText}`, false);
      lastCVContextAt = Date.now();
      logger?.log('cv_context_note', { text: analysis.summaryText });
    }

    return analysis;
  } catch (error) {
    console.warn('CV enhancement pipeline failed', error);
    logger?.log('cv_error', { source, message: error.message });
    return latestCVAnalysis;
  }
}

function buildTapPrompt() {
  const basePrompt = 'Describe this image for safe navigation. Mention obstacles, text, and the safest direction in one or two short sentences.';
  if (!enhancementPipeline?.isEnabled) return basePrompt;
  return enhancementPipeline.buildPrompt(basePrompt, latestCVAnalysis);
}

function handleHazardText(text) {
  const lower = text.toLowerCase();
  const hazardWords = [
    'careful', 'watch out', 'hazard', 'stairs', 'step', 'curb', 'obstacle',
    'traffic', 'car', 'vehicle', 'hole', 'wet', 'drop', 'edge', 'stop'
  ];
  if (hazardWords.some((word) => lower.includes(word))) {
    vibrate([100, 50, 100, 50, 100]);
  }
}

function createClient() {
  if (currentMode === 'local') {
    return new LocalClient({
      wsUrl: currentLocalWsUrl,
      systemPrompt: SYSTEM_PROMPT,
      speechFallback: LOCAL_CONFIG.speechFallback !== false
    });
  }

  return new GeminiClient(apiKeyInput.value.trim(), getGeminiConfig());
}

function getListeningStatusText() {
  return currentMode === 'local' ? 'Tap to describe' : 'Listening…';
}

// ── Start Session ──────────────────────────────────────────────
async function startSession() {
  currentMode = getSelectedMode();

  if (currentMode === 'gemini') {
    const apiKey = apiKeyInput.value.trim();
    if (!apiKey) {
      showError('Please enter your Gemini API key');
      return;
    }
    localStorage.setItem('oe_apiKey', apiKey);
  } else {
    currentLocalWsUrl = localEndpointInput.value.trim() || buildDefaultLocalWsUrl();
    localEndpointInput.value = currentLocalWsUrl;
    localStorage.setItem('oe_localWsUrl', currentLocalWsUrl);
  }

  localStorage.setItem('oe_mode', currentMode);

  logger = new SessionLogger(EVAL_CONFIG);
  enhancementPipeline = new CVEnhancementPipeline(CV_CONFIG);
  latestCVAnalysis = null;
  lastCVContextAt = 0;

  startBtn.disabled = true;
  startBtn.textContent = currentMode === 'gemini' ? 'Connecting…' : 'Starting local mode…';
  setStatus('connecting', currentMode === 'gemini' ? 'Connecting…' : 'Connecting to local backend…');

  try {
    client = createClient();

    client.onSetupComplete = () => {
      setStatus('connected', currentMode === 'gemini' ? 'Connected, starting camera…' : 'Local backend ready, starting camera…');
      vibrate(200);
      logger?.log('setup_complete', {
        mode: currentMode,
        localWsUrl: currentMode === 'local' ? currentLocalWsUrl : null,
        backendInfo: client?.backendInfo || null
      });
    };

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
      setStatus('speaking', 'Speaking…');
      logger?.markAudioChunk(bytes.length);
    };

    client.onText = (text) => {
      console.log(`${currentMode} text:`, text);
      handleHazardText(text);
      logger?.markAIText(text);
    };

    client.onTurnComplete = () => {
      firstAudioChunk = true;
      setStatus('connected', getListeningStatusText());
      logger?.log('turn_complete');
    };

    client.onInterrupted = () => {
      if (streamer) {
        streamer.stop();
        streamer.isInitialized = false;
      }
      logger?.log('interrupted');
    };

    client.onError = (msg) => {
      showError(msg);
      setStatus('error', 'Error');
    };

    await client.connect();

    audioContext = new AudioContext();
    streamer = new AudioStreamer(audioContext);
    streamer.initialize();

    speakingCheckInterval = setInterval(() => {
      const isSpeaking = Boolean(streamer?.isSpeaking || client?.isSpeaking);
      if (isSpeaking) {
        pulseRing.classList.add('active');
        setStatus('speaking', 'Speaking…');
      } else {
        pulseRing.classList.remove('active');
        if (client && client.isConnected) {
          setStatus('connected', getListeningStatusText());
        }
      }
    }, 150);

    camera = new CameraManager({
      width: 768,
      quality: 0.6,
      facingMode: 'environment'
    });
    await camera.initialize(cameraPreview);

    cameraInterval = setInterval(() => {
      if (!client?.isConnected || !camera?.isInitialized) return;
      const frame = camera.capture();
      if (!frame) return;

      client.sendImage(frame);
      lastFrameSentAt = Date.now();
      logger?.markFrameSent({ mode: 'stream', transport: currentMode });
      maybeRunCV(frame, 'stream');

      if (!greetingSent) {
        greetingSent = true;
        setStatus('connected', getListeningStatusText());
      }
    }, 1000 / FPS);

    recorder = new AudioRecorder();
    await recorder.start((base64audio) => {
      if (client?.isConnected) {
        client.sendAudio(base64audio);
      }
    });

    await acquireWakeLock();

    setupScreen.classList.add('hidden');
    sessionScreen.classList.remove('hidden');
    logger?.log('session_started', {
      mode: currentMode,
      localEndpoint: currentMode === 'local' ? currentLocalWsUrl : null,
      cvEnhancements: enhancementPipeline?.isEnabled || false,
      fps: FPS
    });
  } catch (err) {
    showError('Failed to start: ' + err.message);
    startBtn.disabled = false;
    applyModeUI();
    setStatus('error', 'Error');
  }
}

// ── Stop Session ───────────────────────────────────────────────
function stopSession() {
  if (cameraInterval) {
    clearInterval(cameraInterval);
    cameraInterval = null;
  }
  if (speakingCheckInterval) {
    clearInterval(speakingCheckInterval);
    speakingCheckInterval = null;
  }
  if (recorder) {
    recorder.stop();
    recorder = null;
  }
  if (streamer) {
    streamer.stop();
    streamer = null;
  }
  if (camera) {
    camera.dispose();
    camera = null;
  }
  if (client) {
    client.disconnect();
    client = null;
  }
  if (audioContext) {
    audioContext.close();
    audioContext = null;
  }

  enhancementPipeline = null;
  latestCVAnalysis = null;
  releaseWakeLock();
  pulseRing.classList.remove('active');

  sessionScreen.classList.add('hidden');
  setupScreen.classList.remove('hidden');
  startBtn.disabled = false;
  applyModeUI();
  setStatus('disconnected', 'Disconnected');
  logger?.log('session_stopped');
}

// ── Event Listeners ────────────────────────────────────────────
startBtn.addEventListener('click', startSession);
modeSelect.addEventListener('change', () => {
  applyModeUI();
});

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
  logger?.log('mic_toggle', { muted });
});

document.getElementById('tap-zone').addEventListener('click', async (e) => {
  e.preventDefault();
  if (!client?.isConnected || !camera?.isInitialized) return;

  vibrate([50, 30, 50]);
  const frame = camera.capture();
  if (!frame) return;

  await maybeRunCV(frame, 'tap');
  const prompt = buildTapPrompt();
  client.sendImageWithText(frame, prompt);
  logger?.markFrameSent({ mode: 'tap_describe', transport: currentMode });
  logger?.log('tap_describe', { prompt, cvSummary: latestCVAnalysis?.summaryText || null });
});

switchCamBtn.addEventListener('click', async () => {
  if (!camera) return;
  if (cameraInterval) {
    clearInterval(cameraInterval);
    cameraInterval = null;
  }
  await camera.switchCamera(cameraPreview);
  cameraInterval = setInterval(() => {
    if (!client?.isConnected || !camera?.isInitialized) return;
    const frame = camera.capture();
    if (!frame) return;
    client.sendImage(frame);
    logger?.markFrameSent({ mode: 'stream', switchedCamera: true, transport: currentMode });
    maybeRunCV(frame, 'stream');
  }, 1000 / FPS);
  vibrate(50);
  logger?.log('camera_switch', { facingMode: camera.facingMode });
});

const savedKey = window.OPALITE_API_KEY || localStorage.getItem('oe_apiKey');
if (savedKey && savedKey !== 'PASTE_YOUR_KEY_HERE') {
  apiKeyInput.value = savedKey;
}

const savedMode = localStorage.getItem('oe_mode') || LOCAL_CONFIG.preferredMode || 'gemini';
modeSelect.value = savedMode === 'local' ? 'local' : 'gemini';
currentMode = getSelectedMode();

const savedLocalWsUrl = localStorage.getItem('oe_localWsUrl') || buildDefaultLocalWsUrl();
localEndpointInput.value = savedLocalWsUrl;
currentLocalWsUrl = savedLocalWsUrl;

applyModeUI();

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && client?.isConnected) {
    acquireWakeLock();
  }
});
