cmake_minimum_required(VERSION 3.10)
project(zlink_root_entrypoint NONE)

message(FATAL_ERROR
  "The repository root is no longer a CMake entry point.\n"
  "Configure one project at a time instead:\n"
  "  core          : cmake -S core -B core/build\n"
  "  bindings/c    : cmake -S bindings/c -B bindings/c/build "
  "-DZLINK_C_CORE_BUILD_DIR=<repo>/core/build\n"
  "  bindings/cpp  : cmake -S bindings/cpp -B bindings/cpp/build "
  "-DZLINK_CPP_CORE_BUILD_DIR=<repo>/core/build")
