/**
 * Manages camera capture: initializes stream, captures JPEG frames,
 * supports front/back camera switching on mobile.
 */
export class CameraManager {
  constructor({ width = 640, quality = 0.5, facingMode = 'environment' } = {}) {
    this.width = width;
    this.quality = quality;
    this.facingMode = facingMode;
    this.stream = null;
    this.video = null;
    this.canvas = null;
    this.ctx = null;
    this.isInitialized = false;
  }

  async initialize(previewElement) {
    const constraints = {
      video: {
        width: { ideal: 1280 },
        height: { ideal: 720 },
        facingMode: this.facingMode
      }
    };

    this.stream = await navigator.mediaDevices.getUserMedia(constraints);

    this.video = document.createElement('video');
    this.video.srcObject = this.stream;
    this.video.playsInline = true;
    this.video.muted = true;
    this.video.autoplay = true;

    // Attach to preview element if provided
    if (previewElement) {
      previewElement.srcObject = this.stream;
      previewElement.playsInline = true;
      previewElement.muted = true;
      previewElement.autoplay = true;
      await previewElement.play().catch(() => {});
    }

    await this.video.play();

    // Set up capture canvas
    const vw = this.video.videoWidth;
    const vh = this.video.videoHeight;
    const aspect = vh / vw;
    this.canvas = document.createElement('canvas');
    this.canvas.width = this.width;
    this.canvas.height = Math.round(this.width * aspect);
    this.ctx = this.canvas.getContext('2d');

    this.isInitialized = true;
  }

  /**
   * Capture a single JPEG frame as base64 (no data: prefix).
   */
  capture() {
    if (!this.isInitialized) throw new Error('Camera not initialized');
    // Skip if video isn't actually playing yet
    if (this.video.readyState < 2) return null;
    this.ctx.drawImage(this.video, 0, 0, this.canvas.width, this.canvas.height);
    return this.canvas.toDataURL('image/jpeg', this.quality).split(',')[1];
  }

  /**
   * Switch between front and back cameras.
   */
  async switchCamera(previewElement) {
    this.facingMode = this.facingMode === 'environment' ? 'user' : 'environment';
    this.dispose();
    await this.initialize(previewElement);
  }

  dispose() {
    if (this.stream) {
      this.stream.getTracks().forEach(t => t.stop());
      this.stream = null;
    }
    if (this.video) {
      this.video.srcObject = null;
      this.video = null;
    }
    this.canvas = null;
    this.ctx = null;
    this.isInitialized = false;
  }
}
