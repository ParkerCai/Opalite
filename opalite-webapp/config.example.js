// Copy values you need into config.local.js (gitignored).
// Do not commit real keys or private inference endpoints.

window.OPALITE_API_KEY = 'PASTE_YOUR_KEY_HERE';

window.OPALITE_LOCAL = {
  preferredMode: 'gemini',
  wsUrl: '/ws',
  speechFallback: true
};

window.OPALITE_CV = {
  enabled: false,
  analysisIntervalMs: 2500,
  sendContextNotes: true,
  places365: {
    enabled: false,
    endpoint: 'http://localhost:8001/places365',
    timeoutMs: 3500,
    topK: 3
  },
  monodepth2: {
    enabled: false,
    endpoint: 'http://localhost:8001/monodepth2',
    timeoutMs: 4500
  },
  retinanet: {
    enabled: false,
    endpoint: 'http://localhost:8001/retinanet',
    timeoutMs: 3500,
    threshold: 0.35,
    maxDetections: 8
  }
};

window.OPALITE_EVAL = {
  enabled: true
};
