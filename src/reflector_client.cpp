#include "reflector_client.h"

#include <esp_system.h>

#include <cstring>

#include "dstar_header.h"

namespace {

// D-Plus replies to the UDP source port and does not require clients to bind
// port 20001 locally. An ephemeral source avoids collisions with another
// hotspot or a router's existing 20001 mapping on the same Internet address.
// A direct control-session test against REF012 confirmed this behavior.
constexpr uint16_t LOCAL_PORT = 0;
constexpr uint32_t LINK_TIMEOUT_MS = 12000;
constexpr uint32_t CONNECT_RETRY_MS = 1000;
constexpr uint32_t POLL_INTERVAL_MS = 1000;
constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;
constexpr uint32_t STREAM_TIMEOUT_MS = 1200;
constexpr uint32_t MAX_RECONNECT_DELAY_MS = 60000;
// D-Plus gateways conventionally receive five redundant headers before media.
// Give those UDP datagrams time to leave before sending the first 20 ms frame.
constexpr uint32_t TX_HEADER_GUARD_MS = 60;
constexpr uint8_t NULL_AMBE[9] = {0x9e, 0x8d, 0x32, 0x88, 0x26,
                                  0x1a, 0x3f, 0x61, 0xe8};

uint16_t randomStreamId() {
  const uint16_t id = static_cast<uint16_t>(esp_random() & 0xffffU);
  return id == 0 ? 1 : id;
}

}  // namespace

void ReflectorClient::begin(ConfigStore& store) {
  store_ = &store;
  chooseStartupTarget();
}

void ReflectorClient::setCallbacks(RxStartCallback start, RxFrameCallback frame,
                                   RxEndCallback end, void* context) {
  rxStart_ = start;
  rxFrame_ = frame;
  rxEnd_ = end;
  callbackContext_ = context;
}

void ReflectorClient::chooseStartupTarget() {
  const AppConfig& config = store_->get();
  reflector_ = config.defaultReflector;
  module_ = config.defaultModule;
  if (config.startupMode == "last") {
    String last = store_->lastReflector();
    last.trim();
    last.toUpperCase();
    if (last.length() == 8 && last[6] == ' ' &&
        isValidReflector(last.substring(0, 6))) {
      reflector_ = last.substring(0, 6);
      module_ = normalizeModule(last.substring(7), "C");
    }
  }
  desiredLink_ = config.startupMode != "manual";
  intentionalUnlink_ = !desiredLink_;
}

void ReflectorClient::updateConfig() {
  if (!store_) return;
  if (!desiredLink_) chooseStartupTarget();
}

void ReflectorClient::directoryUpdated() {
  hostCacheValid_ = false;
  if (!linked() && desiredLink_ && !intentionalUnlink_) {
    reconnectDelayMs_ = 1000;
    nextReconnectMs_ = 0;
    status_ = "reflector directory updated";
  }
}

String ReflectorClient::stationCallsign() const {
  String call = store_->get().callsign;
  call.trim();
  call.toUpperCase();
  call = call.substring(0, 7);
  while (call.length() < 7) call += ' ';
  return call + store_->get().localModule;
}

String ReflectorClient::wireReflectorCallsign() const {
  // XLX uses its DCS alias on port 30051; keep the user-facing XLX name.
  if (reflector_.startsWith("XLX"))
    return "DCS" + reflector_.substring(3) + " " + module_;
  return reflectorCallsign();
}

String ReflectorClient::reflectorCallsign() const {
  return reflector_ + " " + module_;
}

String ReflectorClient::targetText() const { return reflectorCallsign(); }

String ReflectorClient::stateText() const {
  switch (state_) {
    case ReflectorState::WifiDown:
      return "no wifi";
    case ReflectorState::Unlinked:
      return "unlinked";
    case ReflectorState::Linking:
      return "linking";
    case ReflectorState::Linked:
      return "linked";
    case ReflectorState::Error:
      return "error";
  }
  return "unknown";
}

bool ReflectorClient::requestLink(String reflector, String module,
                                  bool persistAsDefault) {
  reflector = normalizeReflector(reflector);
  module = normalizeModule(module, "C");
  if (!isValidCallsign(store_->get().callsign) ||
      !isValidReflector(reflector)) {
    status_ = !isValidCallsign(store_->get().callsign)
                  ? "set a valid amateur-radio callsign first"
                  : "invalid REF, XRF, DCS or XLX reflector";
    return false;
  }
  if (txActive_ || rxActive_) {
    status_ = "voice stream active";
    return false;
  }

  if (linked() || state_ == ReflectorState::Linking) {
    sendConnect(false);
    closeSocket();
  }
  reflector_ = reflector;
  module_ = module;
  desiredLink_ = true;
  intentionalUnlink_ = false;
  reconnectDelayMs_ = 1000;
  nextReconnectMs_ = 0;
  if (persistAsDefault) {
    store_->get().defaultReflector = reflector_;
    store_->get().defaultModule = module_;
    store_->save();
  }
  return wifiWasConnected_ ? startLinkAttempt() : true;
}

void ReflectorClient::requestUnlink(bool intentional) {
  if (txActive_) endTransmission();
  finishIncoming();
  if (socketOpen_) {
    sendConnect(false);
    sendConnect(false);
  }
  closeSocket();
  desiredLink_ = !intentional;
  intentionalUnlink_ = intentional;
  state_ =
      wifiWasConnected_ ? ReflectorState::Unlinked : ReflectorState::WifiDown;
  status_ = intentional ? "unlinked by user" : "connection unavailable";
}

bool ReflectorClient::startLinkAttempt() {
  if (!wifiWasConnected_ || !desiredLink_) return false;
  if (!isValidCallsign(store_->get().callsign)) {
    state_ = ReflectorState::Unlinked;
    status_ = "set a valid amateur-radio callsign first";
    nextReconnectMs_ = millis() + 30000;
    return false;
  }
  // A retry of the same target need not rescan LittleFS on the PTT/web loop.
  // A new target or a successful directory upload invalidates this cache.
  if ((!hostCacheValid_ || host_.name != reflector_) &&
      !directory_.find(reflector_, host_)) {
    DirectoryInfo info;
    String error;
    if (!directory_.inspect(ReflectorDirectory::hostsPath(), info, error)) {
      scheduleReconnect("D-Star directory unavailable: " + error);
    } else {
      scheduleReconnect(reflector_ + " not found in D-Star directory");
    }
    return false;
  }
  IPAddress address;
  if (!address.fromString(host_.ipv4)) {
    scheduleReconnect("reflector address is invalid");
    return false;
  }
  hostCacheValid_ = true;
  protocol_ = reflector_wire::protocolFor(reflector_.c_str());
  closeSocket();
  linkStation_ = stationCallsign();
  socketOpen_ = udp_.begin(LOCAL_PORT) == 1;
  if (!socketOpen_) {
    scheduleReconnect("unable to open UDP port");
    return false;
  }
  state_ = ReflectorState::Linking;
  status_ = "linking to " + targetText();
  linkStartedMs_ = lastConnectMs_ = millis();
  lastPacketMs_ = linkStartedMs_;
  sendConnect(true);
  Serial.printf("[reflector] %s via %s:%u\n", status_.c_str(),
                host_.ipv4.c_str(), reflector_wire::port(protocol_));
  return true;
}

void ReflectorClient::scheduleReconnect(const String& reason) {
  txActive_ = false;
  finishIncoming();
  closeSocket();
  state_ = wifiWasConnected_ ? ReflectorState::Error : ReflectorState::WifiDown;
  status_ = reason;
  if (desiredLink_ && wifiWasConnected_) {
    nextReconnectMs_ = millis() + reconnectDelayMs_;
    reconnectDelayMs_ = min(reconnectDelayMs_ * 2U, MAX_RECONNECT_DELAY_MS);
    reconnectCount_++;
  }
  Serial.printf("[reflector] %s; retry in %lums\n", reason.c_str(),
                static_cast<unsigned long>(reconnectDelayMs_));
}

void ReflectorClient::closeSocket() {
  if (socketOpen_) udp_.stop();
  socketOpen_ = false;
  clearReceiveCandidates();
}

bool ReflectorClient::sendPacket(const uint8_t* data, size_t length) {
  if (!socketOpen_ || host_.ipv4.isEmpty()) {
    sendFailures_++;
    return false;
  }
  IPAddress address;
  if (!address.fromString(host_.ipv4) ||
      !udp_.beginPacket(address, reflector_wire::port(protocol_))) {
    sendFailures_++;
    return false;
  }
  udp_.write(data, length);
  if (udp_.endPacket() == 1) return true;
  sendFailures_++;
  return false;
}

void ReflectorClient::sendConnect(bool link) {
  uint8_t packet[519];
  const size_t size = reflector_wire::connect(
      packet, sizeof(packet), protocol_, linkStation_.c_str(),
      wireReflectorCallsign().c_str(), link);
  if (size) sendPacket(packet, size);
}

void ReflectorClient::sendLogin() {
  uint8_t packet[28]{};
  packet[0] = 0x1c;
  packet[1] = 0xc0;
  packet[2] = 0x04;
  String call = store_->get().callsign;
  call.trim();
  call.toUpperCase();
  memcpy(packet + 4, call.c_str(), min(static_cast<size_t>(16), call.length()));
  memcpy(packet + 20, "DV019999", 8);
  sendPacket(packet, sizeof(packet));
}

void ReflectorClient::sendPoll() {
  uint8_t packet[17];
  const size_t size = reflector_wire::poll(packet, sizeof(packet), protocol_,
                                           linkStation_.c_str(),
                                           wireReflectorCallsign().c_str());
  if (size && sendPacket(packet, size)) sentPolls_++;
}

void ReflectorClient::service(bool wifiConnected) {
  if (!wifiConnected) {
    if (wifiWasConnected_) {
      if (txActive_) txActive_ = false;
      finishIncoming();
      closeSocket();
      state_ = ReflectorState::WifiDown;
      status_ = "waiting for wifi";
      nextReconnectMs_ = 0;
    }
    wifiWasConnected_ = false;
    return;
  }

  if (!wifiWasConnected_) {
    wifiWasConnected_ = true;
    state_ = ReflectorState::Unlinked;
    status_ = intentionalUnlink_ ? "unlinked" : "wifi restored";
    reconnectDelayMs_ = 1000;
    nextReconnectMs_ = 0;
  }

  drainPackets();
  serviceIncomingStreams();
  serviceJitterBuffer();

  // Packet handlers refresh lastPacketMs_ and rxLastMediaMs_. Read the clock
  // AFTER them: an earlier 'now' minus a newer timestamp wraps unsigned time
  // and immediately tears down a healthy connection/receive stream.
  const uint32_t now = millis();

  if (rxActive_ && now - rxLastMediaMs_ > STREAM_TIMEOUT_MS) finishIncoming();

  if (state_ == ReflectorState::Linking) {
    if (now - linkStartedMs_ > LINK_TIMEOUT_MS) {
      scheduleReconnect("link timeout");
    } else if (now - lastConnectMs_ >= CONNECT_RETRY_MS) {
      lastConnectMs_ = now;
      sendConnect(true);
    }
  } else if (state_ == ReflectorState::Linked) {
    const uint32_t pollInterval =
        protocol_ == reflector_wire::Protocol::DPlus    ? POLL_INTERVAL_MS
        : protocol_ == reflector_wire::Protocol::DExtra ? 10000U
                                                        : 5000U;
    if (now - lastPollMs_ >= pollInterval) {
      lastPollMs_ = now;
      sendPoll();
    }
    if (now - lastPacketMs_ > (protocol_ == reflector_wire::Protocol::DPlus
                                   ? CONNECTION_TIMEOUT_MS
                                   : 60000U)) {
      if (txActive_) txActive_ = false;
      finishIncoming();
      scheduleReconnect("reflector timeout");
    }
  } else if (desiredLink_ && !intentionalUnlink_ &&
             (nextReconnectMs_ == 0 ||
              static_cast<int32_t>(now - nextReconnectMs_) >= 0)) {
    startLinkAttempt();
  }
}

void ReflectorClient::drainPackets() {
  if (!socketOpen_) return;
  uint8_t packet[600];
  // Bound each pass so a burst cannot starve PTT, audio delivery, or HTTP.
  for (uint8_t serviced = 0; socketOpen_ && serviced < 32; ++serviced) {
    const int length = udp_.parsePacket();
    if (length <= 0) break;
    const int read =
        udp_.read(packet, min(length, static_cast<int>(sizeof(packet))));
    IPAddress expected;
    expected.fromString(host_.ipv4);
    if (read > 0 && udp_.remoteIP() == expected &&
        udp_.remotePort() == reflector_wire::port(protocol_)) {
      receivedPackets_++;
      if (read == 3 && packet[0] == 0x03 && packet[1] == 0x60 &&
          packet[2] == 0x00) {
        receivedPolls_++;
      }
      if (protocol_ == reflector_wire::Protocol::DPlus)
        lastPacketMs_ = millis();
      handlePacket(packet, static_cast<size_t>(read));
    } else if (read > 0) {
      rejectedPeerPackets_++;
    }
  }
}

void ReflectorClient::handlePacket(const uint8_t* packet, size_t length) {
  if (protocol_ != reflector_wire::Protocol::DPlus) {
    handleOtherProtocol(packet, length);
    return;
  }
  const bool dsvt = length >= 6 && memcmp(packet + 2, "DSVT", 4) == 0;
  if (!dsvt && (length == 3 || length == 5 || length == 8 || length == 28)) {
    handleControl(packet, length);
  } else if (length >= dplus::HEADER_PACKET_SIZE && packet[0] == 0x3a) {
    handleHeader(packet, length);
  } else if (dsvt && (packet[0] == 0x1d || packet[0] == 0x20)) {
    handleFrame(packet, length);
  } else {
    unknownPackets_++;
  }
}

void ReflectorClient::markLinked() {
  state_ = ReflectorState::Linked;
  status_ = "linked to " + targetText();
  reconnectDelayMs_ = 1000;
  nextReconnectMs_ = 0;
  store_->saveLastReflector(targetText());
  sendPoll();
  Serial.printf("[reflector] %s\n", status_.c_str());
}

void ReflectorClient::handleOtherProtocol(const uint8_t* packet,
                                          size_t length) {
  using namespace reflector_wire;
  const Control command =
      control(packet, length, protocol_, linkStation_.c_str(),
              wireReflectorCallsign().c_str());
  if (command != Control::Unknown) lastPacketMs_ = millis();
  if (command == Control::Poll) {
    ++receivedPolls_;
    if (protocol_ == Protocol::DCS && linked()) sendPoll();
    return;
  }
  if (command == Control::Ack) {
    if (state_ == ReflectorState::Linking) markLinked();
    return;
  }
  if (command == Control::Nak || command == Control::Unlink) {
    if (desiredLink_)
      scheduleReconnect(command == Control::Nak ? "link refused"
                                                : "remote unlink");
    return;
  }
  Media media;
  if (!readMedia(packet, length, protocol_, media)) {
    ++mediaParseFailures_;
    return;
  }
  if (!linked() || txActive_) {
    ++busyMediaDrops_;
    return;
  }
  if (media.hasHeader) {
    ++receivedHeaders_;
    // DCS includes routing in every packet; it can arrive on several modules.
    const String target = wireReflectorCallsign();
    if (protocol_ == Protocol::DCS &&
        memcmp(media.rfHeader + 3, target.c_str(), 8) != 0) {
      ++busyMediaDrops_;
      return;
    }
    if (!media.end) rememberIncomingHeader(media.rfHeader, media.stream);
  }

  if (media.hasVoice) {
    ++receivedVoicePackets_;
    lastPacketMs_ = millis();
    acceptFrame(media.frame, media.stream, media.sequence, media.end);
  }
}

void ReflectorClient::handleControl(const uint8_t* packet, size_t length) {
  if (length == 5 && packet[4] == 1 && state_ == ReflectorState::Linking) {
    sendLogin();
  } else if (length == 8 && state_ == ReflectorState::Linking) {
    if (memcmp(packet + 4, "OKRW", 4) == 0) {
      markLinked();
    } else {
      scheduleReconnect(memcmp(packet + 4, "BUSY", 4) == 0
                            ? String("reflector busy")
                            : String("link refused"));
    }
  } else if (length == 5 && packet[4] == 0 && desiredLink_) {
    scheduleReconnect("remote unlink");
  }
}

void ReflectorClient::handleHeader(const uint8_t* packet, size_t length) {
  receivedHeaders_++;
  if (!linked() || txActive_) {
    busyMediaDrops_++;
    return;
  }
  uint8_t header[dplus::RF_HEADER_SIZE];
  uint16_t streamId = 0;
  if (!dplus::readHeader(packet, length, header, streamId)) {
    mediaParseFailures_++;
    return;
  }
  // This UDP socket is authenticated to one reflector and accepts packets only
  // from that reflector's IP/port. Do not additionally require RPT1/RPT2 to
  // equal the reflector callsign: normal D-Plus gateways legitimately preserve
  // or rewrite those route fields in several ways. The former exact-match gate
  // silently discarded otherwise valid streams.
  rememberIncomingHeader(header, streamId);
}

void ReflectorClient::handleFrame(const uint8_t* packet, size_t length) {
  receivedVoicePackets_++;
  uint8_t frame[dplus::DV_FRAME_SIZE];
  uint16_t streamId = 0;
  uint8_t sequence = 0;
  bool end = false;
  if (!dplus::readFrame(packet, length, frame, streamId, sequence, end)) {
    mediaParseFailures_++;
    return;
  }
  acceptFrame(frame, streamId, sequence, end);
}

bool ReflectorClient::beginTransmission() {
  if (!linked() || txActive_) return false;
  finishIncoming();
  clearReceiveCandidates();
  txStreamId_ = randomStreamId();
  txSequence_ = 0;
  txPacketCounter_ = 0;
  dstar::buildHeader(txHeader_, wireReflectorCallsign(), stationCallsign(),
                     "CQCQCQ", store_->get().callsign, store_->get().mySuffix);
  uint8_t packet[dplus::HEADER_PACKET_SIZE];
  const size_t size = reflector_wire::header(packet, sizeof(packet), protocol_,
                                             txHeader_, txStreamId_);
  if (!size && protocol_ != reflector_wire::Protocol::DCS) return false;
  uint8_t sent = protocol_ == reflector_wire::Protocol::DCS ? 1 : 0;
  for (uint8_t i = 0; size && i < 5; ++i) {
    if (sendPacket(packet, size)) {
      sent++;
      transmittedHeaderPackets_++;
    }
  }
  if (sent == 0) {
    txStreamId_ = 0;
    return false;
  }
  txActive_ = true;
  txNextVoiceMs_ = millis() + TX_HEADER_GUARD_MS;
  transmittedStreams_++;
  return true;
}

bool ReflectorClient::voiceFrameDue() const {
  return txActive_ && static_cast<int32_t>(millis() - txNextVoiceMs_) >= 0;
}

bool ReflectorClient::sendVoice(const uint8_t* ambe) {
  if (!txActive_ || !linked()) return false;
  uint8_t frame[dplus::DV_FRAME_SIZE];
  memcpy(frame, ambe, 9);
  if (txSequence_ == 0) {
    frame[9] = 0x55;
    frame[10] = 0x2d;
    frame[11] = 0x16;
  } else {
    // No-data marker 66 66 66 XOR the D-STAR slow-data scrambler. Raw 66s
    // decode to a reserved mini-header and confuse data-aware receivers.
    frame[9] = 0x16;
    frame[10] = 0x29;
    frame[11] = 0xf5;
  }
  uint8_t packet[100];
  const size_t size =
      reflector_wire::voice(packet, sizeof(packet), protocol_, txHeader_, frame,
                            txStreamId_, txSequence_, txPacketCounter_, false);
  if (!size || !sendPacket(packet, size)) return false;
  const uint32_t now = millis();
  // Preserve 20 ms cadence, but do not burst an old backlog after a long task
  // stall. At most the caller sends two frames per loop pass.
  if (static_cast<int32_t>(now - txNextVoiceMs_) > 40) {
    txNextVoiceMs_ = now + 20;
  } else {
    txNextVoiceMs_ += 20;
  }
  txSequence_ = dplus::nextSequence(txSequence_);
  ++txPacketCounter_;
  transmittedVoiceFrames_++;
  return true;
}

bool ReflectorClient::sendSilence() {
  // A slow encoder must not stall the D-Star packet clock. Use the standard
  // silence frame in empty 20 ms slots; speech remains explicitly best effort.
  if (!sendVoice(NULL_AMBE)) return false;
  ++transmitUnderruns_;
  return true;
}

void ReflectorClient::endTransmission() {
  if (!txActive_) return;
  uint8_t packet[100];
  const size_t size = reflector_wire::voice(
      packet, sizeof(packet), protocol_, txHeader_, nullptr, txStreamId_,
      txSequence_, txPacketCounter_, true);
  if (size && sendPacket(packet, size)) transmittedEndPackets_++;
  txActive_ = false;
  txStreamId_ = 0;
  txNextVoiceMs_ = 0;
}
