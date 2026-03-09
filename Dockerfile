FROM gcc:latest AS builder
WORKDIR /app
COPY . .
RUN make

FROM alpine:3.21

WORKDIR /usr/local/bin
COPY --from=builder /app/goteam .
CMD ["./goteam"]
