const echoLabels = {
  armed: 'Echo armed', recording: 'Echo recording', finalizing: 'Echo processing', playing: 'Echo playback',
};
const text = value => typeof value === 'string' ? value : '';
const count = value => Number.isFinite(value) ? String(value) : '—';

export function statusView(status = {}, { busy = new Set(), online = false } = {}) {
  const diagnosticsEnabled = status.diagnosticsEnabled === true;
  const resources = status.resources || {};
  const kib = value => Number.isFinite(value) ? (value / 1024).toFixed(0) : "—";
  const audioBusy = busy.has('audio');
  const echoState = status.echoState;
  const idle = echoState === 'idle';
  const armed = echoState === 'armed';
  const liveAudio = Boolean(status.incoming || status.transmitting);
  return {
    diagnosticsEnabled,
    state: online ? echoLabels[echoState] || (status.transmitting ? 'Transmitting'
      : status.incoming ? 'Receiving' : text(status.reflectorState) || 'Ready') : 'Device unavailable',
    tone: !online ? 'offline' : status.transmitting || echoState === 'recording' ? 'tx'
      : status.incoming || ['finalizing', 'playing', 'armed'].includes(echoState) ? 'rx'
        : status.reflectorLinked ? 'linked' : status.wifiConnected ? 'rx' : 'offline',
    echoLabel: armed ? 'Cancel echo test' : 'Arm echo test',
    echoDisabled: !online || audioBusy || busy.has('rx') || (!idle && !armed) || (!armed && liveAudio),
    speakerDisabled: !online || audioBusy || busy.has('rx') || liveAudio || !idle,
    // Recovery is an operational control, including in builds without diagnostics.
    recoverDisabled: busy.has('rx') || audioBusy,
    snapshotDisabled: !diagnosticsEnabled || !online || busy.has('rx') || audioBusy,
    echoStatus: [text(status.echoStatus), text(status.audioError), diagnosticsEnabled && status.echoFrames
      ? `${count(status.echoFrames)} AMBE frames` : '', diagnosticsEnabled ? text(status.audioCapture) : '']
      .filter(Boolean).join(' · ') || 'Waiting for echo status…',
    reflectorMedia: diagnosticsEnabled
      ? `RX ${count(status.reflectorAcceptedVoiceFrames)}/${count(status.reflectorRxVoicePackets)} voice · `
        + `${count(status.reflectorAcceptedHeaders)}/${count(status.reflectorRxHeaders)} headers · `
        + `TX ${count(status.reflectorTxVoiceFrames)} voice (${count(status.reflectorTxUnderruns)} silence fills) · `
        + `parse ${count(status.reflectorMediaParseFailures)} · stream ${count(status.reflectorWrongStreamFrames)} · `
        + `no-header ${count(status.reflectorVoiceWithoutHeader)} · `
        + `RX silence: received ${count(status.rxReceivedNullFrames)}, filled ${count(status.rxConcealedFrames)} · `
        + `jitter recoveries ${count(status.rxJitterResyncs)} · discarded fast data ${count(status.rxFastDataFrames)}, unclassified ${count(status.rxUnclassifiedFrames)}, competing voice ${count(status.rxCompetingVoiceFrames)}` : '',
    resourceStatus: diagnosticsEnabled && status.resources
      ? `${count(resources.cpuMHz)} MHz · network core ${count(resources.networkCore)}, codec core ${count(resources.codecCore)} · `
        + `free internal RAM ${kib(resources.internalFreeBytes)} KiB (largest ${kib(resources.internalLargestBytes)}) · `
        + `free PSRAM ${kib(resources.psramFreeBytes)} KiB · `
        + `app ${kib(resources.appBytes)}/${kib(resources.appPartitionBytes)} KiB · `
        + `loop active ${Number.isFinite(resources.loopActivePermille) ? (resources.loopActivePermille / 10).toFixed(1) : '—'}% wall time (not total CPU load)` : '',
    audioPlayback: diagnosticsEnabled ? text(status.audioPlayback) : '',
    codecPerformance: diagnosticsEnabled
      ? [text(status.codecPerformance), text(status.dspAcceleration)].filter(Boolean).join(' · ') : '',
    audioHardware: diagnosticsEnabled ? text(status.audioHardware) : '',
    resetDiagnostic: diagnosticsEnabled ? text(status.resetDiagnostic) : '',
  };
}

// Shared locks prevent a status refresh from enabling a control during its POST.
export function createActionRunner(onChange = () => {}) {
  const busy = new Set();
  return {
    busy,
    async run(group, action) {
      if (busy.has(group)) return false;
      busy.add(group);
      onChange();
      try { await action(); return true; }
      finally { busy.delete(group); onChange(); }
    },
  };
}
