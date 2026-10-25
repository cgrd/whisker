// SPDX-License-Identifier: MIT
#include "haptic.h"
#include <cstdint>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>

std::string Haptic::findFFDevice() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return {};

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long ffbits[FF_MAX / (sizeof(unsigned long) * 8) + 1] = {};
        if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffbits)), ffbits) >= 0) {
            bool has_rumble = ffbits[FF_RUMBLE / (sizeof(unsigned long) * 8)] &
                              (1UL << (FF_RUMBLE % (sizeof(unsigned long) * 8)));
            if (has_rumble) {
                char devname[256] = {};
                ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);
                ::close(fd);
                closedir(dir);
                fprintf(stderr, "haptic: found %s at %s\n", devname, path.c_str());
                return path;
            }
        }
        ::close(fd);
    }
    closedir(dir);
    return {};
}

bool Haptic::init() {
    std::string path = findFFDevice();
    if (path.empty()) {
        fprintf(stderr, "haptic: no force feedback device found\n");
        return false;
    }

    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        fprintf(stderr, "haptic: failed to open %s\n", path.c_str());
        return false;
    }

    struct ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.replay.length = 150;
    effect.replay.delay = 0;
    effect.u.rumble.strong_magnitude = 0xC000;
    effect.u.rumble.weak_magnitude = 0x8000;

    if (ioctl(fd_, EVIOCSFF, &effect) < 0) {
        fprintf(stderr, "haptic: failed to upload effect\n");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    effect_id_ = effect.id;
    fprintf(stderr, "haptic: initialized (effect id %d)\n", effect_id_);
    return true;
}

void Haptic::shutdown() {
    if (fd_ >= 0) {
        if (effect_id_ >= 0) {
            ioctl(fd_, EVIOCRMFF, effect_id_);
            effect_id_ = -1;
        }
        ::close(fd_);
        fd_ = -1;
    }
}

void Haptic::pulse(int duration_ms, int strength) {
    if (fd_ < 0 || effect_id_ < 0) return;

    struct ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = effect_id_;
    effect.replay.length = static_cast<uint16_t>(duration_ms);
    effect.replay.delay = 0;
    effect.u.rumble.strong_magnitude = static_cast<uint16_t>(strength);
    effect.u.rumble.weak_magnitude = static_cast<uint16_t>(strength / 2);

    ioctl(fd_, EVIOCSFF, &effect);

    struct input_event play = {};
    play.type = EV_FF;
    play.code = static_cast<uint16_t>(effect_id_);
    play.value = 1;
    ::write(fd_, &play, sizeof(play));
}
