/**
 * Streams PCM16 audio from the Gemini API for real-time playback.
 * Buffers chunks and schedules them with precise Web Audio API timing.
 */
export class AudioStreamer {
  constructor(audioContext) {
    this.context = audioContext;
    this.sampleRate = 24000;
    this.bufferSize = Math.floor(this.sampleRate * 0.2); // 200ms — balance latency and reliability
    this.audioQueue = [];
    this.processingBuffer = new Float32Array(0);
    this.isPlaying = false;
    this.scheduledTime = 0;
    this.initialBufferTime = 0.01; // 10ms — start playing almost immediately
    this.isInitialized = false;
    this.scheduledSources = new Set();
    this.checkInterval = null;

    // Gain node for volume control + fade out on stop
    this.gainNode = this.context.createGain();
    this.gainNode.connect(this.context.destination);
  }

  initialize() {
    if (this.context.state === 'suspended') {
      this.context.resume();
    }
    this.isStreamComplete = false;
    this.scheduledTime = this.context.currentTime + this.initialBufferTime;
    // Cancel any pending gain ramps (from stop() fade-out) before restoring volume
    this.gainNode.gain.cancelScheduledValues(this.context.currentTime);
    this.gainNode.gain.setValueAtTime(1, this.context.currentTime);
    this.isInitialized = true;
  }

  /**
   * Feed raw PCM16 bytes (Uint8Array) from the API response.
   */
  streamAudio(chunk) {
    if (!this.isInitialized) return;

    // Convert Int16 → Float32
    const float32 = new Float32Array(chunk.length / 2);
    const view = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    for (let i = 0; i < float32.length; i++) {
      float32[i] = view.getInt16(i * 2, true) / 32768;
    }

    // Append to processing buffer
    const newBuf = new Float32Array(this.processingBuffer.length + float32.length);
    newBuf.set(this.processingBuffer);
    newBuf.set(float32, this.processingBuffer.length);
    this.processingBuffer = newBuf;

    // Prevent memory runaway
    if (this.processingBuffer.length > this.bufferSize * 4) {
      this.processingBuffer = new Float32Array(0);
    }

    // Split into playable chunks
    while (this.processingBuffer.length >= this.bufferSize) {
      this.audioQueue.push(this.processingBuffer.slice(0, this.bufferSize));
      this.processingBuffer = this.processingBuffer.slice(this.bufferSize);
    }

    if (!this.isPlaying) {
      this.isPlaying = true;
      this.scheduledTime = this.context.currentTime + this.initialBufferTime;
      this._scheduleNext();
    }
  }

  _scheduleNext() {
    if (!this.isPlaying) return;
    const AHEAD = 0.2;

    while (this.audioQueue.length > 0 && this.scheduledTime < this.context.currentTime + AHEAD) {
      const data = this.audioQueue.shift();
      const buf = this.context.createBuffer(1, data.length, this.sampleRate);
      buf.getChannelData(0).set(data);
      const src = this.context.createBufferSource();
      src.buffer = buf;
      src.connect(this.gainNode);

      this.scheduledSources.add(src);
      src.onended = () => this.scheduledSources.delete(src);

      const t = Math.max(this.scheduledTime, this.context.currentTime);
      src.start(t);
      this.scheduledTime = t + buf.duration;
    }

    if (this.audioQueue.length === 0 && this.processingBuffer.length < this.bufferSize) {
      // Wait for more data
      if (!this.checkInterval) {
        this.checkInterval = setInterval(() => {
          if (this.audioQueue.length > 0) {
            clearInterval(this.checkInterval);
            this.checkInterval = null;
            this._scheduleNext();
          }
        }, 50);
      }
    } else {
      const nextMs = (this.scheduledTime - this.context.currentTime) * 1000;
      setTimeout(() => this._scheduleNext(), Math.max(0, nextMs - 50));
    }
  }

  stop() {
    this.isPlaying = false;
    for (const src of this.scheduledSources) {
      try { src.stop(); src.disconnect(); } catch (_) {}
    }
    this.scheduledSources.clear();
    this.audioQueue = [];
    this.processingBuffer = new Float32Array(0);
    if (this.checkInterval) {
      clearInterval(this.checkInterval);
      this.checkInterval = null;
    }
    try {
      this.gainNode.gain.linearRampToValueAtTime(0, this.context.currentTime + 0.1);
    } catch (_) {}
  }

  /**
   * Clear queued audio without full stop/reinit cycle.
   * Avoids the gain fade-out that causes stutter on restart.
   */
  clear() {
    for (const src of this.scheduledSources) {
      try { src.stop(); src.disconnect(); } catch (_) {}
    }
    this.scheduledSources.clear();
    this.audioQueue = [];
    this.processingBuffer = new Float32Array(0);
    this.isPlaying = false;
    if (this.checkInterval) {
      clearInterval(this.checkInterval);
      this.checkInterval = null;
    }
    // Keep gain at 1 — no fade out
    this.gainNode.gain.cancelScheduledValues(this.context.currentTime);
    this.gainNode.gain.setValueAtTime(1, this.context.currentTime);
    this.scheduledTime = this.context.currentTime + this.initialBufferTime;
  }

  /** Is there audio currently scheduled/playing? */
  get isSpeaking() {
    return this.scheduledSources.size > 0 || this.audioQueue.length > 0;
  }
}
