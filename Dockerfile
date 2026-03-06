FROM gcc:latest AS builder
WORKDIR /app
COPY . .
RUN make

FROM debian:bookworm-slim
WORKDIR /usr/local/bin
COPY --from=builder /app/goteam . 
RUN chmod +x /usr/local/bin/goteam
CMD ["./goteam"]
