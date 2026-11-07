// One request at a time, with only the newest unsent preview retained.
// flush() is a barrier: a Save request cannot be overtaken by an old preview.
export function createPreviewQueue(send, { timers = globalThis, onResult = () => {} } = {}) {
  let timer, pending, running, stopped = false;
  async function drain() {
    if (running) return running;
    running = (async () => {
      while (pending && !stopped) {
        const value = pending; pending = undefined;
        try { await send(value); if (!pending && !stopped) onResult(null); }
        catch (error) { if (!pending && !stopped) onResult(error); }
      }
    })();
    try { await running; } finally { running = undefined; }
  }
  return {
    set(value) {
      if (stopped) return;
      pending = { ...value };
      timers.clearTimeout(timer);
      timer = timers.setTimeout(() => { void drain(); }, 120);
    },
    async flush() {
      timers.clearTimeout(timer);
      if (running) await running;
      if (pending) await drain();
    },
    stop() { stopped = true; pending = undefined; timers.clearTimeout(timer); },
  };
}

export function initSpeakerFilters({ $, api, formBody, signal, runAction, toast }) {
  const defaults = { enabled: false, lowCutHz: 200, highCutHz: 3200, slopeDb: 6 };
  const read = () => ({ enabled: $('filterBypass').checked ? '0' : '1',
    lowCutHz: $('lowCut').value, highCutHz: $('highCut').value, slopeDb: $('filterSlope').value });
  function render() {
    for (const id of ['lowCut', 'highCut']) {
      $(id + 'Value').value = `${$(id).value} Hz`;
      $(id).setAttribute('aria-valuetext', `${$(id).value} hertz`);
    }
  }
  const queue = createPreviewQueue(value => api.json('/api/audio/filters', {
    method: 'POST', body: formBody(value), signal,
  }), { onResult(error) {
    $('filterStatus').textContent = error ? error.message : 'Applied.';
  } });
  function load(value) {
    $('filterBypass').checked = !value.enabled;
    $('lowCut').value = value.lowCutHz;
    $('highCut').value = value.highCutHz;
    $('filterSlope').value = String(value.slopeDb ?? 6);
    render();
  }
  function preview() { render(); $('filterStatus').textContent = 'Applying...'; queue.set(read()); }
  for (const id of ['lowCut', 'highCut']) $(id).addEventListener('input', preview);
  $('filterBypass').addEventListener('change', preview);
  $('filterSlope').addEventListener('change', preview);
  $('filterReset').addEventListener('click', () => { load(defaults); preview(); });
  $('filterForm').addEventListener('submit', event => {
    event.preventDefault();
    runAction('filters', async () => {
      await queue.flush();
      try {
        await api.json('/api/audio/filters', { method: 'POST', body: formBody({ ...read(), save: '1' }), signal });
        $('filterStatus').textContent = 'Saved.';
        toast('Speaker filter settings saved');
      } catch (error) { $('filterStatus').textContent = error.message; throw error; }
    });
  });
  signal.addEventListener('abort', () => queue.stop(), { once: true });
  return { load };
}
