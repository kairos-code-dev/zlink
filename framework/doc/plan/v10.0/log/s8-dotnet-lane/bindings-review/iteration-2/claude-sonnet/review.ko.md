# S8 DOTNET bindings 전환 리뷰 — iteration 2 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `115c3d73d`
- 시작 시점: `git ls-files bindings/dotnet/src bindings/dotnet/samples | grep -vE 'native/|/obj/|/bin/'` →
  **208개** 파일, aggregate SHA-256 = `d6acf3e49cdb1f96aac8d92e6b403d79502f2f65866687581b0dc4308ad4c048`
  (manifest 값과 일치)
- 종료 시점: 동일 명령 재실행 → **208개** 파일, 동일 hash (변경 없음, `git status`로 scope 파일 무수정 확인)
- 현재 `HEAD`(`77dc73cd7`)와 대상 commit `115c3d73d` 사이 `bindings/dotnet/src`·`bindings/dotnet/samples`
  diff는 0줄(`git diff 115c3d73d HEAD -- bindings/dotnet/src bindings/dotnet/samples`) — `HEAD`에서
  검토해도 대상 commit과 완전히 동일한 내용.
- 검토 방법: iteration-1 finding-ledger + 양 리뷰(`codex`, `claude-sonnet`) + `s8-common-raw-layer-drift.ko.md`
  정독 → 각 finding을 소스 대조로 해소 판정 → Core 헤더(`core/include/zlink/**/*.h`) 전량과
  `core/src/libzlink.vers`(10.0.0 export 정본) + 스테이징된 `libzlink.so.10.0.0`의 `nm -D` 결과를 기준으로
  dotnet 208개 파일 전량을 재정독. 특히 **전체 185개 P/Invoke 선언을 스크립트로 추출해 Core 헤더 시그니처와
  파라미터 개수를 자동 대조**하는 방식으로 raw 계층 드리프트를 전수 조사했다(직접 작성한 1회성 분석
  스크립트, 결과는 아래 §3에 근거로 인용 — scope 파일은 수정하지 않음). build/실행은 하지 않음.

## 2. iteration-1 finding 해소 판정

전량 해소로 판정한다. 반례 없음.

| Finding | 판정 | 근거 |
|---|---|---|
| DF1 RequiredExportNames 제거 심볼 잔존 | **해소** | `NativeMethods.Core.cs:13-195`의 182개 전부 `core/src/libzlink.vers`(196개 export)에 존재, 스테이징된 `libzlink.so.10.0.0`의 `nm -D --defined-only` 결과에도 전부 존재(diff 0건 확인) |
| DF2 actor join-admission reply 파손 | **해소** | `MeshDispatch.cs:282-315` `ReplyJoin(ActorJoinResult, ...)`/`AcceptJoin`/`RejectJoin`이 `NativeMethods.zlink_actor_join_reply`(`NativeMethods.Actor.cs:52`)를 직접 호출 |
| DF3 MeshNode routing-id 설정 표면 부재 | **해소** | `IMeshNode.cs:20` `SetRoutingId(RoutingId)` 추가, `zlink_set_routing_id`와 연결(`MeshNode.cs:30`). 샘플도 `SampleSupport.cs:106-109`에서 `SetRoutingId`→`SetBind`→`AddChannel`→`Start` 순서로 통일 사용 |
| DF4 caller-init 구조체 size/version 0 | **해소** | `MeshDispatchRuntime.cs:124`, `MeshNode.cs:96,201,426`, `Spot.cs:27,121`, `MeshNode.Actors.cs:50,234,251`, `StreamSessionService.cs:42` 전부 `StructSize = (uint)Marshal.SizeOf<T>()`, `Version = 1` 설정 확인 |
| DF5 actor transfer fence API 누락 | **해소** | `IMeshNode.cs:162-179` `PrepareActorTransfer/CommitActorTransfer/ActivateActorTransfer/AbortActorTransfer` 4종 전부 노출, `NativeMethods.Actor.cs:78-92` P/Invoke 배선. `ZlinkActorTransferPrepare`/`...PrepareResult`/`...Control`(`NativeMeshModels.cs:239-278`) 필드 순서·타입이 `actor.h:88-123`과 1:1 일치 확인 |
| DF6 StreamSocket.SetRoutingId native 미설정 | **해소** | `StreamSocket.cs:18-25` `Kernel.SetOption(SocketOptions.RoutingId, ...)` 사용으로 통일(다른 routed 소켓과 동일 경로) |
| DF7 batch/claim finalizer 부재 | **해소** | `MeshDispatchRuntime.cs:79,140,244` `GC.SuppressFinalize` + `~MeshReadyBatch()`(79)/`~MeshClaim()`(147)/`~MeshReceiveBatch()`(248) finalizer 확인 |
| DF8 Mesh endpoint 256~511B 거부 | **해소** | `BoundaryValidation.cs:9-14` `MeshEndpointMaxBytes = 511` 전용 validator 신설, `MeshNode.cs:47,88`(`SetBind`/`ConnectPeer`)에서 사용 |
| DI2-1 raw IStreamSocket actor bind/unbind 잔존 | **해소** | `IStreamSocket.cs`에 `BindActor`류 없음. `IStreamSessionService.cs:77-81`로 단일화. `StreamActorInterop.cs`는 `ActorInterop`(actor-ref marshalling 헬퍼)로 재정의되어 `MeshNode.Actors.cs`/`MeshDispatchRuntime.cs`/`StreamSessionService.cs`가 공유 |
| DI2-2 typed kind_data 폐기 | **해소** | `MeshDispatchRuntime.cs:282-284,307-367` `DecodeKindData`가 `native.KindData`/`KindDataSize`를 `MeshRecordKind`/`MeshOperationKind`별로 typed record(`ActorJoinCompletion` 등)로 해석 |
| DI3-1 제거 심볼·구개념 잔재 no-hit | **해소** | scope 전체 재실행 결과 `SpotNode`/`RouteBridge`/`spot_node`/`subjects`/`internal_sockets`/`set_pub_routing_id`\|`set_sub_routing_id`/`dispatch_workers`/`recv_actor_part`/`msg_gets` 전부 0건 |

## 3. I1/I2/I3 Finding·Evidence·Verdict

### I1 — 계약 구현 일치

DF1~DF8이 잡아낸 issue 계열(로드 게이트·join reply·구조체 세팅·finalizer 등)은 전부 해소됐으나,
**전체 185개 P/Invoke 선언을 Core 헤더 시그니처와 자동 대조**한 결과 iteration-1에서 다루지 않은
새로운 raw 계층 드리프트를 3건 확인했다. 이는 프롬프트가 명시한 "raw 계층 드리프트(예:
`zlink_subscribe_handler` 제거 — Core 미export이나 P/Invoke·사용 잔존 시 파손) 판정" 범주에 해당한다.

#### Finding I1-1 [blocker] — `zlink_router_recv_part` P/Invoke에 Core가 제거한 파라미터가 남아, ROUTER 소켓 수신마다 네이티브 스택을 손상시킨다

- **파일/라인**: `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Socket.cs:58-67`
  (`zlink_router_recv_part`, `zlink_router_recv_part_nowait`), 호출부
  `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Receive.cs:100-109`,
  `SocketKernel.ReceiveCore.cs:89,182-187`
- **문제**: Core 10.0.0의 `zlink_router_recv_part`는 6개 파라미터다
  (`core/include/zlink/socket/api.h:271-277`: `router_, source_node_rid_out_, request_seq_out_, part_out_,
  has_more_out_, flags_`). `s8-common-raw-layer-drift.ko.md` §2가 명시한 대로 이전 버전에 있던
  `spot_rid` out-param이 10.0.0에서 제거됐고, cpp·node lane은 이미 수정했다고 기록돼 있다. 그러나
  dotnet의 P/Invoke 선언은 여전히 7개 파라미터를 요구한다: `router, out sourceNodeRoutingId,
  out sourceSpotRoutingId, out requestSeq, ref part, out hasMore, flags`. 세 번째 위치에 남은
  `sourceSpotRoutingId`가 그 뒤 모든 인자를 한 칸씩 밀어낸다.
- **근거(ABI 분석)**: x86-64 System V cdecl에서 처음 6개 포인터/정수 인자는
  `rdi, rsi, rdx, rcx, r8, r9`에 실린다. 호출부(`SocketKernel.Receive.cs:108`)가 넘기는 순서대로
  네이티브가 실제로 받는 값은:
  - `rdx`(managed 3번째 인자 `sourceSpotRoutingId`의 주소) → native 3번째 파라미터 `request_seq_out_`로
    해석돼, native가 8바이트 `uint64_t` 값을 여기 쓴다(크기는 우연히 맞음, 의미만 틀림).
  - `rcx`(managed 4번째 인자 `requestSeq`, `out ulong` — 스택에 8바이트만 할당된 지역변수의 주소) →
    native 4번째 파라미터 `part_out_`(`zlink_msg_t*`)로 해석돼, native가
    **64바이트짜리 `zlink_msg_t`**(`core/include/zlink/message/api.h:16-29`, `ZlinkMsg.Data[64]`와 동일
    크기)를 그 8바이트 지역변수 위치에 통째로 옮겨 쓴다 → **56바이트 스택 버퍼 오버플로우**.
  - `r8`(managed 5번째 인자 `part`, 실제 메시지를 받아야 할 64바이트 버퍼의 주소) → native 5번째
    파라미터 `has_more_out_`(`zlink_part_flag_t*`, 4바이트)로 해석돼, 정작 메시지 내용은 `part`에
    전혀 쓰이지 않는다.
  - `r9`(managed 6번째 인자 `hasMore`의 스택 변수 주소, 즉 유효한 메모리 주소 값) → native 6번째
    파라미터 `flags_`(값 전달 `zlink_recv_flags_t`, 정수 비트마스크)로 **그대로 정수값처럼 해석**된다
    — 임의의 대형 정수를 flags로 넘기는 셈이 된다.
  - managed 7번째 인자(`flags`, 스택에 쌓임)는 native가 6개 파라미터만 읽으므로 전혀 읽히지 않는다.
  - 비교: 같은 파일의 `zlink_dealer_recv_part`(`NativeMethods.Core.cs:299-301`, 6파라미터)와
    `zlink_router_request_part`/`zlink_router_reply_part`(`NativeMethods.Core.cs:308-324`,
    각 8/5파라미터)는 Core 헤더와 정확히 일치 — `recv_part`만 고립된 드리프트다.
- **재현 경로**: `IRouterSocket.Recv(...)`(`RoutedSocketContracts.cs:24`) → `SocketKernel.Receive`/
  `ReceiveCore` → `_policy.UsesRouterRoutedReceiveEnvelope`(`SocketTypePolicy.cs:10`, ROUTER 소켓이면
  항상 true) → 위 경로. `RequestReplyAsync`, `DealerRouterRecv` 샘플이 ROUTER 소켓 수신을 사용한다.
  즉 **ROUTER 소켓으로 메시지를 하나라도 수신하면 항상** 이 경로를 탄다.
- **영향**: 스택 손상(문서화된 56바이트 오버런)과 메시지 유실(수신 페이로드가 실제로는 `part`에
  전달되지 않음)이 매 호출마다 발생한다. `dotnet build`로는 감지되지 않으며(컴파일은 성공), 첫 실제
  ROUTER 수신 시점에 크래시하거나 조용히 스택을 훼손한다 — DF1이 지적한 "빌드 green으로는 불가시"와
  동일한 성격의, 더 심각한 결함이다.
- **수정 제안**: `zlink_router_recv_part`/`_nowait`에서 `sourceSpotRoutingId` 파라미터를 제거해
  Core 6-파라미터 시그니처와 일치시키고, 호출부·후속 처리(`ReceiveRouterParts` 등)에서
  spot-routing-id 복원 로직도 함께 제거한다.

#### Finding I1-2 [high] — `zlink_msg_refcnt` P/Invoke에 Core가 요구하는 `error_out_` 파라미터가 빠져 있다

- **파일/라인**: `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Core.cs:288`
  (`internal static extern int zlink_msg_refcnt(ref ZlinkMsg msg);`), 호출부
  `bindings/dotnet/src/Zlink/Runtime/Messaging/Message.Native.cs:57`, 공개 API
  `bindings/dotnet/src/Zlink/Contracts/Messaging/Message.cs:75`(`Message.RefCount`)
- **문제**: Core `zlink_msg_refcnt`는 2개 파라미터다(`core/include/zlink/message/api.h:116`:
  `zlink_msg_refcnt (const zlink_msg_t *msg_, zlink_config_result_t *error_out_)`). 구현
  (`core/src/api/message/message_api.cpp:146-156`)은 성공 경로에서도 `if (error_out_) *error_out_ =
  ZLINK_CONFIG_OK;`로 **항상 두 번째 인자를 통해 쓰기를 시도**한다(널 체크는 있지만, null 여부는
  managed가 넘긴 값이 아니라 해당 레지스터에 우연히 남아있는 값에 좌우된다). dotnet은 `ref ZlinkMsg msg`
  단 1개 인자만 선언해, native의 2번째 파라미터 슬롯(`rsi`)에는 호출 시점 레지스터에 남아있던 임의
  값이 그대로 전달된다.
- **재현 경로**: 공개 `Message.RefCount` 프로퍼티(`Message.cs:75`)를 호출하면 항상 이 경로를 탄다.
- **영향**: `error_out_`에 해당하는 레지스터 내용이 우연히 0이면 안전하게 넘어가지만, 0이 아닌 임의
  주소(예: 이전 호출에서 남은 포인터)라면 그 주소에 4바이트를 쓰는 정의되지 않은 동작(크래시 또는
  임의 메모리 변조)이 된다. 환경/직전 호출 이력에 따라 재현 여부가 갈리는 전형적인 미정의 동작(UB)
  버그이지만, ABI 계약 위반 자체는 항상 성립한다.
- **수정 제안**: `out ZlinkConfigResult errorOut` 파라미터를 추가해 Core 시그니처와 일치시킨다.

#### Finding I1-3 [blocker] — 공개 `IStreamSocket.DetachStream()`이 Core 10.0.0에 없는 `zlink_stream_detach`를 호출해 항상 실패한다

- **파일/라인**: `bindings/dotnet/src/Zlink/Contracts/Sockets/IStreamSocket.cs:44`(`DetachStream()` 계약),
  `bindings/dotnet/src/Zlink/Runtime/Sockets/StreamSocket.cs:38-41`,
  `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Stream.cs:129-137`(`Kernel.DetachStream()`),
  P/Invoke 선언 `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Socket.cs:63-64`
- **문제**: Core 10.0.0의 `socket/api.h`는 STREAM 소켓용으로 `zlink_stream_packet_handler`
  하나만 export한다(85-86행). `zlink_stream_detach`는 Core 헤더·`libzlink.vers`·스테이징된
  `.so`의 `nm -D` 결과 어디에도 없다(직접 확인). 그런데 dotnet의 공개 `IStreamSocket.DetachStream()`
  구현이 `_streamAttached`가 true일 때(즉 `OnPacket(handler)`로 패킷 핸들러를 붙인 뒤)
  이 존재하지 않는 심볼을 직접 호출한다:
  ```csharp
  public void DetachStream()
  {
      ...
      if (!_streamAttached) return;
      var rc = NativeMethods.zlink_stream_detach(Handle);   // EntryPointNotFoundException
      ...
  }
  ```
  같은 심볼이 `SocketKernel.Lifecycle.cs:19-31`의 `Dispose()` 경로에서도 호출되지만 그쪽은
  `try { ... } catch { }`로 예외를 삼켜 조용히 무시된다 — 명시적 `DetachStream()` 호출 경로만
  예외가 그대로 전파된다.
- **재현 경로**: `stream.OnPacket(handler)`(`StreamPacketCallback`, `ActorGatewayRelay` 샘플이 실사용)
  뒤 `stream.DetachStream()`을 호출하면(인터페이스 문서 자체가 "현재 붙어있는 stream peer를 분리한다"고
  기술한 정상 사용 패턴) 항상 `EntryPointNotFoundException`이 발생한다. 이를 우회할 다른 공개 경로는
  없다.
- **영향**: `IStreamSocket` 공개 표면에서 명시적 detach가 항상 실패한다 — DF2("join-admission reply가
  공개 표면에서 항상 실패")와 동일한 심각도·성격의 결함이다.
- **수정 제안**: `DetachStream()`을 제거하거나(Core에 대응 기능이 없으므로), Core가 실제로 지원하는
  방식(예: 소켓 close로만 분리 가능하다면 그렇게 문서화)으로 재정의한다.

**Verdict I1: NOT CLEAN** (3건: blocker 2, high 1)

### I2 — POSD·DDD

iteration-1과 동일하게 파일 크기 분포·책임 분리를 재확인했다: 최대 파일 `TypedExceptions.cs`(610줄,
공개 예외 계층이라 크기가 자연스러움), `MeshNode.cs`(503줄), `Poller.cs`(405줄) — god-file 없음.
`NativeMethods.*`/`SocketKernel.*`는 여전히 partial class로 책임별 분할돼 있다. iteration-1 이후
추가된 `MeshNode.Actors.cs`(transfer API), `BoundaryValidation.cs`(511 validator), `ActorTransfer.cs`
등도 단일 책임 파일로 적절히 분리됐다.

Finding I1-3(§3)에서 드러난 `SocketKernel.Stream.cs`의 `AttachStreamRaw`류 죽은 코드(§I3 참고)는
POSD 관점에서도 "레퍼런스 lane이 얇은 1:1 번역 계층이어야 한다"는 원칙과 상충하지만, 그 성격은
정리 완결성(I3) 쪽이 더 정확한 분류라 판단해 I3에 등록했다(I2에는 중복 등록하지 않음).

- Finding: 없음
- **Verdict I2: CLEAN**

### I3 — 정리 완결성

DI3-1이 다룬 리터럴 문자열 목록(`SpotNode`/`RouteBridge`/`spot_node`/`msg_gets` 등)은 전부 0건으로
재확인했다(§4). 그러나 그 목록 밖에 있는 **또 다른 raw 계층 폐기 심볼 잔재**를 발견했다 — manifest
§3이 명시적으로 "스스로 판정하라"고 지시한 `zlink_subscribe_handler` 및 인접 `zlink_stream_attach_raw`다.

#### Finding I3-1 [medium] — raw 계층에서 제거된 `zlink_stream_attach_raw`·`zlink_subscribe_handler` P/Invoke와 그 지원 코드가 죽은 채로 남아 있다

- **파일/라인**:
  - `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Socket.cs:71-73`(`zlink_stream_attach_raw`
    P/Invoke), `:87-88`(`zlink_subscribe_handler` P/Invoke)
  - `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Stream.cs:9-63`(`AttachStreamRaw(StreamRawPacketHandler)`,
    `AttachStreamRaw(StreamUInt32PacketHandler)`, `OnStreamRaw`, `OnStreamRawUInt32` — 총 4개 메서드)
  - `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Callbacks.cs:51-77`(`SubscribeHandler`)
  - `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketCallbacks.cs:10,12,19`(`StreamRawPacketHandler`,
    `StreamUInt32PacketHandler`, `SocketSubscribeHandler` delegate 타입)
  - `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketCallbackRegistry.cs:17,19,20,22-25,48-50`
    (관련 필드)
  - `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketTypePolicy.cs:54,96`(`SocketCapability.SubscribeHandler`)
- **문제**: Core 10.0.0은 `zlink_stream_attach_raw`(구 raw STREAM 콜백 attach)와
  `zlink_subscribe_handler`(구 push-dispatch 구독 콜백)를 export하지 않는다(`libzlink.vers`, `nm -D`
  모두 부재 확인). 그런데 dotnet은 두 심볼 모두 여전히 P/Invoke 선언·호출부·지원 delegate·콜백
  레지스트리 필드·`SocketTypePolicy` capability 분기까지 온전히 유지하고 있다. scoped grep으로
  전체 208개 파일을 확인한 결과 `AttachStreamRaw`·`SubscribeHandler`(Kernel 메서드)를 부르는 공개
  Contracts 표면이나 samples 코드는 **0건**이다 — 즉 현재는 도달 불가능한 죽은 코드지만, 존재
  자체가 (a) 폐기된 push-dispatch/raw-attach 개념의 리터럴 잔재이며 (b) 향후 누군가 이 internal
  메서드를 공개 표면에 연결하면 Finding I1-3과 동일한 `EntryPointNotFoundException`을 재도입하는
  함정이 된다.
- **영향**: 런타임 동작에는 영향 없음(도달 불가). 다만 raw 계층 드리프트 정리가 완결되지 않았다 —
  `s8-common-raw-layer-drift.ko.md`가 정확히 이 두 심볼을 "dead P/Invoke, 각 lane에서 제거" 대상으로
  지목했음에도 아직 반영되지 않았다.
- **수정 제안**: `AttachStreamRaw`(2 오버로드) · `OnStreamRaw`/`OnStreamRawUInt32` · `SubscribeHandler`
  메서드와 관련 delegate 타입(`StreamRawPacketHandler`, `StreamUInt32PacketHandler`,
  `SocketSubscribeHandler`, `ZlinkStreamOnRawDelegate`, `ZlinkSubscribeHandlerDelegate`)·콜백 레지스트리
  필드·`SocketCapability.SubscribeHandler` 분기·두 P/Invoke 선언을 전부 삭제한다.

### 폐기 개념 no-hit 판정 (프롬프트 요구 scoped grep, 재실행)

| 개념 | grep 대상 | 결과 |
|---|---|---|
| SpotNode | `\bSpotNode\b` | 0건 |
| RouteBridge | `RouteBridge` | 0건 |
| spot_node | `spot_node` | 0건 |
| subjects | `\bsubjects\b` | 0건 |
| internal_sockets | `internal_sockets`\|`InternalSockets` | 0건 |
| pub-sub 별도 routing_id | `set_pub_routing_id`\|`set_sub_routing_id` | 0건 |
| dispatch_workers | `dispatch_workers`\|`DispatchWorkers` | 0건 |
| recv_actor_part | `recv_actor_part`\|`RecvActorPart` | 0건 |
| msg_gets | `msg_gets` | 0건 |

DI3-1이 다룬 9개 항목은 전부 no-hit(PASS)이다. 다만 §3 Finding I3-1이 지적하듯, 이 리스트에 없는
`zlink_stream_attach_raw`/`zlink_subscribe_handler` 잔재가 별도로 존재하므로 I3 전체 결과는
NOT CLEAN이다.

**Verdict I3: NOT CLEAN** (1건: medium)

## 4. 종합

iteration-1에서 확인된 8개 blocker/high급 결함(DF1-DF8)과 2개 POSD 관찰(DI2-1, DI2-2), 그리고 정리
완결성 결함(DI3-1)은 commit `115c3d73d`에서 전부 실제로 해소됐다 — 반례 없이 확인했다.

그러나 프롬프트가 명시적으로 요구한 "raw 계층 드리프트... P/Invoke·사용 잔존 시 파손" 판정과 전체
scope 재검토 과정에서, **iteration-1이 다루지 않은 새로운 결함 3건(I1)과 1건(I3)**을 발견했다. 특히
Finding I1-1(`zlink_router_recv_part` 파라미터 개수 불일치)은 ROUTER 소켓 수신마다 스택을 손상시키는
매우 심각한 ABI 계약 위반으로, `dotnet build` green이나 samples 컴파일 성공으로는 전혀 감지되지 않는
종류의 결함이다 — DF1이 iteration-1에서 지적한 것과 정확히 같은 함정("빌드 green≠런타임 정상")이
이번에도 다른 지점에서 재현됐다. 이 발견은 전체 185개 P/Invoke 선언을 Core 헤더 시그니처와 자동
대조하는 방법으로 얻었으며, 육안 검토만으로는 놓치기 쉬운 종류의 결함이다.

I2는 CLEAN이다. I1·I3는 NOT CLEAN이므로 iteration 2 전체 결과는 NOT CLEAN이다.

BINDINGS REVIEW NOT CLEAN
