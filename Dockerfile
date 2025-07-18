FROM alpine:latest AS builder

RUN apk add --no-cache  \
    build-base \
    clang20 \
    lld \
    libc++  \
    libc++-static \
    libc++-dev \
    musl-dev \
    cmake \
    ninja \
    git \
    compiler-rt \
    llvm20 \
    linux-headers\
    llvm-libunwind-static\
    	llvm-libunwind-dev\
    	llvm-libunwind upx

COPY . .

RUN mkdir /build
RUN cmake --preset release -DCMAKE_BUILD_TYPE=Release -S . -B /build
RUN cmake --build /build --target activity-check-bot

RUN upx --lzma --best /build/activity-check-bot

FROM alpine:latest

COPY --from=builder /build/activity-check-bot /

CMD ["/activity-check-bot"]