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

FROM golang:1.26-alpine AS builder

RUN apk add --no-cache gcc musl-dev sqlite-dev

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .

# go.mod/go.sum may be out of sync after upstream merges (new imports
# missing from go.sum). `go mod tidy` reconciles before the build.
RUN go mod tidy

ENV CGO_ENABLED=1 GOOS=linux
RUN go build -trimpath -ldflags="-s -w" -o /out/stackChan main.go

FROM alpine:latest

RUN apk add --no-cache ca-certificates tzdata

WORKDIR /app

COPY --from=builder /out/stackChan /app/stackChan
COPY --from=builder /src/manifest/config/config.yaml /app/config.yaml
COPY manifest/docker/entrypoint.sh /app/entrypoint.sh

# Backend v2 calls s.SetServerRoot("web/management") at boot and FATAs if the
# directory is missing. The frontend admin UI is built separately (npm) and not
# bundled here. Provide an empty dir so the API boots; the UI is not needed for
# the robot pipeline.
RUN mkdir -p /app/web/management && chmod +x /app/entrypoint.sh

EXPOSE 12800

# Entrypoint provisions RSA keys at first boot (backend v2 panics on empty keys).
# Mount /app/rsa-keys as a volume to keep stable keys across container recreates.
CMD ["/app/entrypoint.sh"]
