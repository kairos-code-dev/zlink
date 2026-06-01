/* SPDX-License-Identifier: MPL-2.0 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ZLINK_FRAMEWORK_CPP_SOURCE_DIR
#error "ZLINK_FRAMEWORK_CPP_SOURCE_DIR must be defined"
#endif

namespace
{

bool
require_exists (const std::filesystem::path &path)
{
  if (std::filesystem::exists (path)) {
    return true;
  }
  std::cerr << "missing required path: " << path << '\n';
  return false;
}

bool
public_headers_do_not_include_runtime (const std::filesystem::path &root)
{
  bool ok = true;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (root)) {
    if (!entry.is_regular_file () || entry.path ().extension () != ".hpp") {
      continue;
    }

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      if (line.find ("src/runtime") != std::string::npos) {
        std::cerr << "public header references runtime implementation: "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
    }
  }
  return ok;
}

} // namespace

int
main ()
{
  const std::filesystem::path root { ZLINK_FRAMEWORK_CPP_SOURCE_DIR };

  bool ok = true;
  ok &= require_exists (
    root / "framework/include/zlink/framework/contracts");
  ok &= require_exists (root / "framework/src/runtime");
  ok &= require_exists (
    root / "connector/include/zlink/stream_connector/contracts");
  ok &= require_exists (root / "connector/src/runtime");
  ok &= require_exists (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public");
  ok &= require_exists (
    root / "unreal-connector/Source/ZLinkStreamConnector/Private");

  ok &= public_headers_do_not_include_runtime (
    root / "framework/include");
  ok &= public_headers_do_not_include_runtime (
    root / "connector/include");

  return ok ? 0 : 1;
}
