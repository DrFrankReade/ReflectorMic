// SPDX-License-Identifier: GPL-2.0-or-later
// Cache deterministic decoder filter results, without approximating atan2 or
// changing oscillator phases. Current/previous models commonly share pitches.
// Internal use only: firmware decodes on one worker; other builds use TLS.
struct s3_render_cache_entry {
    uint32_t pitch;
    int valid;
    uint8_t gain_valid[57], phase_valid[57];
    float gain[57], phase[57];
};

static struct s3_render_cache_entry* s3_render_cache(float pitch) {
    static MBE_THREAD_LOCAL struct s3_render_cache_entry slots[3];
    static MBE_THREAD_LOCAL unsigned next;
    uint32_t key;
    memcpy(&key, &pitch, sizeof(key));
    for (unsigned i = 0; i < 3; ++i) {
        if (slots[i].valid && slots[i].pitch == key) return &slots[i];
    }
    struct s3_render_cache_entry* slot = &slots[next];
    next = (next + 1) % 3;
    memset(slot, 0, sizeof(*slot));
    slot->pitch = key;
    slot->valid = 1;
    return slot;
}

static float s3_render_gain(struct s3_render_cache_entry* cache, int harmonic, float hz) {
    if (!cache->gain_valid[harmonic]) {
        cache->gain[harmonic] = mbe_low_band_gain(hz);
        cache->gain_valid[harmonic] = 1;
    }
    return cache->gain[harmonic];
}

static float s3_render_phase(struct s3_render_cache_entry* cache, int harmonic, float hz) {
    if (!cache->phase_valid[harmonic]) {
        cache->phase[harmonic] = mbe_low_band_phase(hz);
        cache->phase_valid[harmonic] = 1;
    }
    return cache->phase[harmonic];
}
