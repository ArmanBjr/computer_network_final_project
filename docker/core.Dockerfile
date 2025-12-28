FROM ubuntu:24.04


ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake pkg-config \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY core /app/core
COPY client /app/client

RUN apt-get update && apt-get install -y \
    build-essential cmake pkg-config \
    libboost-system-dev libboost-thread-dev \
    && rm -rf /var/lib/apt/lists/*


EXPOSE 9000 9100

CMD ["/app/core/build/fsx_core_server"]
