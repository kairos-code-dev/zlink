[English](README.md) | [한국어](README.ko.md)

[Spec Index](https://zlink.systems/core/spec/) · [Bindings Policy](../README.md)

# C++ Binding Final Structure

This document defines the completed C++ library shape after the binding
refactor. It is not an exhaustive list of every method. The concrete public
contract is
`bindings/cpp/include/zlink/Contracts/`.

In the completed structure, `Contracts/`, installed header projections, tests,
samples, perf runners, and runtime behavior all map the stable capabilities of
`core/include/zlink.h` into C++-idiomatic types.
`core/include/zlink.h` is the semantic source for supported native capability,
but it is not the public C++ header shape. The completed C++ public contract
does not expose C API identifiers, C structs, C callbacks, native handles, or
`ZLINK_*` macros as the user-facing contract.

This README is the C++ binding's final-state interpretation of the shared
policy in `../README.md`. The completed C++ binding does not keep old include
paths, alias methods, alternate projections, or wrapper layers only to preserve
the previous surface. This document is the acceptance standard for the
refactored C++ binding, not a progress report about the current tree.
Do not weaken this document to match a partial implementation. If the code and
this document disagree, the code is incomplete unless the user explicitly asks
to change the target design.

This binding follows the shared bindings architecture map with C++ naming:
`Contracts/` owns the installed public headers and `Runtime/` owns the private
implementation under `src/`. The folder names are repository organization, not
namespace segments that users should depend on.

## Public Contract Source

- Public contract: `bindings/cpp/include/zlink/Contracts/`.
- Runtime implementation: `bindings/cpp/src/Runtime/`.
- Public entrypoint projection: `bindings/cpp/include/zlink.hpp`.
- Installed projection: `bindings/cpp/include/zlink.hpp` and
  `bindings/cpp/include/zlink/Contracts/...`.
- Compiled library: the C++ binding builds and installs the C++ library target
  `zlink_cpp` in addition to the core native `zlink` library.
- Language baseline: C++20.
- Namespace: all public types live under `zlink`; service types live under
  `zlink::service`.
- Internal implementation: native bridge helpers, callback trampolines, request
  progress helpers, non-public `detail` helpers, private implementation
  headers, and `.cpp` files under `bindings/cpp/src/Runtime/`.
- Documentation role: this README defines the shape, boundaries, and required
  semantic coverage. `Contracts/` owns the exact member list; installed headers
  must project it intentionally.

## Non-Negotiable Acceptance Criteria

The refactor is not complete until every item below is true. Build success or
CTest success alone is not enough.

1. The repository layout matches the `Repository Layout` section in this
   document. That section is an exact target inventory for public contract
   headers and a minimum private runtime ownership map. It is not an example,
   suggestion, or naming inspiration.
2. Extra public files are rejected by default. A public file can remain only
   when this README first names it and explains the independent contract
   concept it owns. "It existed before the refactor" is not a contract purpose.
3. Extra runtime files are allowed only under `src/Runtime/` and only when they
   are private implementation support for a listed public contract.
4. `bindings/cpp/include/zlink/Contracts/` contains public C++ contract
   headers only. It does not contain implementation headers, native marshalling
   helpers, private state structs, or old compatibility folders.
5. No file under `bindings/cpp/include/zlink/Contracts/` includes a private
   runtime header or a private runtime include path.
6. No file under `bindings/cpp/include/zlink/Contracts/` includes `<zlink.h>`
   directly or indirectly as part of the C++ public contract design. If a
   generated or transitional native constant bridge is ever needed, it must be
   explicitly documented here before use.
7. No public C++ contract header exposes C API names as public types or member
   signatures. Public headers must not expose names such as `zlink_msg_t`,
   `zlink_routing_id_t`, `zlink_actor_ref_t`, `zlink_*_fn`,
   `zlink_*_option_t`, or raw `void *` native handles.
8. No public C++ contract header uses `ZLINK_*` macros as its public contract
   surface. Public C++ enums and constants must be C++ names. Their native
   values are verified in runtime/tests against `core/include/zlink.h`.
9. Every public contract header is self-contained. Including any single
   `bindings/cpp/include/zlink/Contracts/**/*.hpp` header in a C++20
   translation unit must compile without relying on prior includes.
10. Public samples, public tests, perf runners, and codec packages include only
   `<zlink.hpp>` or public `Contracts/...` headers. They do not include
   `src/Runtime/...` or any runtime helper path.
11. Samples, tests, perf runners, codec packages, and external applications link
   the compiled `zlink_cpp` target. They do not compile private runtime source
   files directly and do not link the core native library as a substitute for
   the C++ binding.
12. CMake install/export metadata exists for the public headers and compiled
    `zlink_cpp` target before the binding is considered package-complete.
13. The final review compares this document to the filesystem, CMake graph,
    public include graph, public C API leakage scan, public compatibility scan,
    build output, CTest output, samples, and perf smoke results. Any mismatch
    is a remaining issue, not a documentation adjustment opportunity.
14. The work is not complete while public headers still contain native C API
    storage, native conversion helpers, inline native calls, native callback
    signatures, or runtime implementation logic. Passing tests do not override
    this boundary.
15. The work is not complete while samples, tests, perf runners, or codec
    packages depend on private runtime paths or direct C API calls that a normal
    C++ binding user cannot call through the public C++ contract.
16. The work is not complete while the public layout has extra legacy
    categories, wrapper include paths, or compatibility aliases that are not
    deliberately listed in this document.
17. The work is not complete while any public header exists only because it was
    present before the refactor. Each public header must either be listed in
    `Repository Layout` or be added to this README with a short reason that
    explains the public concept it owns.
18. The work is not complete while `zlink_cpp` is only a convenience target for
    tests. It must be the application-facing C++ binding target used by
    samples, tests, perf runners, codecs, and install/export metadata.
19. The work is not complete while any audit command in this README reports a
    mismatch after the final source change. The final answer must include the
    exact remaining mismatch count or the zero-result evidence.
20. The work is not complete until a fresh review after the last edit confirms
    that samples and perf were updated to the same public include/link model as
    tests.
21. The work is not complete while C API headers are copied, generated, or
    vendored under `bindings/cpp/include/` as part of the C++ installed include
    projection. The C++ public include tree contains `<zlink.hpp>` and
    `zlink/Contracts/...` only. The core C header remains the native semantic
    source, but it is consumed through the core build/include dependency, not
    mixed into the C++ public header layout.
22. The work is not complete while the C++ binding does not build from a clean
    CMake graph after the final source change. A broken build is a hard blocker,
    even when layout scans look correct.
23. The work is not complete while public headers expose `compat`,
    `compatibility`, `legacy`, `shim`, or alias namespaces or names that exist
    only to preserve the pre-refactor API. If a compatibility surface is ever
    reintroduced, this README must first define it as a deliberate public
    package boundary. Otherwise it is a mismatch.

Do not mark an implementation goal complete until the audit commands below, the
build, CTest, sample smoke, and relevant perf smoke all pass after the final
code change. A previous passing result is stale once any public header, runtime
source, CMake file, sample, test, or perf runner changes.

Recommended audit commands:

```sh
find bindings/cpp/include/zlink/Contracts -type f | sort
find bindings/cpp/src/Runtime -type f | sort
find bindings/cpp/include -type f -name '*.h' | sort
rg -n 'Runtime/|<Runtime/|zlink/Runtime' bindings/cpp/include/zlink/Contracts
rg -n '#include\\s*[<"]zlink\\.h|\\bzlink_[A-Za-z0-9_]+\\b|\\bZLINK_[A-Z0-9_]+\\b' \
  bindings/cpp/include/zlink/Contracts
rg -n '\\bcompat\\b|compat::|compatibility|legacy|shim|alias' \
  bindings/cpp/include/zlink/Contracts bindings/cpp/include/zlink.hpp
rg -n '#include\\s*[<"].*(src/Runtime|Runtime/|zlink/Runtime)|#include\\s*[<"]zlink\\.h[">]|#include\\s*[<"]zlink/[^">]*\\.h[">]|#include\\s*[<"]zlink_enum\\.h[">]|#include\\s*[<"]zlink_errno\\.h[">]|\\bzlink_[a-z][A-Za-z0-9_]*\\s*\\(' \
  bindings/cpp/samples bindings/cpp/tests bindings/cpp/perf
git diff --check -- bindings/cpp doc/spec/bindings/cpp/README.md
```

The private dependency scan above is the completion gate for samples, tests,
and perf. It checks private runtime includes, copied native C headers,
and direct lowercase C API function calls. Broader text searches for
`zlink_*`, `ZLINK_*`, or build variable names can be useful during
investigation, but they are not a completion count by themselves because tests
and CMake files may contain guard strings, target names, or diagnostic text.
When a broad search finds a line, classify it before counting it as a defect.

The public file list comparison must be exact. Use the `Repository Layout`
inventory as the expected public header set and count both missing files and
extra files. An extra compatibility header is a mismatch even when it only
includes another public header. A missing listed header is a mismatch even when
the same type is available through a different legacy header.

When reporting layout status, use these terms consistently:

- `missing public headers`: files named in `Repository Layout` that do not
  exist under `bindings/cpp/include/zlink/Contracts/`.
- `extra public headers`: files under `bindings/cpp/include/zlink/Contracts/`
  that are not named in `Repository Layout`.
- `runtime misplaced files`: private implementation files that still live under
  `bindings/cpp/include/zlink/Contracts/` or any installed public include path.
- `C API header copies`: `.h` native C API headers that still live under
  `bindings/cpp/include/` instead of the core C include tree.
- `private dependency hits`: samples, tests, perf, or codecs that include
  private runtime paths or call the C API directly instead of the public C++
  contract.
- `public compatibility hits`: public contract names, namespaces, forwarding
  headers, aliases, or shim terms that exist only to preserve the old C++
  binding surface.

All named mismatch counts and leakage counts must be zero before completion.
A build pass, a CTest pass, or an older audit result does not reduce those
counts.

The C API leakage scan above should return zero public contract hits. Any
exception requires an explicit subsection in this README that names the exact
identifier and explains why no C++ contract representation is possible. An
undocumented scan hit is a defect.

The compatibility scan above should return zero public contract hits. A
namespace such as `compat`, a public alias kept only for a previous name, or a
forwarding header that only redirects to the new layout is a defect unless this
README first defines it as an intentional public compatibility package. This
refactor currently has no such package.

Every public contract header must also pass a standalone include check:

```sh
tmpdir=$(mktemp -d)
while IFS= read -r header; do
  rel=${header#bindings/cpp/include/}
  printf '#include <%s>\nint main(){return 0;}\n' "$rel" > "$tmpdir/test.cpp"
  c++ -std=c++20 -Icore/include -Icore/external/boost \
    -Ibindings/cpp/include -fsyntax-only "$tmpdir/test.cpp" || exit 1
done < <(find bindings/cpp/include/zlink/Contracts -type f -name '*.hpp' | sort)
rm -rf "$tmpdir"
```

The final implementation review must use a loop, not a single pass:

1. Run the layout and leakage scans.
2. Fix every mismatch against this document.
3. Rebuild `zlink_cpp`, tests, samples, perf targets, and codec targets that are
   affected by the change.
4. Run CTest and the sample smoke set.
5. Run relevant perf smoke for changed hot paths and compare meaning against
   `bindings/c/perf`.
6. Repeat from step 1 until there are no document/layout/API/build/test/sample
   or perf-smoke mismatches.

If any item remains, report it as remaining work. Do not call the goal complete
and do not commit/push the partial state as a finished refactor.

Completion is a current-state claim, not a progress claim. It is invalid to
mark the goal complete because many files were moved, because the build reached
a later failure, because one audit category reached zero, or because a previous
turn reported success. Completion requires all evidence in the same final
review cycle after the last source change.

The review must report the concrete evidence. A final status that only says
"build passed" or "tests passed" is insufficient. The report must include the
layout mismatch count, public C API leakage result, runtime include leakage
result, public compatibility scan result, private dependency scan result for
samples/tests/perf/codecs, build command, CTest command, sample smoke command,
and perf smoke command used after the last source change.

Completion is invalid if any required evidence is stale. Evidence becomes stale
after any change to a public header, runtime source, CMake file, codec, sample,
test, perf source, or this README. In that case the affected audit, build, and
runtime checks must be rerun and the final report must use the rerun result.

C++ is no longer modeled as a header-only binding. Do not create a second
`bindings/cpp/src/zlink/Contracts/` tree. Public contracts stay in installed
headers, and implementation moves behind `.cpp` files and private runtime
headers. The contract/runtime split remains: `Contracts/` declares the user
surface, while `src/Runtime/` contains implementation support for that surface.
Do not copy a Java or .NET interface-heavy layout into C++; C++ uses installed
headers, RAII classes, concrete values, and opaque implementation state as its
natural boundary.

C++20 is the minimum supported language level for the bindings library. The
bindings library may expose `async_result_t<T>` completion objects and callback
submit, but it does not own coroutine awaiters, the framework handler executor,
or the framework dispatcher. Framework coroutines wrap the bindings completion
object or callback completion at the framework execution boundary. The shared
async execution surface policy is defined in
[bindings async execution surface policy](../async-coroutine-policy.ko.md).

## Repository Layout

The completed C++ binding uses these paths consistently. The file names below
are the target ownership map and acceptance inventory. Public contract files
must match this list exactly unless this section is updated first with a clear
contract reason. A category can split further only when a public concept or
runtime responsibility has an independent reason to change, and that reason
must be added to this section before the implementation is accepted.

```text
bindings/cpp/
+-- CMakeLists.txt
+-- include/
|   +-- zlink.hpp
|   +-- zlink/
|       +-- Contracts/
|       |   +-- Core/
|       |   |   +-- capability.hpp
|       |   |   +-- context.hpp
|       |   |   +-- context_options.hpp
|       |   |   +-- routing_id.hpp
|       |   |   +-- utilities.hpp
|       |   +-- Messaging/
|       |   |   +-- message.hpp
|       |   |   +-- received.hpp
|       |   |   +-- topic_message.hpp
|       |   |   +-- subscription_event.hpp
|       |   |   +-- operation_contracts.hpp
|       |   |   +-- request_result.hpp
|       |   +-- Sockets/
|       |   |   +-- socket_contracts.hpp
|       |   |   +-- message_socket_contracts.hpp
|       |   |   +-- routed_socket_contracts.hpp
|       |   |   +-- pubsub_socket_contracts.hpp
|       |   |   +-- stream_socket.hpp
|       |   |   +-- socket_options.hpp
|       |   |   +-- results.hpp
|       |   +-- Eventing/
|       |   |   +-- monitor.hpp
|       |   |   +-- poller.hpp
|       |   |   +-- poll_event.hpp
|       |   |   +-- timers.hpp
|       |   |   +-- events.hpp
|       |   |   +-- status.hpp
|       |   +-- Service/
|       |   |   +-- spot_node.hpp
|       |   |   +-- spot.hpp
|       |   |   +-- actor.hpp
|       |   |   +-- spot_node_models.hpp
|       |   |   +-- actor_models.hpp
|       |   |   +-- operation_contracts.hpp
|       |   +-- Errors/
|       |       +-- errors.hpp
|       |       +-- results.hpp
+-- src/
|   +-- Runtime/
|       +-- zlink_cpp.cpp
|       +-- Core/
|       |   +-- capability.cpp
|       |   +-- context.cpp
|       |   +-- utilities.cpp
|       |   +-- operation_detail.hpp
|       |   +-- runtime_helpers.hpp
|       |   +-- types_impl.hpp
|       +-- Messaging/
|       |   +-- message.cpp
|       +-- Errors/
|       |   +-- error.cpp
|       +-- Eventing/
|       |   +-- monitor.cpp
|       |   +-- poller.cpp
|       |   +-- timers.cpp
|       +-- Sockets/
|       |   +-- base_socket.cpp
|       |   +-- pair.cpp
|       |   +-- dealer.cpp
|       |   +-- pubsub.cpp
|       |   +-- router.cpp
|       |   +-- stream.cpp
|       |   +-- detail.hpp
|       +-- Options/
|       |   +-- socket_options.cpp
|       +-- Service/
|       |   +-- actor.cpp
|       |   +-- actor_ops.cpp
|       |   +-- detail.hpp
|       |   +-- request_reply.cpp
|       |   +-- spot.cpp
|       |   +-- spot_node.cpp
|       |   +-- actor_detail.hpp
|       |   +-- spot_state.hpp
|       |   +-- spot_submit.hpp
|       +-- Native/
|           +-- socket_handle.hpp
|           +-- native_message_parts.hpp
|           +-- native_parts.hpp
|           +-- native_options.hpp
|           +-- native_send_result.hpp
+-- native/
+-- samples/
+-- tests/
+-- perf/
```

`CMakeLists.txt` defines the compiled C++ binding target `zlink_cpp` and links
it to the core native `zlink` library. Samples, tests, perf binaries, and
applications link that target instead of compiling private
runtime source files directly.

`Contracts/` is the installed public contract surface under
`bindings/cpp/include/zlink/`. `Runtime/` is private implementation support
under `bindings/cpp/src/Runtime/`. The `zlink` namespace and `zlink.hpp` are
the C++ projection of the contract. Do not expose `Contracts` or `Runtime` as
namespace segments.

The completed C++ include projection does not contain native C API headers
such as `bindings/cpp/include/zlink.h`, `bindings/cpp/include/zlink/*.h`,
`bindings/cpp/include/zlink_enum.h`, or `bindings/cpp/include/zlink_errno.h`.
Those files are not C++ contract headers and are not accepted as convenience
copies. If the C++ runtime needs the native C API, it includes it from the core
native include dependency inside `src/Runtime/` implementation files.

Runtime helper headers are not public contract API. Public samples, perf, and
tests include `<zlink.hpp>` and link the C++ binding library; they do not include
runtime helper paths.
Wrapper headers such as `include/zlink/message.hpp`,
`include/zlink/services/spot.hpp`, or `include/zlink/sockets/dealer.hpp` are
not part of the completed layout. The completed tree does not replace them with
forwarding headers.

Public headers such as `base_socket.hpp`, `dealer.hpp`, `router.hpp`,
`publisher_socket.hpp`, `subscriber_socket.hpp`, `spot_common.hpp`,
`spot_node_ops.hpp`, or `spot_socket_ops.hpp` are not accepted as leftover
implementation buckets. They are examples of public files that must either be
removed, merged into the listed category contract headers, or first added to
the `Repository Layout` inventory with a clear public contract purpose.

Monitor, poller, and timer contracts live under the shared `Eventing/`
category. `Contracts/Monitoring/` is not part of the completed public contract,
and the completed tree does not keep `Monitoring/` forwarding headers.

File granularity follows the common policy in `../README.md`: keep one file
per independent public concept or tight operation/model group. Very small
marker, delegate, enum, or pass-through helper files should be merged into the
nearby contract file when that makes the public shape easier to read.

## .NET Contract Category Projection

The C++ binding uses the `.NET` public contract category layout as the
classification standard. This is a category and responsibility projection, not
a C# shape copy. C++ keeps C++20 naming, headers, RAII facades, move semantics,
and concrete values.

The `Repository Layout` section intentionally follows the .NET contract
classification: Core, Messaging, Sockets, Eventing, Service, and Errors. The
.NET source of truth is the [.NET binding blueprint](../dotnet/README.md),
especially its Contract Folder Layout and Runtime Folder Layout sections. This
C++ README defines the C++20 projection of those categories, so file names and
types may differ when C++ ownership, RAII, move-only resources, or performance
require a different shape.

The category ownership is strict. A public C++ type should not move to a
different category just because the runtime implementation is easier to place
elsewhere. Runtime helper code can split further under `src/Runtime/`, but the
public contract owner remains the corresponding category.

This projection is not strict for C# interface style. .NET socket role
interfaces identify socket roles in the public contract. They do not require
C++ to expose `isocket_t`, `istream_socket_t`, `ISocket`, or `IStreamSocket` by
default. Use concrete RAII facades unless users need true substitutable
behavior. If a substitutable role is required, keep the interface narrow and
keep send, receive, poll, and dispatch hot paths free of avoidable virtual
dispatch.

## Public Contract At A Glance

The completed C++ binding makes the public contract visible without adding
interface-only layers. A user can start at `<zlink.hpp>`, then follow this map
to the owning contract header.

- Core: `context_t`, context options, routing id, version/capability helpers,
  `atomic_counter_t`, `stopwatch_t`, and `thread_t` live in
  `Contracts/Core/`.
- Messaging: `message_t`, `received_t`, `topic_message_t`,
  `subscription_event_t`, and multipart helpers live in
  `Contracts/Messaging/`.
- Sockets: `pair_socket_t`, `dealer_socket_t`, `router_socket_t`,
  `pub_socket_t`, `sub_socket_t`, `xpub_socket_t`, `xsub_socket_t`,
  `stream_socket_t`, stream-bound actor snapshots, and
  send/recv/request/reply builders live in `Contracts/Sockets/`.
- Eventing: `socket_monitor_t`, monitor events, poller, one-shot `poll(...)`,
  poll event, timer, and
  readiness helpers live in `Contracts/Eventing/`.
- Service: `spot_node_t`, `spot_t`, `actor_ref_t`, actor lifecycle models, and
  service operation builders live in `Contracts/Service/`.
- Errors: public exception and result-domain types live in `Contracts/Errors/`.

The map above is the public API index. It is the C++ equivalent of a contract
surface overview; it does not imply `IContext`, `ISpot`, `IActor`, or similar
abstract interfaces. Public resource objects remain concrete RAII facades unless
callers need true substitutable behavior. Narrow interfaces are allowed only for
roles that users naturally replace, such as codecs, callbacks, handlers, or poll
targets.

The public contract is read in two steps:

1. Start at `<zlink.hpp>` to see the public contract categories included by the
   C++ binding.
2. Open the owning `Contracts/...` header to inspect the concrete public type
   and its public member list.

For example, the completed SPOT surface is visible as a concrete facade, not as
an interface/implementation pair:

```cpp
namespace zlink::service {

class spot_t {
public:
    spot_t(spot_t&&) noexcept = default;
    spot_t(const spot_t&) = delete;

    send_operation_t send();
    reply_operation_t reply();
    int recv(received_t& out, recv_flags_t flags = recv_flags_t::none);
    void set_send_ready_handler(std::function<void()> handler);
    void close();
};

} // namespace zlink::service
```

`zlink.hpp` acts as the public table of contents for those facades:

```cpp
#include "zlink/Contracts/Core/context.hpp"
#include "zlink/Contracts/Core/context_options.hpp"
#include "zlink/Contracts/Core/routing_id.hpp"
#include "zlink/Contracts/Core/capability.hpp"
#include "zlink/Contracts/Messaging/message.hpp"
#include "zlink/Contracts/Messaging/received.hpp"
#include "zlink/Contracts/Messaging/topic_message.hpp"
#include "zlink/Contracts/Messaging/subscription_event.hpp"
#include "zlink/Contracts/Messaging/operation_contracts.hpp"
#include "zlink/Contracts/Sockets/message_socket_contracts.hpp"
#include "zlink/Contracts/Sockets/routed_socket_contracts.hpp"
#include "zlink/Contracts/Sockets/pubsub_socket_contracts.hpp"
#include "zlink/Contracts/Eventing/poll_event.hpp"
#include "zlink/Contracts/Eventing/poller.hpp"
#include "zlink/Contracts/Service/spot_node.hpp"
#include "zlink/Contracts/Service/spot.hpp"
#include "zlink/Contracts/Service/actor.hpp"
#include "zlink/Contracts/Errors/errors.hpp"
```

Runtime details stay behind the facade. Public headers may name opaque
implementation state, but they must not expose native handles, callback
trampolines, part loops, request pumps, or marshalling helpers:

```cpp
namespace zlink::service {

class spot_t {
public:
    spot_t(spot_t&&) noexcept;
    spot_t(const spot_t&) = delete;
    ~spot_t();

    send_operation_t send();
    reply_operation_t reply();
    void close();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace zlink::service
```

This structure keeps the public surface easy to scan while preserving C++
ownership semantics. `spot_t`, `spot_node_t`, and `actor_ref_t` are the contract;
`src/Runtime/...` and private `zlink::detail` helpers are implementation
support.

## Refactor Recovery Rules

If a previous implementation attempt partially moved files, the next attempt
starts by measuring the current tree against this README. Do not assume the
current `bindings/cpp` tree is closer to completion just because it builds.

If a previous attempt claimed completion while the layout, public/private
boundary, samples, perf, or codec integration still differed from this README,
treat that claim as a process failure, not as design evidence. The recovery task
is to re-audit from the filesystem and source graph, then continue from the
actual mismatches. Do not defend or preserve a partial shape just because it was
introduced by an earlier refactor attempt.

The first report in a resumed refactor must state that the current tree is
being audited against this document. It must not claim the layout is complete
until the audit has compared actual public files, runtime files, include
dependencies, and CMake link targets to the requirements above.

The resumed refactor must begin with a current-state report in this shape:

```text
Current C++ binding refactor audit:
- missing public headers: <count>
- extra public headers: <count>
- runtime misplaced files: <count>
- C API header copies: <count>
- public C API leakage hits: <count>
- runtime include leakage hits: <count>
- public compatibility hits: <count>
- private dependency hits in samples/tests/perf/codecs: <count>
- build status: <not run | pass | fail, with first failing target>
- CTest status: <not run | pass | fail, with failing test>
- sample smoke status: <not run | pass | fail>
- perf smoke status: <not run | pass | fail>
```

If any count is nonzero, the report must say `refactor incomplete`. If any
required command has not been run after the last edit, the report must say
`evidence stale` or `evidence missing`. Do not summarize a nonzero audit as
"mostly done" when deciding whether the goal can close.

Use this order:

1. Capture `git status --short` and scope the work to `bindings/cpp` plus this
   README unless the user explicitly expands the scope.
2. List public contract files and compare them to `Repository Layout`.
3. List runtime files and confirm every implementation helper lives under
   `src/Runtime/`.
4. List native `.h` files under `bindings/cpp/include/`. Any result is a
   misplaced C API header copy unless this README later defines a separate C
   compatibility package.
5. Run the C API leakage scan on `Contracts/` before editing. The scan result
   is the initial defect list, not a warning to ignore.
6. Run the public compatibility scan on `Contracts/` and `zlink.hpp` before
   editing. Any hit is a real defect unless this README first defines an
   intentional compatibility package.
7. Run the private dependency scan on samples, tests, perf, and codecs before
   editing. Any private include or direct C API dependency is part of the
   defect list.
8. Remove or relocate public headers that are only old implementation buckets.
9. Move native calls, callback trampolines, native storage, and native
   conversion helpers into `.cpp` files or private runtime headers.
10. Update CMake so `zlink_cpp` is the target used by samples, tests, perf,
   codecs, and install/export metadata.
11. Rebuild and rerun the audit loop after every batch.

Partial states are acceptable only as intermediate working states. They are not
acceptable as a committed or pushed completed refactor.

If the tree does not compile, stop treating higher-level validation as current.
Fix the build first, then rerun the layout and leakage scans. A stale passing
CTest result from before the last edit is not evidence for the current tree.

Do not change files outside `bindings/cpp` and this README unless the audit
shows a direct dependency that must change for the C++ binding to build or run.
When another binding subtree is already dirty, leave it untouched and do not
stage, revert, or explain it as part of C++ completion.

## Implementation Restart Prompt

Use the following prompt when restarting this refactor in a fresh agent or
goal. The prompt is intentionally strict because partial build success is not
evidence of completion.

```text
Work in /home/hep7/project/kairos/zlink.

Refactor bindings/cpp to match doc/spec/bindings/cpp/README.md exactly.
The README is the final-state acceptance standard, not a description of the
current implementation. C++20 is the baseline. Compatibility with the old C++
binding surface is not required.

Create or continue a goal only for this objective. Do not mark it complete
until fresh evidence after the last source change proves every required count
is zero and all required build/test/sample/perf checks have passed. A build
pass, CTest pass, or previous audit result is not completion by itself.

Do not weaken the README to match the current code. If code and README differ,
the implementation is incomplete unless the user explicitly changes the target
design.

Keep the contract/runtime split:
- public installed headers live under bindings/cpp/include/zlink/Contracts
- the public entrypoint is bindings/cpp/include/zlink.hpp
- private implementation lives under bindings/cpp/src/Runtime
- public code links the compiled zlink_cpp target

Do not mix C API headers or native runtime helpers into Contracts. Public
Contracts headers must not include <zlink.h>, expose zlink_* identifiers,
ZLINK_* macros, native callback signatures, raw native handles, native storage,
or native conversion helpers.

Start by auditing, not editing:
1. Capture git status for bindings/cpp and doc/spec/bindings/cpp/README.md.
2. Compare the actual public contract file list with the README Repository
   Layout inventory. Report missing public headers and extra public headers.
3. Compare runtime files with src/Runtime ownership. Report misplaced runtime
   files if any implementation helper remains in a public include path.
4. Report every native .h file under bindings/cpp/include as a C API header
   copy mismatch. The completed C++ include tree contains zlink.hpp and
   zlink/Contracts/... only.
5. Run the README C API leakage scan on Contracts.
6. Run the README public compatibility scan on Contracts and zlink.hpp. Public
   compat, legacy, shim, or alias surfaces are defects unless this README first
   defines them as an intentional package boundary.
7. Run the README private dependency scan on samples, tests, perf, and codecs.
8. Try a build before broad edits if the tree may already be broken. If it is
   broken, report the first compiler error and fix it before claiming any check
   is current.
9. Report the concrete mismatch counts before the first code batch using this
   exact status shape:
   - missing public headers: <count>
   - extra public headers: <count>
   - runtime misplaced files: <count>
   - C API header copies: <count>
   - public C API leakage hits: <count>
   - runtime include leakage hits: <count>
   - public compatibility hits: <count>
   - private dependency hits in samples/tests/perf/codecs: <count>
   - build status: <not run | pass | fail>
   - CTest status: <not run | pass | fail>
   - sample smoke status: <not run | pass | fail>
   - perf smoke status: <not run | pass | fail>

Then fix in scoped batches:
1. Move implementation, native conversion, callback trampoline, request pump,
   and native handle logic out of public headers into .cpp files or private
   headers under src/Runtime.
2. Remove or merge public headers that are only legacy implementation buckets.
3. Update zlink.hpp, CMake, samples, tests, perf, and codecs to use the public
   Contracts surface and link zlink_cpp.
4. Preserve performance: do not add avoidable hot-path virtual dispatch,
   heap allocation, std::function rebuilding, hidden waits, sleeps, broad
   locks, or unnecessary copies.
5. Repeat audit -> fix -> build -> test -> sample smoke -> perf smoke until
   there are no README/layout/API/build/test/sample/perf mismatches.

Required final evidence after the last source change:
- missing public header count and extra public header count
- misplaced runtime file count
- C API header copy count under bindings/cpp/include
- public C API leakage scan result
- runtime include leakage scan result
- public compatibility scan result
- private dependency scan result for samples/tests/perf/codecs
- cmake build command/result for zlink_cpp and affected targets
- CTest command/result
- sample smoke command/result
- relevant perf smoke command/result and comparison meaning against
  bindings/c/perf

Do not mark the goal complete, commit, or push while any mismatch remains.
If any check cannot be run, report the refactor as incomplete and explain the
blocker instead of substituting an older result.

If the implementation becomes difficult to finish in one turn, leave a precise
handoff note with the latest audit counts, the first failing compiler/test
error, and the next file to fix. Do not convert that handoff into goal
completion.
```

## Core Capability Ownership

Every stable core capability exposed by C++ follows these ownership rules:

1. Add the public type or method to the correct
   `bindings/cpp/include/zlink/Contracts/` category.
2. Update `bindings/cpp/include/zlink.hpp` and any deliberate installed
   projection header.
3. Decide the C++ domain owner: context, message, socket, monitor, timer,
   service, SPOT, actor, error, or option.
4. Keep raw C handle access, `*_part` loops, callback userdata, trampoline
   state, and native marshalling helpers in `src/Runtime/` headers and `.cpp`
   files.
5. Add public-header tests and at least one sample/perf update when the new
   capability affects user workflows or measurement.
6. Check that the new public API is not just a shallow C wrapper. If it only
   forwards without improving ownership, validation, or shape, keep it
   internal.
7. Obsolete public names are absent. Deprecated aliases, forwarding overloads,
   and alternate public headers are absent unless a later document explicitly
   changes this C++ policy.

For explicit Spot routing-id acquisition, the C++ binding exposes
`spot_node_t::get_or_create_spot(routing_id_t)` and maps it directly to
`zlink_spot_node_spot_get_or_new(...)`. It returns the owned `spot_t` facade
and the creation flag. Do not implement this behavior by composing
`spot_lookup()` and `create_spot()`.

## Library Shape

The C++ binding should feel like a small native C++ library over the core C
contract.

- Public resource objects are RAII classes that own or borrow native handles
  according to their documented lifetime.
- Destructors release resources without requiring callers to know native close
  sequencing. Resource destructors and other non-trivial methods are defined
  out-of-line in `.cpp` files.
- Small value-type operations may remain inline when they do not expose native
  ownership, callback state, request state, or marshalling details.
- Public methods use `snake_case`.
- Public value types such as message, routing id, received metadata, topic
  message, result, error, enum, and option types stay concrete.
- Public resource headers use opaque implementation state, such as Pimpl, when
  native handle layout, callback state, request state, or ABI-sensitive storage
  would otherwise leak into the contract.
- Use templates, overloads, and move semantics only when they simplify caller
  ownership or avoid copies. Do not expose template machinery as a substitute
  for a clear domain type.
- Use virtual interfaces only when callers need substitutable behavior. Do not
  wrap every handle in an abstract interface by default.
- Operation builders are required for multipart send, publish, request, reply,
  actor, and SPOT operations so native request state stays hidden and ownership
  stays clear.

## Contract / Runtime Placement Rules

- Public declarations and user-visible behavior belong in `Contracts/`.
- Public free functions, static helpers, extension-style helpers, and builder
  convenience helpers belong in `Contracts/` when users can call them directly.
- Runtime handle owners, socket kernels, request pumps, callback trampolines,
  and part-loop helpers belong in `src/Runtime/`.
- FFI declarations, raw C handles, native struct mirrors, marshalling helpers,
  and platform loading code belong in `src/Runtime/Native/`.
- `zlink.hpp` must project `Contracts/`, not make `Runtime/` helper paths the
  public include style.
- Contract headers must not include private runtime headers. If a public class
  needs implementation state, expose only an incomplete `impl` type or another
  opaque private member and define the behavior in `.cpp`.
- Runtime concrete classes are not user entry points. If behavior is public, it
  is declared by `Contracts/` and implemented through `src/Runtime/`.

## C API Boundary Rules

The C++ binding is a C++ projection of the native C contract. It is not a C
header re-export with C++ method names.

- `core/include/zlink.h` remains the semantic source for native capability,
  result values, and supported operations.
- `Contracts/` must expose C++ names, C++ value types, C++ resource facades,
  and C++ operation builders.
- `Contracts/` must not expose C native storage or C callback machinery in
  public signatures, fields, base classes, or template parameters.
- `Contracts/` must not require users to include or understand `<zlink.h>`.
  A user including `<zlink.hpp>` or any single `Contracts/...` header should
  see a C++ API, not native C ABI details.
- Native conversions such as `zlink_msg_t`, `zlink_routing_id_t`,
  `zlink_actor_ref_t`, `zlink_monitor_event_t`, and `zlink_*_result_t`
  construction belong in `src/Runtime/` or in private helper functions.
- Public C++ enums may preserve native numeric values, but the public header
  should not use `ZLINK_*` macros as the visible contract. Use tests or runtime
  static assertions to verify that C++ values remain aligned with
  `core/include/zlink.h`.
- Public resource classes must not store raw native handles in their public
  layout. Use opaque implementation state and define non-trivial behavior
  out-of-line.
- Public contract tests must include a C API leakage scan. If a C API
  identifier appears in `Contracts/`, either remove it or document a precise
  exception in this section before accepting it.

The C++ folders mirror the .NET standard classification with C++ naming and
RAII/Pimpl idioms.

- `Contracts/Core`: context, routing id, version/capability helpers, and
  package-level facade declarations.
- `Contracts/Messaging`: message, received metadata, topic/subscription
  payloads, and operation payload contracts.
- `Contracts/Sockets`: socket resource facades, socket operation builders,
  socket options, send/recv/request/reply/publish surfaces.
- `Contracts/Eventing`: monitor, poller, poll events, timer, and event handler
  contracts.
- `Contracts/Service`: SpotNode, Spot, Actor, topology,
  and service operation builders.
- `Contracts/Errors`: public error/result domains.
- `src/Runtime/Core`: context implementation and runtime facade support.
- `src/Runtime/Handles`: native handle ownership, close state, lifetime
  checks, and reference tracking.
- `src/Runtime/Messaging`: message materialization, multipart progress, and
  request progress.
- `src/Runtime/Buffers`: byte buffer ownership, copy/borrow policy, pooled or
  pinned storage, and routing-id codecs.
- `src/Runtime/Sockets`: socket kernels and socket family implementations.
- `src/Runtime/Eventing`: monitor, poller, poll event, timer, and dispatch
  loop implementations.
- `src/Runtime/Service`: SpotNode, Spot, Actor, topology,
  and service operation implementations.
- `src/Runtime/Options`: option validation and native option id/value mapping.
- `src/Runtime/Errors`: native errno/result conversion into public error
  domains.
- `src/Runtime/Native`: C ABI declarations, native type mirrors, symbol
  loading, and marshalling helpers.

## Build And Packaging Policy

Moving C++ off header-only means the binding has an additional compiled
artifact. The completed binding therefore maintains these build rules:

- The C++ binding builds the library target `zlink_cpp`.
- The C++ binding is built as C++20. Do not add framework executor parameters,
  framework dispatcher parameters, or alternate operation-start names solely for
  coroutine support.
- `zlink_cpp` links against the core native `zlink` library and has a version
  compatibility rule with that core library.
- Linux, macOS, and Windows packages build the C++ library for each supported
  architecture and runtime toolchain.
- CMake install/export metadata must let applications consume both public
  headers and the compiled C++ binding target.
- Samples, tests, and perf runners link the same installed-style C++ target
  that applications use. They must not depend on private runtime source paths.
- Runtime search paths, DLL lookup rules, and packaged native artifacts must be
  tested because applications now load both the core native library and the C++
  binding library.
- Public headers avoid exposing ABI-sensitive implementation storage. Public
  method signatures may remain C++-idiomatic, but native handle layout, callback
  state, request state, and marshalling buffers stay out of installed headers.
- `zlink_cpp` packages are scoped to a supported compiler, standard library,
  runtime, platform, and architecture combination. A package built with one C++
  runtime ABI is not assumed compatible with another.
- Pimpl hides object layout and private native state. It does not make every
  public C++ signature ABI-neutral; STL types, exceptions, allocators, and
  inline public functions still follow the package's supported C++ ABI.

## Contract Folder Layout

`Contracts/` is the source ownership map for public C++ declarations.
`zlink.hpp` projects these categories into the `zlink` namespace.

- `Core/`: context, context options, routing id, utility resources, and public
  free functions such as version or capability helpers.
- `Messaging/`: message, received metadata, topic messages, subscription
  events, stream packet callbacks, and builder payload helpers. Codec helpers
  are not part of the C++ binding package; framework-level serialization lives
  in framework codec extensions.
- `Sockets/`: socket behavior, socket families, typed options, request/reply,
  and publish/subscribe surfaces.
- `Eventing/`: monitor, monitor snapshot/event, poller, poll event, timer, and
  public poll helpers.
- `Service/`: SPOT node, SPOT handle, topology models,
  actor refs, actor lifecycle, and operation builders.
- `Errors/`: exception or typed error-result domains.
- Enum, flag, and result types live in the category that defines their meaning.
  Do not create an `Enums/` folder just to group declarations by syntax.

## Canonical Interface Rules

- Data-plane `recv`, routed recv, subscribe, and subscription-event receive use
  caller-provided output storage such as `received_t&`,
  `topic_message_t&`, or `subscription_event_t&`.
- Send, routed send, publish, request, reply, SPOT operations, and Actor
  location/session operations return move-only fluent builders.
- Builder start methods take only the target identity, topic, channel, routing
  id, or request sequence. Payload, flags, timeout, callback, and async submit
  choices are builder steps.
- SPOT channel-targeted operations use `send_to_channel(...)` and
  `request_to_channel(...)`. SPOT topic publish stays `publish(topic)`.
- Handler registration methods use `set_..._handler` names. For example, send
  readiness uses `set_send_ready_handler(...)`, raw STREAM packet handling uses
  `set_packet_handler(...)`, monitor events use `set_monitor_handler(...)`, and
  SPOT dispatch uses `set_dispatch_handler(...)`.
- `on_...` names are not public registration methods in the completed C++ API.
  They are reserved for internal or protected hooks if such hooks are needed.
- Do not add single-payload shortcut overloads with the same name as an
  operation start method. `send(message)`, `send(routing_id, message)`,
  `publish(topic, message)`, `send_to_channel(channel, message)`, and
  `send_to_spot(..., message)` are not public contract members; callers use
  `send(...).message(message).submit()`.
- Multipart payload is accumulated by repeated `message(...)` calls.
  `messages(...)` convenience is allowed when it delegates to the same builder
  contract and is declared in `Contracts/`.
- Dealer sockets must not expose protocol envelope helpers such as
  `request_frame(...)` or `reply(request_token, parts)`. A dealer can start a
  request through `request()`, but it cannot reply to an arbitrary token
  because it has no API-level peer routing id.
- Do not add operation-start overload families such as `send_no_wait`,
  `publish_with_flags`, or `request_async`; keep one operation name and let
  the builder absorb the variation. Terminal builder method names follow
  [bindings async execution surface policy](../async-coroutine-policy.ko.md).
- Do not keep canonical-name bypasses such as `on_send_ready(...)`,
  `on_packet(...)`, `on_event(...)`, or operation aliases. Call sites use the
  canonical public contract instead of layered aliases.

## 64-bit Byte HWM And Monitoring Contract

HWM values and the Auto HWM planning unit use `byte_count_t`. This value type
stores only `uint64_t` bytes and makes the unit explicit through its
`bytes(...)` factory and `bytes()` accessor. The former `message_count_t` is
not retained as an alias or adapter. Zero means an unlimited HWM, and the
manual default is `4,096,000 bytes`.

```cpp
auto options = socket.options ();
options.send_hwm (zlink::byte_count_t::bytes (send_limit)); // Set the send-pipe byte HWM.
options.recv_hwm (zlink::byte_count_t::bytes (0));          // Zero is an unlimited receive HWM.

auto context_options = context.options ();
context_options.auto_hwm_msg_unit_bytes (
  zlink::byte_count_t::bytes (planning_unit)); // Supply the 64-bit Auto HWM planning input.
```

Monitor snapshots project Core monitoring ABI v2. Planned, applied, deferred,
and in-flight HWM fields use an `_bytes` suffix and `uint64_t`. Separate
booleans identify whether deferred fields are valid. Pending-message and
profile-slot values remain count diagnostics and do not share names with byte
fields.

## Capability Coverage

The public headers cover these groups in the completed C++ binding.

- Core: context, version/capability helpers, context options, shutdown,
  and auto-HWM recalculation.
- Messaging: message ownership, builder multipart input, received metadata, topic
  messages, subscription events, routing ids, and callback types.
- Socket families: pair, dealer, router, pub, sub, xpub, xsub, stream, common
  options, typed socket options, bind/connect/disconnect, TLS, callbacks, and
  request/reply surfaces.
- Eventing: socket monitor, monitor event, monitor snapshot, poller, poll
  event, timer, and readiness flags.
- Services: SPOT node, SPOT handle, topology snapshots,
  actor refs, actor lifecycle, and actor operations.
- Errors: typed exception or error-result surfaces that preserve the core
  result domains.

The C++ surface should not expose raw native handles, `*_part` loops, callback
userdata, internal inproc endpoints, or request pump objects as public concepts.

## Lifetime And Ownership

C++ callers should not have to reason about C handle cleanup.

- Resource classes release their native handle in their destructor and support
  explicit `close` or equivalent lifecycle methods when the handle can fail to
  close.
- Move-only resource classes are preferred over shared mutable handle
  ownership.
- Message values should support efficient move and explicit copy when copying
  is requested.
- `message_t::from(...)` creates an independent copy of caller bytes. For
  caller-owned buffers that must be sent without copying, the advanced
  `external_message_t::from(span, free_fn, hint)` overload transfers the buffer
  to the message and calls `free_fn(data, hint)` once when the message releases
  it.
- Data-plane receive and subscribe paths use caller-provided storage.
- Service control/admission receive paths such as Actor join request receive may
  use optional or typed result-return forms when that is clearer for C++ callers.
  They must still distinguish no-data from hard receive failure.
- Callbacks must keep native callback lifetime and user callable lifetime
  internally consistent.

## Error And Result Policy

The binding may use exceptions or typed result objects, but the public shape
must preserve core semantics.

- No-data and temporary backpressure remain distinct from hard failures.
- Request, submit, recv, bind, connect, config, handler, and close failures
  keep their result-domain meaning.
- `pollout` is a send-recovery readiness signal, not a generic writable bit.
- ROUTER/PUB defaults, SPOT HWM defaults, and SPOT dispatch worker semantics
  follow the core header.

## Performance Policy

- Build multipart values directly from the core part substrate.
- Use opaque implementation state, such as Pimpl, for resource and control
  objects. Do not add Pimpl to every small value. Message values, routing ids,
  flags, result enums, option values, and small snapshots should remain concrete
  and cheap to move or inspect.
- Trivial value operations may remain inline or `constexpr` in public contract
  headers when they do not expose native ownership, callback state, request
  state, or marshalling details.
- Moving behavior from headers into `.cpp` files must not add per-message
  virtual dispatch, avoidable `std::function` construction or copying,
  unnecessary heap allocation, or avoidable copies in send, receive, poll,
  timer, and dispatch loops.
- Operation builders may own runtime state, but builder steps should be
  move-only and reuse existing capacity when possible. Adding a payload part
  must not allocate solely because contract and runtime code are split.
- Native multipart conversion should prefer stack-backed small buffers and fall
  back to heap storage only when the part count or payload shape requires it.
- Callback registration may allocate or wrap user callables at registration
  time. Dispatch hot paths must reuse that stored state instead of rebuilding
  wrappers for every event or message.
- Avoid hidden waits, sleeps, busy waits, broad locks, and joins in hot paths.
- Perf and samples must include installed public headers only.
- Perf and samples must link the public C++ binding target, not private runtime
  object files or helper source directories.
- The C++ perf meaning must match `bindings/c/perf`: same pattern semantics,
  same transport meaning, same client-count policy, and no private fast path.
- After moving hot-path code into `.cpp` files, validation must include public
  header compilation, link/run against `zlink_cpp`, sample smoke, and relevant
  C++ perf smoke. Compare changed hot-path measurements with the C perf baseline
  before treating the refactor as complete.
- Perf runners should print or otherwise verify both the loaded `zlink_cpp`
  path and the loaded core `libzlink` path, so stale library artifacts do not
  pollute the result.

## Completed Structure Requirements

The completed C++ binding satisfies these requirements:

- Installed headers expose all stable user-facing core capabilities.
- The C++ binding builds and installs a compiled C++ library target in addition
  to public headers.
- `Contracts/Eventing/` is the only public eventing category. `Contracts/Monitoring/`
  is gone, and `zlink.hpp` includes the Eventing headers.
- Old wrapper include paths are gone. Applications, samples, perf, and tests
  include `<zlink.hpp>` or deliberate `Contracts/...` headers only.
- Public headers plus the compiled C++ binding target are enough for
  applications, perf, samples, and framework adapters.
- Private helper headers and private runtime source paths are not needed by
  users.
- Value types remain concrete unless abstraction removes real complexity.
- Public APIs hide native part loops, raw handles, and callback userdata.
- Handler registration uses `set_..._handler` names, with no public `on_...`
  aliases.
- Public helper/free functions and builder convenience methods are declared in
  `Contracts/`, not only in runtime helpers.
- Service control/admission receive exceptions are documented where they differ
  from data-plane caller-provided storage.
- Perf tests use the same measurement meaning as C perf.

## Actor And Spot Route Results

C++ exposes Actor and Spot route lookup results as concrete contract types.

- `actor_route_t` preserves the resolved Actor ref, `actor.node_rid`,
  `current_spot_rid`, and `current_spot_kind`.
- `spot_route_t` preserves `spot_rid`, `owner_node_rid`, and `spot_kind`.
- `spot_kind` distinguishes Entry Spot from user Spot. Invalid kind is not a
  successful route result.
- `spot_node_spot_entry_t` and `spot_node_actor_entry_t` expose the same Spot
  kind/current Spot fields as the core snapshots.

C++ exposes `spot_node_t::send_to_actor(actor_ref_t)` and
`spot_node_t::request_to_actor(actor_ref_t)` for resolved Actor refs.
`send_to_actor` consumes one or more message parts on successful submit and completes when
the Actor owner mailbox accepts the handoff. `request_to_actor` consumes the
request parts on successful submit and delivers the Actor handler reply parts
to the callback or awaitable result. C++ must not reintroduce the removed
Discovery route table or resolver APIs as compatibility helpers.
