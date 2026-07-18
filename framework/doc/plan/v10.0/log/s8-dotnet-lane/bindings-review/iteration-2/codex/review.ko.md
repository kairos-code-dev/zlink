# S8 DOTNET bindings 전환 리뷰 — R1 Codex — iteration 2

## 1. Scope 확인

- 대상 commit: `115c3d73d`. HEAD(`a9e6b521b`)와 `bindings/dotnet/{src,samples}` diff 공집합 → 대상 상태 그대로 검토.
- 시작: 208 files, aggregate SHA-256(`LC_ALL=C sort` 재sha256sum) = `d6acf3e49cdb1f96aac8d92e6b403d79502f2f65866687581b0dc4308ad4c048` (manifest 일치).
- 종료: 동일 명령 재확인 208 files / `d6acf3e49cdb...` (변경 없음, scope 파일 무수정).
- `tests/Zlink.Tests` 제외. build/test/sanitizer/package 미수행 — coordinator의 library+samples build green, no-hit 0, RequiredExportNames⊆ABI 증거만 인정.
- Core 10.0.0 기준선: `core/src/libzlink.vers` + staged `bindings/dotnet/native/linux-x64/libzlink.so.10.0.0`(`nm -D --defined-only`) 읽기 전용 대조.

## 2. iteration-1 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| DF1 RequiredExportNames | RESOLVED | 182개 전량 `libzlink.vers`에 존재(자동 대조 missing=[]). 로드 gate 통과. |
| DF2 typed join-reply | RESOLVED | `MeshReceiveRecord.ReplyJoin(ActorJoinResult)`+`AcceptJoin`/`RejectJoin`, typed `ActorJoinResult` enum. `zlink_actor_join_reply` 배선(MeshDispatch.cs:282,296), record kind 검증 후에만 허용. |
| DF3 IMeshNode routing-id | RESOLVED | `IMeshNode.SetRoutingId`(IMeshNode.cs:20). |
| DF4 caller-init size/version | RESOLVED | caller-init 구조체 struct_size/version=1 선설정(예: MeshNode.Actors.cs transfer 경로 `StructSize=Marshal.SizeOf`, Version=1). |
| DF5 transfer fence API | RESOLVED | `PrepareActorTransfer`/commit/activate/abort + typed token/result(MeshNode.Actors.cs:228+, `zlink_mesh_node_actor_transfer_*`). |
| DF6 StreamSocket.SetRoutingId native | RESOLVED | `Kernel.SetOption(SocketOptions.RoutingId, ...)`(StreamSocket.cs:20), local field 제거. |
| DF7 batch/claim finalizer | RESOLVED | `~MeshReadyBatch`/`~MeshClaim`/`~MeshReceiveBatch` finalizer + `GC.SuppressFinalize`. |
| DF8 511 endpoint validator | RESOLVED | `BoundaryValidation.ValidateMeshEndpoint`(MeshEndpointMaxBytes=511), SetBind/ConnectPeer 사용. |
| DI2-1 raw IStreamSocket actor 소유 | RESOLVED | IStreamSocket에서 BindActor/UnbindActor/SendBoundActor/BoundActors 제거. actor binding은 IStreamSessionService(`zlink_stream_session_*`) 단일 경계. |
| DI2-2 typed kind_data | RESOLVED | `DecodeKindData`(MeshDispatchRuntime.cs:307)가 native KindData/KindDataSize를 record-kind별 typed payload로 해석. |
| DI3-1 제거 심볼/개념 잔재 | RESOLVED | no-hit 8종 전량 0(§4). `msg_gets`/`GetProperty`/RouteBridge 구조체/`router_*_spot_part`/`*ToSpot`(IRouterSocket) 제거. spot 메시징은 present `zlink_spot_send_to_spot`/`zlink_spot_request_to_spot`로 이관. |

iter-1 finding은 모두 해소. 새 반례 없이 재개하지 않는다.

## 3. 전체 scope 재검토 (3축)

### I1 — 계약 구현 일치

#### Finding I1-1 [I1][blocker] — public `IStreamSocket.DetachStream()`이 Core 10.0.0에서 제거된 `zlink_stream_detach`에 바인딩되어 호출 시 항상 파손

- **파일/라인**: `bindings/dotnet/src/Zlink/Contracts/Sockets/IStreamSocket.cs:44`(public `void DetachStream()`), `bindings/dotnet/src/Zlink/Runtime/Sockets/StreamSocket.cs:38-41`(→`Kernel.DetachStream()`), `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Stream.cs:129-140`(→`zlink_stream_detach`), P/Invoke 선언 `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Socket.cs:75-76`, 그리고 dispose 경로 `bindings/dotnet/src/Zlink/Runtime/Sockets/SocketKernel.Lifecycle.cs:21-33`.
- **문제**: Core 10.0.0 raw STREAM 모델은 `zlink_stream_packet_handler`(present)만 노출하고 **detach 개념은 제거**됐다. `zlink_stream_detach`는 `core/src/libzlink.vers`·staged `libzlink.so.10.0.0`(`nm -D`) 어디에도 없다. 그러나 dotnet은 public 계약 메서드 `IStreamSocket.DetachStream()`을 그대로 노출하고, 이는 `EntryPoint` alias 없이 literal `zlink_stream_detach` P/Invoke로 배선된다. `.NET` marshaller는 최초 호출 시 export 해석에 실패해 `EntryPointNotFoundException`을 던진다(`ThrowCloseIfError`에 도달조차 못 함).
- **근거**:
  - `core/include/zlink/socket/api.h`의 stream 섹션에 `zlink_stream_packet_handler`만 있고 detach 없음.
  - `nm -D --defined-only libzlink.so.10.0.0` → `zlink_stream_detach` MISSING(`zlink_stream_packet_handler`는 PRESENT).
  - 전 P/Invoke entrypoint를 ABI와 자동 대조한 결과 missing-symbol 바인딩은 `zlink_stream_detach`·`zlink_stream_attach_raw`·`zlink_subscribe_handler` 3건.
  - 샘플 `StreamPacketCallback`/`ActorGatewayRelay`/`ActorSinglePlayerQueue`의 `stream.OnPacket(...)`(AttachStreamPacket)이 `_streamAttached=true`를 설정 → `SocketKernel.Dispose`가 매 teardown마다 `zlink_stream_detach` 호출(`catch {}`로 삼켜져 기능은 유지되나, 제거 심볼에 대한 P/Invoke가 상시 발화).
- **영향**: `DetachStream()`을 호출하는 어떤 애플리케이션도 `EntryPointNotFoundException`으로 실패한다. dotnet은 참조 lane이므로 이 raw STREAM lifecycle 표면 오염이 타 언어 기준으로 전파될 위험.
- **수정 제안**: `IStreamSocket.DetachStream()`·`StreamSocket.DetachStream`·`SocketKernel.DetachStream`·`zlink_stream_detach` P/Invoke를 제거하고, `SocketKernel.Dispose`의 detach 호출도 삭제(10.0.0 raw STREAM teardown=socket close). 관련 `_streamAttached`/`ClearStream` 로직 정리.

**Verdict I1: NOT CLEAN** (1건)

### I2 — POSD·DDD 리팩터링

- Finding 없음.
- **Evidence**: src god-file 없음(최대 `TypedExceptions.cs` 610, `MeshNode.cs` 503, `Poller.cs` 405 — 모두 책임 범위 내). `NativeMethods.*`/`SocketKernel.*`는 partial class 책임별 분할. Spot/MeshNode는 Core 1:1 얇은 번역 계층으로 바인딩 계층에 적정 — 불필요 추상화 없음(프로젝트 방침 부합). pull-dispatch 저수준성은 Core 계약을 정직 노출한 것으로 framework 계층 편의화 대상(바인딩 결함 아님).
- **Verdict I2: CLEAN**

### I3 — 정리 완결성

#### Finding I3-1 [I3][high] — Core 10.0.0에서 제거된 심볼에 대한 dead P/Invoke·dead 내부 경로 잔존

- **파일/라인**:
  - `zlink_stream_attach_raw`: P/Invoke `NativeMethods.Socket.cs:71-73`, 사용부 `SocketKernel.Stream.cs:9-63`(`AttachStreamRaw` 2 overload)+`OnStreamRaw`/`OnStreamRawUInt32`. public 도달 경로 없음(StreamSocket은 `OnPacket`=present `zlink_stream_packet_handler`만 노출).
  - `zlink_subscribe_handler`: P/Invoke `NativeMethods.Socket.cs:86-88`, 사용부 `SocketKernel.Callbacks.cs:51-71`(`SubscribeHandler`)+`OnNativeSubscribe`, delegate `SocketCallbacks.cs:19`, capability `SocketTypePolicy.cs:54,96`, registry 필드 `SocketCallbackRegistry.cs:23-25,48-50`. public contract에서 `SocketKernel.SubscribeHandler` 호출부 0건.
- **문제**: 두 심볼 모두 Core 10.0.0 ABI(`libzlink.vers`·`.so`)에 부재. `EntryPoint` alias 없이 literal 바인딩이라, 호출되면 `EntryPointNotFoundException`. 현재는 public 배선이 없어 dead code지만, 제거 심볼에 대한 P/Invoke가 남아 있어 정리 미완결(raw 계층 드리프트 §s8-common-raw-layer-drift 1·4 항목). 특히 XPUB subscribe-notification은 10.0.0에서 `zlink_xpub_recv_part` 모델로 바뀌었으므로 `zlink_subscribe_handler` 콜백 경로 자체가 폐기 대상.
- **근거**: 전 P/Invoke↔ABI 자동 대조, `nm -D` MISSING 확인. RequiredExportNames(182)에는 포함되지 않아 로드 gate는 통과(manifest 정확) — 그래서 build/load-gate로는 탐지 불가.
- **영향**: 죽은 선언·경로지만 참조 lane 표면에 제거된 raw 심볼이 잔존. 향후 소비·리팩터링 시 파손 유발.
- **수정 제안**: `zlink_stream_attach_raw`·`zlink_subscribe_handler` P/Invoke 및 `AttachStreamRaw`(2 overload)/`OnStreamRaw*`, `SubscribeHandler`/`OnNativeSubscribe`/`SocketSubscribeHandler` delegate/`SubscribeHandler` capability/registry 필드를 삭제. raw SUB/XPUB 구독 통지가 필요하면 `zlink_xpub_recv_part`/`zlink_set_subscription` 모델로 재배선.

**Verdict I3: NOT CLEAN** (1건)

## 4. 폐기 개념 no-hit 판정

검사 범위 `bindings/dotnet/src`, `bindings/dotnet/samples`(native/obj/bin 제외).

| 개념 | 결과 | 근거 |
|---|---:|---|
| `SpotNode` | PASS, 0 | scoped grep |
| `RouteBridge` | PASS, 0 | scoped grep(iter-1 NativeTypes 구조체 제거됨) |
| `spot_node` | PASS, 0 | scoped grep |
| `subjects` | PASS, 0 | scoped grep |
| `internal_sockets` | PASS, 0 | scoped grep |
| pub/sub 별도 routing id | PASS, 0 | scoped grep |
| `dispatch_workers` | PASS, 0 | scoped grep |
| `recv_actor_part` | PASS, 0 | scoped grep |
| `msg_gets` | PASS, 0 | scoped grep(Message.GetProperty 제거됨) |

no-hit 8종 전량 PASS. 단, 위 no-hit 목록에 포함되지 않은 raw 계층 제거 심볼 3종(`zlink_stream_detach`·`zlink_stream_attach_raw`·`zlink_subscribe_handler`)이 P/Invoke로 잔존(§3 I1-1·I3-1).

## 5. 종합

iter-1 finding(DF1-DF8, DI2-1/2, DI3-1)은 commit `115c3d73d`에서 전량 해소. 그러나 raw socket 계층 드리프트 흡수가 미완결이다: Core 10.0.0에서 제거된 `zlink_stream_detach`가 **public** `IStreamSocket.DetachStream()`에 배선되어 호출 시 항상 파손(I1-1), `zlink_stream_attach_raw`·`zlink_subscribe_handler`는 제거 심볼 dead P/Invoke·dead 경로로 잔존(I3-1). `zlink_poller_wait_pinned`는 `EntryPoint="zlink_poller_wait"` alias로 정상(오탐 아님). I2는 CLEAN. iteration 2는 각 축 finding 0을 요구하므로 미충족.

BINDINGS REVIEW NOT CLEAN
