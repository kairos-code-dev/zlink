# R2 (Claude Sonnet) Progress — S8 DOTNET bindings review iteration 1

Start: scope verified — 206 files, aggregate SHA-256 = c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b (matches manifest)

## Plan
- Enumerate scope files, map structure (src vs samples)
- Read Core 10.0.0 C API headers: mesh_node.h, dispatch.h, spot.h, actor.h, stream_session.h, socket/api.h
- I1: contract match — P/Invoke decls vs C API, marshalling, lifetimes, pull-dispatch surface, pre-start config
- I2: POSD/DDD — module depth, responsibility boundaries, wrapper leakage
- I3: cleanup — deprecated concept residue, dead P/Invoke, scoped greps

## Status
- [done] Read all 5 Core headers (mesh_node.h, dispatch.h, spot.h, actor.h, stream_session.h) + socket/api.h
- [done] Read NativeMethods.{MeshNode,Dispatch,Spot,Actor,StreamSession}.cs — P/Invoke signatures match Core headers closely
- [done] Read IMeshNode.cs, MeshDispatch.cs, ISpot.cs, IStreamSessionService.cs — contract surface
- [FINDING CONFIRMED, source-verified] Actor join-admission reply is unreachable/broken in C#:
  - Core reply_route_t has 3 kinds (mesh_runtime.hpp:461-466): kind_generic, kind_actor_join, kind_transfer_relay
  - zlink_mesh_reply (mesh_dispatch_api.cpp ~877) REJECTS kind_actor_join tokens with EINVAL (only accepts generic/transfer_relay)
  - zlink_actor_join_reply (mesh_actor_api.cpp:1356) is the ONLY function accepting kind_actor_join tokens
  - dotnet MeshReceiveRecord.Reply() (Contracts/Service/MeshDispatch.cs:226-244) ALWAYS calls native zlink_mesh_reply
  - NativeMethods.Actor.cs:52 declares zlink_actor_join_reply P/Invoke but it is NEVER invoked anywhere in managed code (confirmed via grep)
  - Result: app draining ready-index, hitting a SpotControl record with OperationKind.ActorJoin, calling .Reply() -> native EINVAL, join always fails. No accept/reject path exists in C# public surface at all.
  - This is THE coordinator's "known observation" — confirmed as a genuine functional break, not just missing convenience.
- Transfer API (zlink_mesh_node_actor_transfer_*) — confirmed absent from ALL bindings (cpp too), pre-existing/universal gap, judged NOT a finding (framework-internal-consumer API per core spec comment; framework/languages/{cpp,dotnet} implement actor transfer via a different app-level mechanism)
- [done] I1 marshalling/struct_size/version checks — clean (StructSize/Version populated correctly at all call sites checked)
- [done] I2 POSD/DDD pass — no god-files, appropriately thin binding layer, pull-dispatch low-level-ness judged deliberate/spec-faithful not a finding — CLEAN
- [done] I3 scoped greps for deprecated concepts (SpotNode/RouteBridge/spot_node/subjects/internal_sockets/pub-sub-rid/dispatch_workers/recv_actor_part/msg_gets) — MAJOR HITS found:
  - NativeMethods.Core.cs RequiredExportNames (lines 8-124): ~35+ pre-10.0.0 symbols (zlink_spot_node_*, zlink_spot_route_bridge_*, zlink_spot_recv_subscription_event, zlink_msg_gets, router/spot "_part" bridging, etc.) NONE of which exist in core/src/libzlink.vers (10.0.0 canonical ABI) or in the actual staged libzlink.so.10.0.0 (verified via nm -D, read-only inspection of existing artifact, not build/run)
  - NativeLibraryLoader.ValidateRequiredExports() (lines 147-162) throws DllNotFoundException on ANY missing required export -> dotnet bindings CANNOT load against real Core 10.0.0 library at all, at first native call. This is invisible to "dotnet build green" (build doesn't execute Main). CRITICAL/blocker finding, registered in both I1 and I3.
  - IRouterSocket.SendToSpot/RequestToSpot/ReplyToSpot (RoutedSocketContracts.cs:73,78,84) still public, wired to zlink_router_*_spot_part functions that don't exist in 10.0.0 ABI
  - Message.GetProperty() (Message.cs:301) still public, wired to zlink_msg_gets which doesn't exist in 10.0.0 ABI
  - ZlinkSpotRouteBridgeOptions/EndpointOptions dead structs (NativeTypes.cs:72,81) + 4 dead push-dispatch delegate types (NativeMethods.Core.cs:348-366)

## FINAL STATUS: COMPLETE
- review.ko.md written to this directory
- Verdict: I1 NOT CLEAN (2 blocker), I2 CLEAN, I3 NOT CLEAN (1 blocker/1 high/1 medium/1 low)
- Overall: BINDINGS REVIEW NOT CLEAN
- Scope re-verified at end: 206 files, same aggregate SHA-256, no scope file modified during review (confirmed via git status)

