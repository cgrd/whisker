# SPDX-License-Identifier: MIT
# Bootstrap only: pacman below moves every package to the ARCH_MIRROR snapshot
FROM docker.io/library/archlinux:base-20261025.0.616225@sha256:e82623936fa1a69700b48e98b058df6eaab18b737c27473a7234a40fdecb9413 AS deps

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
COPY scripts/fetch-source.sh /usr/local/bin/fetch-source

RUN fetch-source https://zlib.net/fossils/zlib-1.3.1.tar.gz \
        9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 \
    && cd zlib-1.3.1 \
    && ./configure --prefix=$PREFIX --static \
    && make -j$(nproc) && make install

RUN fetch-source https://github.com/glennrp/libpng/archive/refs/tags/v1.6.43.tar.gz \
        fecc95b46cf05e8e3fc8a414750e0ba5aad00d89e9fdf175e94ff041caf1a03a \
    && cd libpng-1.6.43 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF \
        -DCMAKE_PREFIX_PATH=$PREFIX \
    && cmake --build build -j$(nproc) && cmake --install build

RUN fetch-source https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz \
        0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289 \
    && cd freetype-2.13.3 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON \
        -DCMAKE_PREFIX_PATH=$PREFIX \
    && cmake --build build -j$(nproc) && cmake --install build

RUN fetch-source https://cairographics.org/releases/pixman-0.43.4.tar.gz \
        a0624db90180c7ddb79fc7a9151093dc37c646d8c38d3f232f767cf64b85a226 \
    && cd pixman-0.43.4 \
    && meson setup build --prefix=$PREFIX --default-library=static \
        -Dgtk=disabled -Dlibpng=disabled -Dopenmp=disabled -Dtests=disabled \
    && meson compile -C build && meson install -C build

RUN fetch-source https://cairographics.org/releases/cairo-1.18.2.tar.xz \
        a62b9bb42425e844cc3d6ddde043ff39dbabedd1542eba57a2eb79f85889d45a \
    && cd cairo-1.18.2 \
    && meson setup build --prefix=$PREFIX --default-library=static \
        -Dfontconfig=disabled -Dxlib=disabled -Dxcb=disabled \
        -Dfreetype=enabled -Dpng=enabled -Dtests=disabled \
        -Dspectre=disabled -Dsymbol-lookup=disabled -Dgtk_doc=false \
    && meson compile -C build && meson install -C build

RUN fetch-source https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz \
        65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1 \
    && cd opus-1.5.2 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX -DBUILD_SHARED_LIBS=OFF \
        -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
    && cmake --build build -j$(nproc) && cmake --install build

RUN fetch-source https://downloads.xiph.org/releases/opus/libopusenc-0.2.1.tar.gz \
        8298db61a8d3d63e41c1a80705baa8ce9ff3f50452ea7ec1c19a564fe106cbb9 \
    && cd libopusenc-0.2.1 \
    && ./configure --prefix=$PREFIX --enable-static --disable-shared \
        --disable-doc --disable-examples \
    && make -j$(nproc) && make install

RUN fetch-source https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz \
        5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 \
    && cd SDL2-2.32.10 \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_X11=OFF -DSDL_WAYLAND=ON \
        -DSDL_WAYLAND_LIBDECOR=OFF \
    && cmake --build build -j$(nproc) && cmake --install build

RUN fetch-source https://freedesktop.org/software/libevdev/libevdev-1.13.3.tar.xz \
        abf1aace86208eebdd5d3550ffded4c8d73bb405b796d51c389c9d0604cbcfbf \
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
