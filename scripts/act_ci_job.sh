#!/bin/bash
# Run a GitHub Actions job locally through act without letting actions/checkout
# touch the real worktree.
#
# Defaults are tuned for WSL + Docker Desktop + a host proxy on 127.0.0.1:7890:
# containers reach that proxy as host.docker.internal:7890.
#
# Usage:
#   scripts/act_ci_job.sh [job]
#
# Useful overrides:
#   ACT_PROXY=                 # disable proxy env injection
#   ACT_PROXY=http://host.docker.internal:7890
#   ACT_WORKDIR=/tmp/cinux-act-gcc-smoke
#   ACT_IMAGE=catthehacker/ubuntu:act-latest
#   ACT_OFFLINE=0              # allow act to fetch actions if cache is cold

set -euo pipefail

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
fi

JOB="${1:-gcc-smoke}"
EVENT="${ACT_EVENT:-push}"
IMAGE="${ACT_IMAGE:-catthehacker/ubuntu:act-latest}"
PROXY="${ACT_PROXY:-http://host.docker.internal:7890}"
OFFLINE="${ACT_OFFLINE:-1}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKDIR="${ACT_WORKDIR:-/tmp/cinux-act-$JOB}"

case "$WORKDIR" in
    /tmp/cinux-act-*) ;;
    *)
        echo "[act-ci] refusing unsafe ACT_WORKDIR: $WORKDIR" >&2
        echo "[act-ci] use a /tmp/cinux-act-* directory" >&2
        exit 1
        ;;
esac

if ! command -v act >/dev/null 2>&1; then
    echo "[act-ci] act not found on PATH" >&2
    exit 1
fi

if ! command -v rsync >/dev/null 2>&1; then
    echo "[act-ci] rsync not found on PATH" >&2
    exit 1
fi

mkdir -p "$WORKDIR"

echo "[act-ci] syncing worktree -> $WORKDIR"
rsync -a --delete \
    --exclude='/.git/' \
    --exclude='/.git' \
    --exclude='/build/' \
    --exclude='/.cache/' \
    "$REPO_ROOT/" "$WORKDIR/"

python3 - "$WORKDIR" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])


def strip_checkout(path: pathlib.Path) -> None:
    lines = path.read_text().splitlines(keepends=True)
    out = []
    skip_indent = None
    checkout_re = re.compile(r"^(\s*)-\s+uses:\s+actions/checkout@v\d+\s*$")

    for line in lines:
        if skip_indent is not None:
            if line.startswith(" " * skip_indent + "- "):
                skip_indent = None
            else:
                continue

        match = checkout_re.match(line)
        if match:
            skip_indent = len(match.group(1))
            continue

        out.append(line)

    path.write_text("".join(out))


strip_checkout(root / ".github/workflows/ci.yml")
strip_checkout(root / ".github/actions/setup-cinux/action.yml")

action = root / ".github/actions/setup-cinux/action.yml"
text = action.read_text()
text = text.replace(
    "run: sudo apt-get install -y ccache ${{ inputs.apt-packages }}",
    "run: |\n"
    "        sudo -E apt-get update\n"
    "        sudo -E apt-get install -y ccache ${{ inputs.apt-packages }}",
)
action.write_text(text)
PY

cmd=(
    act "$EVENT"
    -j "$JOB"
    -P "ubuntu-latest=$IMAGE"
    --cache-server-addr 127.0.0.1
    --artifact-server-addr 127.0.0.1
    --bind
    --reuse
)

if [ "$OFFLINE" != "0" ]; then
    cmd+=(--pull=false --action-offline-mode)
fi

if [ -n "$PROXY" ]; then
    cmd+=(
        --env "HTTP_PROXY=$PROXY"
        --env "HTTPS_PROXY=$PROXY"
        --env "http_proxy=$PROXY"
        --env "https_proxy=$PROXY"
        --env "NO_PROXY=localhost,127.0.0.1,::1"
        --env "no_proxy=localhost,127.0.0.1,::1"
    )
fi

echo "[act-ci] running job '$JOB' in $WORKDIR"
echo "[act-ci] image=$IMAGE offline=$OFFLINE proxy=${PROXY:-disabled}"
(
    cd "$WORKDIR"
    "${cmd[@]}"
)
