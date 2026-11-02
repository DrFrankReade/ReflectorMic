import { ApiError } from './api.js';

export function delay(ms, signal) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) { reject(new ApiError('Request cancelled', { code: 'aborted' })); return; }
    const abort = () => {
      clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
      reject(new ApiError('Request cancelled', { code: 'aborted' }));
    };
    const timer = setTimeout(() => {
      signal?.removeEventListener('abort', abort);
      resolve();
    }, ms);
    signal?.addEventListener('abort', abort, { once: true });
  });
}

export async function waitUntilVisible(document, signal) {
  if (signal?.aborted) throw new ApiError('Request cancelled', { code: 'aborted' });
  if (!document.hidden) return;
  await new Promise((resolve, reject) => {
    const cleanup = () => {
      document.removeEventListener('visibilitychange', change);
      signal?.removeEventListener('abort', abort);
    };
    const change = () => { if (!document.hidden) { cleanup(); resolve(); } };
    const abort = () => { cleanup(); reject(new ApiError('Request cancelled', { code: 'aborted' })); };
    document.addEventListener('visibilitychange', change);
    signal?.addEventListener('abort', abort, { once: true });
  });
}

export async function scanWifi(api, { signal, wait = delay, beforePoll = async () => {},
  onProgress = () => {}, maxAttempts = 40 } = {}) {
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    await beforePoll();
    const { status, data } = await api.request('/api/wifi/scan', { signal });
    if (status === 202) {
      onProgress('Scanning for networks…');
      if (attempt + 1 < maxAttempts) await wait(Math.min(500 * 2 ** attempt, 2000), signal);
      continue;
    }
    if (status !== 200 || !Array.isArray(data?.networks)) {
      throw new ApiError('The device returned an invalid Wi-Fi scan result.', { code: 'response' });
    }
    return data.networks;
  }
  throw new ApiError('Wi-Fi scan is still running. Try again shortly.', { code: 'timeout' });
}

export async function receiveDiagnostic(api, { recover = false, diagnosticsEnabled = () => false,
  signal, wait = delay, beforePoll = async () => {}, onProgress = () => {}, maxAttempts = 30 } = {}) {
  if (!recover && !diagnosticsEnabled()) throw new ApiError('RX diagnostics are disabled.');
  await api.json(recover ? '/api/audio/rx-recover' : '/api/audio/rx-snapshot', { method: 'POST', signal });
  onProgress(recover ? 'Receive restart requested.' : 'Preparing RX diagnostic…');
  // Recovery must never depend on a diagnostic endpoint being compiled in.
  if (!diagnosticsEnabled()) return null;
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    await beforePoll();
    if (!diagnosticsEnabled()) return null;
    try {
      const { status, data } = await api.request('/api/audio/rx-trace', { signal, responseType: 'blob' });
      if (status === 200) return diagnosticsEnabled() ? data : null;
      if (status !== 202) throw new ApiError('Unexpected RX diagnostic response.', { status });
    } catch (error) {
      if (error.status !== 409 && error.status !== 202) throw error;
    }
    if (attempt + 1 < maxAttempts) await wait(Math.min(150 * 2 ** attempt, 1000), signal);
  }
  throw new ApiError('RX snapshot is still pending. Try saving again shortly.', { code: 'timeout' });
}
