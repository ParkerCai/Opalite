/**
 * WebSocket client for the Gemini Live API (BidiGenerateContent).
 * Handles setup, sending media/text, and receiving audio/text responses.
 */
export class GeminiClient {
  constructor(apiKey, config) {
    this.apiKey = apiKey;
    this.config = config;
    this.ws = null;
    this.onAudio = null;    // (Uint8Array) => void
    this.onText = null;     // (string) => void
    this.onTurnComplete = null;
    this.onInterrupted = null;
    this.onSetupComplete = null;
    this.onError = null;
  }

  get url() {
    return `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContent?key=${this.apiKey}`;
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);

      this.ws.addEventListener('open', () => {
        // Send setup config
        this._send({ setup: this.config });
        resolve();
      });

      this.ws.addEventListener('error', (e) => {
        const msg = 'WebSocket error: ' + (e.message || 'connection failed');
        if (this.onError) this.onError(msg);
        reject(new Error(msg));
      });

      this.ws.addEventListener('close', (e) => {
        console.info('WebSocket closed', e.code, e.reason);
        if (this.onError && e.code !== 1000) {
          this.onError('Connection closed: ' + (e.reason || 'unknown'));
        }
      });

      this.ws.addEventListener('message', async (event) => {
        if (event.data instanceof Blob) {
          await this._handleMessage(event.data);
        }
      });
    });
  }

  disconnect() {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }

  get isConnected() {
    return this.ws && this.ws.readyState === WebSocket.OPEN;
  }

  // --- Send methods ---

  sendAudio(base64pcm) {
    this._send({
      realtimeInput: {
        mediaChunks: [{ mimeType: 'audio/pcm', data: base64pcm }]
      }
    });
  }

  sendImage(base64jpeg) {
    this._send({
      realtimeInput: {
        mediaChunks: [{ mimeType: 'image/jpeg', data: base64jpeg }]
      }
    });
  }

  sendText(text, endOfTurn = true) {
    this._send({
      clientContent: {
        turns: [{ role: 'user', parts: [{ text }] }],
        turnComplete: endOfTurn
      }
    });
  }

  // --- Internal ---

  async _handleMessage(blob) {
    const text = await blob.text();
    let response;
    try {
      response = JSON.parse(text);
    } catch {
      console.warn('Non-JSON message from Gemini');
      return;
    }

    // Setup complete acknowledgement
    if (response.setupComplete) {
      if (this.onSetupComplete) this.onSetupComplete();
      return;
    }

    // Server content (audio, text, interruptions, turn complete)
    if (response.serverContent) {
      const sc = response.serverContent;

      if (sc.interrupted) {
        if (this.onInterrupted) this.onInterrupted();
        return;
      }

      if (sc.turnComplete) {
        if (this.onTurnComplete) this.onTurnComplete();
      }

      if (sc.modelTurn && sc.modelTurn.parts) {
        for (const part of sc.modelTurn.parts) {
          // Audio part
          if (part.inlineData && part.inlineData.mimeType &&
              part.inlineData.mimeType.startsWith('audio/pcm')) {
            if (this.onAudio) {
              const raw = atob(part.inlineData.data);
              const bytes = new Uint8Array(raw.length);
              for (let i = 0; i < raw.length; i++) {
                bytes[i] = raw.charCodeAt(i);
              }
              this.onAudio(bytes);
            }
          }
          // Text part
          else if (part.text) {
            if (this.onText) this.onText(part.text);
          }
        }
      }
    }
  }

  _send(obj) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify(obj));
  }
}
