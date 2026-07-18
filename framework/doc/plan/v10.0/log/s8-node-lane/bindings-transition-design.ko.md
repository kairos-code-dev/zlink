# S8-NODE lane — Node.js(TS/N-API) bindings 10.0.0 전환 설계

RouteMesh 10.0.0: SpotNode/route-bridge/PUB-SUB/push-dispatch → MeshNode + pull dispatch + spot/actor/
stream_session. **Runtime raw-socket 레이어 생존.** 2티어(contracts 공개 인터페이스 + runtime 구현) +
`native/src/` N-API addon.

## 규모 (native addon)
- `addon_spot.cc`(2,548) — spot_node 수명·bind·pub/sub rid·route_bridge·publisher·spot·dispatch → 재작성.
- `addon_spot_actors.cc`(706) — actor + bound session → `zlink_mesh_node_actor_*`·`stream_session`.
- `addon_spot_request_callbacks.cc`(388) → reply-token.
- `addon_spot_node_snapshots.cc`(360) — status/peers/subjects/internal_sockets/spots → mesh_node status/peer만.
- `addon_spot_actor_values.cc`(180) — spot-level actor 열거 marshaling → 삭제.
- `addon_exports.cc`(210) — N-API 등록 테이블 → spot 메서드 ~40개 개명/삭제.
- `addon_core.cc`(3,333) — raw socket 생존, `zlink_msg_gets`·part-push helper만 손봄.

## contracts/runtime (TS)
- `contracts/service/spot/spot_node.ts`(122) `SpotNode` → `MeshNode`+축소.
- `spot_route_bridge.ts`(37) `SpotRouteBridge`·`EndpointOptions`·`EndpointCapabilities`·`SpotNodePublisher`
  → **삭제**(publisher는 mesh_node publisher로).
- `spot_models.ts`(89)·`spot_node_status_models.ts`(108)·`spot_dispatch_models.ts`(50) → 축소·pull.
- runtime: `spot.ts`(488)·`spot_node.ts`(361)·`spot_route_bridge.ts`(172,삭제)·`actor.ts`(103)·
  `actor_invokers.ts`(341)·`actor_operations.ts`(274)·`actor_models.ts`(325)·`spot_raw_models.ts`(164).
- native TS surface: `runtime/native/binding_service.ts`(278) addon ~55 메서드 타입 → 재작성.
  `src/index.ts` `createSpotNode`(:99) → `createMeshNode`.

## 삭제 심볼 (N-API, file:line)
- route_bridge: `addon_spot.cc` `zlink_spot_route_bridge_new`:1675·`_close`:1690·`_attach_router_channel`:1716·
  `_send`:1744·`_request`:1797·`_handle_router_received`:1837·`_drain`:1858. exports `addon_exports.cc`:142-150.
- spot_node→mesh: `addon_spot.cc` `_new`:1504·`_destroy`:1520·`_set_pub_bind`:1536·`_set_router_bind`:1552·
  `_set_pub_routing_id`:1570·`_set_sub_routing_id`:1588·`_connect_peer`(_rid):1604/1623·
  `_disconnect_peer`(_rid):1639/1657·`_entry_spot`:2036·`_spot_lookup`:2062·`_spot_get_or_new`:2094·
  publisher `_publisher_new`:1873·`_publish`:1894·`_close`:1913.
- subjects/sockets/spots: `addon_spot_node_snapshots.cc` `_subjects`:266·`_internal_sockets`:305·`_spots`:344
  (peers/status:143-232 축소 생존).
- spot-level actor 열거: `addon_spot_actors.cc` `_actors`:19/27, `addon_spot_actor_values.cc`:163.
- per-part push: `_actor_recv_part`(`addon_spot_actors.cc`:283, `addon_spot.cc`:700),
  `zlink_spot_subscribe_part`:513/537·`zlink_spot_publish_part`:420.
- push dispatch: `spot_dispatch_event` handler:759·`zlink_spot_dispatch_event_handler`:1149·
  enums `ZLINK_SPOT_DISPATCH_*`:779-785 → pull(ready-index/claim).
- message property: `zlink_msg_gets` `addon_message_values.h`:77.
- actor remap: `_actor_new_with_request`:54·`_destroy`:83·`_lookup`:115·`_join_spot`:156·
  `_join_entry_spot`:201·`_leave_spot`:241·`_send_bound_session_msg`:320·`_send_to_actor`:359·
  `_request_to_actor`:417·`_reply_no_bind`:447·`_bind_remote_session`:476·`_close_bound_session`:498.

## 배선
- `package.json` version 9.0.4 → 10.0.0(예상 SONAME 구동).
- `runtime/native/native_load_paths.ts:8` `LINUX_SONAME='libzlink.so.9'` → `.so.10`.
- `scripts/verify_prebuilds.js`(15/63/80) package.json major 추종 검증.
- `binding.gyp` `core/build/lib/libzlink.so`(또는 `$ZLINK_LIB_PATH`) 링크, `include_dirs ../../core/include`,
  rpath `$ORIGIN`. 새 헤더로 addon 재컴파일 필요.
- `prebuilds/linux-x64`·`native/linux-x64`·`linux-x86_64`에 9.0.4/10.0.0 양쪽 staged, 기본
  `libzlink.so`→`.so.9` → `.so.10` repoint + addon `.so.10` 링크 재빌드.

## samples (`samples/`, mirror `dist-tools/samples/`)
- 삭제: `spot_channel_example.ts`. 재작성: `spot_request_sample.ts`·`spot_pubsub_example.ts`·
  `spot_rpc_example.ts`·`spot_timer_example.ts` + actor 6 + `sample_support.ts`.
- 생존(raw): `dealer_router_recv`·`monitor_recv`·`pair_recv`·`pubsub_recv`·`request_reply`·`stream_recv`·
  `stream_packet_callback`.
- 후속(2차): tests(`spot_dispatch_drain`·`spot_request_to_spot`·`version`·fixtures),
  perf(`perf/multi/perf_multi_spot_*` 7, `perf/single/perf_spot.ts`).

## 순서
cpp 패턴 확정 후 미러. 군: 1)addon N-API(spot/actor/snapshots/exports) retarget + route_bridge/actor-values
삭제 2)contracts 개명·삭제 3)runtime TS 재작성 + binding_service.ts 4)samples 5)package.json 버전·SONAME·
심링크·addon 재빌드 6)`npm run build`+tsc green → smoke → bindings 리뷰 campaign. tests/perf는 2차.
