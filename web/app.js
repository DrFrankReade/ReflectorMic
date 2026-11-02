import { createApi, formBody } from './api.js';
import { createPoller, pageVisibility } from './poller.js';
import { createActionRunner, statusView } from './state.js';
import { receiveDiagnostic, scanWifi, waitUntilVisible } from './workflows.js';
import { initTheme } from './theme.js';

const $ = id => document.getElementById(id);
const api = createApi();
const lifecycle = new AbortController();
const beforePoll = () => waitUntilVisible(document, lifecycle.signal);
let storage;
try { storage = window.localStorage; } catch { /* Private browsing may forbid storage. */ }
initTheme(document, storage, window.matchMedia?.('(prefers-color-scheme: dark)'));

let lastStatus = {};
let online = false;
let configLoaded = false;
let statusGeneration = 0;
let awaitingAudioStatus = false;
let toastTimer;
const actions = createActionRunner(renderControls);

function toast(message, error = false) {
  clearTimeout(toastTimer);
  $('toast').textContent = message;
  $('toast').dataset.error = String(error);
  $('toast').hidden = false;
  toastTimer = setTimeout(() => { $('toast').hidden = true; }, error ? 10000 : 6000);
}

function currentView() {
  const busy = new Set(actions.busy);
  if (awaitingAudioStatus) busy.add('audio');
  return statusView(lastStatus, { busy, online });
}

function renderControls() {
  const view = currentView();
  $('echo').disabled = view.echoDisabled;
  $('echo').textContent = actions.busy.has('audio') ? 'Working…' : view.echoLabel;
  $('speakerTest').disabled = view.speakerDisabled;
  $('rxRecover').disabled = view.recoverDisabled;
  $('rxSave').disabled = view.snapshotDisabled;
  $('rxSave').hidden = !view.diagnosticsEnabled;
  $('diagnosticHelp').hidden = !view.diagnosticsEnabled;
  $('diagnosticDisabled').hidden = view.diagnosticsEnabled;
  $('diagnostics').hidden = !view.diagnosticsEnabled;
  for (const id of ['link', 'unlink']) $(id).disabled = actions.busy.has('link');
  $('search').disabled = actions.busy.has('search');
  $('stationFields').disabled = !configLoaded || actions.busy.has('config');
  $('wifiFields').disabled = !configLoaded || actions.busy.has('config');
  $('retryConfig').disabled = actions.busy.has('config');
  $('scanWifi').disabled = actions.busy.has('scan');
  $('scanWifi').textContent = actions.busy.has('scan') ? 'Scanning…' : 'Scan networks';
  $('uploadDirectory').disabled = actions.busy.has('directory');
  $('directoryFile').disabled = actions.busy.has('directory');
  $('uploadDirectory').textContent = actions.busy.has('directory') ? 'Uploading…' : 'Upload directory';
  for (const [group, ids] of [['audio', ['echo', 'speakerTest']], ['rx', ['rxSave', 'rxRecover']],
    ['link', ['link', 'unlink']], ['scan', ['scanWifi']], ['directory', ['uploadDirectory']]]) {
    for (const id of ids) $(id).setAttribute('aria-busy', String(actions.busy.has(group)));
  }
}

function renderStatus() {
  const view = currentView();
  $('state').textContent = view.state;
  $('dot').dataset.tone = view.tone;
  $('wifi').textContent = lastStatus.wifiConnected ? lastStatus.ssid || 'Connected' : 'Not connected';
  for (const [id, key] of [['ip', 'ip'], ['target', 'target'], ['heard', 'lastHeard'], ['firmware', 'firmware'], ['codec', 'codec'], ['refstatus', 'reflectorStatus']]) {
    $(id).textContent = lastStatus[key] || (id === 'firmware' || id === 'refstatus' ? '' : '—');
  }
  $('ptt').textContent = typeof lastStatus.pttPressed === 'boolean' ? lastStatus.pttPressed ? 'Pressed' : 'Released' : '—';
  for (const id of ['echoStatus', 'reflectorMedia', 'audioPlayback', 'codecPerformance', 'audioHardware', 'resetDiagnostic', 'resourceStatus']) {
    $(id).textContent = view[id];
  }
  renderControls();
}

const poller = createPoller(async signal => {
  const generation = statusGeneration;
  const status = await api.json('/api/status', { signal });
  if (!status || typeof status !== 'object' || Array.isArray(status)) throw new Error('Invalid device status response.');
  if (signal.aborted) return;
  // A response already in flight when an action completed cannot unlock audio.
  if (generation !== statusGeneration) { poller.refresh(); return; }
  lastStatus = status;
  online = true;
  awaitingAudioStatus = false;
  $('connectionStatus').hidden = true;
  renderStatus();
}, {
  ...pageVisibility(document),
  onError(error) {
    online = false;
    awaitingAudioStatus = false;
    $('connectionStatus').hidden = false;
    $('connectionStatus').textContent = `${error.message} Retrying automatically while this page is visible. Displayed values may be stale.`;
    renderStatus();
  },
});

async function runAction(group, action) {
  try { await actions.run(group, action); }
  catch (error) { if (error.code !== 'aborted') toast(error.message, true); }
}

function refreshAfterAction(audio = false) {
  statusGeneration += 1;
  if (audio) awaitingAudioStatus = true;
  poller.refresh();
}

function fillList(id, rows) {
  $(id).replaceChildren(...rows.map(row => {
    const option = document.createElement('option');
    option.value = row.value;
    option.label = row.label;
    return option;
  }));
}

for (const id of ['module', 'defaultModule', 'localModule']) {
  for (let code = 65; code <= 90; code += 1) {
    const letter = String.fromCharCode(code);
    $(id).add(new Option(letter, letter));
  }
}

function showLevels() {
  $('volumeValue').value = $('volume').value;
  const gain = `${Number($('micGain').value).toFixed(2)}×`;
  $('micGainValue').value = gain;
  $('micGain').setAttribute('aria-valuetext', gain);
}
$('volume').addEventListener('input', showLevels);
$('micGain').addEventListener('input', showLevels);
$('open').addEventListener('change', () => { $('password').disabled = $('open').checked; });

async function loadConfig() {
  await runAction('config', async () => {
    try {
      const config = await api.json('/api/config', { signal: lifecycle.signal });
      for (const [id, key] of Object.entries({ callsign: 'callsign', localModule: 'localModule', suffix: 'mySuffix',
        defaultReflector: 'defaultReflector', defaultModule: 'defaultModule', startup: 'startupMode',
        timeout: 'txTimeoutSec', volume: 'speakerVolume', micGain: 'micGain', ssid: 'wifiSsid' })) {
        $(id).value = config[key] ?? '';
      }
      $('open').checked = config.wifiOpen === true;
      $('password').disabled = $('open').checked;
      $('reflector').value = config.defaultReflector || 'REF001';
      $('module').value = config.defaultModule || 'C';
      configLoaded = true;
      $('configError').hidden = true;
      showLevels();
    } catch (error) {
      $('configError').hidden = false;
      $('configErrorText').textContent = `Settings could not be loaded. ${error.message}`;
      throw error;
    }
  });
}
$('retryConfig').addEventListener('click', loadConfig);

async function directoryStatus() {
  try {
    const data = await api.json('/api/directory', { signal: lifecycle.signal });
    $('directoryStatus').textContent = `Installed: ${data.supportedEntries ?? data.refEntries} reflector entries (${Math.round(data.bytes / 1024)} KiB) · LittleFS ${Math.round(data.fsUsed / 1024)} / ${Math.round(data.fsTotal / 1024)} KiB used`;
  } catch (error) { $('directoryStatus').textContent = `Directory unavailable: ${error.message}`; }
}

async function searchRefs() {
  await runAction('search', async () => {
    const query = $('reflector').value.toUpperCase().slice(0, 6);
    const data = await api.json(`/api/reflectors?q=${encodeURIComponent(query)}&limit=100`, { signal: lifecycle.signal });
    fillList('refs', data.reflectors.map(row => ({ value: row.name, label: row.ipv4 })));
    $('searchStatus').textContent = `${data.reflectors.length} matching reflectors. Choose a suggestion in the Reflector field.`;
  });
}
$('search').addEventListener('click', searchRefs);
$('linkForm').addEventListener('submit', event => {
  event.preventDefault();
  runAction('link', async () => {
    await api.json('/api/link', { method: 'POST', body: formBody({ reflector: $('reflector').value.toUpperCase(), module: $('module').value }), signal: lifecycle.signal });
    toast('Link requested');
    refreshAfterAction();
  });
});
$('unlink').addEventListener('click', () => runAction('link', async () => {
  await api.json('/api/unlink', { method: 'POST', signal: lifecycle.signal });
  toast('Unlinked');
  refreshAfterAction();
}));
$('echo').addEventListener('click', () => {
  if (currentView().echoDisabled) return;
  const armed = lastStatus.echoState === 'armed';
  runAction('audio', async () => {
    try {
      await api.json(armed ? '/api/echo/cancel' : '/api/echo/arm', { method: 'POST', signal: lifecycle.signal });
      toast(armed ? 'Echo test cancelled' : 'Echo test armed');
    } finally { refreshAfterAction(true); }
  });
});
$('speakerTest').addEventListener('click', () => {
  if (currentView().speakerDisabled) return;
  runAction('audio', async () => {
    try {
      await api.json('/api/audio/speaker-test', { method: 'POST', signal: lifecycle.signal });
      toast('Playing 1 kHz speaker test');
    } finally { refreshAfterAction(true); }
  });
});

function saveBlob(blob) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = 'ReflectorMic-RX.bin';
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
function rxDiagnostic(recover) {
  const view = currentView();
  if (recover ? view.recoverDisabled : view.snapshotDisabled) return;
  runAction('rx', async () => {
    try {
      const blob = await receiveDiagnostic(api, { recover, signal: lifecycle.signal, beforePoll,
        diagnosticsEnabled: () => online && lastStatus.diagnosticsEnabled === true,
        onProgress: message => { $('rxStatus').textContent = message; },
      });
      const message = blob ? 'RX diagnostic downloaded.' : recover ? 'Receive restart requested.' : 'RX diagnostics are disabled.';
      if (blob && online && lastStatus.diagnosticsEnabled === true) saveBlob(blob);
      $('rxStatus').textContent = message;
      toast(message);
    } catch (error) {
      $('rxStatus').textContent = error.message;
      throw error;
    } finally { refreshAfterAction(); }
  });
}
$('rxSave').addEventListener('click', () => rxDiagnostic(false));
$('rxRecover').addEventListener('click', () => rxDiagnostic(true));

$('scanWifi').addEventListener('click', () => runAction('scan', async () => {
  $('scanStatus').textContent = 'Scanning for networks…';
  try {
    const networks = await scanWifi(api, { signal: lifecycle.signal, beforePoll,
      onProgress: message => { $('scanStatus').textContent = message; },
    });
    fillList('networks', networks.map(network => ({ value: network.ssid, label: `${network.rssi} dBm` })));
    $('scanStatus').textContent = `${networks.length} networks found. Choose a suggestion in the Network field or type its name.`;
  } catch (error) { $('scanStatus').textContent = error.message; throw error; }
}));

$('directoryForm').addEventListener('submit', event => {
  event.preventDefault();
  runAction('directory', async () => {
    const file = $('directoryFile').files[0];
    if (!file) throw new Error('Select a directory file first.');
    if (file.size > 1250 * 1024) throw new Error('Directory exceeds the 1,250 KiB upload limit.');
    const body = new FormData();
    body.append('directory', file, file.name);
    const data = await api.json('/api/directory/upload', { method: 'POST', body, timeoutMs: 60000, signal: lifecycle.signal });
    toast(`Installed ${data.supportedEntries ?? data.refEntries} reflector entries`);
    $('directoryFile').value = '';
    await directoryStatus();
    await searchRefs();
  });
});
$('stationForm').addEventListener('submit', event => {
  event.preventDefault();
  runAction('config', async () => {
    const values = { callsign: $('callsign').value, localModule: $('localModule').value, mySuffix: $('suffix').value,
      defaultReflector: $('defaultReflector').value, defaultModule: $('defaultModule').value,
      startupMode: $('startup').value, txTimeoutSec: $('timeout').value, speakerVolume: $('volume').value, micGain: $('micGain').value };
    await api.json('/api/config', { method: 'POST', body: formBody(values), signal: lifecycle.signal });
    toast('Settings saved');
    refreshAfterAction();
  });
});
$('wifiForm').addEventListener('submit', event => {
  event.preventDefault();
  runAction('config', async () => {
    const values = { wifiSsid: $('ssid').value, wifiPassword: $('password').value,
      wifiOpen: $('open').checked ? '1' : '0', reboot: '1' };
    await api.json('/api/config', { method: 'POST', body: formBody(values), signal: lifecycle.signal });
    $('password').value = '';
    toast('Saved; rebooting. Reconnect to the configured Wi-Fi network if needed.');
    refreshAfterAction();
  });
});

window.addEventListener('pagehide', event => {
  poller.stop();
  if (!event.persisted) lifecycle.abort();
});
window.addEventListener('pageshow', () => { if (!lifecycle.signal.aborted) poller.start(); });
renderControls();
loadConfig();
// Avoid scanning LittleFS on every page load while the setup panel is closed.
let directoryLoaded = false;
$('setupDiagnostics').addEventListener('toggle', () => {
  if ($('setupDiagnostics').open && !directoryLoaded) {
    directoryLoaded = true;
    directoryStatus();
  }
});
poller.start();
