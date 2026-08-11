#include "effect_pt2399_delay.h"

// 32,768 samples per channel in DMAMEM (131KB total internal RAM)
// Gives up to 3.0 seconds max delay time using PT2399 4x variable downsampling
#define DELAY_BUF_SIZE 32768
#define MASK (DELAY_BUF_SIZE - 1)

static DMAMEM int16_t delay_mem_l[DELAY_BUF_SIZE];
static DMAMEM int16_t delay_mem_r[DELAY_BUF_SIZE];

AudioEffectPT2399Delay::AudioEffectPT2399Delay() 
    : AudioStream(2, inputQueueArray)
    , target_delay_ms_(400.0f)
    , smooth_delay_ms_(400.0f)
    , smooth_downsample_(1.0f)
    , feedback_(0.3f)
    , mix_(0.3f)
    , cross_feedback_(0.15f)
    , write_pos_f_(0.0f)
    , lp_in_l_(0.0f)
    , lp_in_r_(0.0f)
    , lp_fb_l_(0.0f)
    , lp_fb_r_(0.0f) {
    clear();
}

void AudioEffectPT2399Delay::clear() {
    AudioNoInterrupts();
    memset(delay_mem_l, 0, sizeof(delay_mem_l));
    memset(delay_mem_r, 0, sizeof(delay_mem_r));
    write_pos_f_ = 0.0f;
    lp_in_l_ = 0.0f;
    lp_in_r_ = 0.0f;
    lp_fb_l_ = 0.0f;
    lp_fb_r_ = 0.0f;
    AudioInterrupts();
}

void AudioEffectPT2399Delay::setDelayTime(float ms) {
    if (ms < 50.0f) ms = 50.0f;
    if (ms > 3000.0f) ms = 3000.0f;
    target_delay_ms_ = ms;
}

void AudioEffectPT2399Delay::setFeedback(float fb) {
    if (fb < 0.0f) fb = 0.0f;
    if (fb > 0.95f) fb = 0.95f;
    feedback_ = fb;
}

void AudioEffectPT2399Delay::setMix(float mix) {
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    mix_ = mix;
}

void AudioEffectPT2399Delay::setCrossFeedback(float xfb) {
    if (xfb < 0.0f) xfb = 0.0f;
    if (xfb > 0.4f) xfb = 0.4f;
    cross_feedback_ = xfb;
}

void AudioEffectPT2399Delay::update() {
    audio_block_t *blockL = receiveReadOnly(0);
    audio_block_t *blockR = receiveReadOnly(1);

    audio_block_t *outL = allocate();
    audio_block_t *outR = allocate();

    if (!outL || !outR) {
        if (outL) release(outL);
        if (outR) release(outR);
        if (blockL) release((audio_block_t *)blockL);
        if (blockR) release((audio_block_t *)blockR);
        return;
    }

    // Calculate target downsampling rate based on target delay time
    // For ms <= 1400: 1:1 full rate (44.1kHz)
    // For ms > 1400: downsample factor scales smoothly up to 2.15 (20.5kHz -> 11kHz)
    float target_ds = 1.0f;
    if (target_delay_ms_ > 1400.0f) {
        target_ds = 1.0f + ((target_delay_ms_ - 1400.0f) / 1600.0f) * 1.15f;
    }

    for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        // PER-SAMPLE SMOOTHING: eliminates zipper noise completely!
        // Updates read position and downsampling factor continuously on every single sample
        smooth_delay_ms_ += (target_delay_ms_ - smooth_delay_ms_) * 0.0008f;
        smooth_downsample_ += (target_ds - smooth_downsample_) * 0.0008f;

        float in_l_samp = blockL ? ((float)blockL->data[i] / 32768.0f) : 0.0f;
        float in_r_samp = blockR ? ((float)blockR->data[i] / 32768.0f) : 0.0f;

        // Anti-alias lowpass filter: cutoff drops dynamically as downsampling increases
        // PT2399 warm analog character (8kHz cutoff at 1x down to 2kHz cutoff at max delay)
        float filter_alpha = 0.55f / smooth_downsample_;
        lp_in_l_ += filter_alpha * (in_l_samp - lp_in_l_);
        lp_in_r_ += filter_alpha * (in_r_samp - lp_in_r_);

        // Write head advances by 1.0 / downsample per sample
        float step = 1.0f / smooth_downsample_;
        write_pos_f_ += step;
        if (write_pos_f_ >= (float)DELAY_BUF_SIZE) {
            write_pos_f_ -= (float)DELAY_BUF_SIZE;
        }

        uint32_t write_idx = (uint32_t)write_pos_f_;
        if (write_idx >= DELAY_BUF_SIZE) write_idx = 0;

        // Calculate effective physical read offset in buffer
        float delay_samples_phys = (smooth_delay_ms_ * 44.1f) / smooth_downsample_;
        if (delay_samples_phys > (float)(DELAY_BUF_SIZE - 4)) {
            delay_samples_phys = (float)(DELAY_BUF_SIZE - 4);
        }

        float read_pos = write_pos_f_ - delay_samples_phys;
        while (read_pos < 0.0f) read_pos += (float)DELAY_BUF_SIZE;
        while (read_pos >= (float)DELAY_BUF_SIZE) read_pos -= (float)DELAY_BUF_SIZE;

        uint32_t idx0 = (uint32_t)read_pos;
        uint32_t idx1 = (idx0 + 1) & MASK;
        float frac = read_pos - (float)idx0;

        // Linear interpolation of delay line samples
        float s0_l = (float)delay_mem_l[idx0] / 32768.0f;
        float s1_l = (float)delay_mem_l[idx1] / 32768.0f;
        float s0_r = (float)delay_mem_r[idx0] / 32768.0f;
        float s1_r = (float)delay_mem_r[idx1] / 32768.0f;

        float wet_l = s0_l + frac * (s1_l - s0_l);
        float wet_r = s0_r + frac * (s1_r - s0_r);

        // Feedback LP filtering (repeats get darker on each pass)
        lp_fb_l_ += filter_alpha * (wet_l - lp_fb_l_);
        lp_fb_r_ += filter_alpha * (wet_r - lp_fb_r_);

        // Feed filtered signal back into delay line with smooth tanhf soft saturation
        float raw_write_l = lp_in_l_ + (lp_fb_l_ * feedback_) + (lp_fb_r_ * cross_feedback_);
        float raw_write_r = lp_in_r_ + (lp_fb_r_ * feedback_) + (lp_fb_l_ * cross_feedback_);
        float write_l = tanhf(raw_write_l);
        float write_r = tanhf(raw_write_r);

        delay_mem_l[write_idx] = (int16_t)(write_l * 32767.0f);
        delay_mem_r[write_idx] = (int16_t)(write_r * 32767.0f);

        // Output mix: dry + wet
        float out_l_val = in_l_samp * (1.0f - mix_ * 0.5f) + wet_l * mix_;
        float out_r_val = in_r_samp * (1.0f - mix_ * 0.5f) + wet_r * mix_;

        int32_t il = (int32_t)(out_l_val * 32767.0f);
        int32_t ir = (int32_t)(out_r_val * 32767.0f);
        if (il > 32767) il = 32767; else if (il < -32768) il = -32768;
        if (ir > 32767) ir = 32767; else if (ir < -32768) ir = -32768;

        outL->data[i] = (int16_t)il;
        outR->data[i] = (int16_t)ir;
    }

    transmit(outL, 0);
    transmit(outR, 1);
    release(outL);
    release(outR);

    if (blockL) release((audio_block_t *)blockL);
    if (blockR) release((audio_block_t *)blockR);
}
