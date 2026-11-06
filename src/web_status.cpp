#include "web_server.h"

#include "firmware_version.h"
#if REFLECTORMIC_DIAGNOSTICS
#include "esp_dsp_trial.h"
#include "runtime_diagnostics.h"
#include "resource_diagnostics.h"
#endif

void ReflectorMicWebServer::sendStatus() {
  String out = "{";
  out += "\"firmware\":" + json(REFLECTORMIC_FIRMWARE) + ',';
  out += "\"diagnosticsEnabled\":" +
         String(REFLECTORMIC_DIAGNOSTICS ? "true" : "false") + ',';
  appendConnectionStatus(out);
  out += "\"audioError\":" + json(audio_->errorText()) + ',';
#if REFLECTORMIC_DIAGNOSTICS
  appendAudioDiagnostics(out);
  out += "\"resetDiagnostic\":" + json(RuntimeDiagnostics::summary()) + ',';
  appendReflectorCounters(out);
  out += "\"resources\":" + ResourceDiagnostics::snapshotJson() + ',';
#endif
  out += "\"lastHeard\":" + json(reflector_->lastHeard()) + ',';
  out += "\"codec\":" + json(audio_->codecVersion());
  out += '}';
  server_.send(200, "application/json", out);
}

void ReflectorMicWebServer::appendConnectionStatus(String &out) const {
  out += "\"wifiConnected\":" + String(wifi_->connected() ? "true" : "false") + ',';
  out += "\"ssid\":" + json(wifi_->activeSsid()) + ',';
  out += "\"ip\":" + json(wifi_->ipAddress()) + ',';
  out += "\"reflectorState\":" + json(reflector_->stateText()) + ',';
  out += "\"reflectorStatus\":" + json(reflector_->statusText()) + ',';
  out += "\"reflectorLinked\":" + String(reflector_->linked() ? "true" : "false") + ',';
  out += "\"target\":" + json(reflector_->targetText()) + ',';
  out += "\"hostIp\":" + json(reflector_->hostIp()) + ',';
  out += "\"incoming\":" + String(reflector_->incoming() ? "true" : "false") + ',';
  out += "\"transmitting\":" + String(reflector_->transmitting() ? "true" : "false") + ',';
  out += "\"pttPressed\":" + String(pttPressed_ ? "true" : "false") + ',';
  out += "\"echoState\":" + json(echo_->stateText()) + ',';
  out += "\"echoStatus\":" + json(echo_->statusText()) + ',';
  out += "\"echoFrames\":" + String(echo_->frameCount()) + ',';
}

#if REFLECTORMIC_DIAGNOSTICS
void ReflectorMicWebServer::appendAudioDiagnostics(String &out) const {
  out += "\"dspAcceleration\":" + json(espDspDiagnostic()) + ',';
  out += "\"audioCapture\":" + json(audio_->captureDiagnostic()) + ',';
  out += "\"audioPlayback\":" + json(audio_->playbackDiagnostic()) + ',';
  out += "\"codecPerformance\":" + json(audio_->codecDiagnostic()) + ',';
  out += "\"audioHardware\":" + json(audio_->hardwareDiagnostic()) + ',';
}
void ReflectorMicWebServer::appendReflectorCounters(String &out) const {
  out += "\"reconnects\":" + String(reflector_->reconnectCount()) + ',';
  out += "\"rxDrops\":" + String(reflector_->droppedRxFrames()) + ',';
  out += "\"rxConcealedFrames\":" + String(reflector_->concealedRxFrames()) + ',';
  out += "\"rxReceivedNullFrames\":" + String(reflector_->receivedNullFrames()) + ',';
  out += "\"rxJitterTargetMs\":" + String(reflector_->jitterTargetMs()) + ',';
  out += "\"rxJitterRefills\":" + String(reflector_->jitterRefills()) + ',';
  out += "\"rxJitterResyncs\":" + String(reflector_->jitterResyncs()) + ',';
  out += "\"rxFastDataFrames\":" + String(reflector_->fastDataFrames()) + ',';
  out += "\"rxUnclassifiedFrames\":" + String(reflector_->unclassifiedFrames()) + ',';
  out += "\"rxCompetingVoiceFrames\":" + String(reflector_->competingVoiceFrames()) + ',';
  out += "\"reflectorRxPackets\":" + String(reflector_->receivedPackets()) + ',';
  out += "\"reflectorRxPolls\":" + String(reflector_->receivedPolls()) + ',';
  out += "\"reflectorTxPolls\":" + String(reflector_->sentPolls()) + ',';
  out += "\"reflectorSendFailures\":" + String(reflector_->sendFailures()) + ',';
  out += "\"reflectorRejectedPeers\":" +
         String(reflector_->rejectedPeerPackets()) + ',';
  out += "\"reflectorRxHeaders\":" + String(reflector_->receivedHeaders()) + ',';
  out += "\"reflectorAcceptedHeaders\":" + String(reflector_->acceptedHeaders()) + ',';
  out += "\"reflectorRxVoicePackets\":" +
         String(reflector_->receivedVoicePackets()) + ',';
  out += "\"reflectorAcceptedVoiceFrames\":" +
         String(reflector_->acceptedVoiceFrames()) + ',';
  out += "\"reflectorRxEndPackets\":" +
         String(reflector_->receivedEndPackets()) + ',';
  out += "\"reflectorMediaParseFailures\":" +
         String(reflector_->mediaParseFailures()) + ',';
  out += "\"reflectorVoiceWithoutHeader\":" +
         String(reflector_->voiceWithoutHeader()) + ',';
  out += "\"reflectorWrongStreamFrames\":" +
         String(reflector_->wrongStreamFrames()) + ',';
  out += "\"reflectorBusyMediaDrops\":" +
         String(reflector_->busyMediaDrops()) + ',';
  out += "\"reflectorUnknownPackets\":" +
         String(reflector_->unknownPackets()) + ',';
  out += "\"reflectorTxStreams\":" + String(reflector_->transmittedStreams()) + ',';
  out += "\"reflectorTxHeaders\":" +
         String(reflector_->transmittedHeaderPackets()) + ',';
  out += "\"reflectorTxVoiceFrames\":" +
         String(reflector_->transmittedVoiceFrames()) + ',';
  out += "\"reflectorTxEnds\":" +
         String(reflector_->transmittedEndPackets()) + ',';
  out += "\"reflectorTxUnderruns\":" + String(reflector_->transmitUnderruns()) + ',';
}
#endif
