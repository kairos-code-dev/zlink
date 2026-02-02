# vcpkg Overlay Port (zlink)

This repo provides an overlay port under `vcpkg/ports/zlink`.

## Using the overlay
```bash
vcpkg install zlink --overlay-ports=./vcpkg/ports
```

## Updating for a new release
1) Create a release tag and GitHub release.
2) Download the source tarball (or use `git archive`).
3) Update `REF` and `SHA512` in `vcpkg/ports/zlink/portfile.cmake`.
4) Update `version-string` in `vcpkg/ports/zlink/vcpkg.json`.
