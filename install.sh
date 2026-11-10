#!/usr/bin/env bash
set -euo pipefail
module_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
prefix="${IIDECISION_INSTALL_PREFIX:-$HOME/.local/SDK/iiDecision}"
cmake -S "$module_dir" -B "$module_dir/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" "$@"
cmake --build "$module_dir/build" --parallel "${BUILD_JOBS:-4}"
if [[ "${IIDECISION_VALIDATE_DEVICES:-0}" == 1 ]]; then
    ctest --test-dir "$module_dir/build" --output-on-failure
else
    ctest --test-dir "$module_dir/build" --output-on-failure \
        -E '^iiDecision\.Acceleration\.(mlx|opencl)$'
fi
cmake --install "$module_dir/build"
