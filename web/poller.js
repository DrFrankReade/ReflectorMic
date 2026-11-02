// One request at a time. Delays start after completion, including failed requests.
export function createPoller(task, { intervalMs = 1000, maxDelayMs = 15000,
  isVisible = () => true, subscribeVisibility = () => () => {},
  onError = () => {}, timers = globalThis } = {}) {
  let running = false;
  let inFlight = false;
  let refreshQueued = false;
  let failures = 0;
  let timer;
  let controller;
  let unsubscribe = () => {};

  function clearTimer() { timers.clearTimeout(timer); timer = undefined; }
  function schedule(delay) {
    clearTimer();
    if (running && isVisible()) timer = timers.setTimeout(tick, delay);
  }
  async function tick() {
    clearTimer();
    if (!running || !isVisible()) return;
    if (inFlight) { refreshQueued = true; return; }
    inFlight = true;
    refreshQueued = false;
    const active = new AbortController();
    controller = active;
    try {
      await task(active.signal);
      if (!active.signal.aborted) failures = 0;
    } catch (error) {
      if (!active.signal.aborted) { failures += 1; onError(error); }
    } finally {
      inFlight = false;
      controller = undefined;
      const delay = failures ? Math.min(maxDelayMs, intervalMs * 2 ** Math.min(failures, 16))
        : refreshQueued ? 0 : intervalMs;
      schedule(delay);
    }
  }
  function refresh() {
    if (!running) return;
    if (inFlight) refreshQueued = true;
    else schedule(0);
  }
  return {
    start() {
      if (running) return;
      running = true;
      unsubscribe = subscribeVisibility(() => {
        if (isVisible()) refresh();
        else clearTimer();
      });
      refresh();
    },
    stop() {
      running = false;
      refreshQueued = false;
      clearTimer();
      controller?.abort();
      unsubscribe();
    },
    refresh,
  };
}

export function pageVisibility(document) {
  return {
    isVisible: () => !document.hidden,
    subscribeVisibility(listener) {
      document.addEventListener('visibilitychange', listener);
      return () => document.removeEventListener('visibilitychange', listener);
    },
  };
}
