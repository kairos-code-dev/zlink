#!/usr/bin/env bash
set -euo pipefail

BINDING="$1"
ROOT_DIR="$2"

case "$BINDING" in
  python)
    command -v python3 >/dev/null 2>&1 || command -v python >/dev/null 2>&1 || {
      echo "python is required" >&2; exit 1;
    }
    ;;
  node)
    command -v node >/dev/null 2>&1 || { echo "node is required" >&2; exit 1; }
    node_lib="${ROOT_DIR}/core/build/linux-x64/lib/libzlink.so"
    if [[ ! -f "${node_lib}" ]]; then
      node_lib="${ROOT_DIR}/build_cpp/lib/libzlink.so"
    fi
    if command -v node-gyp >/dev/null 2>&1; then
      (cd "$ROOT_DIR/bindings/node" && ZLINK_LIB_PATH="${node_lib}" node-gyp rebuild) >/dev/null
    elif command -v npx >/dev/null 2>&1; then
      (cd "$ROOT_DIR/bindings/node" && ZLINK_LIB_PATH="${node_lib}" npx --yes node-gyp rebuild) >/dev/null
    else
      echo "node-gyp (or npx) is required for node binding benchmark build" >&2
      exit 1
    fi
    ;;
  dotnet)
    command -v dotnet >/dev/null 2>&1 || { echo "dotnet is required" >&2; exit 1; }
    dotnet build "$ROOT_DIR/bindings/dotnet/benchwithzlink/Zlink.BindingBench/Zlink.BindingBench.csproj" -c Release >/dev/null
    ;;
  java)
    command -v java >/dev/null 2>&1 || { echo "java is required" >&2; exit 1; }
    (cd "$ROOT_DIR/bindings/java" && ./gradlew -q classes testClasses) >/dev/null
    ;;
  cpp)
    command -v c++ >/dev/null 2>&1 || { echo "c++ compiler is required" >&2; exit 1; }
    mkdir -p "$ROOT_DIR/bindings/cpp/benchwithzlink/build"
    c++ -O3 -std=c++17 \
      -I"$ROOT_DIR/core/include" \
      -I"$ROOT_DIR/bindings/cpp/include" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/pair_bench.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_pair.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_pubsub.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_dealer_dealer.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_dealer_router.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_router_router.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_router_router_poll.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_stream.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_gateway.cpp" \
      "$ROOT_DIR/bindings/cpp/benchwithzlink/bench_pattern_spot.cpp" \
      -L"$ROOT_DIR/bindings/cpp/native/linux-x86_64" -lzlink \
      -Wl,-rpath,"$ROOT_DIR/bindings/cpp/native/linux-x86_64" \
      -o "$ROOT_DIR/bindings/cpp/benchwithzlink/build/pair_bench" >/dev/null
    ;;
  *)
    echo "Unsupported binding: $BINDING" >&2
    exit 1
    ;;
esac
