#!/bin/bash

# libzmq version definitions
LIBZMQ_VERSION="4.3.5"

# Download URLs
LIBZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v${LIBZMQ_VERSION}/zeromq-${LIBZMQ_VERSION}.tar.gz"

# Export for use in other scripts
export LIBZMQ_VERSION
export LIBZMQ_URL

# Display versions
echo "==================================="
echo "Build Configuration"
echo "==================================="
echo "libzmq version:    ${LIBZMQ_VERSION}"
echo "==================================="
