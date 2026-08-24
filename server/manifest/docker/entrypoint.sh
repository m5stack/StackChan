#!/bin/sh
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT
#
# Entrypoint for the multi-stage stackchan-server Docker image.
#
# Backend v2 (utility/rsa.go) panics at init() if any of the 4 RSA keys is
# empty in the config. This entrypoint provisions them at first boot so the
# operator never has to leak PEM material into a committed config.yaml.
#
# Behavior:
#   1. If config.yaml already has PEM material, skip (operator-provided).
#   2. Else generate two 2048-bit RSA pairs to $RSA_KEYS_DIR (mount-friendly).
#   3. Rewrite config.yaml: the rsa: section is replaced with a freshly-built
#      block carrying the generated PEM as YAML block scalars.
#
# Idempotent: reuses existing keys if $RSA_KEYS_DIR/server_private.pem exists.
#
# Env:
#   CONFIG_PATH   default /app/config.yaml
#   RSA_KEYS_DIR  default /app/rsa-keys
set -eu

CONFIG_PATH="${CONFIG_PATH:-/app/config.yaml}"
RSA_KEYS_DIR="${RSA_KEYS_DIR:-/app/rsa-keys}"

# Workaround for upstream bug: device.NewV1 (ControllerV1.GetUserAccountInfo)
# and stackchandevice.NewV2 (ControllerV2.GetDeviceUserInfo) both register
# GET /stackChan/device/user. Backend v2 FATAs at boot on the duplicate.
# Inject routeOverWrite: true just under the top-level server: key so the
# second registration silently overwrites the first. Idempotent.
if ! grep -q "^  routeOverWrite:" "$CONFIG_PATH" 2>/dev/null; then
    echo "[entrypoint] injecting server.routeOverWrite (upstream duplicate-route bug)"
    awk '
        BEGIN { inserted = 0 }
        /^server:/ && !inserted { print; print "  routeOverWrite: true"; inserted = 1; next }
        { print }
    ' "$CONFIG_PATH" > "$CONFIG_PATH.tmp" && mv "$CONFIG_PATH.tmp" "$CONFIG_PATH"
fi

# Skip if operator pre-filled RSA material in the mounted config.
if grep -q "BEGIN RSA PRIVATE KEY\|BEGIN PRIVATE KEY\|BEGIN PUBLIC KEY" "$CONFIG_PATH" 2>/dev/null; then
    echo "[entrypoint] config.yaml already contains RSA material, skipping generation"
    exec /app/stackChan
fi

echo "[entrypoint] provisioning RSA keys"
mkdir -p "$RSA_KEYS_DIR"

if [ ! -s "$RSA_KEYS_DIR/server_private.pem" ]; then
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$RSA_KEYS_DIR/server_private.pem" 2>/dev/null
    openssl rsa -in "$RSA_KEYS_DIR/server_private.pem" -pubout -out "$RSA_KEYS_DIR/server_public.pem"  2>/dev/null
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$RSA_KEYS_DIR/client_private.pem" 2>/dev/null
    openssl rsa -in "$RSA_KEYS_DIR/client_private.pem" -pubout -out "$RSA_KEYS_DIR/client_public.pem"  2>/dev/null
    echo "[entrypoint] generated fresh RSA pairs in $RSA_KEYS_DIR"
else
    echo "[entrypoint] reusing existing RSA pairs from $RSA_KEYS_DIR"
fi

# Build a sed script that deletes the rsa: section and inserts the new block.
# The rsa: section spans from /^rsa:/ to the line *before* the next top-level
# key (/^[a-z]/ after the opening line). The c\ command replaces it.
SED_SCRIPT=$(mktemp)
trap 'rm -f "$SED_SCRIPT" /tmp/rsa_block.yaml' EXIT

# 1) Write the rsa: block to a temp file. 6-space indent matches field depth.
emit_block() { printf '|\n'; sed -e 's/^/      /' -e '/^$/d' "$1"; }

{
    echo "rsa:"
    echo "  server:"
    printf '    public: ';  emit_block "$RSA_KEYS_DIR/server_public.pem"
    printf '    private: '; emit_block "$RSA_KEYS_DIR/server_private.pem"
    echo "  client:"
    printf '    public: ';  emit_block "$RSA_KEYS_DIR/client_public.pem"
    printf '    private: '; emit_block "$RSA_KEYS_DIR/client_private.pem"
} > /tmp/rsa_block.yaml

# 2) sed script: delete the rsa: section, then insert the new block in place.
#    Using a placeholder line, then r (read file) — most portable sed feature.
cat > "$SED_SCRIPT" <<'EOF'
/^rsa:/{
    # Loop: delete this line; if next line starts with a letter, we are done.
    # Otherwise it is a child of rsa: — keep deleting.
    :loop
    N
    /\nrsa:/!{
        /\n[a-z]/!{
            s/.*\n//
            b loop
        }
    }
    # Replace everything from ^rsa: up to (not including) the next top-level
    # key with the generated block via `r` after a marker.
    s/^\(rsa:\n\)\(\(  [^\n]*\|\n\)*\)\n\([a-z]\)/RSA_BLOCK_PLACEHOLDER\n\4/
}
EOF

# The hand-rolled sed above is fragile across BSD/GNU sed. Fall back to a
# simple, robust awk one-liner that does the same job.
awk '
    BEGIN { in_rsa = 0 }
    /^rsa:/ { in_rsa = 1; while ((getline line < "/tmp/rsa_block.yaml") > 0) print line; close("/tmp/rsa_block.yaml"); next }
    in_rsa && /^[a-z]/ { in_rsa = 0 }
    in_rsa { next }
    { print }
' "$CONFIG_PATH" > "$CONFIG_PATH.tmp" && mv "$CONFIG_PATH.tmp" "$CONFIG_PATH"

if ! grep -q "BEGIN RSA PRIVATE KEY\|BEGIN PRIVATE KEY\|BEGIN PUBLIC KEY" "$CONFIG_PATH"; then
    echo "[entrypoint] ERROR: RSA substitution failed, config.yaml still has empty keys" >&2
    exit 1
fi

echo "[entrypoint] RSA keys provisioned, starting stackchan-server"
exec /app/stackChan
