#!/usr/bin/env bash

set -euo pipefail

LOCAL_CACHE="${LOCAL_CACHE:-/tmp/cache/}"

if [ "$#" -eq 0 ]; then
  set -- //cyber/...
fi

bazel build --config=ci --distdir="$LOCAL_CACHE" "$@"
