/**
 * Captures microphone audio via AudioWorklet, outputs base64 PCM16 chunks.
 */
export class AudioRecorder {
  constructor() {
    this.sampleRate = 16000;
    this.stream = null;
    this.audioContext = null;
    this.source = null;
    this.processor = null;
    this.onAudioData = null;
    this.isRecording = false;
    this.isMuted = false;
  }

  async start(onAudioData) {
    this.onAudioData = onAudioData;

    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        sampleRate: this.sampleRate,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true
      }
    });

    this.audioContext = new AudioContext({ sampleRate: this.sampleRate });
    this.source = this.audioContext.createMediaStreamSource(this.stream);

    await this.audioContext.audioWorklet.addModule('js/worklets/audio-processor.js');
    this.processor = new AudioWorkletNode(this.audioContext, 'audio-recorder-worklet');

    this.processor.port.onmessage = (event) => {
      if (!this.isRecording || this.isMuted) return;
      if (event.data.event === 'chunk' && this.onAudioData) {
        const base64 = this._arrayBufferToBase64(event.data.data.int16arrayBuffer);
        this.onAudioData(base64);
      }
    };

    this.source.connect(this.processor);
    this.processor.connect(this.audioContext.destination);
    this.isRecording = true;
  }

  toggleMute() {
    this.isMuted = !this.isMuted;
    if (this.stream) {
      this.stream.getAudioTracks().forEach(t => t.enabled = !this.isMuted);
    }
    return this.isMuted;
  }

  stop() {
    if (this.stream) {
      this.stream.getTracks().forEach(t => t.stop());
      this.stream = null;
    }
    if (this.audioContext) {
      this.audioContext.close();
      this.audioContext = null;
    }
    this.isRecording = false;
  }

  _arrayBufferToBase64(buffer) {
    const bytes = new Uint8Array(buffer);
    let binary = '';
    for (let i = 0; i < bytes.length; i++) {
      binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
  }
}
