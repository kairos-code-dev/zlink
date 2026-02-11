# FROM		debian:7       # APT repo no more available.
# FROM		debian:8
# FROM		debian:9
# FROM		debian:10
# FROM		debian:11

# FROM		ubuntu:12.04   # APT repo no more available.
# FROM		ubuntu:14.04
# FROM		ubuntu:16.04
# FROM		ubuntu:18.04
# FROM		ubuntu:20.04
FROM		ubuntu:22.04

ENV		DEBIAN_FRONTEND=noninteractive

RUN		apt-get update -y -q

RUN		apt-get install -y -q \
			apt-utils

RUN		apt-get install -y -q \
			autoconf \
			automake \
			cmake \
			file \
			git \
			libtool \
			pkg-config \
			unzip \
			wget

# Create user zlink, and run Android build with it.
# This ensures that nothing weird is performed with
# ROOT privileges.
RUN		useradd -d /home/zlink -m -s /bin/bash zlink
USER		zlink
WORKDIR		/home/zlink

# Install android NDK (from NDK 23):
ENV		NDK_VERSION=android-ndk-r25

RUN		wget -q -O ndk_archive.zip http://dl.google.com/android/repository/${NDK_VERSION}-linux.zip

RUN		unzip -q ndk_archive.zip

# Clone and build zlink
ENV		ANDROID_NDK_ROOT=/home/zlink/${NDK_VERSION}

RUN		git clone --quiet --depth 1 https://github.com/kairos-code-dev/zlink.git

RUN		zlink/core/builds/android/ci_build.sh
