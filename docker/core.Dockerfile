# ---------- stage 1: build ----------
FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN printf 'Acquire::Retries "10";\nAcquire::http::Timeout "60";\nAcquire::https::Timeout "60";\n' \
    > /etc/apt/apt.conf.d/99retries

# Use ArvanCloud mirror for apt (for restricted/slow internet)
RUN sed -i 's|http://archive.ubuntu.com/ubuntu|http://mirror.arvancloud.ir/ubuntu|g' /etc/apt/sources.list \
    && sed -i 's|http://security.ubuntu.com/ubuntu|http://mirror.arvancloud.ir/ubuntu|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential \
    cmake \
    pkg-config \
    git \
    libboost-system-dev \
    libboost-thread-dev \
    libpq-dev \
    libssl-dev \
    zlib1g-dev \
    libopus-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY core/ ./core/

RUN cmake -S ./core -B ./core/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build ./core/build -j$(nproc)

# ---------- stage 2: runtime ----------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Use ArvanCloud mirror for apt (for restricted/slow internet)
RUN sed -i 's|http://archive.ubuntu.com/ubuntu|http://mirror.arvancloud.ir/ubuntu|g' /etc/apt/sources.list \
    && sed -i 's|http://security.ubuntu.com/ubuntu|http://mirror.arvancloud.ir/ubuntu|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libpq5 \
    libssl3 \
    zlib1g \
    libopus0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /src/core/build/fsx_core /usr/local/bin/fsx_core

EXPOSE 9000 9001/udp 9100

ENV FSX_TCP_PORT=9000
ENV FSX_UDP_PORT=9001
ENV FSX_ADMIN_PORT=9100

CMD ["fsx_core"]
    