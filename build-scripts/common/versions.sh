#!/bin/bash

# libzmq and libsodium version definitions
LIBZMQ_VERSION="4.3.5"
LIBSODIUM_VERSION="1.0.19"

# Download URLs
LIBZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v${LIBZMQ_VERSION}/zeromq-${LIBZMQ_VERSION}.tar.gz"
LIBSODIUM_URL="https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}-RELEASE/libsodium-${LIBSODIUM_VERSION}.tar.gz"

# Export for use in other scripts
export LIBZMQ_VERSION
export LIBSODIUM_VERSION
export LIBZMQ_URL
export LIBSODIUM_URL

# Display versions
echo "==================================="
echo "Build Configuration"
echo "==================================="
echo "libzmq version:    ${LIBZMQ_VERSION}"
echo "libsodium version: ${LIBSODIUM_VERSION}"
echo "==================================="
