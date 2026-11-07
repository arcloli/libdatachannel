#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
build="$(cd -- "${1:-$root/build}" && pwd)"
out="$build/mcip-tests"
mkdir -p "$out"
flags=(-std=c++17 -O1 -g -Wall -Wextra -pthread -DRTC_ENABLE_MEDIA=1
       -I"$root/include" -L"$build" -Wl,-rpath,"$build")
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for name in rtcp_reports nack-window loopback; do
  "${CXX:-g++}" "${flags[@]}" "$root/mcip/tests/$name.cpp" -ldatachannel-mcip -o "$out/$name"
done
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
timeout 40 "$out/rtcp_reports"
timeout 10 "$out/nack-window"
printf '%s\n' "Loopback fixture compiled only; run it in an isolated network namespace."
