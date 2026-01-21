# Security mechanism removal summary (CURVE/GSSAPI)

## Scope
- Remove CURVE and GSSAPI implementation, docs, tests, and build/packaging hooks.
- Keep NULL and PLAIN mechanisms; TLS is the supported security transport.

## Deleted files
### GSSAPI implementation
- src/gssapi_client.cpp
- src/gssapi_client.hpp
- src/gssapi_mechanism_base.cpp
- src/gssapi_mechanism_base.hpp
- src/gssapi_server.cpp
- src/gssapi_server.hpp

### Docs (CURVE/GSSAPI)
- doc/zmq_curve.txt
- doc/zmq_curve_keypair.txt
- doc/zmq_curve_public.txt
- doc/zmq_gssapi.txt

### Tests and tools
- tests/testutil_security.cpp
- tests/testutil_security.hpp
- tools/curve_keygen.cpp

### Build scripts
- builds/cmake/Modules/Findsodium.cmake
- builds/fuzz/ci_build.sh
- config.sh

## API/option removals
- include/zmq.h: removed ZMQ_GSSAPI* constants and ZMQ_GSSAPI mechanism id.
- doc/zmq_setsockopt.txt: removed CURVE and GSSAPI options.
- doc/zmq_getsockopt.txt: removed CURVE and GSSAPI queries.
- doc/zmq_has.txt: removed curve and gssapi capabilities.
- doc/zmq.txt: removed CURVE mechanism and key helper links.
- doc/zmq_z85_encode.txt / doc/zmq_z85_decode.txt: replaced CURVE-specific examples.

## Build/packaging cleanup
- Dockerfile: removed libsodium/krb5 deps and configure flags.
- ci_deploy.sh: removed CURVE/libsodium gating.
- packaging/debian/rules: removed libsodium/krb5 configure flags.
- packaging/debian/control: removed libsodium/krb5 build deps.
- packaging/debian/zeromq.dsc: removed libsodium/krb5 build deps.
- packaging/redhat/zeromq.spec: removed libsodium/krb5 toggles and curve_keygen packaging.
- builds/cmake/platform.hpp.in: removed CURVE/libsodium defines.
- builds/mingw32/platform.hpp: removed ZMQ_USE_LIBSODIUM.
- builds/gyp/platform.hpp: removed ZMQ_HAVE_CURVE.

## Tests and build verification
- Command: ./build.sh
- Result: build success
- Tests: 61 total, 0 failed
- Skipped tests: test_connect_null_fuzzer, test_bind_null_fuzzer, test_connect_fuzzer, test_bind_fuzzer
- Build dir: build/
