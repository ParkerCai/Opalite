/**
 * AudioWorklet processor for capturing microphone input as PCM16 chunks.
 * Runs in a separate audio thread for low-latency processing.
 */
class AudioRecorderWorklet extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = [];
    this.bufferSize = 2048; // ~128ms at 16kHz
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || !input[0]) return true;

    const channelData = input[0];

    // Convert float32 samples to int16
    for (let i = 0; i < channelData.length; i++) {
      const s = Math.max(-1, Math.min(1, channelData[i]));
      this.buffer.push(s < 0 ? s * 0x8000 : s * 0x7fff);
    }

    // When buffer is full, send chunk to main thread
    if (this.buffer.length >= this.bufferSize) {
      const int16Array = new Int16Array(this.buffer.splice(0, this.bufferSize));
      this.port.postMessage({
        event: 'chunk',
        data: { int16arrayBuffer: int16Array.buffer }
      }, [int16Array.buffer]);
    }

    return true;
  }
}

registerProcessor('audio-recorder-worklet', AudioRecorderWorklet);
