// SPDX-License-Identifier: MIT
#include "tray.h"
#include "icon.h"
#include <systemd/sd-bus.h>
#include <cairo/cairo.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include <arpa/inet.h>

struct TrayState {
    sd_bus* bus = nullptr;
    sd_bus_slot* sni_slot = nullptr;
    sd_bus_slot* menu_slot = nullptr;
    sd_bus_slot* name_slot = nullptr;
    std::atomic<bool> quit{false};
    bool recording = false;
    std::string bus_name;

    std::vector<uint32_t> idle_pixels;
    std::vector<uint32_t> recording_pixels;
    int icon_size = 64;
};

static TrayState g_tray;

static void renderIcon(std::vector<uint32_t>& out, int size, float r, float g_col, float b) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surf);

    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, r, g_col, b);
    drawWhiskerIcon(cr, size / 2.0, size / 2.0, size * 0.9);

    cairo_surface_flush(surf);
    unsigned char* data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    out.resize(size * size);
    for (int y = 0; y < size; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < size; ++x) {
            uint32_t px = row[x];
            uint8_t a = (px >> 24) & 0xff;
            uint8_t cr_r = (px >> 16) & 0xff;
            uint8_t cr_g = (px >> 8) & 0xff;
            uint8_t cr_b = px & 0xff;
            out[y * size + x] = htonl((a << 24) | (cr_r << 16) | (cr_g << 8) | cr_b);
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

static std::atomic<pid_t> g_settings_pid{0};

static void openSettings() {
    pid_t existing = g_settings_pid.load();
    if (existing > 0 && kill(existing, 0) == 0)
        return;

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/proc/self/exe", "whisker", "--settings", nullptr);
        _exit(127);
    }
    if (pid > 0)
        g_settings_pid.store(pid);
}

static const std::vector<uint32_t>& currentPixels() {
    return g_tray.recording ? g_tray.recording_pixels : g_tray.idle_pixels;
}

static int prop_category(sd_bus*, const char*, const char*, const char*,
                         sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "ApplicationStatus");
}

static int prop_id(sd_bus*, const char*, const char*, const char*,
                   sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "whisker");
}

static int prop_title(sd_bus*, const char*, const char*, const char*,
                      sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "Whisker");
}

static int prop_status(sd_bus*, const char*, const char*, const char*,
                       sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "Active");
}

static int prop_icon_name(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "whisker");
}

static int prop_icon_pixmap(sd_bus*, const char*, const char*, const char*,
                            sd_bus_message* reply, void*, sd_bus_error*) {
    auto& px = currentPixels();
    int sz = g_tray.icon_size;

    sd_bus_message_open_container(reply, 'a', "(iiay)");
    sd_bus_message_open_container(reply, 'r', "iiay");
    sd_bus_message_append(reply, "ii", sz, sz);
    sd_bus_message_append_array(reply, 'y', px.data(), px.size() * sizeof(uint32_t));
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    return 0;
}

static int prop_overlay_icon_name(sd_bus*, const char*, const char*, const char*,
                                  sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "");
}

static int prop_overlay_icon_pixmap(sd_bus*, const char*, const char*, const char*,
                                    sd_bus_message* reply, void*, sd_bus_error*) {
    sd_bus_message_open_container(reply, 'a', "(iiay)");
    sd_bus_message_close_container(reply);
    return 0;
}

static int prop_attention_icon_name(sd_bus*, const char*, const char*, const char*,
                                    sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "");
}

static int prop_attention_icon_pixmap(sd_bus*, const char*, const char*, const char*,
                                      sd_bus_message* reply, void*, sd_bus_error*) {
    sd_bus_message_open_container(reply, 'a', "(iiay)");
    sd_bus_message_close_container(reply);
    return 0;
}

static int prop_attention_movie_name(sd_bus*, const char*, const char*, const char*,
                                     sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "");
}

static int prop_tooltip(sd_bus*, const char*, const char*, const char*,
                        sd_bus_message* reply, void*, sd_bus_error*) {
    sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
    sd_bus_message_append(reply, "s", "");
    sd_bus_message_open_container(reply, 'a', "(iiay)");
    sd_bus_message_close_container(reply);
    sd_bus_message_append(reply, "ss", "Whisker", "Speech-to-text daemon");
    sd_bus_message_close_container(reply);
    return 0;
}

static int prop_item_is_menu(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "b", 0);
}

static int prop_menu(sd_bus*, const char*, const char*, const char*,
                     sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", "/MenuBar");
}

static int prop_icon_theme_path(sd_bus*, const char*, const char*, const char*,
                                sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "");
}

static int prop_window_id(sd_bus*, const char*, const char*, const char*,
                          sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "i", 0);
}

static int sni_activate(sd_bus_message* msg, void*, sd_bus_error*) {
    openSettings();
    return sd_bus_reply_method_return(msg, "");
}

static int sni_secondary_activate(sd_bus_message* msg, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(msg, "");
}

static int sni_context_menu(sd_bus_message* msg, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(msg, "");
}

static int sni_scroll(sd_bus_message* msg, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(msg, "");
}

static int sni_provide_xdg_activation_token(sd_bus_message* msg, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(msg, "");
}

static uint32_t g_menu_revision = 1;

static int menu_get_layout(sd_bus_message* msg, void*, sd_bus_error*) {
    int32_t parent_id;
    int32_t depth;
    sd_bus_message_read(msg, "ii", &parent_id, &depth);
    // skip property names array
    sd_bus_message_skip(msg, "as");

    sd_bus_message* reply = nullptr;
    sd_bus_message_new_method_return(msg, &reply);

    sd_bus_message_append(reply, "u", g_menu_revision);

    // root item (id=0)
    sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    sd_bus_message_append(reply, "i", 0);
    sd_bus_message_open_container(reply, 'a', "{sv}");
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "children-display");
    sd_bus_message_open_container(reply, 'v', "s");
    sd_bus_message_append(reply, "s", "submenu");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    sd_bus_message_open_container(reply, 'a', "v");

    // Settings item (id=1)
    sd_bus_message_open_container(reply, 'v', "(ia{sv}av)");
    sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    sd_bus_message_append(reply, "i", 1);
    sd_bus_message_open_container(reply, 'a', "{sv}");
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "label");
    sd_bus_message_open_container(reply, 'v', "s");
    sd_bus_message_append(reply, "s", "Settings");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_open_container(reply, 'a', "v");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    // Quit item (id=2)
    sd_bus_message_open_container(reply, 'v', "(ia{sv}av)");
    sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    sd_bus_message_append(reply, "i", 2);
    sd_bus_message_open_container(reply, 'a', "{sv}");
    sd_bus_message_open_container(reply, 'e', "sv");
    sd_bus_message_append(reply, "s", "label");
    sd_bus_message_open_container(reply, 'v', "s");
    sd_bus_message_append(reply, "s", "Quit");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_open_container(reply, 'a', "v");
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    sd_bus_message_close_container(reply); // close children array
    sd_bus_message_close_container(reply); // close root struct

    sd_bus_send(sd_bus_message_get_bus(msg), reply, nullptr);
    sd_bus_message_unref(reply);
    return 0;
}

static int menu_get_group_properties(sd_bus_message* msg, void*, sd_bus_error*) {
    sd_bus_message* reply = nullptr;
    sd_bus_message_new_method_return(msg, &reply);
    sd_bus_message_open_container(reply, 'a', "(ia{sv})");
    sd_bus_message_close_container(reply);
    sd_bus_send(sd_bus_message_get_bus(msg), reply, nullptr);
    sd_bus_message_unref(reply);
    return 0;
}

static int menu_event(sd_bus_message* msg, void*, sd_bus_error*) {
    int32_t id;
    const char* event_type;
    sd_bus_message_read(msg, "i", &id);
    sd_bus_message_read(msg, "s", &event_type);

    if (strcmp(event_type, "clicked") == 0) {
        if (id == 1) openSettings();
        if (id == 2) g_tray.quit.store(true);
    }

    return sd_bus_reply_method_return(msg, "");
}

static int menu_event_group(sd_bus_message* msg, void*, sd_bus_error*) {
    sd_bus_message* reply = nullptr;
    sd_bus_message_new_method_return(msg, &reply);
    sd_bus_message_open_container(reply, 'a', "i");
    sd_bus_message_close_container(reply);
    sd_bus_send(sd_bus_message_get_bus(msg), reply, nullptr);
    sd_bus_message_unref(reply);
    return 0;
}

static int menu_about_to_show(sd_bus_message* msg, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(msg, "b", 0);
}

static int menu_about_to_show_group(sd_bus_message* msg, void*, sd_bus_error*) {
    sd_bus_message* reply = nullptr;
    sd_bus_message_new_method_return(msg, &reply);
    sd_bus_message_open_container(reply, 'a', "i");
    sd_bus_message_close_container(reply);
    sd_bus_message_open_container(reply, 'a', "i");
    sd_bus_message_close_container(reply);
    sd_bus_send(sd_bus_message_get_bus(msg), reply, nullptr);
    sd_bus_message_unref(reply);
    return 0;
}

static int menu_prop_version(sd_bus*, const char*, const char*, const char*,
                             sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "u", 3);
}

static int menu_prop_text_direction(sd_bus*, const char*, const char*, const char*,
                                    sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "ltr");
}

static int menu_prop_status(sd_bus*, const char*, const char*, const char*,
                            sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "normal");
}

static int menu_prop_icon_theme_path(sd_bus*, const char*, const char*, const char*,
                                     sd_bus_message* reply, void*, sd_bus_error*) {
    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_close_container(reply);
    return 0;
}

static const sd_bus_vtable sni_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", sni_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", sni_secondary_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", sni_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", sni_scroll, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ProvideXdgActivationToken", "s", "", sni_provide_xdg_activation_token, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Category", "s", prop_category, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", prop_id, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", prop_title, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", prop_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("WindowId", "i", prop_window_id, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName", "s", prop_icon_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", prop_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("OverlayIconName", "s", prop_overlay_icon_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", prop_overlay_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("AttentionIconName", "s", prop_attention_icon_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", prop_attention_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("AttentionMovieName", "s", prop_attention_movie_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", prop_tooltip, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ItemIsMenu", "b", prop_item_is_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Menu", "o", prop_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconThemePath", "s", prop_icon_theme_path, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("NewTitle", "", 0),
    SD_BUS_SIGNAL("NewIcon", "", 0),
    SD_BUS_SIGNAL("NewAttentionIcon", "", 0),
    SD_BUS_SIGNAL("NewOverlayIcon", "", 0),
    SD_BUS_SIGNAL("NewToolTip", "", 0),
    SD_BUS_SIGNAL("NewStatus", "s", 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable menu_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", menu_get_layout, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetGroupProperties", "aias", "a(ia{sv})", menu_get_group_properties, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Event", "isvu", "", menu_event, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("EventGroup", "a(isvu)", "ai", menu_event_group, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShow", "i", "b", menu_about_to_show, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShowGroup", "ai", "aiai", menu_about_to_show_group, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Version", "u", menu_prop_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("TextDirection", "s", menu_prop_text_direction, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", menu_prop_status, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconThemePath", "as", menu_prop_icon_theme_path, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_SIGNAL("ItemsPropertiesUpdated", "a(ia{sv})a(ias)", 0),
    SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
    SD_BUS_VTABLE_END,
};

bool Tray::init() {
    renderIcon(g_tray.idle_pixels, g_tray.icon_size, 0.851f, 0.871f, 0.910f);
    renderIcon(g_tray.recording_pixels, g_tray.icon_size, 0.0f, 0.824f, 1.0f);

    int r = sd_bus_open_user(&g_tray.bus);
    if (r < 0) {
        fprintf(stderr, "whisker: tray: failed to connect to session bus: %s\n", strerror(-r));
        return false;
    }

    g_tray.bus_name = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";

    r = sd_bus_request_name(g_tray.bus, g_tray.bus_name.c_str(), 0);
    if (r < 0) {
        fprintf(stderr, "whisker: tray: failed to acquire bus name: %s\n", strerror(-r));
        sd_bus_unref(g_tray.bus);
        g_tray.bus = nullptr;
        return false;
    }

    r = sd_bus_add_object_vtable(g_tray.bus, &g_tray.sni_slot,
        "/StatusNotifierItem",
        "org.kde.StatusNotifierItem", sni_vtable, nullptr);
    if (r < 0) {
        fprintf(stderr, "whisker: tray: failed to register SNI object: %s\n", strerror(-r));
        shutdown();
        return false;
    }

    r = sd_bus_add_object_vtable(g_tray.bus, &g_tray.menu_slot,
        "/MenuBar",
        "com.canonical.dbusmenu", menu_vtable, nullptr);
    if (r < 0) {
        fprintf(stderr, "whisker: tray: failed to register menu object: %s\n", strerror(-r));
        shutdown();
        return false;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    r = sd_bus_call_method(g_tray.bus,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem",
        &error, &reply, "s", g_tray.bus_name.c_str());
    if (r < 0) {
        fprintf(stderr, "whisker: tray: failed to register with watcher: %s\n",
                error.message ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);
        shutdown();
        return false;
    }
    sd_bus_error_free(&error);
    if (reply) sd_bus_message_unref(reply);

    fprintf(stderr, "whisker: tray active\n");
    return true;
}

void Tray::shutdown() {
    if (g_tray.menu_slot) {
        sd_bus_slot_unref(g_tray.menu_slot);
        g_tray.menu_slot = nullptr;
    }
    if (g_tray.sni_slot) {
        sd_bus_slot_unref(g_tray.sni_slot);
        g_tray.sni_slot = nullptr;
    }
    if (g_tray.bus) {
        sd_bus_release_name(g_tray.bus, g_tray.bus_name.c_str());
        sd_bus_unref(g_tray.bus);
        g_tray.bus = nullptr;
    }
}

void Tray::setRecording(bool recording) {
    if (g_tray.recording == recording)
        return;
    g_tray.recording = recording;
    if (g_tray.bus) {
        sd_bus_emit_signal(g_tray.bus, "/StatusNotifierItem",
            "org.kde.StatusNotifierItem", "NewIcon", "");
    }
}

void Tray::poll() {
    if (!g_tray.bus) return;
    while (sd_bus_process(g_tray.bus, nullptr) > 0)
        ;
}

bool Tray::quitRequested() const {
    return g_tray.quit.load();
}
