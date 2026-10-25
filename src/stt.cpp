// SPDX-License-Identifier: MIT
#include "stt.h"
#include <transcribe.h>
#include <cstdio>

SpeechToText::SpeechToText() = default;

SpeechToText::~SpeechToText() { shutdown(); }

static uint32_t backendMask(const std::string& backend) {
    if (backend == "vulkan") return TRANSCRIBE_BACKEND_MASK_VULKAN | TRANSCRIBE_BACKEND_MASK_CPU;
    if (backend == "cpu")    return TRANSCRIBE_BACKEND_MASK_CPU;
    return TRANSCRIBE_BACKEND_MASK_ALL;
}

bool SpeechToText::init(const std::string& model_path, const std::string& backend) {
    transcribe_backend_init_params bp;
    transcribe_backend_init_params_init(&bp);
    bp.allowed_backends = backendMask(backend);
    transcribe_init_backends_ex(&bp);

    transcribe_model_load_params load_params;
    transcribe_model_load_params_init(&load_params);

    int status = transcribe_model_load_file(model_path.c_str(), &load_params, &model_);
    if (status != 0) {
        fprintf(stderr, "stt: failed to load model: %s (status %d)\n", model_path.c_str(), status);
        return false;
    }

    transcribe_session_params session_params;
    transcribe_session_params_init(&session_params);

    status = transcribe_session_init(model_, &session_params, &session_);
    if (status != 0) {
        fprintf(stderr, "stt: failed to create session (status %d)\n", status);
        transcribe_model_free(model_);
        model_ = nullptr;
        return false;
    }

    fprintf(stderr, "stt: model loaded (%s backend)\n", backend.c_str());
    return true;
}

void SpeechToText::shutdown() {
    if (session_) { transcribe_session_free(session_); session_ = nullptr; }
    if (model_) { transcribe_model_free(model_); model_ = nullptr; }
}

std::string SpeechToText::transcribe(const std::vector<float>& audio) {
    if (!session_ || audio.empty()) return {};

    transcribe_run_params params;
    transcribe_run_params_init(&params);

    int status = transcribe_run(session_, audio.data(), audio.size(), &params);
    if (status != 0) {
        fprintf(stderr, "stt: transcribe_run failed (status %d)\n", status);
        return {};
    }

    const char* text = transcribe_full_text(session_);
    return text ? text : "";
}

bool SpeechToText::streamBegin() {
    if (!session_) return false;

    transcribe_run_params run_params;
    transcribe_run_params_init(&run_params);

    transcribe_stream_params stream_params = {};

    int status = transcribe_stream_begin(session_, &run_params, &stream_params);
    if (status != 0) {
        fprintf(stderr, "stt: stream_begin failed (status %d)\n", status);
        return false;
    }
    return true;
}

bool SpeechToText::streamFeed(const float* data, size_t n_samples) {
    if (!session_ || !data || n_samples == 0) return false;

    transcribe_stream_update update = {};
    int status = transcribe_stream_feed(session_, data, n_samples, &update);
    if (status != 0) {
        fprintf(stderr, "stt: stream_feed failed (status %d)\n", status);
        return false;
    }
    return true;
}

std::string SpeechToText::streamFinalize() {
    if (!session_) return {};

    transcribe_stream_update update = {};
    int status = transcribe_stream_finalize(session_, &update);
    if (status != 0) {
        fprintf(stderr, "stt: stream_finalize failed (status %d)\n", status);
        return {};
    }

    transcribe_stream_text text_out = {};
    transcribe_stream_text_init(&text_out);
    status = transcribe_stream_get_text(session_, &text_out);
    if (status != 0) {
        fprintf(stderr, "stt: stream_get_text failed (status %d)\n", status);
        return {};
    }

    return text_out.full_text ? text_out.full_text : "";
}

void SpeechToText::streamReset() {
    if (session_) transcribe_stream_reset(session_);
}
