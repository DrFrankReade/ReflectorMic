import { createPreviewQueue } from './speaker-filters.js';

export function initLiveVolume({ $, api, formBody, signal }) {
  const preview = createPreviewQueue(value => api.json('/api/audio/volume', {
    method: 'POST', body: formBody(value), signal,
  }), { onResult(error) {
    $('volumeStatus').textContent = error ? error.message : 'Volume applied live. Save settings to keep it after restart.';
  } });
  $('volume').addEventListener('input', () => {
    $('volumeStatus').textContent = 'Applying volume…';
    preview.set({ volume: $('volume').value });
  });
  signal.addEventListener('abort', () => preview.stop(), { once: true });
  return preview;
}
