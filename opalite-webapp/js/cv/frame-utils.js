export function base64ToBlob(base64, mimeType = 'image/jpeg') {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return new Blob([bytes], { type: mimeType });
}

export async function postFrame(endpoint, base64jpeg, extra = {}, timeoutMs = 4000) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ image: base64jpeg, ...extra }),
      signal: controller.signal
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    return await response.json();
  } finally {
    clearTimeout(timeout);
  }
}

export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function average(values = []) {
  if (!values.length) return null;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

export function bboxCenter(box = []) {
  if (!Array.isArray(box) || box.length !== 4) return null;
  const [x1, y1, x2, y2] = box;
  return {
    x: (x1 + x2) / 2,
    y: (y1 + y2) / 2,
    width: Math.max(0, x2 - x1),
    height: Math.max(0, y2 - y1)
  };
}

export function formatConfidence(score) {
  if (score == null || Number.isNaN(Number(score))) return 'n/a';
  return Number(score).toFixed(2);
}

export function uniqueStrings(values = []) {
  return [...new Set(values.filter(Boolean))];
}
