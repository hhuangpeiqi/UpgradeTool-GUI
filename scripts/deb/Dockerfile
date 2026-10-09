FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        dpkg-dev \
        file \
        git \
        libgl-dev \
        libegl-dev \
        libqt6opengl6-dev \
        libxkbcommon-dev \
        libvulkan-dev \
        qt6-base-dev \
        qt6-declarative-dev \
        qt6-declarative-dev-tools \
        qml6-module-qtquick \
        qml6-module-qtquick-controls \
        qml6-module-qtquick-layouts \
        qml6-module-qtquick-templates \
        qml6-module-qtquick-window \
        qml6-module-qtqml-workerscript \
        qt6-qpa-plugins \
        libqt6svg6 \
        xvfb \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
