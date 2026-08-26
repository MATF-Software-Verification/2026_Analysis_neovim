FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake curl gettext ninja-build git unzip pkg-config \
    valgrind \
    linux-tools-common linux-tools-generic \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
