# SPDX-License-Identifier: MIT
FROM archlinux:base AS deps

# Pinned snapshot keeps glibc at 2.41 to match SteamOS. The scheduled CI build
# overrides it with a live mirror to catch upstream breakage early.
# Today's keyring disables keys that signed packages in the snapshot, so install the
# snapshot's keyring (signature checked by today's keyring) and verify against that,
# as of the snapshot date: some of its certifications use SHA-1 or have since expired.
ARG ARCH_MIRROR=https://archive.archlinux.org/repos/2025/02/01/\$repo/os/\$arch
ARG ARCH_KEYRING_URL=https://archive.archlinux.org/repos/2025/02/01/core/os/x86_64/archlinux-keyring-20250123-1-any.pkg.tar.zst
ARG ARCH_SNAPSHOT_TIME=20250201T000000
# The snapshot's pacman can't apply its landlock download sandbox in rootless containers
RUN echo "Server = ${ARCH_MIRROR}" > /etc/pacman.d/mirrorlist \
    && sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf \
    && pacman-key --init \
    && pacman-key --populate archlinux \
    && if [ -n "$ARCH_KEYRING_URL" ]; then \
        pacman -U --noconfirm --disable-download-timeout "$ARCH_KEYRING_URL" \
        && gpgconf --homedir /etc/pacman.d/gnupg --kill all \
        && rm -rf /etc/pacman.d/gnupg \
        && mkdir -m 755 /etc/pacman.d/gnupg \
        && printf 'allow-weak-key-signatures\nfaked-system-time %s\n' "$ARCH_SNAPSHOT_TIME" \
            > /etc/pacman.d/gnupg/gpg.conf \
        && pacman-key --init \
        && pacman-key --populate archlinux; \
    fi \
    && pacman -Syyuu --noconfirm --disable-download-timeout --overwrite='*' \
    && gpgconf --homedir /etc/pacman.d/gnupg --kill all \
    && pacman -S --noconfirm --disable-download-timeout --needed \
        base-devel cmake git pkgconf file fuse2 curl \
        meson ninja python \
        pipewire systemd-libs \
        wayland wayland-protocols \
        vulkan-headers vulkan-icd-loader glslang spirv-headers shaderc \
        xxd libxkbcommon \
    && pacman -Scc --noconfirm

ENV PREFIX=/opt/static
ENV PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig:${PREFIX}/share/pkgconfig
ENV CFLAGS="-fPIC -O2" CXXFLAGS="-fPIC -O2"

WORKDIR /deps

RUN curl -sL https://zlib.net/fossils/zlib-1.3.1.tar.gz | tar xz \
    && cd zlib-1.3.1 \
    && ./configure --prefix=$PREFIX --static \
    && make -j$(nproc) && make install

RUN curl -sL https://github.com/glennrp/libpng/archive/refs/tags/v1.6.43.tar.gz | tar xz \
    && cd libpng-1.6.43 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF \
        -DCMAKE_PREFIX_PATH=$PREFIX \
    && cmake --build build -j$(nproc) && cmake --install build

RUN curl -sL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz | tar xJ \
    && cd freetype-2.13.3 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON \
        -DCMAKE_PREFIX_PATH=$PREFIX \
    && cmake --build build -j$(nproc) && cmake --install build

RUN curl -sL https://cairographics.org/releases/pixman-0.43.4.tar.gz | tar xz \
    && cd pixman-0.43.4 \
    && meson setup build --prefix=$PREFIX --default-library=static \
        -Dgtk=disabled -Dlibpng=disabled -Dopenmp=disabled -Dtests=disabled \
    && meson compile -C build && meson install -C build

RUN curl -sL https://cairographics.org/releases/cairo-1.18.2.tar.xz | tar xJ \
    && cd cairo-1.18.2 \
    && meson setup build --prefix=$PREFIX --default-library=static \
        -Dfontconfig=disabled -Dxlib=disabled -Dxcb=disabled \
        -Dfreetype=enabled -Dpng=enabled -Dtests=disabled \
        -Dspectre=disabled -Dsymbol-lookup=disabled -Dgtk_doc=false \
    && meson compile -C build && meson install -C build

RUN curl -sL https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz | tar xz \
    && cd opus-1.5.2 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DBUILD_SHARED_LIBS=OFF \
        -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
    && cmake --build build -j$(nproc) && cmake --install build

RUN curl -sL https://downloads.xiph.org/releases/opus/libopusenc-0.2.1.tar.gz | tar xz \
    && cd libopusenc-0.2.1 \
    && ./configure --prefix=$PREFIX --enable-static --disable-shared \
        --disable-doc --disable-examples \
    && make -j$(nproc) && make install

RUN curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz | tar xz \
    && cd SDL2-2.30.10 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_X11=OFF -DSDL_WAYLAND=ON \
        -DSDL_WAYLAND_LIBDECOR=OFF \
    && cmake --build build -j$(nproc) && cmake --install build

RUN curl -sL https://freedesktop.org/software/libevdev/libevdev-1.13.3.tar.xz | tar xJ \
    && cd libevdev-1.13.3 \
    && meson setup build --prefix=$PREFIX --default-library=static \
        -Dtests=disabled -Ddocumentation=disabled \
    && meson compile -C build && meson install -C build

FROM deps AS build
WORKDIR /build
COPY . .

RUN git config --global advice.detachedHead false

ARG CMAKE_ARGS=""
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
        -DSTATIC_PREFIX=/opt/static $CMAKE_ARGS \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

ENV APPIMAGE_EXTRACT_AND_RUN=1
RUN bash appimage/collect-licenses.sh third-party-licenses \
    && bash appimage/build-appimage.sh

CMD ["cp", "Whisker-x86_64.AppImage", "/output/"]
