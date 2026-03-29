import { formatConfidence, postFrame } from './frame-utils.js';

function riskFromDistance(distanceM) {
  if (distanceM == null) return 'unknown';
  if (distanceM < 1.0) return 'high';
  if (distanceM < 2.0) return 'medium';
  return 'low';
}

export class Monodepth2Client {
  constructor(config = {}) {
    this.config = {
      enabled: false,
      endpoint: '',
      timeoutMs: 4500,
      ...config
    };
  }

  get isEnabled() {
    return Boolean(this.config.enabled && this.config.endpoint);
  }

  async analyze(frame) {
    if (!this.isEnabled) return null;

    const payload = await postFrame(this.config.endpoint, frame, {}, this.config.timeoutMs);
    const nearestDistanceM = Number(payload.nearestDistanceM ?? payload.minDistanceM ?? NaN);
    const centerDistanceM = Number(payload.centerDistanceM ?? payload.forwardDistanceM ?? NaN);

    const result = {
      module: 'monodepth2',
      nearestDistanceM: Number.isFinite(nearestDistanceM) ? nearestDistanceM : null,
      centerDistanceM: Number.isFinite(centerDistanceM) ? centerDistanceM : null,
      freeSpaceDirection: payload.freeSpaceDirection || 'unknown',
      depthConfidence: Number(payload.confidence ?? payload.depthConfidence ?? 0),
      obstacleRisk: payload.obstacleRisk || riskFromDistance(nearestDistanceM),
      notes: payload.notes || null
    };

    if (
      result.nearestDistanceM == null &&
      result.centerDistanceM == null &&
      result.freeSpaceDirection === 'unknown'
    ) {
      return null;
    }

    return result;
  }

  summarize(result) {
    if (!result) return null;

    const parts = [];
    if (result.nearestDistanceM != null) {
      parts.push(`nearest obstacle ${result.nearestDistanceM.toFixed(1)} m`);
    }
    if (result.centerDistanceM != null) {
      parts.push(`forward depth ${result.centerDistanceM.toFixed(1)} m`);
    }
    if (result.freeSpaceDirection && result.freeSpaceDirection !== 'unknown') {
      parts.push(`more open space on the ${result.freeSpaceDirection}`);
    }
    if (result.obstacleRisk && result.obstacleRisk !== 'unknown') {
      parts.push(`risk ${result.obstacleRisk}`);
    }
    if (result.depthConfidence) {
      parts.push(`depth confidence ${formatConfidence(result.depthConfidence)}`);
    }

    return parts.join(', ');
  }
}
