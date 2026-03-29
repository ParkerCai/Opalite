import { Places365Client } from './places365-client.js';
import { Monodepth2Client } from './monodepth2-client.js';
import { RetinaNetClient } from './retinanet-client.js';

export class CVEnhancementPipeline {
  constructor(config = {}) {
    this.config = {
      enabled: false,
      analysisIntervalMs: 2500,
      sendContextNotes: true,
      ...config
    };

    this.modules = [
      new Places365Client(this.config.places365),
      new Monodepth2Client(this.config.monodepth2),
      new RetinaNetClient(this.config.retinanet)
    ];

    this.lastAnalyzedAt = 0;
    this.lastSummary = '';
    this.latest = null;
    this.inFlight = null;
  }

  get isEnabled() {
    return Boolean(this.config.enabled && this.modules.some((module) => module.isEnabled));
  }

  shouldAnalyze(now = Date.now()) {
    if (!this.isEnabled) return false;
    if (this.inFlight) return false;
    return now - this.lastAnalyzedAt >= this.config.analysisIntervalMs;
  }

  async analyze(frame) {
    if (!this.isEnabled) return null;
    if (this.inFlight) return this.inFlight;

    this.inFlight = (async () => {
      const settled = await Promise.allSettled(
        this.modules
          .filter((module) => module.isEnabled)
          .map(async (module) => ({
            module: module.constructor.name,
            result: await module.analyze(frame)
          }))
      );

      const analysis = {
        timestamp: new Date().toISOString(),
        scene: null,
        depth: null,
        detections: null,
        errors: []
      };

      for (const item of settled) {
        if (item.status === 'rejected') {
          analysis.errors.push(item.reason?.message || 'unknown CV module failure');
          continue;
        }

        const result = item.value.result;
        if (!result) continue;

        if (result.module === 'places365') analysis.scene = result;
        if (result.module === 'monodepth2') analysis.depth = result;
        if (result.module === 'retinanet') analysis.detections = result;
      }

      analysis.summaryText = this.summarize(analysis);
      this.latest = analysis;
      this.lastSummary = analysis.summaryText || this.lastSummary;
      this.lastAnalyzedAt = Date.now();
      return analysis;
    })();

    try {
      return await this.inFlight;
    } finally {
      this.inFlight = null;
    }
  }

  summarize(analysis) {
    if (!analysis) return '';
    const parts = [];

    if (analysis.scene) {
      parts.push(this.modules[0].summarize(analysis.scene));
    }
    if (analysis.depth) {
      parts.push(this.modules[1].summarize(analysis.depth));
    }
    if (analysis.detections) {
      parts.push(this.modules[2].summarize(analysis.detections));
    }

    return parts.filter(Boolean).join('. ');
  }

  buildPrompt(basePrompt, analysis = this.latest) {
    if (!analysis || !analysis.summaryText) return basePrompt;
    return `${basePrompt}\n\nComputer vision priors (use only if they match the image): ${analysis.summaryText}`;
  }

  needsContextUpdate(analysis) {
    if (!analysis?.summaryText) return false;
    return analysis.summaryText !== this.lastSummary;
  }
}
