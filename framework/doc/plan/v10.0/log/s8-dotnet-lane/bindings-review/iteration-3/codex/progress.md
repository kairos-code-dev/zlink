# S8 DOTNET bindings review iteration-3 — R1 (opus) progress

REVIEWER only. No build/test/run/modify. Static comparison + local greps/reads.

## Scope verification
- Target commit: `481221b24` (iter-1·iter-2 fixes merged). Working HEAD `6eeb596e`.
  `git merge-base --is-ancestor 481221b24 HEAD` = true; `git diff 481221b24 HEAD -- bindings/dotnet/{src,samples}` = empty → dotnet scope byte-identical to target.
- Scope hash (start): `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d` (208 files) — MATCH.
- Scope hash (end): `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d` — MATCH (no files modified).

## Steps executed
1. Read canonical iter-3 prompt, iter-1 + iter-2 finding-ledgers, iter-3 manifest.
2. No-hit sweep for all removed symbols/concepts (iter-1 + iter-2): all 0.
3. iter-2 signature fixes cross-checked vs Core headers:
   - D2F1 `zlink_router_recv_part`/`_nowait` → 6 params (no `sourceSpotRoutingId`).
   - D2F3 `zlink_msg_refcnt(ref ZlinkMsg, out int errorOut)` = 2 params.
   - D2F2/D2I3-1 `stream_detach`/`stream_attach_raw`/`subscribe_handler` no-hit 0; stream on `zlink_stream_packet_handler` model.
4. iter-1 fixes cross-checked:
   - DF1 `RequiredExportNames` (182) all ⊆ Core ABI; all P/Invoke symbols (177) ⊆ Core ABI (196). Zero drift.
   - DF2 `zlink_actor_join_reply` + typed `ReplyJoin`/`AdmitJoin`/`RejectJoin`.
   - DF3 `IMeshNode.SetRoutingId`.
   - DF4 caller-init `StructSize=`/`Version` set (22 sites, 6 Runtime/Service files).
   - DF5 `PrepareActorTransfer`/commit/activate fence API with StructSize.
   - DF6 `StreamSocket.SetRoutingId` → `Kernel.SetOption(RoutingId)` → native `zlink_set_routing_id`.
   - DF7 finalizers `~MeshReadyBatch`/`~MeshClaim`/`~MeshReceiveBatch`.
   - DF8 `MeshEndpointMaxBytes = 511`.
   - DI2-1 `BindActor`/`UnbindActor` on `IStreamSessionService`; `IStreamSocket` clean of actor ops.
   - DI2-2 `DecodeKindData(native.KindData, KindDataSize)` typed payloads.
5. Fresh adversarial I1 pass: residual `SpotRoutingId`/`spot_rid` search — only hits are mesh-layer record fields (`MeshDispatch.SourceSpotRid`, `NativeMeshModels.*SpotRid`) which correspond to legitimate Core `spot.h`/`actor.h` struct fields; NOT the removed ROUTER-socket-layer spot_rid. `zlink_spot_send_to_spot` 8-param signature matches Core.

## ABI diff artifacts
- Core ABI: `nm -D --defined-only core/build-asan/lib/libzlink.so.10.0.0` → 196 `zlink_*` symbols.
- dotnet P/Invoke: 177 symbols (EntryPoint aliases resolved: `_nowait`→`zlink_router_recv_part`, `_pinned`→`zlink_poller_wait`).
- `comm -23 dotnet core` = empty (subset holds).

## Result
iter-1 (DF1-DF8, DI2-1/2, DI3-1, metadata) + iter-2 (D2F1-D2F3, D2I3-1) all RESOLVED. I1/I2/I3 each 0 findings.
Verdict: BINDINGS REVIEW CLEAN.
