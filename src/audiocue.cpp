// SPDX-License-Identifier: MIT
#include "audiocue.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

static constexpr uint32_t CUE_RATE = 48000;
static constexpr int CUE_DURATION_MS = 100;
static constexpr int CUE_SAMPLES = CUE_RATE * CUE_DURATION_MS / 1000;

static void generateTone(float* buf, int len, float freq_start, float freq_end) {
    float phase = 0.0f;
    int ramp = len / 10;
    for (int i = 0; i < len; i++) {
        float freq = freq_start + (freq_end - freq_start) * static_cast<float>(i) / len;
        phase += freq / CUE_RATE;
        float envelope = 1.0f;
        if (i < ramp)
            envelope = static_cast<float>(i) / static_cast<float>(ramp);
        else if (i > len - ramp)
            envelope = static_cast<float>(len - i) / static_cast<float>(ramp);
        buf[i] = 0.3f * envelope * std::sin(2.0f * M_PI * phase);
    }
}

void AudioCue::onProcess(void* userdata) {
    auto* self = static_cast<AudioCue*>(userdata);

    struct pw_buffer* b = pw_stream_dequeue_buffer(self->stream_);
    if (!b) return;

    struct spa_buffer* buf = b->buffer;
    auto* data = static_cast<float*>(buf->datas[0].data);
    uint32_t n_frames = buf->datas[0].maxsize / sizeof(float);

    if (!self->playing_ || self->tone_pos_ >= self->tone_len_) {
        memset(data, 0, n_frames * sizeof(float));
        buf->datas[0].chunk->size = n_frames * sizeof(float);
        buf->datas[0].chunk->offset = 0;
        buf->datas[0].chunk->stride = sizeof(float);
        pw_stream_queue_buffer(self->stream_, b);
        return;
    }

    uint32_t remain = self->tone_len_ - self->tone_pos_;
    uint32_t to_write = (n_frames < remain) ? n_frames : remain;
    memcpy(data, self->tone_ + self->tone_pos_, to_write * sizeof(float));
    if (to_write < n_frames)
        memset(data + to_write, 0, (n_frames - to_write) * sizeof(float));
    self->tone_pos_ += to_write;
    if (self->tone_pos_ >= self->tone_len_)
        self->playing_ = false;

    buf->datas[0].chunk->size = n_frames * sizeof(float);
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = sizeof(float);
    pw_stream_queue_buffer(self->stream_, b);
}

static const struct pw_stream_events cue_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = AudioCue::onProcess,
};

bool AudioCue::init() {
    loop_ = pw_thread_loop_new("audio-cue", nullptr);
    if (!loop_) {
        fprintf(stderr, "audiocue: failed to create thread loop\n");
        return false;
    }

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Notification",
        nullptr);

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        "whisker-cue",
        props,
        &cue_events,
        this);

    if (!stream_) {
        fprintf(stderr, "audiocue: failed to create stream\n");
        return false;
    }

    uint8_t param_buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(param_buffer, sizeof(param_buffer));
    struct spa_audio_info_raw raw_info = {};
    raw_info.format = SPA_AUDIO_FORMAT_F32;
    raw_info.rate = CUE_RATE;
    raw_info.channels = 1;
    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &raw_info);

    int ret = pw_stream_connect(stream_,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        static_cast<enum pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    if (ret < 0) {
        fprintf(stderr, "audiocue: stream connect failed: %d\n", ret);
        return false;
    }

    ret = pw_thread_loop_start(loop_);
    if (ret < 0) {
        fprintf(stderr, "audiocue: thread loop start failed: %d\n", ret);
        return false;
    }

    tone_ = new float[CUE_SAMPLES];
    tone_len_ = CUE_SAMPLES;

    fprintf(stderr, "audiocue: initialized\n");
    return true;
}

void AudioCue::shutdown() {
    if (loop_)
        pw_thread_loop_stop(loop_);
    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (loop_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    delete[] tone_;
    tone_ = nullptr;
}

void AudioCue::play(bool start) {
    if (!loop_ || !tone_) return;

    pw_thread_loop_lock(loop_);
    if (start)
        generateTone(tone_, tone_len_, 660.0f, 880.0f);
    else
        generateTone(tone_, tone_len_, 660.0f, 440.0f);
    tone_pos_ = 0;
    playing_ = true;
    pw_thread_loop_unlock(loop_);
}
