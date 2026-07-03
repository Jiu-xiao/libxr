# Documentation build image for LibXR.
# Preinstalls the exact Doxygen release and Graphviz used by the
# "Generate and Deploy Doxygen Docs" workflow, so CI does not download and
# verify the Doxygen tarball on every run.
FROM ubuntu:24.04

ARG DOXYGEN_VERSION=1.12.0
ARG DOXYGEN_SHA256=3c42c3f3fb206732b503862d9c9c11978920a8214f223a3950bbf2520be5f647

# graphviz: dot diagrams; ca-certificates/wget: fetch the release tarball once
# at image build time.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        graphviz \
        ca-certificates \
        wget \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    tarball="doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz"; \
    release="Release_$(echo "${DOXYGEN_VERSION}" | tr '.' '_')"; \
    wget --tries=5 --waitretry=10 --retry-connrefused \
         --retry-on-http-error=403,429,500,502,503,504 --timeout=30 \
         "https://github.com/doxygen/doxygen/releases/download/${release}/${tarball}"; \
    echo "${DOXYGEN_SHA256}  ${tarball}" | sha256sum -c -; \
    tar xzf "${tarball}" -C /opt; \
    ln -sf "/opt/doxygen-${DOXYGEN_VERSION}/bin/"* /usr/local/bin/; \
    rm -f "${tarball}"; \
    doxygen --version
