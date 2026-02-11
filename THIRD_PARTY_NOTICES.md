# Third-Party Notices

This repository contains third-party source code and/or redistributable binaries.
This file summarizes known components and where their license texts are located.

## Bundled source dependencies

- Boost (header-only subset)
  - Location: `core/external/boost/`
  - License: Boost Software License 1.0
  - Reference: https://www.boost.org/users/license.html

- wepoll
  - Location: `core/external/wepoll/`
  - License text: `core/external/wepoll/license.txt`
  - License type: BSD-2-Clause style

- Unity test framework
  - Location: `core/external/unity/`
  - License text: `core/external/unity/license.txt`
  - License type: MIT

- SHA1 implementation (WIDE Project)
  - Location: `core/external/sha1/`
  - License text: `core/external/sha1/license.txt`
  - License type: BSD-3-Clause style

## Bundled runtime binaries in bindings

The following folders include prebuilt runtime binaries for convenience:

- `bindings/node/prebuilds/`
- `bindings/dotnet/runtimes/*/native/`
- `bindings/java/src/main/resources/native/`
- `bindings/cpp/native/`
- `bindings/python/src/zlink/native/`

These artifacts may include components such as:

- zlink/libzlink binaries
- Microsoft Visual C++ runtime DLLs (Windows targets)
- OpenSSL runtime libraries, depending on package/target

When redistributing these artifacts, ensure the corresponding third-party
license obligations are met for your target platform and package format.

## Project License

Unless otherwise noted, project source code is licensed under MPL-2.0.
See `LICENSE`.
