/**
 * Local WebSocket client for the Opalite Local prototype backend.
 * Mirrors the GeminiClient surface so app.js can switch modes cleanly.
 */
export class LocalClient {
  constructor({ wsUrl, systemPrompt, speechFallback = true } = {}) {
    this.wsUrl = wsUrl;
    this.systemPrompt = systemPrompt;
    this.speechFallback = speechFallback;
    this.ws = null;
    this.onAudio = null;
    this.onText = null;
    this.onTurnComplete = null;
    this.onInterrupted = null;
    this.onSetupComplete = null;
    this.onError = null;
    this.backendInfo = null;
    this._isSpeaking = false;
    this._utterance = null;
  }

  get url() {
    return this.wsUrl;
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.wsUrl);

      this.ws.addEventListener('open', () => {
        this._send({
          type: 'setup',
          systemPrompt: this.systemPrompt,
          client: {
            name: 'opalite-web',
            speechFallback: this.speechFallback
          }
        });
        resolve();
      });

      this.ws.addEventListener('error', () => {
        const message = 'Local WebSocket connection failed. Start the Opalite Local backend and confirm the URL.';
        this.onError?.(message);
        reject(new Error(message));
      });

      this.ws.addEventListener('close', (event) => {
        this._cancelSpeech();
        if (this.onError && event.code !== 1000) {
          this.onError(`Local connection closed (code ${event.code})${event.reason ? `: ${event.reason}` : ''}`);
        }
      });

      this.ws.addEventListener('message', (event) => {
        this._handleMessage(event.data);
      });
    });
  }

  disconnect() {
    this._cancelSpeech();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
  }

  get isConnected() {
    return this.ws && this.ws.readyState === WebSocket.OPEN;
  }

  get isSpeaking() {
    return this._isSpeaking;
  }

  sendAudio(base64pcm) {
    this._send({
      type: 'audio',
      mimeType: 'audio/pcm',
      audio: base64pcm
    });
  }

  sendImage(base64jpeg) {
    this._send({
      type: 'frame',
      mimeType: 'image/jpeg',
      image: base64jpeg
    });
  }

  sendInterrupt() {
    this._cancelSpeech();
    this._send({ type: 'interrupt' });
  }

  sendText(text, endOfTurn = true) {
    this._send({
      type: 'text',
      text,
      endOfTurn
    });
  }

  sendImageWithText(base64jpeg, text) {
    this._send({
      type: 'describe',
      mimeType: 'image/jpeg',
      image: base64jpeg,
      prompt: text
    });
  }

  _handleMessage(rawMessage) {
    let message;
    try {
      message = JSON.parse(rawMessage);
    } catch {
      console.warn('Non-JSON message from Opalite Local backend');
      return;
    }

    switch (message.type) {
      case 'setup_complete':
        this.backendInfo = message.backendInfo || null;
        this.onSetupComplete?.();
        break;
      case 'output_audio':
        if (message.audio && this.onAudio) {
          const raw = atob(message.audio);
          const bytes = new Uint8Array(raw.length);
          for (let i = 0; i < raw.length; i++) {
            bytes[i] = raw.charCodeAt(i);
          }
          this.onAudio(bytes);
        }
        break;
      case 'output_text':
        if (message.text) {
          this.onText?.(message.text);
          if (this.speechFallback && !message.audioAttached) {
            this._speakText(message.text, message.voice || null);
          }
        }
        break;
      case 'turn_complete':
        this.onTurnComplete?.();
        break;
      case 'interrupted':
        this._cancelSpeech();
        this.onInterrupted?.();
        break;
      case 'error':
        this.onError?.(message.message || 'Local backend error');
        break;
      case 'status':
        if (message.level === 'error') {
          this.onError?.(message.message || 'Local backend error');
        } else {
          console.info('[Opalite Local]', message.message || message);
        }
        break;
      default:
        console.debug('Unhandled local backend message', message);
    }
  }

  _speakText(text, preferredVoiceName) {
    if (!('speechSynthesis' in window) || !text) return;

    this._cancelSpeech();

    const utterance = new SpeechSynthesisUtterance(text);
    utterance.rate = 1;
    utterance.pitch = 1;

    const voices = window.speechSynthesis.getVoices?.() || [];
    if (preferredVoiceName) {
      utterance.voice = voices.find((voice) => voice.name === preferredVoiceName) || null;
    }

    utterance.onstart = () => {
      this._isSpeaking = true;
    };

    utterance.onend = () => {
      if (this._utterance === utterance) {
        this._utterance = null;
      }
      this._isSpeaking = false;
    };

    utterance.onerror = () => {
      if (this._utterance === utterance) {
        this._utterance = null;
      }
      this._isSpeaking = false;
    };

    this._utterance = utterance;
    window.speechSynthesis.speak(utterance);
  }

  _cancelSpeech() {
    if ('speechSynthesis' in window) {
      window.speechSynthesis.cancel();
    }
    this._utterance = null;
    this._isSpeaking = false;
  }

  _send(payload) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify(payload));
  }
}
