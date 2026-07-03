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
#   ACT_PREPARE_ONLY=1         # only sync + patch the temp worktree

set -euo pipefail

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
fi

JOB="${1:-gcc-smoke}"
EVENT="${ACT_EVENT:-push}"
IMAGE="${ACT_IMAGE:-catthehacker/ubuntu:act-latest}"
PROXY="${ACT_PROXY:-http://host.docker.internal:7890}"
OFFLINE="${ACT_OFFLINE:-1}"
PREPARE_ONLY="${ACT_PREPARE_ONLY:-0}"

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

if ! command -v rsync >/dev/null 2>&1; then
    echo "[act-ci] rsync not found on PATH" >&2
    exit 1
fi

if ! command -v git >/dev/null 2>&1; then
    echo "[act-ci] git not found on PATH" >&2
    exit 1
fi

if [ "$PREPARE_ONLY" = "0" ]; then
    if ! command -v act >/dev/null 2>&1; then
        echo "[act-ci] act not found on PATH" >&2
        exit 1
    fi
    if ! command -v docker >/dev/null 2>&1; then
        echo "[act-ci] docker not found on PATH" >&2
        echo "[act-ci] enable Docker Desktop WSL integration or expose docker in this shell" >&2
        exit 1
    fi
    if ! docker version >/dev/null 2>&1; then
        echo "[act-ci] docker daemon is not reachable from this shell" >&2
        echo "[act-ci] check Docker Desktop is running and WSL integration is enabled" >&2
        exit 1
    fi
    if [ "$OFFLINE" != "0" ] && ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "[act-ci] image missing locally: $IMAGE" >&2
        echo "[act-ci] run: docker pull $IMAGE" >&2
        echo "[act-ci] or set ACT_OFFLINE=0 to let act fetch what it needs" >&2
        exit 1
    fi
fi

if [ -e "$WORKDIR" ] && [ ! -w "$WORKDIR" ]; then
    if [ -n "${ACT_WORKDIR:-}" ]; then
        echo "[act-ci] ACT_WORKDIR is not writable: $WORKDIR" >&2
        echo "[act-ci] choose a new /tmp/cinux-act-* path or fix ownership" >&2
        exit 1
    fi
    WORKDIR="$(mktemp -d "/tmp/cinux-act-$JOB-XXXXXX")"
    echo "[act-ci] default workdir was not writable; using $WORKDIR"
else
    mkdir -p "$WORKDIR"
fi

echo "[act-ci] syncing worktree -> $WORKDIR"
rsync -a --delete \
    --exclude='/.git/' \
    --exclude='/.git' \
    --exclude='/build/' \
    --exclude='/.cache/' \
    --exclude='/cache.tzst' \
    --exclude='/manifest.txt' \
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

# act resolves the workspace + composite-action paths (./.github/actions/...)
# against git, but the rsync above excludes the source .git. Give the temp tree
# a throwaway repo with tracked patched files so act can derive a ref and local
# action paths. Without this setup-cinux may fail in 47 ms with
# "repository does not exist", or resolve action.yml to an empty path.
if [ -e "$WORKDIR/.git" ]; then
    rm -rf "$WORKDIR/.git"
fi
git -C "$WORKDIR" init -q -b main
cat >> "$WORKDIR/.git/info/exclude" <<'EOF'
/build/
/.cache/
/cache.tzst
/manifest.txt
EOF
git -C "$WORKDIR" add -A
git -C "$WORKDIR" -c user.email=act@local -c user.name=act \
    commit -q --allow-empty -m act-base >/dev/null

if [ "$PREPARE_ONLY" != "0" ]; then
    echo "[act-ci] prepared temp worktree at $WORKDIR"
    exit 0
fi

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
