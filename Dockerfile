FROM gcc:latest AS builder
WORKDIR /app
COPY . .
RUN make

FROM alpine

WORKDIR /usr/local/bin
COPY --from=builder /app/goteam .
CMD ["./goteam"]
