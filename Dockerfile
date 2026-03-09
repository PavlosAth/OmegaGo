FROM gcc:latest AS builder
WORKDIR /app
COPY . .
RUN make

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/bin
COPY --from=builder /app/goteam .
CMD ["./goteam"]
