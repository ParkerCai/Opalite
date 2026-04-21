export class SessionLogger {
  constructor(config = {}) {
    this.config = {
      enabled: true,
      ...config
    };
    this.startedAt = Date.now();
    this.events = [];
    this.pendingResponseAt = null;

    window.OpaliteEvaluation = {
      exportJSON: (meta = {}) => this.exportJSON(meta),
      downloadJSON: (filename = 'opalite-eval-run.json', meta = {}) => this.downloadJSON(filename, meta),
      summary: () => this.summary()
    };
  }

  log(type, payload = {}) {
    if (!this.config.enabled) return;
    this.events.push({
      t: Date.now(),
      isoTime: new Date().toISOString(),
      type,
      payload
    });
  }

  markFrameSent(payload = {}) {
    this.pendingResponseAt = Date.now();
    this.log('frame_sent', payload);
  }

  markAIText(text, payload = {}) {
    if (this.pendingResponseAt) {
      this.log('response_latency_ms', {
        latencyMs: Date.now() - this.pendingResponseAt,
        ...payload
      });
      this.pendingResponseAt = null;
    }
    this.log('ai_text', { text, ...payload });
  }

  markAudioChunk(byteLength) {
    this.log('ai_audio_chunk', { byteLength });
  }

  summary() {
    const counts = {};
    const latency = [];

    for (const event of this.events) {
      counts[event.type] = (counts[event.type] || 0) + 1;
      if (event.type === 'response_latency_ms' && Number.isFinite(event.payload.latencyMs)) {
        latency.push(event.payload.latencyMs);
      }
    }

    const averageLatencyMs = latency.length
      ? latency.reduce((sum, value) => sum + value, 0) / latency.length
      : null;

    return {
      durationSec: Math.round((Date.now() - this.startedAt) / 1000),
      eventCounts: counts,
      averageLatencyMs,
      latencySamples: latency.length
    };
  }

  exportJSON(meta = {}) {
    return {
      meta,
      summary: this.summary(),
      events: this.events
    };
  }

  downloadJSON(filename = 'opalite-eval-run.json', meta = {}) {
    const blob = new Blob([JSON.stringify(this.exportJSON(meta), null, 2)], {
      type: 'application/json'
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
}
