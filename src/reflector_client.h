#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

#include "app_config.h"
#include "diagnostic_counter.h"
#include "dplus_protocol.h"
#include "dstar_receive_filter.h"
#include "reflector_directory.h"
#include "reflector_protocol.h"
#include "rx_jitter_policy.h"

enum class ReflectorState : uint8_t {
  WifiDown,
  Unlinked,
  Linking,
  Linked,
  Error,
};

struct IncomingCallInfo {
  String from;
  String suffix;
  String destination;
  String rpt1;
  String rpt2;
  uint16_t streamId = 0;
};

class ReflectorClient {
 public:
  using RxStartCallback = void (*)(const IncomingCallInfo&, void*);
  using RxFrameCallback = void (*)(const uint8_t ambe[9], void*);
  using RxEndCallback = void (*)(void*);

  void begin(ConfigStore& store);
  void setCallbacks(RxStartCallback start, RxFrameCallback frame,
                    RxEndCallback end, void* context);
  void service(bool wifiConnected);

  bool requestLink(String reflector, String module,
                   bool persistAsDefault = false);
  void requestUnlink(bool intentional = true);
  void updateConfig();
  void directoryUpdated();

  bool beginTransmission();
  bool voiceFrameDue() const;
  bool sendVoice(const uint8_t ambe[9]);
  bool sendSilence();
  void endTransmission();

  ReflectorState state() const { return state_; }
  bool linked() const { return state_ == ReflectorState::Linked; }
  bool incoming() const { return rxActive_; }
  bool transmitting() const { return txActive_; }
  String stateText() const;
  String statusText() const { return status_; }
  String targetText() const;
  String hostIp() const { return host_.ipv4; }
  String lastHeard() const { return lastHeard_; }
  uint32_t reconnectCount() const { return reconnectCount_; }
  uint32_t droppedRxFrames() const { return droppedRxFrames_; }
  uint32_t concealedRxFrames() const { return concealedRxFrames_; }
  uint32_t receivedNullFrames() const { return receivedNullFrames_; }
  uint32_t jitterTargetMs() const { return jitterPolicy_.targetFrames() * 20U; }
  uint32_t jitterRefills() const { return jitterPolicy_.refills(); }
  uint32_t jitterResyncs() const { return jitterResyncs_; }
  uint32_t fastDataFrames() const { return fastDataFrames_; }
  uint32_t unclassifiedFrames() const { return unclassifiedFrames_; }
  uint32_t competingVoiceFrames() const { return competingVoiceFrames_; }
  uint32_t receivedPackets() const { return receivedPackets_; }
  uint32_t receivedPolls() const { return receivedPolls_; }
  uint32_t sentPolls() const { return sentPolls_; }
  uint32_t sendFailures() const { return sendFailures_; }
  uint32_t rejectedPeerPackets() const { return rejectedPeerPackets_; }
  uint32_t receivedHeaders() const { return receivedHeaders_; }
  uint32_t acceptedHeaders() const { return acceptedHeaders_; }
  uint32_t receivedVoicePackets() const { return receivedVoicePackets_; }
  uint32_t acceptedVoiceFrames() const { return acceptedVoiceFrames_; }
  uint32_t receivedEndPackets() const { return receivedEndPackets_; }
  uint32_t mediaParseFailures() const { return mediaParseFailures_; }
  uint32_t voiceWithoutHeader() const { return voiceWithoutHeader_; }
  uint32_t wrongStreamFrames() const { return wrongStreamFrames_; }
  uint32_t busyMediaDrops() const { return busyMediaDrops_; }
  uint32_t unknownPackets() const { return unknownPackets_; }
  uint32_t transmittedStreams() const { return transmittedStreams_; }
  uint32_t transmittedHeaderPackets() const {
    return transmittedHeaderPackets_;
  }
  uint32_t transmittedVoiceFrames() const { return transmittedVoiceFrames_; }
  uint32_t transmittedEndPackets() const { return transmittedEndPackets_; }
  uint32_t transmitUnderruns() const { return transmitUnderruns_; }

 private:
  struct JitterSlot {
    bool valid = false;
    int32_t index = -1;
    uint8_t ambe[9]{};
  };
  struct ReceiveCandidate {
    bool used = false;
    uint16_t stream = 0;
    uint32_t lastMediaMs = 0;
    uint8_t header[dplus::RF_HEADER_SIZE]{};
    DStarReceiveFilter filter;
  };

  void chooseStartupTarget();
  bool startLinkAttempt();
  void scheduleReconnect(const String& reason);
  void closeSocket();
  bool sendPacket(const uint8_t* data, size_t length);
  void sendConnect(bool link);
  void sendLogin();
  void sendPoll();
  void drainPackets();
  void handlePacket(const uint8_t* packet, size_t length);
  void markLinked();
  void handleOtherProtocol(const uint8_t* packet, size_t length);
  void acceptFrame(const uint8_t* frame, uint16_t streamId, uint8_t sequence,
                   bool end);
  void handleControl(const uint8_t* packet, size_t length);
  void handleHeader(const uint8_t* packet, size_t length);
  void handleFrame(const uint8_t* packet, size_t length);
  void rememberIncomingHeader(const uint8_t* header, uint16_t stream);
  ReceiveCandidate* candidateFor(uint16_t stream);
  void serviceIncomingStreams();
  void drainCandidate(ReceiveCandidate& candidate, bool ending = false);
  void acceptBlock(const ReceiveCandidate& candidate,
                   const DStarReceiveFilter::Block& block);
  void clearReceiveCandidates();

  void beginIncoming(const uint8_t header[dplus::RF_HEADER_SIZE],
                     uint16_t streamId);
  void queueIncomingFrame(const uint8_t ambe[9], uint8_t sequence);
  void trimJitterWindow(int32_t newestIndex);
  void discardJitterBefore(int32_t resume);
  void serviceJitterBuffer();
  void finishIncoming();
  void clearJitter();
  String wireReflectorCallsign() const;
  String stationCallsign() const;
  String reflectorCallsign() const;

  ConfigStore* store_ = nullptr;
  ReflectorDirectory directory_;
  WiFiUDP udp_;
  ReflectorHost host_;
  reflector_wire::Protocol protocol_ = reflector_wire::Protocol::DPlus;
  bool hostCacheValid_ = false;
  ReflectorState state_ = ReflectorState::WifiDown;
  String status_ = "waiting for wifi";
  String reflector_ = "REF001";
  String module_ = "C";
  String
      linkStation_;  // Keepalive identity is fixed for one authenticated link.
  String lastHeard_;
  bool desiredLink_ = false;
  bool intentionalUnlink_ = false;
  bool socketOpen_ = false;
  bool wifiWasConnected_ = false;

  uint32_t linkStartedMs_ = 0;
  uint32_t lastConnectMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastPacketMs_ = 0;
  uint32_t nextReconnectMs_ = 0;
  uint32_t reconnectDelayMs_ = 1000;
  DiagnosticCounter<> reconnectCount_;
  DiagnosticCounter<> receivedPackets_;
  DiagnosticCounter<> receivedPolls_;
  DiagnosticCounter<> sentPolls_;
  DiagnosticCounter<> sendFailures_;
  DiagnosticCounter<> rejectedPeerPackets_;
  DiagnosticCounter<> receivedHeaders_;
  DiagnosticCounter<> acceptedHeaders_;
  DiagnosticCounter<> receivedVoicePackets_;
  DiagnosticCounter<> acceptedVoiceFrames_;
  DiagnosticCounter<> receivedEndPackets_;
  DiagnosticCounter<> mediaParseFailures_;
  DiagnosticCounter<> voiceWithoutHeader_;
  DiagnosticCounter<> wrongStreamFrames_;
  DiagnosticCounter<> busyMediaDrops_;
  DiagnosticCounter<> unknownPackets_;

  bool txActive_ = false;
  uint16_t txStreamId_ = 0;
  uint8_t txSequence_ = 0;
  uint32_t txPacketCounter_ = 0;
  uint8_t txHeader_[dplus::RF_HEADER_SIZE]{};
  uint32_t txNextVoiceMs_ = 0;
  DiagnosticCounter<> transmittedStreams_;
  DiagnosticCounter<> transmittedHeaderPackets_;
  DiagnosticCounter<> transmittedVoiceFrames_;
  DiagnosticCounter<> transmittedEndPackets_;
  DiagnosticCounter<> transmitUnderruns_;

  bool rxActive_ = false;
  bool rxEndPending_ = false;
  uint16_t rxStreamId_ = 0;
  int8_t rxLastSequence_ = -1;
  int32_t rxHighestIndex_ = -1;
  int32_t rxPlayIndex_ = 0;
  uint32_t rxLastMediaMs_ = 0;
  RxJitterPolicy jitterPolicy_;
  uint8_t bufferedFrames_ = 0;
  DiagnosticCounter<> droppedRxFrames_;
  DiagnosticCounter<> concealedRxFrames_;
  DiagnosticCounter<> receivedNullFrames_;
  DiagnosticCounter<> jitterResyncs_;
  DiagnosticCounter<> fastDataFrames_;
  DiagnosticCounter<> unclassifiedFrames_;
  DiagnosticCounter<> competingVoiceFrames_;
  // Bounded admission pool. Data-only headers never own the speaker or evict
  // the active voice stream, even when several reflector modules interleave.
  ReceiveCandidate candidates_[4];
  static constexpr uint8_t JITTER_SLOTS = 32;
  JitterSlot jitter_[JITTER_SLOTS];

  RxStartCallback rxStart_ = nullptr;
  RxFrameCallback rxFrame_ = nullptr;
  RxEndCallback rxEnd_ = nullptr;
  void* callbackContext_ = nullptr;
};
