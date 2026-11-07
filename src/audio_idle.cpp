#include "audio_engine.h"

void AudioEngine::wakeWorker() {
  // Publish flags/settings or queue contents before notifying. The notification
  // remains pending if the worker has not yet entered its wait. Only the codec
  // task uses this handle; the microphone task has its own notification slot.
  if (task_) xTaskNotifyGive(task_);
}

void AudioEngine::waitForIdleWork() {
  // Keep active capture, playback and DMA-tail servicing on their existing
  // cadence. Between sessions there is no deadline: all producers notify us.
  // The queue check also prevents coalesced notifications stranding work.
  if (captureBusy_ || playbackBusy() || codecTestBusy() ||
      (rxQueue_ && uxQueueMessagesWaiting(rxQueue_))) return;
  ++idleWaits_;
  // Do not clear notifications before checking state: an event can arrive
  // between the predicate above and this call. Take consumes that event or
  // blocks atomically, so there is no polling timeout or lost-wakeup window.
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  ++idleWakeups_;
}
