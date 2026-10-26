// SPDX-License-Identifier: MIT
#include "settings.h"
#include "config.h"
#include "trigger_capture.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <imgui_stdlib.h>

#include "font_embed.h"
#include "icon_embed.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct HistoryEntry {
    std::string dir_name;
    std::string full_path;
    std::string text;
    bool expanded = false;
};

struct AudioSource {
    std::string id;
    std::string name;
};

struct KnownModel {
    const char* name;
    const char* tag;
    int size_mb;
    const char* filename;
    const char* url;
};

static const KnownModel known_models[] = {
    {"Parakeet 0.6B", "Largest", 731,
     "parakeet-unified-en-0.6b-Q8_0.gguf",
     "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q8_0.gguf"},
    {"Parakeet 0.6B", "Large", 602,
     "parakeet-unified-en-0.6b-Q6_K.gguf",
     "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q6_K.gguf"},
    {"Parakeet 0.6B", "Medium", 541,
     "parakeet-unified-en-0.6b-Q5_K_M.gguf",
     "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q5_K_M.gguf"},
    {"Parakeet 0.6B", "Smallest", 477,
     "parakeet-unified-en-0.6b-Q4_K_M.gguf",
     "https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/parakeet-unified-en-0.6b-Q4_K_M.gguf"},
};

static void drawSizeBar(int size_mb, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = 16.0f;
    float frac = static_cast<float>(size_mb) / 731.0f;
    // Green for small/fast, blue for large
    int r = static_cast<int>(0 + (0 - 0) * frac);
    int g = static_cast<int>(210 - (210 - 160) * frac);
    int b = static_cast<int>(120 + (220 - 120) * frac);
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), IM_COL32(255, 255, 255, 15), 4.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + width * frac, pos.y + h), IM_COL32(r, g, b, 200), 4.0f);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d MB", size_mb);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(pos.x + 4, pos.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 220), buf);
    ImGui::Dummy(ImVec2(width, h));
}

static constexpr int NUM_KNOWN_MODELS = sizeof(known_models) / sizeof(known_models[0]);

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

struct ModelState {
    std::string local_path;
    bool downloaded = false;
};

static std::string findModelPath(const char* filename) {
    std::string models = modelsDir();
    std::string p1 = models + "/" + filename;
    if (fileExists(p1)) return p1;

    // Check old HuggingFace cache location
    std::string hf_base = expandHome("~/.cache/huggingface/hub/models--handy-computer--parakeet-unified-en-0.6b-gguf");
    std::string snapshots = hf_base + "/snapshots";
    DIR* d = opendir(snapshots.c_str());
    if (d) {
        while (struct dirent* ent = readdir(d)) {
            if (ent->d_name[0] == '.') continue;
            std::string candidate = snapshots + "/" + ent->d_name + "/" + filename;
            if (fileExists(candidate)) {
                closedir(d);
                return candidate;
            }
        }
        closedir(d);
    }
    return {};
}

struct ScannedModel {
    std::string path;
    std::string display_name;
};

static void scanForGGUF(const std::string& dir, bool recurse, std::vector<ScannedModel>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISREG(st.st_mode)) {
            std::string name(ent->d_name);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".gguf")
                out.push_back({full, name});
        } else if (S_ISDIR(st.st_mode) && recurse) {
            scanForGGUF(full, true, out);
        }
    }
    closedir(d);
}

struct DownloadState {
    std::atomic<int> status{0}; // 0=idle, 1=downloading, 2=done, 3=failed
    int model_idx = -1;
    std::string error;
    std::thread thread;

    ~DownloadState() { if (thread.joinable()) thread.join(); }
};

static DownloadState g_download;

static std::atomic<float> g_mic_level{0.0f};

struct MicMonitor {
    pw_main_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};

    static void on_process(void* userdata) {
        auto* mon = static_cast<MicMonitor*>(userdata);
        pw_buffer* buf = pw_stream_dequeue_buffer(mon->stream);
        if (!buf) return;

        spa_data& d = buf->buffer->datas[0];
        if (d.data && d.chunk && d.chunk->size > 0) {
            auto* samples = static_cast<float*>(d.data);
            int n = d.chunk->size / sizeof(float);
            float sum = 0.0f;
            for (int i = 0; i < n; ++i) sum += samples[i] * samples[i];
            float rms = std::sqrt(sum / n);
            float prev = g_mic_level.load();
            float smoothed = (rms > prev) ? rms : prev * 0.92f + rms * 0.08f;
            g_mic_level.store(smoothed);
        }
        pw_stream_queue_buffer(mon->stream, buf);
    }

    void start(const std::string& device) {
        if (running.load()) return;
        running.store(true);

        thread = std::thread([this, device]() {
            pw_init(nullptr, nullptr);
            loop = pw_main_loop_new(nullptr);

            auto props = pw_properties_new(
                PW_KEY_MEDIA_TYPE, "Audio",
                PW_KEY_MEDIA_CATEGORY, "Capture",
                PW_KEY_MEDIA_ROLE, "Monitor",
                PW_KEY_STREAM_CAPTURE_SINK, "false",
                nullptr);
            if (!device.empty() && device != "default")
                pw_properties_set(props, PW_KEY_TARGET_OBJECT, device.c_str());

            static const pw_stream_events events = {
                .version = PW_VERSION_STREAM_EVENTS,
                .process = on_process,
            };

            stream = pw_stream_new_simple(
                pw_main_loop_get_loop(loop), "whisker-monitor",
                props, &events, this);

            uint8_t param_buf[1024];
            spa_pod_builder b = SPA_POD_BUILDER_INIT(param_buf, sizeof(param_buf));
            spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32,
                                                               .rate = 16000, .channels = 1);
            const spa_pod* params[1];
            params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

            pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                params, 1);

            pw_main_loop_run(loop);

            pw_stream_destroy(stream);
            pw_main_loop_destroy(loop);
            stream = nullptr;
            loop = nullptr;
            pw_deinit();
        });
    }

    void stop() {
        if (!running.load()) return;
        running.store(false);
        if (loop) pw_main_loop_quit(loop);
        if (thread.joinable()) thread.join();
        g_mic_level.store(0.0f);
    }

    ~MicMonitor() { stop(); }
};

static MicMonitor g_mic_monitor;

static void startDownload(int idx) {
    if (g_download.status.load() == 1) return;
    if (g_download.thread.joinable()) g_download.thread.join();

    g_download.model_idx = idx;
    g_download.status.store(1);

    std::string dest = modelsDir() + "/" + known_models[idx].filename;
    std::string url = known_models[idx].url;

    g_download.error.clear();
    g_download.thread = std::thread([dest, url]() {
        std::string tmp = dest + ".part";
        std::string errlog = dest + ".err";
        std::string cmd = "curl -fL -o \"" + tmp + "\" \"" + url + "\" 2>\"" + errlog + "\"";
        int ret = system(cmd.c_str());
        if (ret == 0) {
            rename(tmp.c_str(), dest.c_str());
            remove(errlog.c_str());
            g_download.status.store(2);
        } else {
            remove(tmp.c_str());
            FILE* f = fopen(errlog.c_str(), "r");
            if (f) {
                char buf[256]{};
                fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                g_download.error = buf;
            }
            remove(errlog.c_str());
            fprintf(stderr, "whisker: model download failed (exit %d): %s\n", ret, g_download.error.c_str());
            g_download.status.store(3);
        }
    });
}

static bool combo(const char* label, int* current, const char* const items[], int count);

static void drawModelSection(Config& cfg, std::vector<ModelState>& model_states,
                              std::vector<ScannedModel>& scanned, bool& scanned_done) {
    ImGui::Text("Model");
    ImGui::Spacing();

    if (ImGui::BeginTable("##models", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < NUM_KNOWN_MODELS; ++i) {
            ImGui::TableNextRow();
            ImGui::PushID(i);

            bool is_selected = !cfg.model_path.empty() && cfg.model_path == model_states[i].local_path;
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    ImGui::GetColorU32(ImVec4(0.00f, 0.45f, 0.75f, 0.20f)));
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", known_models[i].name);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", known_models[i].tag);

            ImGui::TableNextColumn();
            drawSizeBar(known_models[i].size_mb, ImGui::GetContentRegionAvail().x);

            ImGui::TableNextColumn();
            if (model_states[i].downloaded) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 0.4f, 1.0f));
                ImGui::Text("Downloaded");
                ImGui::PopStyleColor();
            } else if (g_download.status.load() == 1 && g_download.model_idx == i) {
                const char* spinner[] = {"|", "/", "-", "\\"};
                int frame = static_cast<int>(ImGui::GetTime() * 8.0f) % 4;
                ImGui::Text("Downloading %s", spinner[frame]);
            } else {
                ImGui::TextDisabled("Not downloaded");
            }

            ImGui::TableNextColumn();
            if (model_states[i].downloaded) {
                if (is_selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.65f, 0.95f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.65f, 0.95f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.65f, 0.95f, 0.8f));
                    ImGui::Button("Active", ImVec2(-1, 0));
                    ImGui::PopStyleColor(3);
                } else {
                    if (ImGui::Button("Select", ImVec2(-1, 0)))
                        cfg.model_path = model_states[i].local_path;
                }
            } else if (g_download.status.load() == 1 && g_download.model_idx == i) {
                ImGui::BeginDisabled();
                ImGui::Button("...", ImVec2(-1, 0));
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Download", ImVec2(-1, 0)))
                    startDownload(i);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Check for completed download
    if (g_download.status.load() == 2) {
        int idx = g_download.model_idx;
        if (idx >= 0 && idx < NUM_KNOWN_MODELS) {
            model_states[idx].local_path = modelsDir() + "/" + known_models[idx].filename;
            model_states[idx].downloaded = true;
            cfg.model_path = model_states[idx].local_path;
        }
        g_download.status.store(0);
    } else if (g_download.status.load() == 3) {
        if (!g_download.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("Download failed: %s", g_download.error.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Button("Dismiss"))
            g_download.status.store(0);
    }

    ImGui::Spacing();

    if (ImGui::Button("Scan for GGUF models")) {
        scanned.clear();
        scanForGGUF(modelsDir(), false, scanned);
        scanForGGUF(expandHome("~/.cache/huggingface"), true, scanned);

        std::string home = expandHome("~");
        DIR* d = opendir(home.c_str());
        if (d) {
            while (struct dirent* ent = readdir(d)) {
                if (ent->d_name[0] == '.') continue;
                std::string full = home + "/" + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    std::string n(ent->d_name);
                    if (n.size() > 5 && n.substr(n.size() - 5) == ".gguf")
                        scanned.push_back({full, n});
                }
            }
            closedir(d);
        }

        // Deduplicate against known models
        for (auto it = scanned.begin(); it != scanned.end();) {
            bool is_known = false;
            for (int i = 0; i < NUM_KNOWN_MODELS; ++i) {
                if (model_states[i].downloaded && it->path == model_states[i].local_path) {
                    is_known = true;
                    break;
                }
            }
            if (is_known)
                it = scanned.erase(it);
            else
                ++it;
        }
        scanned_done = true;
    }

    if (scanned_done && !scanned.empty()) {
        ImGui::Spacing();
        ImGui::Text("Found models:");
        for (int i = 0; i < static_cast<int>(scanned.size()); ++i) {
            ImGui::PushID(1000 + i);
            bool is_selected = cfg.model_path == scanned[i].path;
            if (is_selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 0.4f, 1.0f));
            ImGui::BulletText("%s", scanned[i].path.c_str());
            if (is_selected) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (is_selected) {
                ImGui::TextDisabled("(active)");
            } else {
                if (ImGui::SmallButton("Select"))
                    cfg.model_path = scanned[i].path;
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Custom model path")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##custom_model_path", &cfg.model_path);
    }

    ImGui::Spacing();

    const char* backends[] = {"vulkan", "cpu"};
    int backend_idx = cfg.backend == "cpu" ? 1 : 0;
    if (combo("Backend", &backend_idx, backends, 2))
        cfg.backend = backends[backend_idx];
}

static std::vector<AudioSource> enumerateAudioSources() {
    std::vector<AudioSource> sources;
    FILE* fp = popen("pactl list sources 2>/dev/null", "r");
    if (!fp) return sources;

    char line[1024];
    std::string current_name;
    while (fgets(line, sizeof(line), fp)) {
        std::string l(line);
        auto trim = [](std::string& s) {
            auto start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) { s.clear(); return; }
            auto end = s.find_last_not_of(" \t\r\n");
            s = s.substr(start, end - start + 1);
        };

        if (l.find("Name:") != std::string::npos) {
            current_name = l.substr(l.find("Name:") + 5);
            trim(current_name);
        } else if (l.find("Description:") != std::string::npos && !current_name.empty()) {
            std::string desc = l.substr(l.find("Description:") + 12);
            trim(desc);
            if (current_name.find("monitor") == std::string::npos)
                sources.push_back({current_name, desc});
            current_name.clear();
        }
    }
    pclose(fp);
    return sources;
}

static std::vector<HistoryEntry> loadHistoryEntries() {
    std::vector<HistoryEntry> entries;
    std::string dir = historyDir();

    DIR* d = opendir(dir.c_str());
    if (!d) return entries;

    while (struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        HistoryEntry e;
        e.dir_name = ent->d_name;
        e.full_path = full;

        std::ifstream txt(full + "/text.txt");
        if (txt.is_open()) {
            std::string content((std::istreambuf_iterator<char>(txt)),
                                std::istreambuf_iterator<char>());
            e.text = content;
        }
        entries.push_back(std::move(e));
    }
    closedir(d);

    std::sort(entries.begin(), entries.end(),
              [](const HistoryEntry& a, const HistoryEntry& b) {
                  return a.dir_name > b.dir_name;
              });
    return entries;
}

static void deleteHistoryEntry(const std::string& path) {
    unlink((path + "/audio.opus").c_str());
    unlink((path + "/text.txt").c_str());
    rmdir(path.c_str());
}

static void playAudio(const std::string& path) {
    std::string cmd = "pw-play \"" + path + "/audio.opus\" &";
    system(cmd.c_str());
}

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowPadding = ImVec2(16, 16);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.078f, 0.141f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.090f, 0.090f, 0.160f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.090f, 0.160f, 0.95f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.35f, 0.5f);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.32f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.11f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.14f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.00f, 0.50f, 0.80f, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.60f, 0.90f, 0.40f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.70f, 1.00f, 0.50f);
    c[ImGuiCol_Button] = ImVec4(0.00f, 0.45f, 0.75f, 0.60f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.55f, 0.85f, 0.70f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.65f, 0.95f, 0.80f);
    c[ImGuiCol_Tab] = ImVec4(0.10f, 0.10f, 0.20f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.55f, 0.85f, 0.60f);
    c[ImGuiCol_TabActive] = ImVec4(0.00f, 0.50f, 0.80f, 0.70f);
    c[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.82f, 1.00f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.60f, 0.90f, 0.70f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.70f, 1.00f, 0.90f);
    c[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.92f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.47f, 0.52f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.35f, 0.5f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.11f, 0.5f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.35f, 0.8f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.25f, 0.40f, 0.8f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.30f, 0.45f, 0.8f);
}

static bool combo(const char* label, int* current, const char* const items[], int count) {
    bool changed = false;
    if (ImGui::BeginCombo(label, items[*current])) {
        for (int i = 0; i < count; ++i) {
            bool selected = (i == *current);
            if (ImGui::Selectable(items[i], selected)) {
                *current = i;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

static void submitKeyCombo(const char* label, std::string& value) {
    const char* keys[] = {"none", "enter", "tab"};
    int idx = 0;
    if (value == "enter") idx = 1;
    else if (value == "tab") idx = 2;
    ImGui::SetNextItemWidth(-1);
    if (combo(label, &idx, keys, 3))
        value = keys[idx];
}

static std::string autostartPath() {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.config/autostart/whisker.desktop";
}

static bool isAutostartEnabled() {
    struct stat st;
    return stat(autostartPath().c_str(), &st) == 0;
}

static void setAutostart(bool enable) {
    std::string path = autostartPath();
    if (enable) {
        std::string dir = path.substr(0, path.rfind('/'));
        mkdir(dir.c_str(), 0755);
        std::ofstream f(path);
        f << "[Desktop Entry]\n"
          << "Type=Application\n"
          << "Name=Whisker\n"
          << "Exec=whisker\n"
          << "Icon=whisker\n"
          << "Terminal=false\n"
          << "X-GNOME-Autostart-enabled=true\n";
    } else {
        unlink(path.c_str());
    }
}

static SettingsRect itemRect() {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    return {min.x, min.y, max.x, max.y};
}

static void drawAudioTab(Config& cfg, std::vector<AudioSource>& sources, int& selected_source,
                         SettingsLayout& layout) {
    ImGui::Text("Audio Input");
    ImGui::Spacing();

    if (ImGui::Button("Refresh Devices"))
        sources = enumerateAudioSources();

    ImGui::SameLine();
    if (sources.empty()) {
        ImGui::TextDisabled("No sources found");
    } else {
        if (ImGui::BeginCombo("##audio_device",
                              selected_source >= 0 ? sources[selected_source].name.c_str() : "Default")) {
            for (int i = 0; i < static_cast<int>(sources.size()); ++i) {
                bool selected = (i == selected_source);
                if (ImGui::Selectable(sources[i].name.c_str(), selected)) {
                    selected_source = i;
                    cfg.audio_device = sources[i].id;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("General");
    ImGui::Spacing();

    ImGui::Checkbox("Show overlay during recording", &cfg.overlay);
    layout.overlay_checkbox = itemRect();
    ImGui::Checkbox("Show tray icon", &cfg.tray);
    ImGui::TextDisabled("Send after dictation");
    submitKeyCombo("##submit_key", cfg.submit_key);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    {
        ImGui::Text("Noise Gate");
        float bar_w = ImGui::GetContentRegionAvail().x;
        float bar_h = 28.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 bar_max(pos.x + bar_w, pos.y + bar_h);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(pos, bar_max, IM_COL32(30, 30, 50, 255), 6.0f);

        float level = g_mic_level.load();
        float level_frac = std::min(1.0f, level / 0.1f);
        if (level_frac > 0.001f) {
            float lx = bar_w * level_frac;
            ImU32 green = (level > cfg.noise_threshold)
                ? IM_COL32(80, 220, 120, 200)
                : IM_COL32(80, 220, 120, 100);
            dl->AddRectFilled(pos, ImVec2(pos.x + lx, pos.y + bar_h), green, 6.0f);
        }

        float thresh_frac = std::min(1.0f, cfg.noise_threshold / 0.1f);
        float tx = pos.x + bar_w * thresh_frac;
        dl->AddLine(ImVec2(tx, pos.y), ImVec2(tx, pos.y + bar_h),
                    IM_COL32(255, 100, 80, 240), 2.5f);
        dl->AddTriangleFilled(
            ImVec2(tx - 5, pos.y), ImVec2(tx + 5, pos.y), ImVec2(tx, pos.y + 6),
            IM_COL32(255, 100, 80, 240));

        ImGui::InvisibleButton("##noise_gate", ImVec2(bar_w, bar_h));
        if (ImGui::IsItemActive()) {
            float mx = ImGui::GetIO().MousePos.x - pos.x;
            cfg.noise_threshold = std::max(0.0f, std::min(0.1f, (mx / bar_w) * 0.1f));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    ImGui::InputInt("History entries to keep", &cfg.history_count);
    if (cfg.history_count < 0) cfg.history_count = 0;

    static bool autostart_cached = isAutostartEnabled();
    if (ImGui::Checkbox("Start on login", &autostart_cached))
        setAutostart(autostart_cached);
}

static void drawPedalConfig(const char* name, PedalConfig& pedal) {
    ImGui::PushID(name);

    ImGui::Text("%s Pedal", name);
    ImGui::Spacing();

    const char* actions[] = {"none", "key", "transcribe", "exec"};
    int action_idx = static_cast<int>(pedal.action);
    ImGui::TextDisabled("Action");
    ImGui::SetNextItemWidth(-1);
    if (combo("##action", &action_idx, actions, 4))
        pedal.action = static_cast<PedalAction>(action_idx);

    if (pedal.action == PedalAction::Key) {
        ImGui::TextDisabled("Key combo");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##key", &pedal.key);

        const char* modes[] = {"tap", "hold"};
        int mode_idx = pedal.mode == PedalMode::Tap ? 0 : 1;
        ImGui::TextDisabled("Mode");
        ImGui::SetNextItemWidth(-1);
        if (combo("##mode", &mode_idx, modes, 2))
            pedal.mode = mode_idx == 0 ? PedalMode::Tap : PedalMode::Hold;
    }

    if (pedal.action == PedalAction::Transcribe) {
        const char* outputs[] = {"type", "clipboard", "both"};
        int out_idx = static_cast<int>(pedal.output);
        ImGui::TextDisabled("Output");
        ImGui::SetNextItemWidth(-1);
        if (combo("##output", &out_idx, outputs, 3))
            pedal.output = static_cast<OutputMode>(out_idx);

        ImGui::TextDisabled("Suffix key");
        submitKeyCombo("##suffix", pedal.suffix);
    }

    if (pedal.action == PedalAction::Exec) {
        ImGui::TextDisabled("On press");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##exec_press", &pedal.exec_press);
        ImGui::TextDisabled("On release");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##exec_release", &pedal.exec_release);
    }

    ImGui::PopID();
}

static void trackItem(SettingsLayout& layout, const char* name) {
    layout.items[name] = itemRect();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        layout.hovered = name;
}

static void drawPedals(Config& cfg, SettingsLayout& layout) {
    ImGui::Text("Pedals");
    ImGui::Spacing();

    float avail = ImGui::GetContentRegionAvail().x;
    float col_w = (avail - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;

    ImGui::BeginChild("left_pedal", ImVec2(col_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    drawPedalConfig("Left", cfg.pedals[0]);
    ImGui::EndChild();
    trackItem(layout, "left_pedal");

    ImGui::SameLine();

    ImGui::BeginChild("center_pedal", ImVec2(col_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    drawPedalConfig("Center", cfg.pedals[1]);
    ImGui::EndChild();
    trackItem(layout, "center_pedal");

    ImGui::SameLine();

    ImGui::BeginChild("right_pedal", ImVec2(col_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    drawPedalConfig("Right", cfg.pedals[2]);
    ImGui::EndChild();
    trackItem(layout, "right_pedal");
}

static std::string commandSocketPath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return std::string(runtime ? runtime : "/tmp") + "/whisker.sock";
}

// The daemon waits up to 10 s for a button, so give it a little longer before giving up.
static constexpr int kCaptureReplyTimeoutS = 12;

// Asks the running daemon to capture a controller button over its command socket, on a thread
// so the window keeps drawing while the daemon waits for a press.
struct ButtonCapture {
    std::atomic<bool> waiting{false};
    std::atomic<bool> done{false};
    CaptureReply reply;
    std::thread thread;
    std::mutex mutex;
    int fd = -1;
    bool cancelled = false;

    void start() {
        if (waiting.load()) return;
        join();
        cancelled = false;
        done.store(false);
        waiting.store(true);
        thread = std::thread([this]() {
            reply = request();
            waiting.store(false);
            done.store(true);
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            cancelled = true;
            if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
        }
        join();
    }

    void join() {
        if (thread.joinable()) thread.join();
    }

    ~ButtonCapture() { stop(); }

private:
    CaptureReply request() {
        int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s < 0) return {{}, {}, std::string("socket: ") + strerror(errno)};
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::string path = commandSocketPath();
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(s);
            return {{}, {}, "Whisker isn't running"};
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancelled) {
                ::close(s);
                return {};
            }
            fd = s;
        }
        timeval timeout = {kCaptureReplyTimeoutS, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::string line;
        bool complete = false;
        if (::send(s, "capture\n", 8, MSG_NOSIGNAL) == 8) {
            char c;
            while (line.size() < 256 && ::read(s, &c, 1) == 1) {
                if (c == '\n') {
                    complete = true;
                    break;
                }
                line += c;
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            fd = -1;
        }
        ::close(s);
        if (!complete) return {{}, {}, "No reply from Whisker"};
        return parseCaptureReply(line);
    }
};

struct TriggerState {
    bool capturing_key = false;
    std::string key_message;
    ButtonCapture button;
    std::string button_message;
};

// Draws a value in an input-like frame; empty values show `empty_text` dimmed.
static void valueBox(const std::string& value, const char* empty_text, float width, bool active) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFrameHeight();
    ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg);
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), bg, style.FrameRounding);
    if (active)
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + h), ImGui::GetColorU32(ImGuiCol_CheckMark),
                    style.FrameRounding, 0, 1.5f);
    bool empty = value.empty();
    ImU32 color = ImGui::GetColorU32(empty ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    dl->AddText(ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y), color,
                empty ? empty_text : value.c_str());
    ImGui::Dummy(ImVec2(width, h));
}

static void statusLine(const std::string& message) {
    if (message.empty()) return;
    ImGui::TextDisabled("%s", message.c_str());
}

static void drawKeyboardTrigger(Config& cfg, TriggerState& state, SettingsLayout& layout) {
    ImGui::Text("Keyboard shortcut");
    ImGui::TextDisabled("Hold this combo to record, in any app");
    float box_w = 260.0f;
    if (state.capturing_key) {
        std::string held = modifierPrefix(static_cast<Uint16>(SDL_GetModState())) + "...";
        valueBox(held, "", box_w, true);
    } else {
        valueBox(cfg.trigger_key, "Disabled", box_w, false);
    }
    trackItem(layout, "shortcut_value");
    ImGui::SameLine();
    if (state.capturing_key) {
        if (ImGui::Button("Cancel##shortcut")) {
            state.capturing_key = false;
            state.key_message.clear();
        }
    } else if (ImGui::Button("Record shortcut")) {
        state.capturing_key = true;
        state.key_message = "Press a key combo, Esc cancels";
    }
    trackItem(layout, "record_shortcut");
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.trigger_key.empty() || state.capturing_key);
    if (ImGui::Button("Clear##shortcut")) {
        cfg.trigger_key.clear();
        state.key_message = "Keyboard shortcut disabled";
    }
    ImGui::EndDisabled();
    trackItem(layout, "clear_shortcut");
    statusLine(state.key_message);
}

static void drawControllerTrigger(Config& cfg, TriggerState& state, SettingsLayout& layout) {
    if (state.button.done.exchange(false)) {
        const CaptureReply& r = state.button.reply;
        if (!r.button.empty()) {
            cfg.trigger_button = r.button;
            state.button_message = "Using " + r.button + (r.device.empty() ? "" : " on " + r.device);
        } else {
            state.button_message = r.error;
        }
    }

    bool waiting = state.button.waiting.load();
    ImGui::Text("Controller button");
    ImGui::TextDisabled("Hold this button or chord on any controller to record");
    valueBox(cfg.trigger_button, "Disabled", 260.0f, waiting);
    trackItem(layout, "button_value");
    ImGui::SameLine();
    ImGui::BeginDisabled(waiting);
    if (ImGui::Button("Press the button you want")) {
        state.button.start();
        state.button_message.clear();
    }
    trackItem(layout, "capture_button");
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg.trigger_button.empty());
    if (ImGui::Button("Clear##button")) {
        cfg.trigger_button.clear();
        state.button_message = "Controller button disabled";
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    trackItem(layout, "clear_button");
    if (waiting) {
        const char* spinner[] = {"|", "/", "-", "\\"};
        int frame = static_cast<int>(ImGui::GetTime() * 8.0f) % 4;
        ImGui::TextDisabled("Press and release a button or chord on any controller... %s", spinner[frame]);
    } else {
        statusLine(state.button_message);
    }
}

static void drawTriggersTab(Config& cfg, TriggerState& state, SettingsLayout& layout) {
    drawKeyboardTrigger(cfg, state, layout);

    ImGui::Spacing();
    ImGui::Spacing();

    drawControllerTrigger(cfg, state, layout);

    ImGui::Spacing();
    ImGui::Spacing();

    const char* modes[] = {"hold", "toggle"};
    int mode_idx = cfg.trigger_mode == "toggle" ? 1 : 0;
    ImGui::Text("Mode");
    ImGui::TextDisabled("Hold: release to stop. Toggle: press again to stop.");
    ImGui::SetNextItemWidth(260.0f);
    if (combo("##trigger_mode", &mode_idx, modes, 2))
        cfg.trigger_mode = modes[mode_idx];
    trackItem(layout, "trigger_mode");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawPedals(cfg, layout);
}

// Consumes key presses while the Triggers page records a shortcut, so ImGui never sees them.
static bool handleKeyCapture(TriggerState& state, Config& cfg, const SDL_Event& event) {
    if (!state.capturing_key) return false;
    if (event.type == SDL_KEYUP) return true;
    if (event.type != SDL_KEYDOWN) return false;
    SDL_Scancode key = event.key.keysym.scancode;
    Uint16 mod = event.key.keysym.mod;
    if (key == SDL_SCANCODE_ESCAPE && modifierPrefix(mod).empty()) {
        state.capturing_key = false;
        state.key_message.clear();
        return true;
    }
    if (key >= SDL_SCANCODE_LCTRL && key <= SDL_SCANCODE_RGUI) return true;
    std::string combo = keyComboFromScancode(key, mod);
    if (combo.empty()) {
        state.key_message = std::string("Whisker can't use ") + SDL_GetScancodeName(key) +
                            " as a trigger, try another key. Esc cancels";
        return true;
    }
    cfg.trigger_key = combo;
    state.capturing_key = false;
    state.key_message.clear();
    return true;
}

static void drawHistoryTab(std::vector<HistoryEntry>& entries, bool& needs_reload) {
    if (ImGui::Button("Refresh")) needs_reload = true;

    if (!entries.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
        static bool confirm_clear = false;
        if (!confirm_clear) {
            if (ImGui::Button("Clear All")) confirm_clear = true;
        } else {
            if (ImGui::Button("Confirm?")) {
                for (auto& e : entries) deleteHistoryEntry(e.full_path);
                entries.clear();
                confirm_clear = false;
                needs_reload = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) confirm_clear = false;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();

    if (entries.empty()) {
        ImGui::TextDisabled("No recordings yet");
        return;
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        auto& e = entries[i];
        ImGui::PushID(i);

        std::string timestamp = e.dir_name;
        std::replace(timestamp.begin(), timestamp.end(), 'T', ' ');
        std::replace(timestamp.begin(), timestamp.end(), '-', ':');
        if (timestamp.size() >= 10) {
            timestamp[4] = '-';
            timestamp[7] = '-';
        }

        std::string preview = e.text.substr(0, 80);
        if (e.text.size() > 80) preview += "...";

        bool open = ImGui::TreeNode("##entry", "%s", timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", preview.c_str());

        if (open) {
            ImGui::TextWrapped("%s", e.text.c_str());
            ImGui::Spacing();

            if (ImGui::Button("Play")) playAudio(e.full_path);
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                deleteHistoryEntry(e.full_path);
                entries.erase(entries.begin() + i);
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

struct SettingsUi {
    std::string config_path;
    Config cfg;
    std::vector<AudioSource> sources;
    int selected_source = -1;
    std::vector<ModelState> model_states;
    std::vector<ScannedModel> scanned_models;
    bool scanned_done = false;
    std::vector<HistoryEntry> history_entries;
    bool history_needs_reload = false;
    TriggerState triggers;
    SettingsTab active_tab = SettingsTab::Audio;
    bool running = true;
    bool saved = false;
    SettingsLayout layout;
};

static void setupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(AtkinsonHyperlegible_Regular_ttf),
        AtkinsonHyperlegible_Regular_ttf_len, 18.0f, &font_cfg);

    applyStyle();
}

static void loadUi(SettingsUi& ui, const std::string& config_path, std::vector<AudioSource> sources) {
    ui.config_path = config_path;
    ui.cfg = loadConfig(config_path);
    ui.sources = std::move(sources);
    for (int i = 0; i < static_cast<int>(ui.sources.size()); ++i) {
        if (ui.sources[i].id == ui.cfg.audio_device) {
            ui.selected_source = i;
            break;
        }
    }

    ui.model_states.resize(NUM_KNOWN_MODELS);
    for (int i = 0; i < NUM_KNOWN_MODELS; ++i) {
        ui.model_states[i].local_path = findModelPath(known_models[i].filename);
        ui.model_states[i].downloaded = !ui.model_states[i].local_path.empty();
    }
    if (ui.cfg.model_path.empty() || !fileExists(ui.cfg.model_path)) {
        for (int i = 0; i < NUM_KNOWN_MODELS; ++i) {
            if (ui.model_states[i].downloaded) {
                ui.cfg.model_path = ui.model_states[i].local_path;
                break;
            }
        }
    }
    ui.history_entries = loadHistoryEntries();
}

static void drawFrame(SettingsUi& ui) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::NewFrame();
    ui.layout.items.clear();
    ui.layout.hovered.clear();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float sidebar_w = 140.0f;
    float bottom_h = 64.0f;

    ImGui::BeginChild("sidebar", ImVec2(sidebar_w, -bottom_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::Spacing();

    const char* tabs[] = {"Audio", "Model", "Triggers", "History"};
    for (int i = 0; i < 4; ++i) {
        bool selected = (ui.active_tab == static_cast<SettingsTab>(i));
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.50f, 0.80f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.55f, 0.85f, 0.70f));
        }
        if (ImGui::Button(tabs[i], ImVec2(sidebar_w - 2 * ImGui::GetStyle().WindowPadding.x, 36)))
            ui.active_tab = static_cast<SettingsTab>(i);
        if (selected)
            ImGui::PopStyleColor(2);
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("content", ImVec2(0, -bottom_h), ImGuiChildFlags_Borders);
    ui.layout.content = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                         ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                         ImGui::GetWindowPos().y + ImGui::GetWindowSize().y};

    switch (ui.active_tab) {
    case SettingsTab::Audio: drawAudioTab(ui.cfg, ui.sources, ui.selected_source, ui.layout); break;
    case SettingsTab::Model: drawModelSection(ui.cfg, ui.model_states, ui.scanned_models, ui.scanned_done); break;
    case SettingsTab::Triggers: drawTriggersTab(ui.cfg, ui.triggers, ui.layout); break;
    case SettingsTab::History:
        if (ui.history_needs_reload) {
            ui.history_entries = loadHistoryEntries();
            ui.history_needs_reload = false;
        }
        drawHistoryTab(ui.history_entries, ui.history_needs_reload);
        break;
    }

    ImGui::EndChild();

    ImGui::SetCursorPosY(io.DisplaySize.y - bottom_h);
    ImGui::Separator();
    ImGui::Spacing();

    float btn_w = 100.0f;
    ImGui::SetCursorPosX(io.DisplaySize.x - 2 * btn_w - 30);
    if (ImGui::Button("Cancel", ImVec2(btn_w, 32)))
        ui.running = false;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.55f, 0.85f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.65f, 0.95f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.75f, 1.00f, 1.0f));
    if (ImGui::Button("Save", ImVec2(btn_w, 32))) {
        saveConfig(ui.cfg, ui.config_path);
        ui.saved = true;
        ui.running = false;
    }
    ui.layout.save_button = itemRect();
    ui.layout.save_hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(3);

    ImGui::End();
    ImGui::Render();
}

static void presentFrame(SDL_Renderer* renderer) {
    // The SDL_Renderer backend scales clip rects by the framebuffer scale but leaves vertices in
    // window coordinates. Without a matching render scale, HiDPI and fractionally scaled Wayland
    // outputs draw the UI shrunk into the top-left corner, clip it wrongly and offset the mouse.
    ImGuiIO& io = ImGui::GetIO();
    SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 20, 20, 36, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}

SettingsLayout settingsRenderOffscreen(SDL_Renderer* renderer, const std::string& config_path,
                                       float width, float height, float framebuffer_scale,
                                       float mouse_x, float mouse_y, int frames, SettingsTab tab) {
    setupImGui();
    ImGui_ImplSDLRenderer2_Init(renderer);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(framebuffer_scale, framebuffer_scale);

    SettingsUi ui;
    loadUi(ui, config_path, {});
    ui.active_tab = tab;
    for (int i = 0; i < frames; ++i) {
        io.AddMousePosEvent(mouse_x, mouse_y);
        ImGui_ImplSDLRenderer2_NewFrame();
        drawFrame(ui);
        presentFrame(renderer);
    }
    ImTextureData* atlas = io.Fonts->TexData;
    ui.layout.font_texture_ready = atlas && atlas->Status == ImTextureStatus_OK &&
                                   atlas->TexID != ImTextureID_Invalid;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    return ui.layout;
}

int settingsRun(const std::string& config_path) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Whisker Settings",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1200, 850, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
        const_cast<unsigned char*>(whisker_icon_256_rgba),
        256, 256, 32, 256 * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    setupImGui();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    SettingsUi ui;
    loadUi(ui, config_path, enumerateAudioSources());
    g_mic_monitor.start(ui.cfg.audio_device);

    while (ui.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!handleKeyCapture(ui.triggers, ui.cfg, event))
                ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) ui.running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE) ui.running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        drawFrame(ui);
        presentFrame(renderer);
        SDL_RenderPresent(renderer);
    }

    g_mic_monitor.stop();
    ui.triggers.button.stop();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return ui.saved ? 0 : 1;
}
