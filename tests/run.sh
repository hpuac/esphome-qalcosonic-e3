#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
  -Icomponents/qalcosonic_e3 components/qalcosonic_e3/parser.cpp \
  tests/test_parser.cpp -o "$build_dir/test_parser"
"$build_dir/test_parser"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
  -Icomponents/qalcosonic_e3 tests/test_link.cpp -o "$build_dir/test_link"
"$build_dir/test_link"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
  -Itests/stubs -Icomponents/qalcosonic_e3 \
  components/qalcosonic_e3/parser.cpp components/qalcosonic_e3/qalcosonic_e3.cpp \
  tests/test_component.cpp -o "$build_dir/test_component"
"$build_dir/test_component" tests/fixtures/wired_nominal.hex tests/fixtures/wired_low_delta_t_error.hex
