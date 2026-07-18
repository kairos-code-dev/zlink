# S8 DOTNET bindings review iteration-2 — R2 (Claude Sonnet) progress

- Started: scope verified 208 files, hash d6acf3e4... matches manifest.
- HEAD (77dc73cd7) has 0-diff vs target 115c3d73d for bindings/dotnet/src+samples -> reviewing at HEAD is equivalent.
- Read iteration-1 finding-ledger + both iter-1 reviews (codex, claude-sonnet) + s8-common-raw-layer-drift.ko.md.
- Next: verify DF1-DF8, DI2-1, DI2-2, DI3-1 resolved at HEAD; then fresh I1/I2/I3 pass; check raw-layer-drift item #1 (zlink_subscribe_handler) independent judgment.

## iter-1 finding verification (all resolved)
- DF1 RequiredExportNames: 182 entries, all present in libzlink.vers + staged .so nm -D. Confirmed via diff script.
- DF2 actor join reply: MeshReceiveRecord.ReplyJoin/AcceptJoin/RejectJoin -> zlink_actor_join_reply wired.
- DF3 routing-id: IMeshNode.SetRoutingId present, samples set RoutingId+Bind+Channel before Start via SampleSupport helper.
- DF4 struct_size/version: confirmed set via Marshal.SizeOf<T>() across all caller-init outputs checked.
- DF5 transfer API: PrepareActorTransfer/Commit/Activate/Abort wired to native, struct layout matches actor.h field-for-field.
- DF6 StreamSocket.SetRoutingId: now uses Kernel.SetOption (native), consistent with other socket types.
- DF7 finalizers: MeshReadyBatch/MeshClaim/MeshReceiveBatch all have ~Finalizer + GC.SuppressFinalize.
- DF8 511 endpoint validator: BoundaryValidation.ValidateMeshEndpoint(511) used in SetBind/ConnectPeer.
- DI2-1 raw stream actor: IStreamSocket no longer has BindActor/UnbindActor; StreamActorInterop.cs repurposed to shared ActorInterop marshalling helper used by MeshNode/Spot/StreamSessionService.
- DI2-2 typed kind_data: DecodeKindData interprets native KindData/KindDataSize into typed payloads (ActorJoinCompletion etc).
- DI3-1 no-hit: SpotNode/RouteBridge/spot_node/subjects/internal_sockets/pub-sub-rid/dispatch_workers/recv_actor_part/msg_gets all 0 hits confirmed via scoped grep.

## Fresh full-scope findings (NEW, iteration-2)
Ran automated cross-check: extracted all 185 P/Invoke declarations (all NativeMethods*.cs) and diffed against
core/include/zlink/**/*.h ZLINK_EXPORT signatures by param count. Found:
- NF1 [I1][blocker]: zlink_router_recv_part (+ _nowait alias) declares 7 params, Core exports only 6
  (spot_rid out-param removed per s8-common-raw-layer-drift.ko.md item 2, cpp/node already fixed but dotnet wasn't).
  Extra middle param shifts every subsequent SysV register -> native writes a 64-byte zlink_msg_t into an 8-byte
  `ulong requestSeq` stack slot (56-byte overflow) and misinterprets a live pointer as the flags_ bitmask.
  Reachable via any ROUTER socket receive (SocketKernel.Receive.cs:108, ReceiveCore.cs:186), used in
  RequestReplyAsync/DealerRouterRecv samples.
- NF2 [I1][high]: zlink_msg_refcnt declares 1 param, Core requires 2 (msg_, error_out_). Reachable via public
  Message.RefCount property (Message.Native.cs:57). Core writes *error_out_ unconditionally on success path
  (message_api.cpp:154) -> writes through whatever garbage occupies that unset register.
- NF3 [I1][blocker]: IStreamSocket.DetachStream() (public, documented) calls zlink_stream_detach, which Core
  10.0.0 does not export (only zlink_stream_packet_handler remains). Reachable after any OnPacket() attach;
  throws unhandled EntryPointNotFoundException (SocketKernel.Stream.cs:129-137). Same symbol swallowed silently
  in Dispose()'s try/catch (Lifecycle.cs) so only the explicit-call path surfaces.
- NF4 [I3][medium]: zlink_stream_attach_raw and zlink_subscribe_handler P/Invokes call symbols Core no longer
  exports; confirmed dead (zero public-surface callers) via grep sweep -> dead-code cluster (2 AttachStreamRaw
  overloads, 2 delegate types, 2 On* handler methods, SubscribeHandler + delegate + registry fields +
  SocketCapability.SubscribeHandler policy entry) left over from pre-10.0.0 raw layer, not caught by the
  DI3-1 no-hit literal-string list.
- zlink_poller_wait_pinned checked and is NOT a finding: DllImport EntryPoint="zlink_poller_wait" correctly
  aliases the exported symbol (unsafe pinned-pointer overload of the same native function).

Verdict: I1 NOT CLEAN (3), I2 CLEAN, I3 NOT CLEAN (1). Writing review.ko.md now.

## Completed
review.ko.md written. Final verdict: I1 NOT CLEAN (3: blocker 2, high 1), I2 CLEAN, I3 NOT CLEAN (1: medium).
Overall: BINDINGS REVIEW NOT CLEAN.
Scope end-check: 208 files, hash d6acf3e4... unchanged (no scope files modified during review).
