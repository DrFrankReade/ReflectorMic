"""Local adaptation of pinned M5Unified's speaker; never rewrite libdeps.

The IDF 4.4 driver owns the S3 clocks and slot format. M5's mixer still supplies
resampling, PCM lifetime/queueing, tones and idle DMA silence. Its asynchronous
raw register clock rewrite is deliberately removed for this fixed board.
"""
import hashlib

# Refuse to silently adapt a different implementation after a dependency bump.
PINNED_SHA256 = '04a021e25ff85a92199e76cf3eaf0f474a38b75b74a79abb93d1f603896e6e4f'


def transform(source):
    digest = hashlib.sha256(source.encode('utf-8')).hexdigest()
    if digest != PINNED_SHA256:
        raise RuntimeError('M5 Speaker_Class.cpp changed; review fixed-clock adaptation: ' + digest)
    marker = '    const i2s_port_t i2s_port = self->_cfg.i2s_port;'
    start = source.index(marker) + len(marker)
    end = source.index('    enum spk_i2s_state', start)
    source = source[:start] + '''

    // ReflectorMic: _setup_i2s installs 48 kHz, two 16-bit Philips slots and
    // MCLK=256*fs through IDF. Do not rewrite clock/slot registers in this task
    // after the peripheral has already started. Rate here is frames/second.
    const int32_t spk_sample_rate_x256 = self->_cfg.sample_rate * SAMPLERATE_MUL;

''' + source[end:]
    # Configure the supported legacy driver before any task sees the port.
    original = '    i2s_config.sample_rate          = 48000; // dummy setting'
    replacement = '''    i2s_config.sample_rate          = _cfg.sample_rate;
    i2s_config.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    i2s_config.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;'''
    if source.count(original) != 1:
        raise RuntimeError('Legacy speaker clock setup not found exactly once')
    source = source.replace(original, replacement)
    # This patch is only valid for the board/configuration tested here.
    anchor = '    if (_cfg.pin_data_out < 0) { return ESP_FAIL; }'
    if source.count(anchor) != 1:
        raise RuntimeError('Speaker setup entry not found exactly once')
    source = source.replace(anchor, anchor + '''
    if (!_cfg.stereo || _cfg.use_dac || _cfg.buzzer || _cfg.sample_rate != 48000
        || _cfg.pin_mck != 11 || _cfg.i2s_port != I2S_NUM_1) { return ESP_FAIL; }
''')
    return source
