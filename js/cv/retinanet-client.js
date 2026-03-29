import { bboxCenter, formatConfidence, uniqueStrings, postFrame } from './frame-utils.js';

const DEFAULT_PATH_CLASSES = [
  'person', 'bicycle', 'car', 'motorcycle', 'bus', 'truck', 'chair', 'bench',
  'stroller', 'suitcase', 'dog', 'backpack', 'potted plant'
];

function normalizeDetection(det = {}) {
  return {
    label: det.label || det.class || det.category || 'object',
    score: Number(det.score ?? det.confidence ?? 0),
    bbox: det.bbox || det.box || det.xyxy || null
  };
}

function selectPathObstacles(detections, config) {
  const pathClasses = config.pathClasses || DEFAULT_PATH_CLASSES;
  return detections.filter((det) => {
    if (!pathClasses.includes(det.label)) return false;
    const center = bboxCenter(det.bbox);
    if (!center) return false;

    const nearBottom = center.y > 0.45;
    const nearCenter = center.x > 0.25 && center.x < 0.75;
    const sizeable = center.width > 0.08 || center.height > 0.08;
    return nearBottom && nearCenter && sizeable;
  });
}

export class RetinaNetClient {
  constructor(config = {}) {
    this.config = {
      enabled: false,
      endpoint: '',
      timeoutMs: 3500,
      threshold: 0.35,
      maxDetections: 8,
      pathClasses: DEFAULT_PATH_CLASSES,
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
      {
        threshold: this.config.threshold,
        maxDetections: this.config.maxDetections
      },
      this.config.timeoutMs
    );

    const detections = (Array.isArray(payload.detections) ? payload.detections : [])
      .map(normalizeDetection)
      .filter((det) => det.score >= this.config.threshold)
      .sort((a, b) => b.score - a.score)
      .slice(0, this.config.maxDetections);

    if (!detections.length) return null;

    const pathObstacles = selectPathObstacles(detections, this.config);

    return {
      module: 'retinanet',
      detections,
      pathObstacles,
      labels: uniqueStrings(detections.map((det) => det.label))
    };
  }

  summarize(result) {
    if (!result) return null;

    const labelList = result.labels.slice(0, 4).join(', ');
    if (!result.pathObstacles.length) {
      return `detected ${labelList}`;
    }

    const topObstacle = result.pathObstacles[0];
    return `detected ${labelList}; obstacle in path: ${topObstacle.label} (${formatConfidence(topObstacle.score)})`;
  }
}
