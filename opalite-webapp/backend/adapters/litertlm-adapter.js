export function getLiteRtLmStatus() {
  const configuredModelPath = process.env.OPALITE_LITERTLM_MODEL_PATH || null;

  return {
    available: false,
    adapter: 'litertlm',
    modelPath: configuredModelPath,
    reason: 'LiteRT-LM is not wired on this Spark host yet. The runtime package and a verified Gemma 4 model path are both missing.'
  };
}
