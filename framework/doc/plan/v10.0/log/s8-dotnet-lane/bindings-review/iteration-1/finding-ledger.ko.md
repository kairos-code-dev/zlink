# S8 DOTNET bindings 리뷰 iteration-1 — 병합 finding ledger

두 리뷰어(R1 Codex, R2 Sonnet) iteration-1(snapshot `29151802f`, 206파일 `c9e0aef9`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. dotnet은 참조 lane이므로 아래 수정으로 확정되는 표면(typed
join-reply·typed kind_data·transfer·metadata)이 타 언어의 기준이 된다.

## I1 계약 구현 일치

### DF1. RequiredExportNames에 제거 심볼 잔존 → 라이브러리 로드 불가 [blocker, 최우선]
`NativeMethods.Core.cs:8-124` `RequiredExportNames`가 `zlink_spot_node_*`·`zlink_spot_route_bridge_*`·
`zlink_msg_gets`·raw STREAM actor 등 ~35개 제거 심볼을 요구. `NativeLibraryLoader
.ValidateRequiredExports()`(`NativeLibraryLoader.cs:147-162`)가 첫 누락에서 DllNotFoundException →
Core 10.0.0(`core/src/libzlink.vers`, staged `libzlink.so.10.0.0` `nm -D` 확인) 대상 **모든 native
호출 즉시 실패**. 빌드 green으로는 불가시. → RequiredExportNames를 현재 10.0.0 export로 재생성,
정의 없는 P/Invoke 제거.

### DF2. actor join-admission reply 경로 파손 [blocker]
`MeshDispatch.cs:226-244` `MeshReceiveRecord.Reply()`가 항상 `zlink_mesh_reply` 호출. Core
`reply_route_t`(`mesh_runtime.hpp:461-466`)는 `kind_actor_join` route에 `zlink_actor_join_reply`
요구, `zlink_mesh_reply`는 그 route를 EINVAL로 거부. 전용 P/Invoke(`NativeMethods.Actor.cs:52`)
선언만 있고 미사용. entry-spot join 승인/거부 불가. → actor control record·join result를 typed
public API로 노출, 전용 join-reply 경로 사용.

### DF3. MeshNode routing-id 설정 표면 부재 [blocker]
`IMeshNode.cs:12` — 읽기 RoutingId·SetBind·AddChannel만. Core start는 routing_id·bind·channel
요구(01-mesh-node §166-172). 여러 actor 샘플은 channel도 없이 즉시 Start. → IMeshNode에
routing-id 설정 연결, 샘플이 고유 rid·bind·channel을 start 전에 설정.

### DF4. caller-init 출력 구조체 size/version 0 → EINVAL [blocker]
공통 계약(README §31-44): caller-init output마다 struct_size·version 선설정, 배열 element도 초기화.
`MeshDispatchRuntime.cs:110-111`(Receive requirements), `MeshNode.cs:173-178`(Status),
`Spot.cs:22-27`, `MeshNode.Actors.cs:43-48`(ActorLookup), `StreamSessionService.cs:36-41`,
`NativeSnapshotReader.cs:30-37`(peer/binding 배열 element) 모두 0 전달. → ref로 전달, 호출 전
struct_size·version=1 설정, 배열 element 초기화.

### DF5. actor transfer fence API 전부 누락 [high]
`actor.h:214-226` prepare/commit/activate/abort + token/result 미노출. → transfer value type·
P/Invoke·public operation·typed transfer-control record 추가.

### DF6. StreamSocket.SetRoutingId가 native 미설정 [high]
`StreamSocket.cs:20-28` — managed field만. `zlink_set_routing_id`(`socket/api.h:136-139`) 미연결
(RouterSocket는 연결됨). → native 연결, local cache를 계약 기준으로 쓰지 않음.

### DF7. batch/claim finalizer·SafeHandle 부재 [medium]
`MeshDispatchRuntime.cs:12-79,92-126,134-215` — ready/receive batch·claim이 unmanaged ownership을
Dispose에서만 해제. 누락 시 claim 계속 점유 → owner dispatch 진행 차단. → SafeHandle 또는 신뢰
finalizer, release 결과·disposed state 일관 관리.

### DF8. Mesh endpoint 256~511B 거부 [medium]
`BoundaryValidation.cs:9-22` 255 validator를 SetBind·ConnectPeer에 사용. Core
`ZLINK_MESH_ENDPOINT_MAX`=511(`mesh_node.h:20`). → endpoint 전용 511 validator.

## I2 POSD·DDD

### DI2-1. raw IStreamSocket이 제거된 actor bind/unbind/send/list 공개 [high]
`IStreamSocket.cs:51-76`·`StreamActorInterop.cs:54-85` — Core 10.0.0은 raw STREAM=transport session,
actor binding은 IStreamSessionService 소유(05-stream-session §7-15). 샘플도 구표면 사용. → raw
STREAM에서 actor 책임 제거, MeshNode.CreateStreamSessionService 단일 경계로 전환.

### DI2-2. typed kind_data 폐기 [medium]
`MeshDispatchRuntime.cs:218-253` — native record의 KindData/KindDataSize를 안 읽고 일반 필드만
복사. actor lifecycle/join completion/send-ready/transfer control을 타입 안전 해석 불가. → 공통
envelope 아래 record-kind별 typed payload, Core-owned view의 batch lifetime을 wrapper가 관리.

## I3 정리 완결성

### DI3-1. 제거 심볼·구개념 잔재 [blocker] (DF1과 동일 root)
- RequiredExportNames·P/Invoke의 제거 심볼(DF1).
- `Message.GetProperty()` → `zlink_msg_gets`(`Message.Native.cs:141-147`, `Message.cs:301-303`) 제거.
- `IRouterSocket.SendToSpot/RequestToSpot/ReplyToSpot`(`RoutedSocketContracts.cs:73,78,84`) →
  `zlink_router_*_spot_part`(10.0.0 ABI 부재) 제거.
- `RouteBridge` native 구조체(`NativeTypes.cs:72-86`) 제거.
- no-hit FAIL: RouteBridge 2·spot_node 25·pub/sub rid 2·msg_gets 3.
→ 제거 심볼·public forwarder·legacy raw STREAM actor 표면·RouteBridge model 삭제. no-hit 통과.

## 처리 방침
coordinator가 격리 수정. Core 계약 준수, 라이브러리+samples 빌드 green, **native load(export
검증) 통과**, no-hit 통과. 참조 lane이므로 typed join-reply·typed kind_data·transfer·metadata
표면을 확정하고 cpp lane과 정합(교차 확인). 완료 후 새 snapshot으로 iteration-2 재검토.
