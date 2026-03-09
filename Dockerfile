FROM gcc:latest AS builder
WORKDIR /app
COPY . .
RUN make

FROM alpine:latest
RUN apk update && apk add --no-cache libc6-compat
WORKDIR /usr/local/bin
COPY --from=builder /app/goteam .
RUN chmod +x /usr/local/bin/goteam
CMD ["./goteam"]
