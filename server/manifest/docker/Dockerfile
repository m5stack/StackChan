# Stage 1: Build Go binary
FROM golang:1.24-alpine AS builder
WORKDIR /build
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -o stackchan main.go

# Stage 2: Minimal runtime image
FROM alpine:latest
ENV WORKDIR=/app
WORKDIR $WORKDIR
COPY --from=builder /build/stackchan $WORKDIR/stackchan
COPY hack/config.yaml $WORKDIR/config.yaml
RUN chmod +x $WORKDIR/stackchan
CMD ["./stackchan"]