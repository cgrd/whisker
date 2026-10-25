// SPDX-License-Identifier: MIT
#include "audio.h"

#include <cstdio>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/debug/types.h>

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr uint32_t CHANNELS = 1;

static void audio_on_process(void* userdata);
static void audio_on_state_changed(void* userdata, enum pw_stream_state old,
                                    enum pw_stream_state state, const char* error);

static const struct pw_stream_events stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = audio_on_state_changed,
    .process = audio_on_process,
};

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    shutdown();
}

bool AudioCapture::init() {
    pw_init(nullptr, nullptr);

    loop_ = pw_thread_loop_new("audio-capture", nullptr);
    if (!loop_) {
        fprintf(stderr, "audio: failed to create thread loop\n");
        return false;
    }

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        nullptr);

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        "whisker-mic",
        props,
        &stream_events,
        this);

    if (!stream_) {
        fprintf(stderr, "audio: failed to create stream\n");
        return false;
    }

    uint8_t param_buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(param_buffer, sizeof(param_buffer));
    struct spa_audio_info_raw raw_info = {};
    raw_info.format = SPA_AUDIO_FORMAT_F32;
    raw_info.rate = SAMPLE_RATE;
    raw_info.channels = CHANNELS;
    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw_info);

    int ret = pw_stream_connect(stream_,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<enum pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    if (ret < 0) {
        fprintf(stderr, "audio: stream connect failed: %d\n", ret);
        return false;
    }

    ret = pw_thread_loop_start(loop_);
    if (ret < 0) {
        fprintf(stderr, "audio: thread loop start failed: %d\n", ret);
        return false;
    }

    return true;
}

void AudioCapture::shutdown() {
    if (loop_) {
        pw_thread_loop_stop(loop_);
    }
    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (loop_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    pw_deinit();
}

void AudioCapture::startCapture() {
    pw_thread_loop_lock(loop_);
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_.clear();
    }
    capturing_.store(true, std::memory_order_release);
    pw_thread_loop_unlock(loop_);
}

std::vector<float> AudioCapture::stopCapture() {
    capturing_.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return std::move(buffer_);
}

bool AudioCapture::isCapturing() const {
    return capturing_.load(std::memory_order_acquire);
}

std::vector<float> AudioCapture::peekRecent(size_t max_samples) const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (buffer_.size() <= max_samples)
        return buffer_;
    return {buffer_.end() - max_samples, buffer_.end()};
}

static void audio_on_process(void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    struct pw_buffer* b = pw_stream_dequeue_buffer(self->stream_);
    if (!b) return;

    struct spa_buffer* buf = b->buffer;
    auto* data = static_cast<float*>(buf->datas[0].data);
    uint32_t n_samples = buf->datas[0].chunk->size / sizeof(float);

    if (self->capturing_.load(std::memory_order_relaxed) && data && n_samples > 0) {
        std::lock_guard<std::mutex> lock(self->buffer_mutex_);
        self->buffer_.insert(self->buffer_.end(), data, data + n_samples);
    }

    pw_stream_queue_buffer(self->stream_, b);
}

static void audio_on_state_changed(void* /*userdata*/, enum pw_stream_state old,
                                    enum pw_stream_state state, const char* error) {
    fprintf(stderr, "audio: stream state %s -> %s",
            pw_stream_state_as_string(old),
            pw_stream_state_as_string(state));
    if (error)
        fprintf(stderr, " (%s)", error);
    fprintf(stderr, "\n");
}
