#!/bin/sh
set -eu
. /opt/inputs.env

fetch() {
    url="$1" hash="$2" name="$3"
    curl --fail --location --retry 3 "$url" -o /tmp/source.tar
    printf '%s  %s\n' "$hash" /tmp/source.tar | sha256sum -c -
    mkdir -p "/opt/$name"
    tar -xf /tmp/source.tar --strip-components=1 -C "/opt/$name"
    rm /tmp/source.tar
}

fetch "https://codeload.github.com/BlockchainCommons/bc-ur/tar.gz/$UR_COMMIT" "$UR_SHA256" bc-ur
fetch "$URTYPES_TEST_URL" "$URTYPES_TEST_SHA256" urtypes
fetch "https://www.linuxtv.org/downloads/zbar/zbar-$ZBAR_VERSION.tar.bz2" "$ZBAR_SHA256" zbar
fetch "https://codeload.github.com/fukuchi/libqrencode/tar.gz/v$QRENCODE_VERSION" "$QRENCODE_SHA256" qrencode
fetch "https://linuxtv.org/downloads/v4l-utils/v4l-utils-$V4L_VERSION.tar.xz" "$V4L_SHA256" v4l

mkdir /tmp/zbar-build
(
    cd /tmp/zbar-build
    /opt/zbar/configure --prefix=/usr/local --libdir=/usr/local/lib \
        --enable-codes=qrcode --disable-video --disable-doc --disable-nls \
        --without-x --without-xshm --without-xv --without-dbus --without-jpeg \
        --without-imagemagick --without-graphicsmagick --without-gtk --without-gir \
        --without-qt --without-java --without-python --disable-static CFLAGS=-Os
    make -j2
    make install
)
cmake -S /opt/qrencode -B /tmp/qrencode-build -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
    -DWITHOUT_PNG=ON -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib
cmake --build /tmp/qrencode-build --parallel 2
cmake --install /tmp/qrencode-build
meson setup /tmp/v4l-build /opt/v4l --prefix=/usr/local --libdir=lib --buildtype=minsize \
    -Dbpf=disabled -Dgconv=disabled -Djpeg=enabled -Dlibdvbv5=disabled \
    -Dqv4l2=disabled -Dqvidcap=disabled -Dv4l2-tracer=disabled \
    -Dv4l-plugins=false -Dv4l-utils=false -Dv4l-wrappers=false -Ddoxygen-doc=disabled
meson compile -C /tmp/v4l-build -j2
meson install -C /tmp/v4l-build
ldconfig
rm -rf /tmp/zbar-build /tmp/qrencode-build /tmp/v4l-build
