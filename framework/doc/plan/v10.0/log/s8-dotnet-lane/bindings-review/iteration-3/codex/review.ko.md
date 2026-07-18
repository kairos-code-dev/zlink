# S8 DOTNET bindings 전환 리뷰 iteration-3 — R1(opus) review

독립 리뷰어 R1. 다른 리뷰어·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조 + 로컬 grep/read만, 파일 무수정.

## 1. Scope 확인
- 대상 commit `481221b24`(iter-1·iter-2 수정 병합). 작업 HEAD `6eeb596e`.
- `481221b24`은 HEAD의 ancestor이고 `git diff 481221b24 HEAD -- bindings/dotnet/{src,samples}`가 비어 있어 dotnet scope는 대상 스냅샷과 byte 동일. 작업 트리 대조 안전.
- 시작 hash = `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d`(208 files) — MATCH.
- 종료 hash = 동일 — MATCH(무수정 확인).

## 2. iter-1·iter-2 finding 해소 판정

### iter-1 (모두 RESOLVED)
| ID | 판정 | Evidence |
|---|---|---|
| DF1 RequiredExportNames 제거심볼 | RESOLVED | `RequiredExportNames` 182개 전부 Core ABI 부분집합. 제거 심볼(spot_node/route_bridge/msg_gets 등) no-hit 0. 전체 P/Invoke 177 ⊆ ABI 196. |
| DF2 join-admission reply 파손 | RESOLVED | `zlink_actor_join_reply`(NativeMethods.Actor.cs:52) + typed `ReplyJoin`/`AdmitJoin`/`RejectJoin`(MeshDispatch.cs:282,304,311). 전용 경로 배선. |
| DF3 MeshNode routing-id 표면 | RESOLVED | `IMeshNode.SetRoutingId`(IMeshNode.cs:20). |
| DF4 caller-init size/version 0 | RESOLVED | `StructSize=`/`Version` 22개 사이트, Runtime/Service 6개 파일(MeshDispatchRuntime/StreamSessionService/Spot/MeshNode/MeshNode.Actors/MeshNodePublisher). |
| DF5 transfer fence API 누락 | RESOLVED | `PrepareActorTransfer`/commit/activate(MeshNode.Actors.cs:228~286), StructSize 설정, `zlink_mesh_node_actor_transfer_*` 배선. |
| DF6 StreamSocket.SetRoutingId 미설정 | RESOLVED | `SetRoutingId`→`Kernel.SetOption(SocketOptions.RoutingId)`→native `zlink_set_routing_id`(SocketOptionAccessor.cs:50). managed cache 아님. |
| DF7 batch/claim finalizer 부재 | RESOLVED | `~MeshReadyBatch`/`~MeshClaim`/`~MeshReceiveBatch`(MeshDispatchRuntime.cs:79,147,248). |
| DF8 endpoint 256~511 거부 | RESOLVED | `MeshEndpointMaxBytes = 511`(BoundaryValidation.cs:13). |
| DI2-1 raw STREAM actor 표면 | RESOLVED | `BindActor`/`UnbindActor`가 `IStreamSessionService`로 이동(05-stream-session 소유), `IStreamSocket`은 actor op 없음. |
| DI2-2 typed kind_data 폐기 | RESOLVED | `DecodeKindData(native.KindData, KindDataSize)`(MeshDispatchRuntime.cs:282~307) record-kind별 typed payload. |
| DI3-1 제거 심볼·구개념 잔재 | RESOLVED | no-hit 8종 0(아래 §4). `RoutedSocketContracts.cs` spot forwarder 제거. |
| metadata | RESOLVED | metadata 표면 IMeshNode/ISpot/IStreamSessionService/MeshDispatch 일관, `zlink_mesh_metadata_view_t` 정합. |

### iter-2 (모두 RESOLVED)
| ID | 판정 | Evidence |
|---|---|---|
| D2F1 router_recv_part 7-param | RESOLVED | `zlink_router_recv_part`·`_nowait` 모두 6-param(router, sourceNodeRoutingId, requestSeq, part, hasMore, flags). Core api.h:272-277과 정합. `sourceSpotRoutingId` 제거. |
| D2F2 DetachStream→stream_detach | RESOLVED | `zlink_stream_detach`/`DetachStream` no-hit 0. stream은 `zlink_stream_packet_handler` 콜백 모델. |
| D2F3 msg_refcnt error_out 누락 | RESOLVED | `zlink_msg_refcnt(ref ZlinkMsg, out int errorOut)` 2-param. Core message/api.h:116과 정합. |
| D2I3-1 dead stream_attach_raw/subscribe_handler | RESOLVED | `zlink_stream_attach_raw`/`AttachStreamRaw`, `zlink_subscribe_handler`/`SubscribeHandler` no-hit 0. |

해소된 finding에 대한 새 반례 없음 → 재개 없음.

## 3. 전체 scope 3축 재검토 (iteration-3: 각 축 finding 0 = CLEAN)

### I1 계약 일치 — CLEAN (finding 0)
- **P/Invoke ⊆ Core 10.0.0 ABI**: dotnet native 심볼 177개 전부 `nm -D libzlink.so.10.0.0`(196 심볼) 부분집합. `comm -23` 결과 공집합. EntryPoint alias(`_nowait`→`zlink_router_recv_part`, `_pinned`→`zlink_poller_wait`) 정상.
- **raw-layer 드리프트 잔존 여부**: `SpotRoutingId`/`spot_rid` 잔존 검색 결과 유일 hit는 mesh 레코드 필드(`MeshDispatch.SourceSpotRid`, `NativeMeshModels.*SpotRid/*Spot*`)로 Core `service/spot.h`·`service/actor.h` 구조체 필드(spot_rid/previous_spot_rid/current_spot_rid)와 정합. iter-2에서 제거된 것은 ROUTER **소켓** 계층의 spot_rid이며 그 경로는 정정됨. 서비스 계층 spot_rid는 정당한 Core 개념 → 드리프트 아님.
- 고위험 시그니처 표본 정합: `zlink_recv_part`(5-param), `zlink_spot_send_to_spot`/`_request_to_spot`(8-param) Core 헤더와 일치.
- marshalling/수명(caller-init StructSize·finalizer)·join-reply·transfer·metadata 표면 모두 정합(§2 근거).

### I2 POSD·DDD — CLEAN (finding 0)
- 경계 소유권 정합: raw transport(`IStreamSocket`) ↔ actor 바인딩(`IStreamSessionService`) ↔ spot 메시징(`ISpot`) ↔ join 승인(`MeshDispatch.ReplyJoin`)이 각각 단일 책임으로 분리. iter-1의 raw-STREAM actor 책임 혼입은 해소된 상태 유지.
- native record → typed payload 해석이 `DecodeKindData` 단일 지점에 집약(중복 dispatch 없음).
- 신규 god-file·계층 침범·중복 표면 미발견.

### I3 정리 — CLEAN (finding 0)
- 제거 심볼·dead P/Invoke no-hit 8종 0(§4).
- RequiredExportNames·전체 P/Invoke 모두 살아있는 ABI 심볼만 참조(dead 선언 0).

## 4. 폐기 no-hit
```
0  zlink_spot_node
0  zlink_spot_route_bridge | RouteBridge
0  zlink_msg_gets
0  zlink_stream_detach
0  zlink_stream_attach_raw
0  zlink_subscribe_handler
0  router *_spot_part | _spot_part
0  pub/xsub rid | subscribe routing
```
전량 0. (`SendToSpot`/`RequestToSpot` 잔존 2·3건은 `ISpot`의 spot-to-spot 메시징으로 `zlink_spot_send_to_spot`/`_request_to_spot`에 배선된 정당 표면 — 제거 대상인 `IRouterSocket` forwarder(`RoutedSocketContracts.cs`)와 무관하며 후자는 제거 확인.)

## 판정
iter-1·iter-2 finding 전량 해소. I1/I2/I3 각 축 finding 0.

BINDINGS REVIEW CLEAN
