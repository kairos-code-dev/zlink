# S8 DOTNET bindings 전환 리뷰 — iteration 1 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `29151802f`
- 시작 시점: `git ls-files bindings/dotnet/src bindings/dotnet/samples | grep -vE 'native/|/obj/|/bin/'` → **206개** 파일, aggregate SHA-256 = `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b` (manifest 값과 일치)
- 종료 시점: 동일 명령 재실행 → **206개** 파일, aggregate SHA-256 = `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b` (변경 없음, 리뷰 중 어떤 scope 파일도 수정하지 않았음을 `git status`로 확인)
- 검토 방법: Core 헤더(`mesh_node.h`, `dispatch.h`, `spot.h`, `actor.h`, `stream_session.h`, `socket/api.h`) 전량 정독 + `core/src/libzlink.vers`(10.0.0 공개 ABI 정본) 대조 + 실제 스테이징된 `libzlink.so.10.0.0`의 `nm -D` 심볼 테이블 확인(빌드·실행이 아닌 이미 존재하는 아티팩트의 읽기 전용 검사) + dotnet 소스 206개 파일 그룹별 정독. 실행 작업(build/test/sanitizer)은 수행하지 않음.

## 2. I1 — 계약 구현 일치

### Finding 1 [I1][blocker] — actor join-admission reply가 C# 공개 표면에서 항상 실패한다

- **파일/라인**: `bindings/dotnet/src/Zlink/Contracts/Service/MeshDispatch.cs:226-244` (`MeshReceiveRecord.Reply`), `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Actor.cs:52` (`zlink_actor_join_reply` P/Invoke 선언, 호출부 없음)
- **문제**: Core는 join-admission reply route를 `reply_route_t::kind_actor_join`으로 분리하고, 이 route는 오직 `zlink_actor_join_reply`(join_result 포함)로만 응답할 수 있다. 범용 `zlink_mesh_reply`는 `kind_generic`/`kind_transfer_relay` route에만 쓸 수 있고, `kind_actor_join` route에 호출하면 `EINVAL`/`ZLINK_SUBMIT_INVALID_ARGUMENT`로 즉시 실패한다. 그런데 dotnet C# 공개 표면(`IMeshNode`, `ISpot`, `MeshReceiveRecord`)에는 `MeshReceiveRecord.Reply()` 단 하나의 reply 경로만 존재하며, 이는 항상 native `zlink_mesh_reply`만 호출한다. `zlink_actor_join_reply`는 P/Invoke로 선언만 되어 있고 관리 코드 어디에서도 호출되지 않는다(scoped grep 확인).
- **근거**:
  - `core/src/runtime/services/mesh/mesh_runtime.hpp:461-466` — `reply_route_t::kind_t { kind_generic=1, kind_actor_join=2, kind_transfer_relay=3 }`
  - `core/src/api/mesh/mesh_dispatch_api.cpp` (`zlink_mesh_reply` 구현) — `if (route.kind != reply_route_t::kind_generic && route.kind != reply_route_t::kind_transfer_relay) { errno = EINVAL; return ZLINK_SUBMIT_INVALID_ARGUMENT; }`
  - `core/src/api/mesh/mesh_actor_api.cpp:1356-1359` (`zlink_actor_join_reply` 구현) — `if (route.kind != reply_route_t::kind_actor_join) { errno = EINVAL; return ZLINK_SUBMIT_INVALID_ARGUMENT; }`
  - `core/doc/spec/core/service/04-actor.md:229-248` — "Join ... enqueues a `SPOT_CONTROL` record ... Only `ZLINK_ACTOR_JOIN_ACCEPTED` commits membership. `ZLINK_ACTOR_JOIN_REJECTED` ... Any other value returns `ZLINK_SUBMIT_INVALID_ARGUMENT`"
  - dotnet 18개 샘플 전체(`bindings/dotnet/samples/*/Program.cs`)에 `ActorJoin`/`JoinEntrySpot`/`JoinSpot` 관련 ready-index 처리 코드가 전혀 없음(scoped grep 0건) — 이 경로가 한 번도 실행/검증되지 않았음을 뒷받침
- **영향**: 애플리케이션이 ready-index를 drain하여 `MeshRecordKind.SpotControl` + `OperationKind.ActorJoin` 레코드를 받고 `.Reply(...)`를 호출하면, native 계층에서 항상 `EINVAL`을 반환한다. C# 공개 표면에서는 entry-spot join admission을 accept도 reject도 할 수 없다 — 이는 문서화된 core 필수 흐름(project memory: entry-spot join admission)이며, dotnet이 framework parity의 참조 lane이므로 이 기능이 참조 lane에서부터 완전히 도달 불가능하다.
- **수정 제안**: `MeshReceiveRecord`에 `OperationKind == ActorJoin` 레코드 전용 reply 오버로드(`ReplyJoin(bool accepted, IReadOnlyList<Message> parts, ...)` 등)를 추가하고 내부적으로 `zlink_actor_join_reply`를 호출하도록 배선한다.

### Finding 2 [I1][blocker] — 동일 근본원인: Core 10.0.0 실제 라이브러리에 대해 dotnet 바인딩이 로드 자체에 실패한다

(I3 섹션의 Finding 3과 동일 근본원인 — `NativeLibraryLoader.ValidateRequiredExports()`가 10.0.0에 존재하지 않는 심볼을 여전히 필수로 요구하여 최초 native 호출 시 `DllNotFoundException`을 던짐. 관찰 가능한 동작에 대한 직접적 영향이므로 I1에도 등록. 상세 근거는 I3 Finding 3 참조.)

### 전이(transfer) API 관련 독립 판정 (finding 아님)

`zlink_mesh_node_actor_transfer_prepare/commit/activate/abort`(actor.h)는 dotnet bindings 어디에도 P/Invoke·Contracts·Runtime 표면이 없다. 그러나:
- cpp bindings(`bindings/cpp/`)에도 동일하게 전무 — dotnet 고유 결함이 아니라 전체 언어 공통의 사전 존재 gap이다.
- 이번 검토 대상 commit(`29151802f`, 샘플을 MeshNode/pull-dispatch로 전환)은 이 API를 다루지 않으며 회귀시키지 않았다.
- `core/doc/spec/core/service/04-actor.md:339` — "The framework location store owns the transfer authority record" — 이 API는 애플리케이션이 아닌 framework 런타임의 내부 소비자를 상정한 설계로 읽힌다. 실제로 `framework/languages/dotnet`과 `framework/languages/cpp`는 actor transfer를 이 fence API가 아닌 별도의 애플리케이션 계층 메커니즘(`ZLinkActorTransferRegistry` 등)으로 구현하고 있어, 어떤 언어의 framework도 이 저수준 fence API를 아직 소비하지 않는다.
- 따라서 관찰 가능한 동작 불일치나 dotnet 고유의 계약 결함으로 보기 어려워 finding으로 등록하지 않는다.

### 그 외 계약 대조

- `zlink_mesh_node_*`, `zlink_mesh_ready_*`, `zlink_mesh_receive_batch_*`, `zlink_mesh_claim_*`, `zlink_spot_*`, `zlink_mesh_node_actor_*`, `zlink_stream_session_*` P/Invoke 시그니처(`NativeMethods.{MeshNode,Dispatch,Spot,Actor,StreamSession}.cs`)는 대응 Core 헤더와 인자 순서·타입·nuint/uint 매핑이 일치.
- `struct_size`/`version` 필드는 호출 전 `Marshal.SizeOf<T>()`와 `1`로 채워짐(`MeshNode.cs:81,398`, `MeshNodePublisher.cs:34`, `Spot.cs:107` 등에서 확인).
- pull-dispatch 수명(`MeshReadyBatch`/`MeshClaim`/`MeshReceiveBatch`)은 모두 `IDisposable`로 노출되고, `Dispose()`가 대응 native destroy/release를 호출함(`MeshDispatchRuntime.cs`).
- `IMeshNode`/`ISpot`/`IStreamSessionService`는 시작 전 필수 설정(`SetBind`, `AddChannel`, routing id는 start 후 read-only)을 정확히 노출.

**Verdict I1: NOT CLEAN** (2건, blocker 2 — 동일 근본원인군 아님, 독립 결함 2개)

## 3. I2 — POSD·DDD 리팩터링

- Finding: 없음
- **Evidence**: 파일 크기 분포 확인 결과 god-file 없음(최대 `TypedExceptions.cs` 610줄, `RouterSocket.cs` 477줄, `MeshNode.cs` 475줄 — 모두 책임 범위 내에서 합리적). `NativeMethods.*`/`SocketKernel.*`는 partial class로 책임별 분할되어 있음. `Spot.cs`/`MeshNode.cs`는 Core 함수 1:1에 가까운 얇은 번역 계층이며, 이는 바인딩 계층(하위 framework의 기반)으로서 적절한 깊이 — 불필요한 추가 추상화를 넣지 않은 것은 POSD 위반이 아니라 올바른 설계.
- **coordinator 관찰(pull-dispatch 저수준성) 독립 판정**: `bindings/dotnet/samples/SampleCommon/SampleSupport.cs:112`의 `PumpReady` 헬퍼가 모든 샘플에 반복 필요한 것은 사실이나(`SpotRpcExample`, `SpotRequestAsync` 등에서 재사용), 이는 Core 자체가 ready-index/claim/batch/reply-token을 저수준 primitive로만 제공하도록 설계했기 때문(Core 헤더에 상위 편의 함수 없음)이며, dotnet bindings는 참조 lane으로서 이 계약을 충실히·정직하게 그대로 노출하고 있다. 편의 계층(이벤트 루프, async pull 헬퍼)은 framework 계층(`framework/languages/dotnet`)의 책임으로 분리하는 것이 이 저장소의 계층 원칙과 일치한다(불필요한 추상화를 바인딩에 넣지 말라는 프로젝트 방침과도 부합). 따라서 이는 I2 finding으로 등록하지 않는다.
- **Verdict I2: CLEAN**

## 4. I3 — 정리 완결성

### Finding 3 [I3][blocker] — 사전-10.0.0 폐기 심볼이 필수 export 검증 목록에 대량 잔존, 로드 시 항상 실패

- **파일/라인**: `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Core.cs:8-124` (`RequiredExportNames` 배열), `bindings/dotnet/src/Zlink/Runtime/Native/NativeLibraryLoader.cs:147-162` (`ValidateRequiredExports`, 누락 시 `DllNotFoundException` 즉시 throw)
- **문제**: `RequiredExportNames`에 `zlink_spot_node_*`(15개 이상), `zlink_spot_route_bridge_*`(6개), `zlink_spot_actor_join_recv`/`zlink_spot_actor_join_reply`, `zlink_spot_recv_subscription_event`, `zlink_spot_node_spots`/`zlink_spot_actors`/`zlink_spot_node_actors`, `zlink_spot_dispatch_event_handler`, `zlink_spot_recv_part`, `zlink_router_request_spot_part`/`zlink_router_reply_spot_part`/`zlink_router_send_spot_part`, `zlink_spot_send_spot_part`/`zlink_spot_request_spot_part`/`zlink_spot_request_router_part`/`zlink_spot_reply_spot_part`/`zlink_spot_reply_router_part`, `zlink_spot_send_channel_part`/`zlink_spot_request_channel_part`/`zlink_spot_publish_part`/`zlink_spot_subscribe_part`, `zlink_set_spot_node_option`/`zlink_get_spot_node_option`, `zlink_remote_actor_get_ref`, `zlink_msg_gets` 등 프롬프트가 명시적으로 "10.0.0 등가물 없음"이라 규정한 폐기 개념(route_bridge, subscription event stream, spot-level actor 열거, per-message ZMTP metadata) 심볼이 그대로 남아 있다. `NativeLibraryLoader.EnsureLoaded()`는 어떤 로드 경로를 타든 `ValidateRequiredExports()`를 호출하고, 이 목록의 모든 항목이 로드된 라이브러리에 존재해야 통과한다.
- **근거**:
  - `core/src/libzlink.vers`("Generated from the formal 10.0.0 spec: exactly the public C ABI") — 위 심볼들이 전부 부재(196개 `zlink_*` export만 존재, `zlink_spot_node`/`route_bridge`/`msg_gets`/`router_request_spot_part` 등 0건)
  - 실제 스테이징된 `bindings/dotnet/native/linux-x64/libzlink.so.10.0.0`에 대한 `nm -D --defined-only` 결과도 동일하게 0건 확인(빌드/실행이 아닌 기존 아티팩트 읽기 전용 검사)
  - `NativeLibraryLoader.cs:147-162` — 루프 중 첫 누락 export에서 `throw new DllNotFoundException(...)`
- **영향**: dotnet bindings가 실제 Core 10.0.0 공유 라이브러리에 대해 어떤 native 호출이든 최초 1회만 시도해도 라이브러리 로드 검증에서 즉시 실패한다. `dotnet build`는 컴파일만 수행하므로 이 결함을 가리지 못했다(manifest의 "0 error, 0 warning"은 빌드 결과일 뿐 실행 결과가 아님). 18개 샘플 중 어느 것도 실행하면 첫 native 호출에서 크래시한다 — 즉 이번 커밋 이후 dotnet bindings는 Core 10.0.0과 통합 시 완전히 비기능 상태다.
- **수정 제안**: `RequiredExportNames`를 `core/src/libzlink.vers`의 10.0.0 공개 ABI 목록과 일치하도록 재작성(불필요하다면 이 검증 게이트 자체를 재고).

### Finding 4 [I3][high] — 폐기된 raw ROUTER↔spot 브리징 공개 계약이 여전히 노출되고, 존재하지 않는 심볼로 배선되어 있다

- **파일/라인**: `bindings/dotnet/src/Zlink/Contracts/Sockets/RoutedSocketContracts.cs:73,78,84` (`IRouterSocket.SendToSpot/RequestToSpot/ReplyToSpot`), `bindings/dotnet/src/Zlink/Runtime/Sockets/RouterSocket.cs:210,267,308,345`, `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.cs:65`, `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.ReceivedSend.cs:129,164`
- **문제**: raw `ROUTER` 소켓에서 spot으로 직접 request/reply/send하는 pre-10.0.0 전용 브리징 기능(`zlink_router_request_spot_part`/`zlink_router_reply_spot_part`/`zlink_router_send_spot_part`)이 `IRouterSocket` 공개 인터페이스 메서드로 여전히 노출되고 실제로 배선되어 있다. 이 세 심볼 모두 Finding 3의 근거(libzlink.vers, nm)에서 확인했듯 Core 10.0.0에 존재하지 않는다.
- **영향**: `IRouterSocket.SendToSpot`/`RequestToSpot`/`ReplyToSpot`을 호출하는 애플리케이션 코드는 (Finding 3의 로드 게이트를 우회한다 해도) `EntryPointNotFoundException`으로 실패한다. RouteMesh 10.0.0에서 spot 주소 지정은 `zlink_spot_send_to_spot`/`MeshNode`/`ISpot` 경로로만 이뤄지며, raw ROUTER 소켓에서 직접 spot을 대상으로 하는 것은 폐기된 route-bridge류 개념의 잔재다.
- **수정 제안**: `IRouterSocket`에서 `SendToSpot`/`RequestToSpot`/`ReplyToSpot`과 그 구현·P/Invoke 선언을 제거한다.

### Finding 5 [I3][medium] — `zlink_msg_gets`(폐기된 per-message ZMTP metadata)가 여전히 활성 공개 API로 배선되어 있다

- **파일/라인**: `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Core.cs:219-220`(P/Invoke 선언), `bindings/dotnet/src/Zlink/Runtime/Messaging/Message.Native.cs:144`(호출부), `bindings/dotnet/src/Zlink/Contracts/Messaging/Message.cs:301-304`(공개 API `Message.GetProperty(string)`)
- **문제**: 프롬프트가 명시한 "폐기되어 10.0.0 등가물이 없는 것" 목록에 `zlink_msg_gets`가 정확히 포함되어 있다. 그럼에도 `Message.GetProperty()`라는 공개 메서드가 여전히 존재하고 내부적으로 `zlink_msg_gets`를 호출한다.
- **영향**: `Message.GetProperty()`를 호출하면 (Finding 3의 로드 게이트를 우회한다 해도) `EntryPointNotFoundException`이 발생한다. 공개 API 표면에 폐기된 개념이 살아있는 메서드로 노출되어 있다.
- **수정 제안**: `Message.GetProperty()`와 하위 `zlink_msg_gets` P/Invoke·구현을 제거한다.

### Finding 6 [I3][low] — RouteBridge 명명 잔재 구조체(미사용)와 폐기 push-dispatch 델리게이트 타입

- **파일/라인**: `bindings/dotnet/src/Zlink/Runtime/Native/NativeTypes.cs:72,81`(`ZlinkSpotRouteBridgeOptions`, `ZlinkSpotRouteBridgeEndpointOptions`), `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Core.cs:348-366`(`ZlinkActorJoinHandlerDelegate`, `ZlinkActorJoinEntrySpotHandlerDelegate`, `ZlinkActorLookupHandlerDelegate`, `ZlinkSpotDispatchEventHandlerDelegate`)
- **문제**: `RouteBridge`라는 폐기 개념 이름을 그대로 쓰는 구조체 2개, 그리고 pull-dispatch 이전의 push-dispatch/콜백 기반 핸들러 델리게이트 타입 4개가 선언되어 있으나 선언 파일 밖에서 전혀 사용되지 않는다(scoped grep으로 확인, dead declarations).
- **영향**: 컴파일이나 런타임 동작에는 영향 없음(죽은 선언). 다만 프롬프트가 명시한 "RouteBridge" 폐기 개념명 자체가 코드에 리터럴로 남아 있고, 이는 I3 no-hit 판정과 직접 상충한다.
- **수정 제안**: 미사용 구조체·델리게이트 4~6종 삭제.

### 폐기 개념 no-hit 판정 (프롬프트 요구 scoped grep)

| 개념 | grep 대상 | 결과 |
|---|---|---|
| SpotNode | `\bSpotNode\b` | 0건 |
| RouteBridge | `RouteBridge` | **2건** — `NativeTypes.cs:72,81` (Finding 6) |
| spot_node | `spot_node` | **다수** — `NativeMethods.Core.cs` `RequiredExportNames` 및 P/Invoke 선언 내 문자열(Finding 3) |
| subjects | `\bsubjects\b` | 0건 |
| internal_sockets | `internal_sockets`/`InternalSockets` | 0건 |
| pub-sub 별도 routing_id | `set_pub_routing_id`/`set_sub_routing_id` | **2건** — `NativeMethods.Core.cs:79-80` `RequiredExportNames` 문자열(Finding 3에 포함) |
| dispatch_workers | `dispatch_workers`/`DispatchWorkers` | 0건 |
| recv_actor_part | `recv_actor_part`/`RecvActorPart` | 0건 (단, `zlink_spot_node_actor_recv_part`가 `RequiredExportNames`에 존재 — Finding 3에 포함) |
| msg_gets | `msg_gets` | **3건** — Finding 5 |

**Verdict I3: NOT CLEAN** (4건: blocker 1, high 1, medium 1, low 1)

## 5. 종합

세 축 중 I1(2건, blocker 2), I3(4건: blocker/high/medium/low 각 1)에서 finding이 발견되어 iteration 1 기준(모든 심각도 0이어야 CLEAN)을 충족하지 못한다. I2는 CLEAN이다.

가장 심각한 결함(Finding 3)은 이번 리뷰에서 처음 확인된 것으로 보이며, `dotnet build` 그린이라는 실행 증거만으로는 탐지되지 않는다 — 실제 Core 10.0.0 네이티브 라이브러리에 대해 dotnet bindings가 최초 native 호출 시점에 항상 로드 실패하는 상태다. Finding 1(actor join-admission reply 불능)은 coordinator가 미리 관찰로 남긴 지점을 소스 레벨로 검증한 결과이며, 단순한 편의 메서드 누락이 아니라 native 계층에서 항상 `EINVAL`을 반환하는 기능적 결함임을 확인했다.

BINDINGS REVIEW NOT CLEAN
