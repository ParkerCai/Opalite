export class OllamaVisionAdapter {
  constructor({
    baseUrl = 'http://127.0.0.1:11434',
    model = 'gemma3:4b'
  } = {}) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.model = model;
  }

  async getStatus() {
    try {
      const response = await fetch(`${this.baseUrl}/api/tags`);
      if (!response.ok) {
        return {
          available: false,
          adapter: 'ollama',
          reason: `Ollama responded with HTTP ${response.status}`
        };
      }

      const data = await response.json();
      const models = Array.isArray(data.models) ? data.models.map((entry) => entry.name) : [];
      const hasModel = models.includes(this.model);

      return {
        available: hasModel,
        adapter: 'ollama',
        model: this.model,
        baseUrl: this.baseUrl,
        reason: hasModel ? null : `Ollama is running, but model ${this.model} is not installed`,
        discoveredModels: models.slice(0, 20)
      };
    } catch (error) {
      return {
        available: false,
        adapter: 'ollama',
        model: this.model,
        baseUrl: this.baseUrl,
        reason: error.message
      };
    }
  }

  async generateTurn({ prompt, systemPrompt, imageBase64 }) {
    const finalPrompt = [
      systemPrompt?.trim(),
      prompt?.trim() || 'Describe what matters for safe navigation in one or two short sentences.'
    ].filter(Boolean).join('\n\n');

    const payload = {
      model: this.model,
      prompt: finalPrompt,
      stream: false,
      options: {
        temperature: 0.2
      }
    };

    if (imageBase64) {
      payload.images = [imageBase64];
    }

    const response = await fetch(`${this.baseUrl}/api/generate`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(payload)
    });

    if (!response.ok) {
      throw new Error(`Ollama generation failed with HTTP ${response.status}`);
    }

    const data = await response.json();
    const text = (data.response || '').trim();

    if (!text) {
      throw new Error('Ollama returned an empty response');
    }

    return {
      text,
      meta: {
        adapter: 'ollama',
        model: this.model
      }
    };
  }
}
