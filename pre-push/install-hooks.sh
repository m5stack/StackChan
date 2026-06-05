#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
hook_path="$repo_root/.git/hooks/pre-push"

if [ -e "$hook_path" ] && ! grep -q "stackchan api docs check" "$hook_path"; then
    echo "Refusing to overwrite existing pre-push hook: $hook_path" >&2
    exit 1
fi

cat > "$hook_path" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

# stackchan api docs check
repo_root="$(git rev-parse --show-toplevel)"
python_bin="$repo_root/.venv/bin/python"
if [ ! -x "$python_bin" ]; then
    python_bin="python3"
fi
"$python_bin" "$repo_root/pre-push/api_docs_check.py"
EOF

chmod +x "$hook_path"
echo "Installed $hook_path"
