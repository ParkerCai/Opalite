export class LlamaCppVisionAdapter {
  constructor({
    baseUrl = 'http://127.0.0.1:8082/v1',
    model = 'gemma4-31b'
  } = {}) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.model = model;
  }

  async getStatus() {
    try {
      const response = await fetch(`${this.baseUrl}/models`);
      if (!response.ok) {
        return {
          available: false,
          adapter: 'llamacpp',
          reason: `llama.cpp responded with HTTP ${response.status}`,
          baseUrl: this.baseUrl,
          model: this.model
        };
      }

      const data = await response.json();
      const models = Array.isArray(data.data)
        ? data.data.map((entry) => entry.id || entry.name).filter(Boolean)
        : [];
      const hasModel = models.includes(this.model);

      return {
        available: hasModel,
        adapter: 'llamacpp',
        model: this.model,
        baseUrl: this.baseUrl,
        reason: hasModel ? null : `llama.cpp is running, but model ${this.model} is not loaded`,
        discoveredModels: models.slice(0, 20)
      };
    } catch (error) {
      return {
        available: false,
        adapter: 'llamacpp',
        model: this.model,
        baseUrl: this.baseUrl,
        reason: error.message
      };
    }
  }

  async generateTurn({ prompt, imageBase64 }) {
    if (!imageBase64) {
      return {
        text: 'Point the camera at the scene, then tap again so I have an image to describe.',
        meta: {
          adapter: 'llamacpp',
          model: this.model,
          baseUrl: this.baseUrl,
          waitingForImage: true
        }
      };
    }

    const normalizedPrompt = typeof prompt === 'string' ? prompt.trim() : '';
    const optimizedPrompt = (!normalizedPrompt || normalizedPrompt.length > 120 || /safe navigation|one or two short sentences|for a blind user/i.test(normalizedPrompt))
      ? 'Give short navigation guidance from this image. Mention obstacles, readable text, and safest direction if obvious.'
      : normalizedPrompt;

    const messages = [
      {
        role: 'system',
        content: 'You are a blind-navigation assistant. Return only one short factual answer. No reasoning. No colors or aesthetics.'
      },
      {
        role: 'user',
        content: [
          {
            type: 'text',
            text: optimizedPrompt || 'Describe this image briefly.'
          },
          {
            type: 'image_url',
            image_url: {
              url: `data:image/jpeg;base64,${imageBase64}`
            }
          }
        ]
      }
    ];

    const response = await fetch(`${this.baseUrl}/chat/completions`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        model: this.model,
        messages,
        stream: false,
        temperature: 0.1,
        max_tokens: 80,
        reasoning: 'off',
        think_budget: 0
      })
    });

    if (!response.ok) {
      throw new Error(`llama.cpp generation failed with HTTP ${response.status}`);
    }

    const data = await response.json();
    const message = data?.choices?.[0]?.message || {};
    const text = (message.content || '').trim();

    if (!text) {
      throw new Error('llama.cpp returned an empty response');
    }

    return {
      text,
      meta: {
        adapter: 'llamacpp',
        model: this.model,
        baseUrl: this.baseUrl,
        optimizedPrompt
      }
    };
  }
}
