ARG DEBIAN_BASE_IMAGE=scratch
FROM ${DEBIAN_BASE_IMAGE}
ARG DEBIAN_SNAPSHOT
RUN rm -f /etc/apt/sources.list /etc/apt/sources.list.d/* \
    && printf '%s\n' \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}/ trixie main" \
      "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/${DEBIAN_SNAPSHOT}/ trixie-security main" \
      > /etc/apt/sources.list \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates curl g++ cmake ninja-build pkg-config libboost-dev libssl-dev python3 strace \
    && rm -rf /var/lib/apt/lists/*

ARG BITCOIN_VERSION
ARG BITCOIN_SOURCE_SHA256
RUN curl --fail --location --retry 3 \
      "https://bitcoincore.org/bin/bitcoin-core-${BITCOIN_VERSION}/bitcoin-${BITCOIN_VERSION}.tar.gz" \
      -o /tmp/bitcoin.tar.gz \
    && printf '%s  %s\n' "$BITCOIN_SOURCE_SHA256" /tmp/bitcoin.tar.gz | sha256sum -c - \
    && tar -xzf /tmp/bitcoin.tar.gz -C /opt \
    && mv "/opt/bitcoin-${BITCOIN_VERSION}" /opt/bitcoin \
    && rm /tmp/bitcoin.tar.gz

ARG BIP39_WORDLIST_URL
ARG BIP39_WORDLIST_SHA256
RUN curl --fail --location --retry 3 "$BIP39_WORDLIST_URL" -o /opt/bip39-english.txt \
    && printf '%s  %s\n' "$BIP39_WORDLIST_SHA256" /opt/bip39-english.txt | sha256sum -c -

COPY CMakeLists.txt /work/CMakeLists.txt
COPY src /work/src
COPY tests /work/tests
RUN python3 -B /work/tests/core_keys.py --source-hash > /opt/bitcoin-source.sha256 \
    && cmake -S /work -B /build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCORE_SOURCE_DIR=/opt/bitcoin -DBIP39_WORDLIST=/opt/bip39-english.txt \
      -DBIP39_WORDLIST_SHA256="$BIP39_WORDLIST_SHA256" \
    && cmake --build /build --parallel 2 \
    && python3 -B /work/tests/core_keys.py --source-hash > /tmp/source-after.sha256 \
    && cmp /opt/bitcoin-source.sha256 /tmp/source-after.sha256
