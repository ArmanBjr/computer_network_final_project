# ---------- stage 1: build ----------
    FROM ubuntu:22.04 AS build

    ENV DEBIAN_FRONTEND=noninteractive
    
    RUN printf 'Acquire::Retries "10";\nAcquire::http::Timeout "60";\nAcquire::https::Timeout "60";\n' \
        > /etc/apt/apt.conf.d/99retries
    
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
     && rm -rf /var/lib/apt/lists/*
    
    WORKDIR /src
    COPY core/ ./core/
    
    RUN cmake -S ./core -B ./core/build -DCMAKE_BUILD_TYPE=Release \
     && cmake --build ./core/build -j
    
    # ---------- stage 2: runtime ----------
    FROM ubuntu:22.04 AS runtime
    
    ENV DEBIAN_FRONTEND=noninteractive
    
    RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libpq5 \
        libssl3 \
     && rm -rf /var/lib/apt/lists/*
    
    WORKDIR /app
    
    COPY --from=build /src/core/build/fsx_core /usr/local/bin/fsx_core
    
    EXPOSE 9000 9100
    
    ENV FSX_TCP_PORT=9000
    ENV FSX_ADMIN_PORT=9100
    
    CMD ["sh", "-c", "fsx_core --tcp ${FSX_TCP_PORT} --admin ${FSX_ADMIN_PORT}"]
    