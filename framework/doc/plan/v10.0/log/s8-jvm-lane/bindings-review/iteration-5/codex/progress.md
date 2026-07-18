# S8 JVM bindings review iteration-5 — R1 (opus) progress

## Scope
- Target commit: `fbd35a1ef` (HEAD `2e8164c17` = freeze on top; scope content identical).
- File count: 251 (verified).
- Aggregate SHA-256: `1351d5dabce124a8cefea369437cc23bd9f1eecf0ce59079056b6add2ad54192` — MATCHES prompt.
- No files modified.

## Steps
1. [x] Scope hash + file count verified (251, hash match).
2. [x] iter-4 JV4-1 resolution: C bridge `zlink_java_reqrep_bridge.c` now 40 lines; dead+broken `zlink_java_router_recv` + helpers + `zlink_router_enable_spot_receive` decl gone. Every Core call in native/src cross-checked against Core signature.
3. [x] 3 dead consts (SPOT_TOPIC / SPOT_PAYLOAD / ERRNO_EFSM) removed — absent in scope. (ErrorCode.EFSM enum is a distinct legitimate entry.)
4. [x] Removed-symbol gate: 194 identifiers from removed-identifiers-10.0.0.json checked vs java scope → 0 hits (EMPTY).
5. [x] FFI descriptor sweep (Native.java, 85 MH handles) — subagent + spot checks.
6. [x] ServiceLayouts / NativeLayouts struct-vs-Core — subagent + MESH_NODE_STATUS spot check.
7. [x] Dead-code / no-hit scan (MH handles all invoked; no TODO/FIXME).

## Key evidence
- C bridge Core calls: `zlink_msg_data(msg)` = Core 1-arg OK; `zlink_send_part_rid(socket,&rid,&parts[i],flags,part_flag)` = 5 args = Core 5 params OK. `zlink_routing_id_t{uint8_t size;uint8_t data[255]}` matches bridge usage.
- `zlink_router_recv_part`: Core 6 params; Native.java descriptor `of(JAVA_INT, ADDRESS×5, JAVA_INT)` = 6 args; both invokeExact callsites pass 6 args. The prior C-side 7-arg bug-class is fully eliminated on both C and Java sides.
- MESH_NODE_STATUS layout matches Core field-order/types; seq(256)=MESH_NAME_MAX+1(255+1), seq(512)=ENDPOINT_MAX+1(511+1).
