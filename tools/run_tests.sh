#!/usr/bin/env bash
set -euo pipefail
module_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$module_root"
build_dir=$(mktemp -d /tmp/wxl-equip-tests.XXXXXX)
trap 'rm -rf -- "$build_dir"' EXIT
compiler=${CXX:-g++}
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -pthread)
case "${1:-}" in
  "") ;;
  --sanitize) flags+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer) ;;
  *) echo "Usage: bash tools/run_tests.sh [--sanitize]" >&2; exit 2 ;;
esac
"$compiler" "${flags[@]}" -Isrc tests/equipment_config_test.cpp -o "$build_dir/config"
"$build_dir/config"
"$compiler" "${flags[@]}" -Isrc -Itests/stubs src/VirtualPath.cpp tests/virtual_path_test.cpp -o "$build_dir/paths"
"$build_dir/paths"
"$compiler" "${flags[@]}" -Itests/engine_stubs -Isrc tests/equipment_lifecycle_test.cpp -o "$build_dir/lifecycle"
"$build_dir/lifecycle"
python3 -m unittest discover -s tests -p 'test_*.py' -v
