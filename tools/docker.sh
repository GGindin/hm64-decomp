#!/usr/bin/env bash
set -euo pipefail

image="${HM64_DOCKER_IMAGE:-hm64-decomp:bookworm}"
host_uid="${SUDO_UID:-$(id -u)}"
host_gid="${SUDO_GID:-$(id -g)}"
docker_flags=(--rm)

if [ -t 0 ] && [ -t 1 ]; then
    docker_flags+=(-it)
fi

docker build -t "$image" -f Dockerfile .
docker run "${docker_flags[@]}" \
    -v "$PWD":/repo \
    -w /repo \
    -e HOST_UID="$host_uid" \
    -e HOST_GID="$host_gid" \
    "$image" "$@"
