# S8 CPP bindings review — iteration 4 — R1 (opus) progress

## Scope confirm
- Target commit: `50faf28fd` (merge; cpp dead-code fix carried by `4a8634bcb`).
- Working tree HEAD `e86213d3a` freezes the scope; aggregate SHA-256 recomputed:
  `e2190823b037b438a53de285a16be3ce40e92499a7ea06dcc6a73b99291b3bdb` == manifest. MATCH.
- File count 120 (39 include + 62 src + 18 samples + 1 CMakeLists). MATCH.

## iter-3 finding resolution (source-level, at fix commit 4a8634bcb)
- C3-1 router_spot dead path: `router_spot`, `set_router_spot_send_context` = 0 refs.
  Both `zlink_router_recv_part` call sites (native_receive.hpp, Sockets/detail.hpp) use
  the 6-arg 10.0.0 signature (no spot_rid out-param) -> branch was genuinely dead.
  router.cpp now unconditionally sets socket_rid context; recv_envelope_t.source_spot_rid
  field removed; socket.cpp make() calls drop the source_spot_rid arg. RESOLVED.
- C3-2 spot_spot enum orphan: `spot_spot` = 0 refs; enum now {none, socket_rid}. RESOLVED.
- C3-3 orphan cascade: resolve_timeout / get_string_option / submit_message_array /
  to_send_result / classify_nonblocking_send_errno / send_parts_no_wait /
  submit_message_parts_no_wait = 0 refs; native_send_result.hpp deleted; dead `using`
  aliases pruned from Service/detail.hpp. RESOLVED.

## Fixpoint / new-orphan check
- native_send.hpp trio (restore/send_parts/reply_parts) mutually used + called from
  received_access.hpp. No caller-less definitions among the touched files.
- Broad orphan + no-hit sweep delegated to read-only sub-agent (in progress).

## Axes (final)
- I1 contract vs Core 10.0.0: deletion-only; recv signatures aligned; residue 0. CLEAN.
- I2 POSD/DDD: no structural regression from deletion. CLEAN. (low: single-value enum L4-3)
- I3 cleanup / no-hit: C3 cascade fully removed; NO new orphan from transitive removal;
  no-hit ZERO. CLEAN. Low residuals only:
  - L4-1 assign_parts_from_native (2 overloads + alias) caller-less — PRE-EXISTING
    (identical 3-occurrence fingerprint at iter-2 e919e9857), NOT introduced by iter-3 removal.
  - L4-2 unused using aliases close_message_array/close_native_parts in service::detail.

## Verdict
All three axes: zero blocker/high/medium. 3 low findings (do not block per iter-4 rule).
BINDINGS REVIEW CLEAN.
