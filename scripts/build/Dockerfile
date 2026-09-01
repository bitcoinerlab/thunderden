ARG DEBIAN_BASE_IMAGE=scratch
FROM ${DEBIAN_BASE_IMAGE}

ARG DEBIAN_SNAPSHOT

RUN rm -f /etc/apt/sources.list /etc/apt/sources.list.d/* \
    && printf '%s\n' \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ trixie main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ trixie-updates main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/ trixie-security main" \
      > /etc/apt/sources.list \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
      build-essential bc bison flex cpio file git rsync unzip wget curl \
      xz-utils tar python3 libncurses-dev gawk patch pkg-config \
      meson ninja-build cmake \
      gnupg parted \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash builder
