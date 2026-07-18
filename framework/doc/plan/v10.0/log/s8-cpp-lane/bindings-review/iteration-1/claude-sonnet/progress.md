# R2 (Claude Sonnet) progress — S8 CPP bindings review iteration-1

- Start: scope verified — 129 files, aggregate SHA-256 e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023
- HEAD: f572f7f97fc0ca5a6c1bdc40f6bf4242238b22d5 (freeze commit on top of review target 2f34aacf2; scope hash matches manifest)
- Status: setting up file inventory, starting I1/I2/I3 review

## Log
- [t0] Read prompt.md, verified scope hash. Creating progress.md.
- [t1] Read core C API headers (mesh_node.h, dispatch.h, spot.h, actor.h, stream_session.h, common.h).
- [t2] Found major I3 root-cause family: stale pre-10.0.0 (SpotNode-era, v9.0.4) vendored files still present in bindings/cpp/include, unused by the actual compile (core/include wins include-path priority) but part of scope:
  - bindings/cpp/include/zlink.h: ZLINK_VERSION 9.0.4, incomplete/stale include set
  - bindings/cpp/include/zlink/common.h: ZLINK_VERSION_MAJOR 9 vs core's 10
  - bindings/cpp/include/zlink/service/actor.h, common.h, spot.h: full pre-transition SpotNode C API surface (zlink_spot_node_*, zlink_spot_route_bridge_*, zlink_spot_node_subjects, zlink_spot_node_internal_sockets, separate pub/sub rid setters) — spot.h alone 455 lines vs core's 139
  - bindings/cpp/include/zlink_enum.h: zlink_spot_node_mode_t/socket_owner_t/option_t (incl DISPATCH_WORKERS_MIN/MAX)/state_t; missing new core error codes (BACKPRESSURED/BUFFER_TOO_SMALL/INVALID_STATE/EALREADY/EDEADLK/ESHUTDOWN)
  - src/Runtime/Options/option_ids.hpp: dead `spot_node_option_id` enum incl. dispatch_workers_min/max — zero usages found
  - src/Runtime/Service/spot_state.hpp: dead `class spot_node_t;` fwd-decl + dead `actor_command_t::node` member (spot_node_t*, never assigned/read)
  - include/zlink/Contracts/Core/routing_id.hpp, Eventing/poller.hpp, Messaging/message.hpp: dead `class spot_node_t;` fwd-decls / friend decl, spot_node_t never defined anywhere in scope
  - include/zlink/Contracts/Eventing/events.hpp: `monitor_target_kind_t` enum entirely unused anywhere in scope; enumerator named `spot_node`
  Verified with scoped grep across full 129-file scope: SpotNode/spot_node HIT, route_bridge HIT, subjects HIT, internal_sockets HIT, dispatch_workers HIT. (recv_actor_part, msg_gets: no hit)
- [t3] Read live Contracts/Service/{mesh_node,dispatch,spot,actor,stream_session}.hpp, mesh_node_models.hpp, actor_models.hpp — clean 1:1 mapping to core, good POSD (pimpl, RAII claim/batch, private handles).
- [t4] Found I1 blocker: samples/sample_common.hpp mesh_start_single_node() never calls set_routing_id(); core mesh_node_start() requires non-empty routing_id (verified in core/src/api/mesh/mesh_node_api.cpp:373 AND spec 01-mesh-node.md §3, not just doc). mesh_node_t::start() throws config_error_t on failure. 9/15 registered samples (all mesh_node/spot/actor based) would throw uncaught exception at runtime despite compiling+linking green. Verified zero `set_routing_id` calls anywhere in samples/.
- [t5] Found I1 medium: core actor cross-node transfer fence API (transfer_prepare/commit/activate/abort, actor.h:214-226) entirely unexposed in C++ bindings — confirmed 0 hits for "transfer" across actor.hpp/actor.cpp/actor_models.hpp. Design doc itself (bindings-transition-design.ko.md:27) states "actor.hpp: ... join/leave/transfer" as intended scope, so this is a confirmed gap vs. the transition's own plan, not just vs core.
- [t6] Found I1 low: zlink_mesh_node_peer_channels() (mesh_node.h:241) unexposed; mesh_node_publisher_t missing set_nodrop/option (mesh_node.h:210-219) unlike spot_t's set_nodrop — asymmetric mapping.
- [t7] Checked I2 (src file sizes 105-571 lines, no god files; pimpl/RAII/private handles verified in dispatch.hpp/spot.cpp/mesh_node.cpp; operation_builder_base.hpp confirmed used, not dead) — CLEAN, no findings.
- [t8] Confirmed final scope hash unchanged (129 files, e1adbf34...), git status clean for scope paths (no file modified during review).
- [t9] Wrote review.ko.md — I1 NOT CLEAN (4 findings), I2 CLEAN, I3 NOT CLEAN (1 root-cause family, multiple sub-locations, all 5 required deprecated-concept probes HIT: SpotNode/spot_node, route_bridge, subjects, internal_sockets, dispatch_workers). Final verdict: BINDINGS REVIEW NOT CLEAN.
- DONE.
