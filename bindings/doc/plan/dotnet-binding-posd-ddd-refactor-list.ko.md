# `bindings/dotnet` POSD·DDD 리팩토링 통합 수정 목록

> 2026-07-08 `bindings/dotnet/src/Zlink`(Contracts+Runtime, 178파일 ~28.7k줄) 전수
> POSD/DDD 리뷰와 Codex 재검토 결과를 합친 통합 수정 목록. 파일:라인은 리뷰 시점 기준이므로 편집 전
> 현재 코드로 재확인한다. `InternalsVisibleTo` 없음 → internal 미참조 = 확정 dead.
> 공개 API 표면은 불변(§E 제외). hot 항목은 커밋 전 baseline vs patched 벤치 필수.

체크박스는 완료 시 갱신한다. 위험 표기: **없음**(control plane, 빌드+테스트로 충분) /
**code-motion**(hot 경로 코드 이동이나 명령 수준 동일) / **벤치**(hot 구조에 닿음, 무회귀 증명 필수).

**통합 검토 반영:**
- Codex 리뷰에서 확인한 `Spot` 단일 publish/channel send 실패 시 `Message` 복구 drift는 B0으로 채택한다. 이 항목은 hot path에 닿으므로 새 allocation, delegate, virtual dispatch 없이 로컬 상태 전환만 정렬한다.
- fluent operation 얕은 구현과 socket capability 지식 누출은 기존 C9/D9/C-track 항목과 합치되, C0에 policy 정렬 항목을 추가한다.
- legacy native payload는 즉시 삭제가 아니라 release packaging 정책 확인 항목으로 A9에 둔다. 기존 memory와 live script 모두 native payload를 release artifact로 취급한다.

---

## A. 삭제 트랙 (파일·데드코드 정리)

- [x] **A1. SocketKernel 레거시 수신 + raw-frame 서브시스템 삭제** (~450줄, 없음)
  - live 수신은 `*Into` 계열뿐. 구식 allocating 경로와 raw-frame 계열 전부 무참조.
  - `SocketKernel.cs:40-92,109-223,392-533`(Subscribe/TryReceiveRaw*/Recv*/CreateRoutedReceived×4/MapSendResult), `SocketKernel.Receive.cs:10-51`, `SocketKernel.ReceiveRaw.cs`(통파일), `SocketKernel.ReceiveCore.cs:297-389,505-515`, `SocketKernel.ReceiveSubscription.cs:11-31,107-130`
  - A2와 대칭이므로 **반드시 같이** 삭제.
- [x] **A2. Service 쪽 copy-fork 대칭 사본 삭제** (~300줄, 없음)
  - Spot이 SocketKernel에서 복제 후 반쯤 버린 죽은 사본.
  - `Spot.cs:306-326,451-514`(SubscribeNoWait/TryReceiveRawSubscribedFrame/MapSendResult/CopyFirstFrameAndCollectPending(IntPtr판)), `Spot.Subscription.cs:11-31,99-122`(SubscribeCore/ReceiveSubscriptionEventCore), `Spot.Lifecycle.cs:83-94`(TryReceiveCore), `Spot.SubscriptionRaw.cs:11-27,152-207`
  - `ActorInterop.CopyMessageFromPointer`(`ActorInterop.cs:36-49`), 쓰기 전용 `SpotSendOperation._channelName`(`SpotOperationsImpl.cs:19,35`)도 함께.
- [x] **A3. 레거시 actor control-plane 파이프라인 삭제** (~200줄, 없음)
  - live join(`ActorInterop.Join`)과 **이중** join 제출 파이프라인. 무참조.
  - `SpotNode.ActorRuntime.cs:385-543`(RemoteActorRef/DestroyActor/DestroyRemoteActor/OnSendReady/JoinActor 3종/LeaveActor), `Actor.cs:100-107`(SendBoundSessionDirect)
  - 연쇄 dead가 되는 `ActorInterop.OnJoinReply/JoinHandler/JoinHandlerPtr`(`ActorInterop.cs:24-28`, `ActorInterop.Callbacks.cs:29-36`)도 삭제.
- [x] **A4. `Message._managedPayload` 죽은 이중 표현 삭제** (~200줄, **벤치**)
  - `ManagedPayloadState`가 어디서도 생성 안 됨(전환 주석 `Message.Native.cs:468-470`이 증거). hot 타입 내 도달 불가 분기 ~14개.
  - `ManagedPayloadState`/`_managedPayload`/`ReleaseManagedBytes` + `Move/MoveTo/CopyTo/Copy`의 죽은 절반 + `Message.Pool.cs:56-57` 방어 분기 삭제. `AsReadOnlyMemoryCore`는 `ToArray()`로 축약. `RefCount` doc 주석 갱신.
  - pooled 객체 레이아웃이 바뀌므로 벤치. 순수 삭제라 중립~플러스 예상.
- [x] **A5. 통파일 3개 삭제** (68줄, 없음)
  - `Runtime/Eventing/MonitorState.cs`(MonitorState/MonitorStatusDetail enum), `Runtime/Service/SpotServiceAttachmentStats.cs`, `Runtime/Native/NativeSpotModels.cs`(ZlinkSpotRoute/ZlinkSpotServiceAttachmentStats) — 셋 다 무참조.
- [x] **A6. 죽은 멤버 일괄 삭제** (~150줄, 없음)
  - `ZlinkException.Native.cs`: `FromLastError`(34), `ThrowIfError`(40), `ThrowBindIfError`(52), `ThrowRecvIfError`(64), `TryMapErrorCode`(146), `LegacyZlinkException`(324)
  - `ErrorCodes.cs`: `ProtocolError`(86), `DisconnectReason`(91)
  - `ServiceRuntimeEnums.cs`: `RegistrySocketRole`(5), `SpotSocketRole`(30) — 테스트의 동명 alias는 public `SpotRole`이라 무관
  - `ActorOperationsImpl.cs:5` `ActorJoinSource`, `RoutingId.Native.cs:248` `NativeRoutingIdBox`
  - `RoutingIdCodec.cs`: `ToOwnedBytes`/`CopyOwnedBytes`/`CanonicalizeThreadLocal`/`t_ownedBytesCache`/`ThreadCacheMaxEntries`/`FromUInt32`/`FromRoutingId`(101-137,265-286,18,12,95)
  - `RequestReplySupport.cs:66-82` `CloneAndSubmitParts`, `RequestCallState.cs:89-93` `RequestTimeoutFromUserData`
  - `SpotNode` `_channelDealers` 필드 + `TryGetChannelDealerHandle`(`SpotNode.cs:10-11`, `SpotNode.Lifecycle.cs:39-52,95-98`) — 채워진 적 없음
- [x] **A7. 셸/이중 릴레이 파일 정리** (없음)
  - `Runtime/Native/RoutingIdInterop.cs`(1메서드 순수 전달) 삭제, 호출처 13곳을 `RoutingId.FromNative` 직접 호출로.
  - `ZlinkRuntime.cs`/`ZlinkPollRuntime`의 이중 릴레이(`Zlink.Core.cs`→`ZlinkRuntime`→`NativeMethods`)를 `*Core` partial 본체로 병합.
- [x] **A8. 죽은 P/Invoke 선언 2건 삭제** (없음, draft 확인 후)
  - `zlink_msg_adopt`(`NativeMethods.Core.cs:193`+preload 34), `zlink_spot_recv_actor_lifecycle`(`NativeMethods.Actor.cs:104`+preload 95, `_with_request`로 대체됨). draft 문서에 msg_adopt 계획 없는지 확인.
- [x] **A9. legacy native payload 정책 확인 후 정리** (없음, release artifact 확인)
  - 현재 `git ls-tree -r --name-only HEAD -- bindings/dotnet/native` 기준으로 `libzlink.so.8.4.2`, `libzlink.so.8.4.3`, `linux-x86_64` RID payload는 남아 있지 않다.
  - NuGet project는 `bindings/dotnet/native/<rid>/`를 package 입력으로 두고, package 안에는 `runtimes/<rid>/native/` 구조로 배치한다. `scripts/local-package/native/sync-local-core-libs.sh`는 로컬 core 빌드 결과를 bindings native 위치에 복사하는 개발 검증용 스크립트다.

---

## B. 결함 수정 (correctness — 리뷰 중 발견)

- [x] **B0. `Spot` 단일 publish/channel send 실패 시 `Message` 소유권 복구 drift 수정** (**벤치**, 계약 drift)
  - 공개 operation 계약은 submit 실패 시 메시지 소유권이 호출자에게 복구되어야 한다고 설명한다(`OperationContracts.cs:10-16`).
  - `Spot.Publish` 단일 메시지 경로는 `message.MoveTo(ref nativePart)` 후 native 호출을 수행하고, `rc != 0`이어도 `submitted = true` 뒤 예외를 던진다. catch는 `!submitted`일 때만 복구한다(`Spot.Publish.cs:156-180`). `Spot.Subscription.cs:165-195`의 DontWait hot path와 `Spot.MultipartSubmit.cs:124-159`의 channel 단일 send도 같은 패턴이다.
  - 반대로 `SocketKernel.SendCore.cs:39-68,72-103`은 실패 errno를 읽은 뒤 `RestoreFrom`을 호출하고 결과를 매핑한다.
  - 수정은 socket의 `shouldRestore` 의미와 맞춘다. hot path라 새 helper를 만들더라도 static/inlinable이어야 하며 allocation, delegate, interface dispatch, lock 추가 금지.
- [x] **B1. `Poller.Dispose` finalizer throw 제거** (없음, 프로세스 크래시급)
  - `~Poller()`→`Dispose()`가 destroy 실패 시 throw → finalizer 탈출 시 프로세스 종료. `Timer.Destroy(bool throwOnError)` 패턴 적용(finalizer는 false). `Poller.cs:226-264`. `GC.SuppressFinalize` 스킵 문제(252)도 함께. `Clear()`(188-189)는 throw 전에 `_handle` 정합 처리.
- [x] **B2. 핸들러 등록 순서 레이스 수정** (없음)
  - `SocketKernel.Callbacks.cs:10-80`은 native 등록 → managed 저장 순서. I/O 스레드가 저장 전 콜백 발화 시 `handler==null`로 메시지 조용히 드랍(151-157). `SocketKernel.Stream.cs:9-127`의 순서(저장→등록→실패 시 롤백)가 정답. Stream.cs의 4벌 attach 프로토콜(저장→등록→롤백)과 함께 등록 헬퍼 1벌화하며 Callbacks.cs를 그 순서로 정렬.
- [x] **B3. backpressure 시 cloned parts 누수 수정** (없음)
  - `StreamSocket.SendBoundActorCore:133-138`만 backpressure catch에서 `DisposeParts(cloned)` 누락(Router/Kernel 사본은 dispose함). native 버퍼 누수. C-track R-clone 통합(§C)으로 근본 해소.
- [x] **B4. `ReceiveRoutedParts` 비-Router 브랜치 `_nowait` alias 미사용(가독성 일관성만, 동작 변화 없음)** (없음)
  - **정정: blocking 버그 아님.** `zlink_recv_part_nowait`는 `EntryPoint="zlink_recv_part"`인 **동일 C 함수**의 managed 가독성 alias일 뿐이고(`NativeMethods.Socket.cs:49-55` 주석 "same C function… choose the non-blocking path explicitly"), DONTWAIT 동작은 전적으로 `flags` 인자로 결정된다. `SocketKernel.ReceiveCore.cs:105`는 `flags`를 그대로 넘기므로(`zlink_recv_part(Handle, …, flags)`) DontWait가 이미 정상 반영된다(C 계약 `core/include/zlink/socket/api.h:510` `zlink_recv_flags_t flags_`, 구현 `flags_ & ZLINK_DONTWAIT` 검사). 형제 브랜치(`ReceiveBasicParts:29-34`, `ReceiveRouterParts:173-181`)는 DontWait 시 `_nowait` alias로 **의도를 표기**할 뿐 호출 대상 native 함수와 flags는 동일하다.
  - 실제 작업은 순수 가독성 일관성: 비-Router 브랜치도 DontWait 분기 시 `zlink_recv_part_nowait` alias를 쓰면 형제와 통일된다. **바이트 단위 동일 호출이라 동작 변화·벤치 불필요.** 우선순위 낮음(선택). 이걸 "blocking 수정"으로 다루면 없는 버그를 고치려다 hot receive path를 불필요하게 건드릴 위험이 있으니 그러지 않는다.
- [x] **B5. `SpotActorLifecycleEvent.Dispose` 멱등 가드 추가** (없음)
  - record+IDisposable인데 `Interlocked` 가드 없음(`Actor.cs:193-197`). `with`-복제 시 이중 dispose가 pooled Message 이중 반납. 형제 `ActorReceived.Dispose`(220-226) 패턴 적용.
- [x] **B6. 백프레셔→false 매핑 경계 검증(먼저 검증, 그다음 필요 시 정렬)** (없음, spec 확인)
  - **주의: "누락된 flag 체크 추가"로 곧장 구현하지 말 것.** 인용된 두 헬퍼는 `SendFlags`를 스코프에 갖지 않는다. `RemoteActorGetRefCallback`(`Lookup.cs:52-91`)이 구동하는 public `ActorLookupOperation`(`ActorManagementOperations.cs:73`)에는 `Flags(...)` 자체가 없어 확인할 DontWait가 없다 → 무조건 backpressure→false가 설계상 정답. reply 콜백 경로(`ActorRequests.cs:127`)는 operation 레이어 `RequestCallbackSubmitOperation.Submit`(`ActorOperationsImpl.cs:239-258`)이 이미 `(_flags & SendFlags.DontWait) != 0`로 게이팅한 뒤 헬퍼를 호출한다.
  - 따라서 작업은 **검증 항목**: (1) 각 콜백 헬퍼를 구동하는 public operation이 flags를 소유하는지 식별, (2) DontWait→false 매핑은 flags를 소유한 그 경계에만 두고, flags 개념이 없는 lookup류는 현행(무조건 false) 유지가 맞는지 spec으로 확정, (3) 실제 drift(같은 flags-소유 경계인데 체크가 갈리는 경우)가 있을 때만 정렬. 헬퍼에 새 flags 계약을 발명하거나 caller 정책을 중복하지 않는다.
- [x] **B7. `PubSocketOptions.WelcomeMessage` 바이너리 손상** (없음, native 옵션 타입 확인)
  - `Message`(임의 바이트) 타입인데 setter가 `value?.GetString()` UTF-8 decode 후 문자열 옵션 경로로(`PubSubSocketOptionFacades.cs:78-87`). 바이너리 payload가 U+FFFD로 손상. native 옵션이 바이트 지원이면 바이트 경로로, 문자열 전용이면 doc 명시(UTF-8 required)+소유권 미소비 명시.

---

## C. 구조 통합 — 지식 중복 소거 (없음/code-motion)

- [x] **C0. socket capability/wire receive 정책을 `SocketTypePolicy` 쪽으로 응집** (code-motion, hot query 주의)
  - `SocketTypePolicy.cs:32-84`가 capability/option 지원 여부를 정의하지만, `SocketKernel.cs:163-170`도 `Router`/`Stream` 여부를 직접 검사해 raw frame receive capability를 고른다. `SocketKernel.ReceiveCore.cs:86-89,355-361`도 Router 여부로 native receive 방식을 직접 나눈다.
  - "어떤 socket type이 어떤 wire receive 방식을 쓰는가"라는 지식이 policy와 kernel 양쪽에 있어 새 socket type 또는 routed semantics 변경 시 변경 범위가 넓다.
  - 생성 시 계산한 bool/enum 또는 inlinable query로 통합한다. per-receive hot path에 추가 lookup이나 virtual dispatch를 넣지 않는다.
- [x] **C1. errno → typed result 직결** (없음~code-motion, ⭐최대 impact)
  - native가 typed rc 반환하는데 버리고 `zlink_errno()` 재호출 + 7개 매직넘버 switch로 재분류. `Context.GetOption`의 `(ConfigResult)errorOut` 패턴이 정답. ~120줄+drift 영구 해소(B 6번 결함 원인). `ZlinkException.Native.cs:40-86,199-322`, 호출처 `Poller.cs:48-52,215-217,296`, `SocketMonitor.cs:52-59`, `Timer.cs:57-59` 등. 포인터 반환 생성자(`zlink_ctx_new` 등)만 errno 유지. 에러 브랜치만 건드려 성공 경로 불변. 예외 `Result` 값이 native 기준으로 정정될 수 있으니 확인.
- [x] **C2. RoutingId 인터닝 3중 → 단일 권위** (code-motion, hot는 우회)
  - `RoutingIdCodec`(전역 락 2) + `RoutingId`(thread-static 1), `RouteCacheKey` struct 두 파일 복붙. `RoutingId`를 단일 인터닝 권위로, codec은 위임. hot 수신은 이미 codec 우회(`TryFromInlineCached`)하므로 전역 락 2개 제거는 중립~개선. `RouteCacheKey`를 `RouteHash.cs` 옆 단일 internal 타입으로.
- [x] **C3. parts→span 평탄화 헬퍼 통일** (**벤치**, ~10벌)
  - array/`CollectionsMarshal.AsSpan`/copy 스위치가 `SocketKernel.Send.cs`에 ~10곳 + `Spot.Publish.cs:49-65`는 바깥·안쪽 **이중 실행**. `RequestReplySupport.PartsAsSpan`은 List 케이스 누락(할당 발생). `NativeMessageParts.AsSpan(parts, ref copied)` 인라인 헬퍼 1개로. 중복 타입테스트 제거+List 할당 제거라 중립~플러스.
- [x] **C4. Dealer/Router 요청 콜백 기계 쌍둥이 통합** (**벤치**, 형태 보존)
  - `DealerRequestCallbackState`(`DealerSocket.cs:300-381`) ≡ `RouterRequestCallbackState`(`RouterSocket.cs:443-523`) 토큰 동일. `DefaultRequestTimeout` 상수도 중복. `Runtime/Messaging`에 `RequestCallbackCompletion` 1클래스로, pinned static delegate는 `static readonly` 유지, userData로 disambiguate. TCS 재도입 금지 주석 준수.
- [x] **C5. Join 제출 쌍둥이 + CallState 3벌 통합** (없음)
  - `SubmitJoinNative` vs `SubmitJoinEntrySpotNative` 95% 동일 ~150줄(`ActorInterop.Join.cs:74-152,220-298`). `ActorJoin/Lookup/JoinEntrySpotCallState`(`Callbacks.cs:195-250`)는 TCS 타입만 다른 3벌. `RequestCallState`의 interlocked 종료 게이트가 이미 정본이니 재사용. `nativeParts[i]=default` 리셋 순서 보존.
- [x] **C6. Task→callback 어댑터 5벌 통합** (없음)
  - 실패→`RequestResult` 매핑이 `ActorInterop.cs:172-203`, `ActorInterop.ActorRequests.cs:127-166`, `Lookup.cs:52-91`, `Join.cs:25-72,170-218`에 복붙. `CallbackDelivery.Complete<T>(task, onFailure, deliver)` 제네릭 헬퍼로. **단** `Spot.RouterRequest`의 "async 경로 래핑 금지" 주석 2곳엔 적용 안 함.
- [x] **C7. 네이티브 스냅샷 배열 마샬링 6벌 통합** (없음)
  - count→`AllocHGlobal`→refill→`PtrToStructure` loop→`FreeHGlobal` ~180줄이 `Spot.cs:239-273`, `SpotNode.Topology.cs:108-302`(5곳). `NativeSnapshotReader.Read<TNative,T>(fill, convert)` 1개로. 2-phase count-then-fill 프로토콜 유지.
- [x] **C8. `SubmitParts` 복붙 + `StackPartLimit=8` 상수 4벌 통합** (code-motion)
  - `SpotRouteBridge.cs:222-256` ≡ `SpotNodePublisher.cs:67-99`(delegate+throwSubmit만 차이). 상수 4곳(`Spot.cs:10`, `SpotNode.ActorRuntime.cs:10`, `SpotRouteBridge.cs:11`, `SpotNodePublisher.cs:10`). `NativeMessageParts`로 이동, `bool throwOnError` 파라미터화. **단** restore-on-failure vs clone-and-close 두 소유권 프로토콜은 통합 금지(이중 free 위험) — 진입점 2개 유지.
- [x] **C9. 단일부 send core move/restore 프로토콜 9벌** (**벤치**, 마지막)
  - "MoveTo → native 실패 시 RestoreFrom" 안전-임계 불변식 9곳(`SocketKernel.SendCore.cs` 8 + `RequestReplySupport.SubmitOwnedSinglePart`). struct-generic 템플릿(`where TSubmit:struct,INativeSingleSubmit`, JIT 특수화로 무-dispatch)으로 1벌화. DONT_WAIT 전용 entry 별도 유지, routed는 `ref ZlinkRoutingId`(256B 복사 회피) op 구조체로. **벤치 게이트 필수**. 비용 크면 우선 9번째 사본만 kernel 템플릿에 위임하는 것부터.

---

## D. 구조 개선 — 중간 이하 (기회 될 때)

- [x] **D1. 핸들 수명주기 6벌 통합** (code-motion) — Context/Poller/Timer/SocketMonitor/AtomicCounter/Stopwatch가 실패 destroy 처리 4가지로 갈림. Timer의 handle-restore를 표준 `NativeOwner` base로. **소켓 send/recv 핸들엔 SafeHandle 확장 금지**(perf 게이트). `Timer._handlerNative`/`SocketMonitor._selfHandle` 순서 보존.
- [x] **D2. `ZlinkPollRuntime` poll 루프 할당 제거** (**벤치**) — `Poll`마다 `new PollEventFlags[count]` + per-call 클로저(`ZlinkPollRuntime.cs:13-62,76-131`). span/flag + 인라인 handle 추출로. Win/Unix ABI 구조체는 통합 금지, 오케스트레이션만.
- [x] **D3. `Timer.PollReadyNoWait` per-call poller 생성 제거** (**벤치**) — 매 non-block Recv마다 poller_new+add+wait+remove+destroy(5 P/Invoke)+배열 할당(`Timer.cs:62-72,195-222`). 인스턴스당 poller 캐시(timer_destroy 전 파괴 순서). native/public API에 flags 추가 금지.
- [x] **D4. `MonitorStatus` 27-positional ctor** (없음) — `in ZlinkMonitorStatus` named-assign ctor 1개로(`MonitorStatus.State.cs:7-52`), `MonitorConverters.FromNative` relay 축약. public 프로퍼티 불변, ctor internal.
- [x] **D5. `RequestProgressPump` 이중 맵 + ReferenceEquals 스니핑** (code-motion) — socket/spot 이중 등록 후 `ReferenceEquals(states, SpotStates)`로 재구분(`RequestProgressPump.cs:82-107`). handle는 유일 `nint`이니 단일 맵으로.
- [x] **D6. Export 목록 2원화 정리** (없음) — `NativeMethods.Core.cs:8-126`(118개)과 Poller 자체 12개 매 생성 시 재검사(`Poller.cs:9-41,266-273`)로 분리. partial별 공치+결과 캐시. `NativeMethods.Core.cs` grab-bag 선언 이동(RequestReply/Util 분리). **단** `_nowait`/`_utf8` 중복 진입점은 의도된 perf 계약 — 주석과 함께 이동만.
- [x] **D7. 옵션 4단 패스스루 축소** (없음) — `SocketBase`의 12 explicit impl + 12 internal wrapper → `Kernel.*` 직결(`SocketBase.cs:126-289`). 도달 불가 `SocketOptionValidation` 제거(팩토리+타입이 이미 보장). `SocketOptionAccessor`는 `Func<IntPtr>` 대신 `SocketHandle` 직접.
- [x] **D8. 구독 이중 경로 제거** (없음) — `SetSubscription`(검증 있음, `SocketKernel.Send.cs:337-357`) vs `SocketOptionAccessor.SetString`의 Subscribe/Unsubscribe 특수분기(검증 없음, 무참조 장전된 총). 후자+미사용 옵션 키 삭제. `RoutingId` 특수분기는 유지+주석.
- [x] **D9. `RouterRequest/ReplyOperation` enum+부분유효 필드 분리** (없음) — `RouterOperationKind`+disjoint 필드가 `default:` arm에서 `InvalidState` 런타임 체크(`SocketOperations.Request.cs:103-234`, `Reply.cs:5-73`). `RouterPeer/SpotRequestOperation`로 타입 분리(불법 상태 표현 불가화). 죽은 `RouterOperationKind.SendToSpot` 멤버 제거. public 빌더 타입 불변.
- [x] **D10. `catch { throw; }` no-op 제거** — `RouterSocket.cs:187-190,315-318`, `DealerSocket.cs:294-297`.
- [x] **D11. Contracts로 새어오른 구현 하강** (없음) — `TopicMessage.Dispose` 15줄 field-reset(`TopicMessage.cs:49-65`), `Received`/`TopicMessage`의 RoutingId 스냅샷 변환 복붙 → `*Core`/공유 헬퍼로. `ActorRef` 검증을 `ActorInterop.ValidateActorId` 의존에서 Contracts-level 헬퍼로(의존 방향 정정).
- [x] **D12. 이중 예외 변환 + 쌍둥이 base 정리** (없음) — `SocketBase.Bind`가 kernel이 만든 예외 재생성(스택 유실, `SocketBase.cs:31-53`). `ConnectableSocketBase` ≡ `ConnectableRoutedMessageSocketBase`(base만 차이). kernel을 errno→예외 단일 권위로. `Unbind`가 connect-domain으로 분류되는 점 확인.
- [x] **D13. actor 수신 stash/resume 응집** (code-motion) — DontWait mid-multipart park/resume 불변식이 `SpotNode`(상태)+`ActorInterop`(마샬링) 공동소유(`SpotNode.cs:13-14,69-111`, `ActorInterop.cs:51-127`). `ActorMessageInbox`로 응집, `ActorInterop`은 FromNative/ToNative만. lock 규율 이동.
- [x] **D14. `ISpotNode` partial 분할 + 깨진 문서 수정** (없음) — ~45멤버 god interface를 관심사별 `partial interface`로(`SpotNode.cs`). 잘못된 xml-doc 12곳 수정("Closes the resource.", "Creates a actor." 등).
- [x] **D15. envelope first/single-part 실패 메시지 통일** (없음) — `Received`/`TopicMessage`/`ActorReceived`의 FirstPart/SinglePartOrThrow가 서로 다른 예외(LINQ vs InvalidOperationException). 내부 헬퍼 1개로. recv 예외를 컨테이너(`MultipartMessageCollection`)에서 던지는 것(`:127-143`)을 envelope로 이관.
- [x] **D16. TopicMessage populate 10벌 / Received Create 미러 8개** (**벤치**) — `RoutingIdSnapshot` 기준 private core 2개로(`TopicMessage.State.cs:19-183`). `Received` static Create 미러 삭제(ctor 직접) + 무시되는 `adoptRoutingBytes` 파라미터 제거(`Received.State.cs:57,78`, 호출처 `SocketKernel.cs:427,468`). 버퍼 스왑+`resetTopic:false` 순서 보존.
- [x] **D17. 소소 통합** — `SingleMessageList`≡`SingleMessageReadOnlyList` 중복(`Received.State.cs:288-319` vs `OperationMessageBuffer.cs:77-108`), `RequestReplySupport` 패스스루 2개(`CloneMessage`/`TakeOwnedParts`), Message invalidation 시퀀스 6벌 → `Invalidate()` 헬퍼(A4와 함께), send-ready 등록 2벌(`Spot`/`SpotNode`, 예외 처리 위치 drift), `SubscriptionIntrospection.At` 예외 제어흐름 → Try 패턴, `TypedExceptions.Native.cs`의 `(ErrorCode)` 캐스팅 → `int` base ctor, `Errors.cs:42` 죽은 override, publish subject 용어 4종(topic/channelName/topicOrChannel/subject) 정리.

---

## E. 바인딩 공개 계약 결정 필요 (단독 수정 금지 — 바인딩 spec/draft 트랙)

바인딩 공개 표면이라 .NET 단독으로 바꾸지 않는다. 정본은 `bindings/doc/spec/dotnet/`(및 언어 중립 `bindings/doc/spec/`)이며, framework 공개 계약 트랙(`framework-public-contract-posd-redesign`)과는 별개다. 각 항목은 바인딩 spec draft로 올리고 C 헤더/spec 대조를 선행한다.

- [x] **E1. reply 경로 `Flags`는 no-op 공개 표면 gap** — `_ = flags;` 5곳(`Spot.Request.cs:143-147`, `Spot.RouterRequest.cs:90-94`, `SocketKernel.cs:230`, `RouterSocket.cs:172,299`, `DealerSocket.cs:279`). public `ReplyOperation.Flags()`가 void로 forward. **C 헤더 확인 완료**: `zlink_dealer_reply_part`/`zlink_router_reply_part`(`core/include/zlink/socket/api.h:332,348`)는 `part_flag_`만 받고 `send_flags_`를 받지 않는다(request 계열만 받음, spec `core/doc/spec/core/socket/router.md`, `service/spot.md`). 즉 이건 "확인 후 plumb"가 아니라 **공개 표면 gap** — 선택지는 (a) no-op `Flags`를 제거/deprecate, 또는 (b) core reply flags를 먼저 draft해 계약을 만든 뒤 plumb. 둘 다 바인딩 spec 결정이 선행. 임시 내부 단계로 죽은 파라미터를 내부 시그니처에서 제거해 무시를 타입에 노출할 수 있다.
  - **완료 상태:** .NET `ReplySubmitOperation`의 `Flags(SendFlags flags)` 표면은 제거했다. core에 reply flags를 draft하는 (b)는 별도 트랙이다.
- [x] **E2. bridge policy `int` 현상 유지 확인** — `SpotRouteBridgeOptions.ErrorReplyPolicy/ReceiveMode`, `EndpointOptions.InboundRelayPolicy`(`SpotRouteBridge.cs:34,39,56`)는 C 헤더에 안정적인 public enum 계약이 없으므로 `int`로 유지한다.
  - **추천 결정: 현상 유지(손대지 말 것).** 확인 결과 C 헤더가 `int inbound_relay_policy;` 등 int이고 dotnet/java 모두 int인 **의도된 parity(reserved 필드)**. 지금 enum화하면 오히려 core spec과 drift. enum화는 core spec부터 바뀔 때만.
- [x] **E3. `MonitorStatus` raw uint → enum 타이핑** — `AutoHwmProfile` uint 프로퍼티(`Monitor.cs:86`)가 동명 enum(`SocketEnums.cs:60`)과 충돌, `IsReady`의 `0x1u` 매직 비트.
  - **완료 상태:** C 헤더에서 enum 계약이 있는 `AutoHwmProfile`만 기존 enum 타입으로 노출한다. `AutoHwmRole`/`AutoHwmPolicyClass`는 C 헤더에 enum 계약이 없으므로 `uint` 진단 필드로 유지한다.
- [x] **E4. envelope record → class** — `ActorReceived`/`SpotActorLifecycleEvent`가 record+IDisposable(`Actor.cs:179,206`) → `with`-복제 이중 dispose 함정. `ActorJoinRequest`는 소유하는데 IDisposable 아님.
  - **완료 상태:** `ActorReceived`/`SpotActorLifecycleEvent`는 sealed class로 전환했고, `ActorJoinRequest`는 `IDisposable`을 구현한다.
- [x] **E5. 중복 enum + Ok ctor 봉인** — (a) `SpotNodeSocketType`(`SpotNodeModels.cs:290`) ≡ `SocketType`(`SocketEnums.cs:8`) 멤버·값 완전 동일 → **삭제하고 `SpotNodeSocketFilter.Type`을 `SocketType?`로**. (b) `TypedExceptions`의 public ctor는 `ErrorCode.Ok`(성공)를 거부하고, native errno를 받는 생성자는 `internal`로 유지한다. `Ok` enum 멤버는 native 미러로 유지한다. (c) `ISpotNode` 역할 인터페이스 추출(→ D14와 연동). 전부 표면 변경이라 cross-language 판정.

---

## 핫패스 보존 게이트 (리팩토링 시 절대 위반 금지)

DONT_WAIT 전용 native entry는 flags 분기로 접지 말고 별도 유지 · routed send `ref ZlinkRoutingId`(256B 복사 회피) · 단일부 수신 `Message.AdoptNativeFromPool` 직결 · `TopicMessage` 버퍼 스왑+`resetTopic:false` 순서 · `RequestCallState` 단일 종료 게이트 · 요청 펌프 TCS 재도입 금지 · pinned static delegate/`Timer._handlerNative`/`SocketMonitor` GCHandle 해제 순서 · 단일부 publish 1 native submit. **hot TU 변경(A4, C3, C4, C9, D2, D3, D16)은 커밋 전 baseline vs patched 벤치 무회귀 증명.** (B4는 동일 native 호출이라 벤치 불요)

## 권장 실행 순서

A(삭제, 표면 축소) → B1·B2·B3(결함) → C1(errno) → C2·C5·C6·C7·C8(중복 통합) → D(기회순) → C9·A4·C3·C4(벤치 게이트) 마지막. E는 별도 계약 트랙. `test_stream_socket.cs`가 워킹트리 수정 중이라 B3 작업 시 충돌 조율.
