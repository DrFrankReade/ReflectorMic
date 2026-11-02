// Keep the deadline active until the response body has also been consumed.
export class ApiError extends Error {
  constructor(message, { status = 0, code = 'request' } = {}) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}

export function createApi({ fetchImpl = globalThis.fetch, timeoutMs = 5000,
  timers = globalThis } = {}) {
  async function request(url, { timeoutMs: deadline = timeoutMs, signal,
    responseType = 'json', ...options } = {}) {
    const controller = new AbortController();
    let timer;
    let cancel;
    const cancelled = new Promise((_, reject) => {
      cancel = () => {
        controller.abort();
        reject(new ApiError('Request cancelled', { code: 'aborted' }));
      };
      if (signal?.aborted) cancel();
      else signal?.addEventListener('abort', cancel, { once: true });
      timer = timers.setTimeout(() => {
        controller.abort();
        reject(new ApiError('Device request timed out. Check the connection before trying again.',
          { code: 'timeout' }));
      }, deadline);
    });
    const perform = async () => {
      if (controller.signal.aborted) throw new ApiError('Request cancelled', { code: 'aborted' });
      const response = await fetchImpl(url, { cache: 'no-store', ...options, signal: controller.signal });
      if (!response.ok) {
        const body = await response.text();
        let message = `Device returned HTTP ${response.status}`;
        try {
          const data = JSON.parse(body);
          const detail = data.error || data.status;
          if (typeof detail === 'string') message = detail.slice(0, 240);
        } catch { /* Captive portals and plain HTTP errors need no JSON body. */ }
        throw new ApiError(message, { status: response.status, code: 'http' });
      }
      if (responseType === 'blob') return { status: response.status, data: await response.blob() };
      if (response.status === 204) return { status: response.status, data: null };
      let data;
      try { data = await response.json(); }
      catch (error) {
        if (controller.signal.aborted) throw error;
        throw new ApiError('The device returned an invalid JSON response.', { code: 'response' });
      }
      return { status: response.status, data };
    };
    try { return await Promise.race([cancelled, perform()]); }
    catch (error) {
      if (error instanceof ApiError) throw error;
      throw new ApiError('Cannot reach the device. Check your Wi-Fi connection.', { code: 'network' });
    } finally {
      timers.clearTimeout(timer);
      signal?.removeEventListener('abort', cancel);
    }
  }
  return {
    request,
    async json(url, options) { return (await request(url, options)).data; },
    async blob(url, options) { return (await request(url, { ...options, responseType: 'blob' })).data; },
  };
}

export const formBody = values => new URLSearchParams(values);
