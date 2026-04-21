export class FallbackAdapter {
  constructor({ blockers = {} } = {}) {
    this.blockers = blockers;
  }

  async getStatus() {
    return {
      available: true,
      adapter: 'fallback',
      mode: 'connectivity-only'
    };
  }

  async generateTurn({ prompt, imageBase64 }) {
    if (!imageBase64) {
      return {
        text: 'Local backend is connected. Point the camera, then tap again so I have a frame to work from.',
        meta: {
          adapter: 'fallback',
          blocked: true
        }
      };
    }

    const llamacppReason = this.blockers?.llamacpp?.reason;
    const ollamaReason = this.blockers?.ollama?.reason;
    const liteRtReason = this.blockers?.litertlm?.reason;
    const promptHint = prompt?.toLowerCase().includes('sign')
      ? 'The local pipeline received your camera frame, but it cannot read the sign yet without a local vision model.'
      : 'The local pipeline received your camera frame, but no verified local multimodal model is configured on this machine yet.';

    return {
      text: `${promptHint} Bring up the llama.cpp Gemma endpoint, install Ollama with a vision model such as gemma3:4b, or finish the LiteRT-LM + Gemma 4 path.`,
      meta: {
        adapter: 'fallback',
        blocked: true,
        blockers: {
          llamacpp: llamacppReason || null,
          ollama: ollamaReason || null,
          litertlm: liteRtReason || null
        }
      }
    };
  }
}
