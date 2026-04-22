# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT
#
# Multi-stage build. Run from the server/ directory:
#   cd server && docker build -t stackchan-server -f manifest/docker/Dockerfile .
#
# Or with docker compose using a Git URL context:
#   build:
#     context: https://github.com/m5stack/StackChan.git#main:server
#     dockerfile: manifest/docker/Dockerfile

FROM golang:1.24-alpine AS builder

RUN apk add --no-cache gcc musl-dev sqlite-dev

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .

ENV CGO_ENABLED=1 GOOS=linux
RUN go build -trimpath -ldflags="-s -w" -o /out/stackChan main.go

FROM alpine:latest

RUN apk add --no-cache ca-certificates tzdata

WORKDIR /app

COPY --from=builder /out/stackChan /app/stackChan
COPY --from=builder /src/manifest/config/config.yaml /app/config.yaml

EXPOSE 12800

CMD ["/app/stackChan"]
