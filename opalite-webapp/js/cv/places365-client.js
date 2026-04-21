import { formatConfidence, postFrame } from './frame-utils.js';

function inferIndoorOutdoor(scene = '') {
  const lower = scene.toLowerCase();
  if (!lower) return 'unknown';

  const indoorHints = [
    'hallway', 'corridor', 'classroom', 'office', 'elevator', 'staircase',
    'lobby', 'cafeteria', 'restaurant', 'kitchen', 'bedroom', 'bathroom', 'store'
  ];
  const outdoorHints = [
    'street', 'sidewalk', 'crosswalk', 'campus', 'parking', 'courtyard',
    'road', 'park', 'plaza', 'building facade', 'bridge', 'alley'
  ];

  if (indoorHints.some((hint) => lower.includes(hint))) return 'indoor';
  if (outdoorHints.some((hint) => lower.includes(hint))) return 'outdoor';
  return 'unknown';
}

export class Places365Client {
  constructor(config = {}) {
    this.config = {
      enabled: false,
      endpoint: '',
      timeoutMs: 3500,
      topK: 3,
      ...config
    };
  }

  get isEnabled() {
    return Boolean(this.config.enabled && this.config.endpoint);
  }

  async analyze(frame) {
    if (!this.isEnabled) return null;

    const payload = await postFrame(
      this.config.endpoint,
      frame,
      { topK: this.config.topK },
      this.config.timeoutMs
    );

    const topK = Array.isArray(payload.topK) ? payload.topK : [];
    const best = payload.scene
      ? { label: payload.scene, score: payload.confidence }
      : topK[0] || null;

    if (!best || !best.label) return null;

    return {
      module: 'places365',
      scene: best.label,
      confidence: Number(best.score ?? payload.confidence ?? 0),
      indoorOutdoor: payload.indoorOutdoor || inferIndoorOutdoor(best.label),
      topK
    };
  }

  summarize(result) {
    if (!result) return null;
    const base = `scene ${result.scene} (${formatConfidence(result.confidence)})`;
    if (result.indoorOutdoor && result.indoorOutdoor !== 'unknown') {
      return `${base}, likely ${result.indoorOutdoor}`;
    }
    return base;
  }
}
