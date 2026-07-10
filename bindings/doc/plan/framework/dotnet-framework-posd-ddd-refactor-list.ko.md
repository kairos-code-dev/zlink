# `framework/languages/dotnet` POSD·DDD 리팩토링 수정 목록

> 2026-07-08 `framework/languages/dotnet/src` 전수 리뷰 결과. 대상은 core 바인딩(`bindings/dotnet`)과
> **별개**인 .NET **framework** 코드다: `Zlink.Framework`(Contracts+Runtime, 320파일 ~36.6k줄) + 위성 6프로젝트
> (`Zlink.Framework.AspNetCore`, `Zlink.HttpClient`, `Systems.Zlink.Stream.Connector`, `Zlink.Framework.Codecs.MessagePack`,
> `Zlink.Framework.Codecs.Protobuf`, `Zlink.Framework.Locations.Redis`). 서브시스템 7개로 나눠 read-only 리뷰.
> 파일:라인은 리뷰 시점 기준이므로 편집 전 현재 코드로 재확인한다.

**dead-code 판정 규칙(중요):** `Zlink.Framework` 어셈블리의 internal은 `Zlink.Framework.UnitTests` /
`Zlink.Framework.Locations.Redis.Tests` / `Zlink.Framework.AspNetCore` **3개에만** 노출된다. `public` 타입은
samples/e2e가 소비하는 계약 표면이므로 삭제 후보에서 제외(§A는 internal/private 무참조 + 도달 불가 분기만).
아래 dead 항목은 전부 tree 전체(src+tests+samples+e2e+testapps, obj/bin 제외) grep으로 무참조 확인했다.
서버-번들 bridge 경로(A-CH1)와 peer disconnect 사슬(A-SP1) 두 대형 삭제는 메인 루프에서 재-grep으로 재확인 완료.

위험 표기: **없음**(control plane, 빌드+테스트로 충분) / **code-motion**(코드 이동·의미 동일) /
**벤치**(per-message/receive/dispatch hot 경로에 닿음, baseline vs patched 무회귀 증명 필수).

체크박스는 완료 시 갱신한다. 이 문서는 두 리뷰의 **병합 정본**이다: 7-에이전트 POSD/DDD 전수 리뷰(A~D
트랙, 입자 단위 dead·결함·중복)와 codex 병렬 리뷰(§E 아키텍처 책임 분리 3건 + 정리 목록 + 진행 중 변경).
겹치는 항목(SPOT route router request 처리)은 §E3에 통합했다.

---

## A. 삭제 트랙 (dead code · 무참조 internal · 도달 불가 분기)

### A-CH. Channels / Messaging

- [x] **A-CH1. 서버-번들 SPOT route bridge 경로 전체 vestigial 삭제** (없음)
  - `ZLinkChannelRuntimeBundle.SpotRouteBridge`(`Runtime/Channels/ZLinkChannelRuntimeBundle.cs:35`) setter가 **어디서도 할당되지 않음**(재확인: `.SpotRouteBridge =` 매치 0). 읽기만 존재(`ZLinkChannelRuntimeManager.cs:76`, `ZLinkSpotRouteRouterDispatcher.cs:131`) → 항상 null.
  - 연쇄로 `ZLinkChannelReceiveLoop.TryHandleSpotRouteBridgePacket`(`ZLinkChannelReceiveLoop.cs:51-66`)는 도달 불가, `ZLinkSpotRouteRouterDispatcher.cs:130-134`의 `ServerRouterTarget` 분기는 항상 `ZLinkConfigurationException` throw. bridge는 `ZLinkRouteChannelRuntime`에만 붙는다.
  - property + 서버루프 bridge 처리 삭제. (의도된 미완성 기능이면 배선 여부를 먼저 확인.)
- [x] **A-CH2. 죽은 멤버 일괄 삭제** (code-motion)
  - `ZLinkRouteChannelRuntime.cs`: `_spotRouteBridgeOwner`(21,127 — write-only 필드, `owner` param 유일 소비처), `CanDispatchRoutePacket`(101), `HasSpotRouteBridge`(73) — 셋 다 무참조.
  - `ZLinkRouteHandlerRegistry.Get`(throwing 변형, `:18-27`) — `TryGet`만 사용. `ZLinkChannelReplyWriter.ReplyRawParts`(`:43-50`) — 무참조 + private `ReplyParts` pass-through.
  - `ZLinkChannelRuntimeBundle.RemoveManualConnection`(57)/`ListManualConnections`(73) — 무참조.
  - `ZLinkClientCallCodec.CopyMetadata`(105), `ZLinkEnvelopeCodec.DecodeBytes`(271) — 무참조. (`EncodePart<T>`(168)은 **live** — `ZLinkActorEntrySpotJoinCoordinator.cs:44`·`ZLinkActorEntrySpotRoutePackets.cs:65`에서 호출, 삭제 제외.)
  - 삭제 후 `ZLinkRouteChannelRuntime`에 새로 남은 전달 전용 저장 필드(`_handlers`, `_internalPackets`, `_codecs`)도 함께 지역 변수/직접 전달로 접었다.
- [x] **A-CH3. 죽은 "no-codecs" 편의 오버로드 클러스터 삭제** (code-motion)
  - `ZLinkClientCallCodec.DecodeEnvelopeReply(...no codecs)`, `ZLinkEnvelopeCodec.EncodeBody(object?, Type?)`, `ZLinkClientCallCodec.EncodeEnvelopeParts(header, message)`를 제거했다.
  - 삭제 뒤에도 `DecodeEnvelopeReplyAndDispose(...no codecs)`와 `EncodeParts(header, body, type)`가 얕은 pass-through로 남지 않도록 live 호출부를 codecs 인자를 명시하는 overload로 옮기고 두 wrapper도 제거했다.
  - 현재 envelope encode/decode helper는 codecs 인자를 받는 경로 하나씩만 남는다. codecs가 필요 없는 호출부는 `null`을 명시한다.

### A-SP. Spots

- [x] **A-SP1. peer disconnect + list-connections 사슬 전체 삭제** (없음)
  - 재확인: node-runtime 밖 호출처 0. `ZLinkSpotNodeRuntime.cs:194-212`(`DisconnectRouter`/`DisconnectPubSub`/`ListRouterConnections`/`ListPubSubConnections`) → 유일 소비처가 `ZLinkSpotPeerConnector.cs:37-47`(`DisconnectRouter`/`DisconnectPubSub`) → 유일 소비처가 `ZLinkSpotPeerConnectionSet.cs:49-81`(`RemoveRouterManual`/`RemovePubSub`), `ListRouterManual`/`ListPubSubManual`.
  - peer 서브시스템의 disconnect/enumerate 표면 전체를 삭제했다. connect 경로(`ZLinkSpotNodeInitializer.ConnectManualPeers`)는 live라 유지했다.
- [x] **A-SP2. discovered-router 추적 삭제** (없음)
  - `ZLinkSpotPeerConnectionSet.cs:7-9,30-47`(`TryAddRouterDiscovered` + `_routerDiscovered`/`_routerDiscoveredRidKeys`) — 프로덕션 호출처 없음, `SpotPeerConnectionSetTests.cs`만 구동 → `_routerDiscovered`는 항상 빔. 테스트가 자기 코드를 살려두는 상태.
  - discovered-router 추적 상태와 이를 살리던 테스트 파일/project include를 삭제했다.
- [x] **A-SP3. 구독 관찰 지표 사슬 삭제** (벤치)
  - `ZLinkSpotActivation.cs:95-105,109`(`SubscriptionMessageCount`/`DispatchCount`/`IgnoreCount`/`LastSubscriptionTopic`/`LastSubscriptionMessageName` + internal `SubscriptionPumpState`) — 무참조(`IZLinkSpotContext`/`IZLinkSpotCommonContext`에 없음, `Contracts/Spots/ZLinkSpot.cs:217-246` 대조).
  - 이들이 `ZLinkSpotSubscriptionRegistry`의 `MessageCount`/`DispatchCount`/`IgnoreCount`/`LastTopic`/`LastMessageName`의 유일 소비처 → `DispatchMessageAsync`의 `Interlocked.Increment`+Last* 쓰기(`ZLinkSpotSubscriptionRegistry.cs:93-94,98,119,159,174`)가 죽은 getter만 먹이려고 **매 구독 메시지마다** 실행. getter+registry bookkeeping 동반 삭제(per-publish 경로라 벤치).
  - 삭제 후 `ZLinkSpotSubscriptionPump.State`/`_lastError`도 `SubscriptionPumpState` 전용 관찰 상태라 함께 제거했고, 남은 pass-through catch도 없앴다.
- [x] **A-SP4. Spot actor 디스크립터 죽은 멤버** (code-motion)
  - `ZLinkSpotActorLifecycleCoordinator.LeaveAsync`(`:31-40`) — `JoinAsync`만 호출됨, `LeaveAsync` 무참조(본문이 ctor 의존을 `_ = actors; …`로 버림 = vestigial 표식). 실제 leave는 `ZLinkSpotActivationActors.NotifyActorLeftAfterJoinCommitCoreAsync`.
  - `ZLinkSpotActorJoinRegistry.HasHandlers`(`:7`) — 무참조.
  - `ZLinkSpotActorDescriptorBuilder.CreateLifecycle`(`:49-68`) — 유일 호출부(`ZLinkSpotActorAttributedDescriptorFactory.cs:177`)가 항상 non-null `invoker`+`passSpotArgument:false` → `invoker ?? CreateInterfaceInvoker(...)` 분기·`passSpotArgument=true` 기본값 도달 불가. 시그니처 축약.
  - 이후 D3에서 남은 join 처리까지 `ZLinkSpotActivationActors`로 접어 `ZLinkSpotActorLifecycleCoordinator` 파일을 제거했다.

### A-HD. Handlers

- [x] **A-HD1. 죽은 핸들러 오버로드/메서드** (code-motion)
  - `ZLinkHandlerInvocationEngine.InvokeAsync(IServiceProvider, Type, …)`(`:7-18`) — 8개 호출부 전부 다른 2개 오버로드 사용, `services`+`handlerType` 판 무참조(내부 `ActivatorUtilities.GetServiceOrCreateInstance` 도달 불가).
  - `ZLinkHandlerContractInspector.TryFindGenericInterface`(`:5-22`) — 무참조(`EnumerateGenericInterfaces`만 사용).
  - 삭제 후 남은 invocation overload 2개는 argument plan 기반 경로와 argument sink 기반 경로로 각각 live라 유지했다.

### A-AS. Actors / Streams

- [x] **A-AS1. raw stream-transport 메서드 클러스터(7) + context 래퍼 삭제** (없음)
  - `Runtime/Streams/ZLinkSessionStreamTransport.cs:15,36,69,127`(`SendRawAsync`/`RequestRawAsync`/`ReplyRawAsync(header,codec,payload)`/`RequestJsonAsync`) + pass-through 래퍼 `Runtime/Streams/ZLinkSessionContext.cs:130,139,150` — 전부 internal 무참조(context→transport 내부 전달만). live reply 경로는 **다른** 오버로드(`ReplyRawAsync(header, ZLinkActorReply)`, `context.Write`/`ReplyActorRawAsync`) 사용. 얇은 전달 계층 통째 제거.
  - `ZLinkActorReply.FromPayload(codec, payload)` 2-arg(`Runtime/Streams/ZLinkActorReply.cs:20-30`) — 유일 caller(`ZLinkSpotHandlerInvoker.cs:138`)가 4-arg 사용.
  - `ZLinkActorRuntimeState.CurrentActorGeneration`(`Runtime/Actors/ZLinkActorRuntimeState.cs:36-39`) — reader 없음.
  - 삭제 후 `ZLinkSessionStreamTransport`는 actor reply/error frame 작성만 맡고, `ZLinkSessionContext.ReplyActorRawAsync`는 actor relay delegate로 직접 사용되므로 새 pass-through 대상은 아니다.

### A-HS. Host / Backend

- [x] **A-HS1. Host 죽은 오버로드·도달 불가 분기** (없음)
  - `ZLinkFrameworkActorFacade.SubmitActorForReplyAsync`(byte[] 오버로드, `Runtime/Host/ZLinkFrameworkActorFacade.cs:203-212`) — live 경로는 runtime partial(`ZLinkFrameworkRuntimeActors.cs:281`)→`_actors.SubmitActorForReplyCoreAsync`. `_actors.SubmitActorForReplyAsync` 매치 0.
  - `ZLinkSpotRouteRouterDispatcher.cs`: `ResolveRouterSpotNode`의 `targetNodeRid` optional 분기(`:98-102`)는 유일 호출부(`:41`)가 2-arg라 도달 불가; `TryResolveLocalAcceptedSpotNode`의 `routerChannelId`(`:48`)는 미사용.
  - `ZLinkFrameworkRuntime.cs:210` `return _state ?? throw …` — `:206`에서 이미 null throw → throw 분기 도달 불가. `return _state`로 축약.
  - `targetNodeRid` 분기 삭제 뒤 `TryResolveLocalAcceptedSpotNode`가 bool/out 전달 래퍼로 남아 `ResolveTarget`에 인라인했다.

### A-LO. Locations / Configuration / Execution

- [x] **A-LO1. 미사용 필드·상수-false 파라미터·discard ctor 인자** (code-motion)
  - `ZLinkLocationRuntimeQueryService.cs:19,30,40` `_resolvers` — 저장만 하고 미사용(쿼리는 store 직행). 필드+ctor param+DI 배선(`ZLinkFrameworkServiceRegistrar.cs:332`) 제거.
  - `ZLinkChannelRegistrationValidator.cs:8,45,79,105` `acceptedBySpotRouteChannel` — 유일 caller가 리터럴 `false`(`ZLinkFrameworkRegistrationValidator.cs:20`) → `&& !acceptedBySpotRouteChannel`(105) 항상 true. 파라미터+항상-참 항 제거.
  - `ZLinkStoreLocationResolvers.cs:23,29,33-34` `options`+`timeProvider` — 즉시 `_ = …`(lease tracker에 시간 위임). 드롭(테스트 위치 인자 갱신).
  - `ZLinkLocationValueCodec.cs:49,64` `TryParseAutoConnectType`/`TryParseRole` — 무참조(Redis store는 자체 codec). parse 방향 dead.
  - 삭제 후 location query는 store 기반 읽기 책임만 남고, resolver constructor는 실제 사용하는 store/tracker/event/observed 인자만 받는다. 새 pass-through나 discard 인자는 남기지 않았다.
- [x] **A-LO2. 노이즈 제거** (없음/벤치)
  - `ZLinkAutoConnectLoop.cs:134,137` `var before = …; _ = before;` — 순수 노이즈 삭제.
  - `ZLinkSerialExecutionQueue.cs:209-212` — `WhenAny(...); if (ReferenceEquals(completed, turn.Suspended)) { }` 빈 본문. serial hot 경로에서 결과 계산 후 빈 분기 → 의도(재-suspension 처리 위치) 확인 후 제거하거나 주석. **먼저 의도 확인.**
  - `ZLinkRuntimeTaskRunner.cs:56-62` `ReportErrorSinkFailure` — 본문이 `_ = name; _ = exception;`. error sink 자체가 throw할 때의 최후 fallback(`ZLinkSerialExecutionQueue.cs:184`)인데 전부 버림. "의도적 swallow" 명시하거나 debug log로 라우팅.
  - `ZLinkSerialExecutionQueue`는 `Task.WhenAny(ownerTask, turn.Suspended)` 대기만 유지하고 빈 분기를 삭제했다. `ZLinkSerialWorkItem`의 `Suspended` 결과 분기는 live라 유지했다.
  - `ReportErrorSinkFailure`는 `ZLinkFrameworkDebugLog.TaskFailure`와 `ZLinkRuntimeErrorSink.ReportUnhandledCallbackException`으로 라우팅해 최후 fallback 의도를 코드로 남겼다.

### A-ST. 위성 프로젝트

- [x] **A-ST1. Stream.Connector 죽은 인코더·필드** (없음)
  - `Runtime/Protocol/Framing/ZlinkStreamFrameCodec.cs:40-52` `Encode`(할당형 full-frame), `:19-25` `EncodePrefix` — 무참조(send는 `WritePrefix`/`WriteFrame`/`GetFrameSize`만).
  - `Runtime/ZlinkStreamOutboundFrame.cs:4` `Header` 필드 — 채워지지만 소비처는 `HeaderBytes`/`PayloadBytes`만. positional 멤버 제거.
  - `Runtime/ZlinkStreamTaskRunner.cs:35` `_ = state.Name;`(로깅 안 됨), `Runtime/ZlinkStreamReceiveDispatcher.cs:110` `WireError.Code`(디코드 후 미사용, 우선순위 낮음).
- [x] **A-ST2. Redis 죽은 Lua 스크립트** (없음)
  - `Zlink.Framework.Locations.Redis/ZLinkRedisLocationScripts.cs:142-160` `RemoveByOwner`(단수, per-kind bulk-remove) — store는 `RemoveAllByOwner`(`ZLinkRedisLocationStore.cs:249`)만 호출. ~19줄 미사용.
- [x] **A-ST3. AspNetCore obsolete public 무참조(다음 계약 break 시 후보)** (없음, 계약)
  - `ServiceCollectionExtensions.cs:28-33` `AddZLinkHandlersFromAssemblyContaining<TMarker>` — `[Obsolete]` + 대체 명시 + 호출처 0. public이라 지금은 보존, 계약 break 시 제거.
  - 완료: src/tests/samples/e2e 호출처 0건을 재확인했지만 public 계약 표면이므로 이번 POSD 리팩토링에서 삭제하지 않는다. 이 항목은 현시점 삭제 대상이 아니라 다음 breaking release에서 제거 여부를 결정할 후보로 분류했다.

---

## B. 결함 수정 (correctness — 리뷰 중 발견)

- [x] **B1. Stream.Connector 빈 metadata value round-trip 실패** (correctness)
  - 인코드는 빈 **키**만 거부(`Runtime/Protocol/ZlinkStreamMetadataCodec.cs:54-73`)하므로 `""` 값은 `valueLength=0`으로 인코드됨(`ZlinkStreamMetadata.With`는 null만 검사, `Contracts/ZlinkStreamMetadata.cs:26`). 그러나 디코드 `DecodeString(...,"value")`(`ZlinkStreamMetadataCodec.cs:87`)는 `length==0`에서 `"Metadata value is invalid"` throw. **이 라이브러리가 만든 프레임을 스스로 디코드 못 함.** 디코드에서 zero-length value 허용(키는 ≥1 유지)하거나 인코드에서 빈 value 거부로 대칭화.
- [x] **B2. Stream.Connector `Send().Submit()` 프레임 이중 빌드/이중 압축** (벤치, correctness)
  - `Runtime/Calls/ZlinkStreamSendBuilder.cs:45-52`가 `ValidateSendEncoded`(→`BuildOutboundFrame`: 압축+correlation-id stamp) 후 `SubmitAsync`→`SendEncodedAsync`가 `BuildOutboundFrame` **재실행**. `Compress()` 시 payload LZ4 이중 압축 + correlation 카운터 2 증가(2번째 id만 전송). 1회 빌드 후 재사용.
- [x] **B3. Stream.Connector transport connect 실패 시 소켓/스트림 누수** (없음)
  - `Runtime/ZlinkStreamTransportFactory.cs:141-164` — `AuthenticateAsClientAsync`(TLS)/`ClientWebSocket.ConnectAsync` throw 시 `TcpClient`/`SslStream`/`ClientWebSocket` 미-dispose. connect 실패 경로 try/dispose.
- [x] **B4. Spot actor-frame 헤더 손상 시 배치 전체 중단 + 잔여 파트 누수** (벤치)
  - `ZLinkSpotActorFrameReader.TryRead`(`:20-30`)는 잘린 body엔 `false`(skip-and-continue) 반환하지만 **헤더 디코드 실패엔 throw**. caller 루프(`ZLinkEntrySpotActorDispatcher.cs:19-25`)는 `if (!TryRead(...)) continue;`만 처리·try/catch 없음 → 헤더 하나 나쁘면 detach된 dispatch task 밖으로 전파, native 배치의 이후 프레임 전부 방치+`Message` 핸들 누수. bool-반환 계약과 불일치. 디코드 실패를 skip 프레임으로 변환(report+dispose), 배치 계속 drain.
- [x] **B5. entry-spot join NotConnected fast-path에서 `replyParts` 누수** (없음)
  - `ZLinkActorEntrySpotJoinCoordinator.cs:59-72` — `result.Result == NotConnected`일 때 `replyParts` dispose 없이 `return await JoinRemoteAsync(...)`. 다른 exit는 `DecodeEntrySpotJoinReply`의 `finally`에서 dispose. native 콜백이 NotConnected와 함께 파트를 실으면 미해제. 원격 분기 전에 `ZLinkMessageParts.DisposeAll(replyParts)`.
- [x] **B6. route/spot HandlerNotFound 예외 이중 생성(동일 메시지)** (없음, 정보 누출)
  - `ZLinkRoutePacketDispatcher.cs:172-186` / `ZLinkSpotRouteDispatcher.cs:63-77` — 동일 `ZLinkFrameworkException(...HandlerNotFound, "No … handler …")`를 `ReplyError`용·`dispatchErrors.Report`용으로 두 번 생성. 오류 identity(kind+문자열)가 한 사실인데 중복. 1회 생성 후 양쪽 전달.
- [x] **B7. 항상-참 분기가 의도를 가림** (없음)
  - `ZLinkRouteChannelRuntime.cs:143-148`(`TrySendViaSpotRouteBridge`)/`:168-173`(`TryRequestViaSpotRouteBridge`) — `if (!accepted) throw` 뒤 `accepted`는 확정 참인데 `if (accepted) …Drain(); return accepted;`가 false 가능한 듯 읽힘. 무조건 실행/반환으로.
- [x] **B8. Location `AlreadyOwned` claim이 rollback 없이 재-activate로 낙하** (없음, 낮은 신뢰·caller 의존)
  - `ZLinkLocationLifecycle.cs:84-107,118-124` — `ClaimActorAsync`가 `AlreadyOwned` 반환 시, `ExecuteActorClaimThenActivateAsync` switch는 `Conflict`/`StoreFailure`만 특수 처리 → `AlreadyOwned`가 `activate(...)` 재도달, `catch when (claim.Status == Claimed)` rollback은 `AlreadyOwned` 미포함. 도달 가능하면 이중 activate + 실패 시 누수. `AlreadyOwned` arm(기존 반환/no-op) 명시 권장.
- [x] **B9. `CloseActorBoundSessionAsync` sync-over-async + 토큰 드롭(냄새)** (없음)
  - `Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs:284-292` — `Async`/`ValueTask` 시그니처인데 블로킹 native `CloseActorBoundSession` 호출, 취소는 호출 **전**만 검사 후 `CompletedTask`. 오늘은 오작동 아님이나 async 계약이 장식적 — close 도중 취소를 기다리는 caller는 취소 못 받음.
  - backend 내부 계약을 동기 `CloseActorBoundSession(...)`으로 바꿔 native API의 실제 동기성을 드러냈다. 상위 runtime/session service의 async 표면은 기존 호출자 계약 때문에 유지하되, 내부 close 경로는 sync 호출 뒤 `ValueTask.CompletedTask`를 반환한다.
  - 검증: 관련 unit filter `FullyQualifiedName~Actor|FullyQualifiedName~Session|FullyQualifiedName~BoundSession|FullyQualifiedName~EntrySpot` 64 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.

---

## C. 구조 통합 — 지식 중복 소거 (없음 / code-motion, 일부 벤치)

- [x] **C1. ⭐stream wire 포맷이 core Streams와 Stream.Connector 위성에 통째 중복** (없음, 최대 유지보수 impact)
  - 위성 `Framing/ZlinkStreamFrameCodec`(2B BE header-len + 4B BE payload-len prefix), `ZlinkStreamHeaderCodec`, `ZlinkStreamMetadataCodec`, `ZlinkStreamCorrelation`, `WireError` 각각이 core `Runtime/Streams`의 `ZLinkStreamFrameCodec.cs:11-16`(동일 6B prefix)·`ZLinkStreamHeaderCodec`·`ZLinkStreamMetadataCodec`·`ZLinkStreamCorrelation`·`ZLinkStreamWireError`와 바이트 동일 쌍둥이. wire 포맷 변경 시 두 곳을 손으로 lockstep. "connector는 의존성 없이 유지" 패키징 근거는 실재하나 **포맷 지식**(prefix 레이아웃·TLV metadata·헤더 플래그 비트·correlation hex)이 누출된 중복. 최소: 두 곳 상호 참조 주석 + 단일 spec test 게이트. 이상: 순수 바이트 레이아웃 상수를 contracts-only 공유 파일로.
  - 완료: core stream frame/header codec과 Stream.Connector frame/header codec에 상호 참조 주석을 두고, `StreamWireInteropTests`를 추가했다. 이 테스트는 같은 header(metadata 빈 값, correlation id, request seq, compressed flag 포함)를 두 header codec이 같은 바이트로 인코드하는지, connector frame을 core frame codec이 해석하는지, core/connector frame 인코딩이 같은지 검증한다.
  - 재검토: connector 독립 패키징을 깨지 않기 위해 새 shared assembly나 public wire helper는 만들지 않았다. 대신 drift gate를 unit test에 두어 포맷 지식이 실제로 갈라지는 순간 실패하게 한다. 새 추상화를 추가하지 않았으므로 리팩토링 결과가 또 다른 얕은 모듈이 되지 않는다.
  - 검증: 관련 unit filter `FullyQualifiedName~StreamWireInterop|FullyQualifiedName~MessageFlow|FullyQualifiedName~DispatchErrorReporter|FullyQualifiedName~UnhandledDispatch|FullyQualifiedName~EntrySpotActorDispatch` 21 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed, `git diff --check` passed. E2E는 실행하지 않았다.
- [x] **C2. 5개 near-identical "decode→dispatch→report" 상태기계** (벤치, per-message)
  - `ZLinkChannelRequestDispatchPipeline.cs`·`ZLinkChannelCommandDispatchPipeline.cs`·`ZLinkChannelPublishDispatchPipeline.cs` + `ZLinkRoutePacketDispatcher.DispatchSendAsync`/`DispatchRequestAsync`(`:49-226`). 골격 반복: `TryGet handler`→`ZLinkMessageFlowLogger.{HandlerMissing/Dropped}`→`dispatchErrors.Report(HandlerMissing)`→`DecodeBody` try/catch→`PayloadDecodeFailed`→dispatch try/catch→`Report(HandlerException)`+`Flow.Trace` guarded. `(Surface,Kind,Reason,Action)` 튜플+flow-guard 관용구 ~15회 복붙(정보 누출+특수/범용 혼합). kind/surface/reply-strategy로 매개화한 dispatch core 1개, per-kind는 decode+reply 배선만.
  - 완료: channel command/request/publish pipeline과 route mesh send/request dispatcher의 flow logging, `ZLinkDispatchFailure` 생성, decode 실패 보고, success trace를 `ZLinkDispatchFlowScope`로 모았다. handler 선택, request reply 작성, publish 다중 endpoint 순회, route source RID 추출, internal route reply 작성은 각 dispatcher에 남겨 메시지 종류별 의미를 숨기지 않았다.
  - 재검토: `ZLinkDispatchFlowScope`는 새 범용 dispatcher가 아니라 dispatch 관측/오류 기록에 닫힌 값 객체다. reply 전략을 delegate로 밀어 넣는 "하나의 dispatch core"는 request seq, raw internal reply, publish fan-out 정책을 호출자 인자로 노출하는 얕은 모듈이 되므로 적용하지 않았다. 따라서 이번 리팩토링 결과는 새 POSD 리팩토링 대상이 아니라 기존 상태기계가 반복하던 관측 지식만 감춘 깊은 보조 객체다.
  - 검증: 관련 unit filter `FullyQualifiedName~Channel|FullyQualifiedName~Dispatch|FullyQualifiedName~MessageFlow|FullyQualifiedName~Route` 68 passed. `dotnet test framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj --nologo` passed. Redis lease-expiry 테스트는 고정 sleep 대신 제한 시간 안에서 실제 만료 관측을 기다리게 바꿔 전체 unit 검증이 환경 지연에 흔들리지 않게 했다. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed, `git diff --check` passed. E2E는 실행하지 않았다.
- [x] **C3. dispatch reporter/tracer 중복 생성 + 삼중 인자** (벤치)
  - `ZLinkChannelPacketDispatcher.cs:15-58` — `(registration.DispatchOptions, ResolveServices(runtime), logger ?? NullLogger…)` 삼중이 5회, `ZLinkDispatchErrorReporter`를 command/publish/request 파이프라인마다 **별도 인스턴스**로 넘김(공유 필드 `_dispatchErrors` 대신). 1회 생성 후 공유 주입(reporter 4+tracer 1 할당 → 1).
  - channel dispatcher 생성자에서 logger, services, `ZLinkDispatchErrorReporter`를 한 번만 만들고 command/publish/request pipeline에 공유 주입했다. 별도 `_flow` 필드는 제거하고 reporter가 소유한 `Flow` tracer를 사용해 reporter/tracer 생명주기를 한 곳에 맞췄다.
  - 검증: 관련 unit filter `FullyQualifiedName~Channel|FullyQualifiedName~Dispatch|FullyQualifiedName~MessageFlow|FullyQualifiedName~Route` 68 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C4. `TryHandleSpotRouteBridgePacket` near-duplicate** (벤치)
  - A-CH1에서 서버 bundle의 도달 불가 bridge 경로를 제거하면서 `ZLinkChannelReceiveLoop` 쪽 중복 처리도 함께 삭제했다. 현재 bridge packet 처리는 실제 route channel bridge가 붙는 `ZLinkRouteReceivePump` 경로에만 남는다.
- [x] **C5. `ResolveSurface(transportName)` 상수-함수+죽은 파라미터 중복** (없음)
  - `ZLinkChannelRequestDispatchPipeline.cs:130-133` / `ZLinkChannelCommandDispatchPipeline.cs:121-124` — `transportName` 무시하고 무조건 `Channel` 반환, 실제 인자는 항상 리터럴 `"Channel"`(`ZLinkChannelPacketDispatcher.cs:89,135`). 파라미터+메서드 제거, 상수 사용.
- [x] **C6. entry-spot vs user-spot outbound 파사드 verbatim 중복** (code-motion)
  - `ZLinkEntrySpotActivationOutbound.cs:1-101` ≡ `ZLinkSpotActivationOutbound.cs:1-152` — `_outboundEndpoint`로의 pass-through 벽(`Publish`/`SendToChannel(Async)`/`RequestToChannel(Async)`/`RequestToSpotAsync`/`SendToSpot(Async)`/`PublishCurrentAsync`)이 두 파일에 유지. 공유 표면을 `ZLinkSpotOutboundEndpoint`/공유 base로 hoist하거나 단일 구현 위임.
  - 진행: `ZLinkSpotOutboundSurface`/`IZLinkSpotOutboundSink` 중간 surface를 제거하고 `Outbound`가 `ZLinkSpotOutboundEndpoint`를 직접 가리키게 했다. async wrapper들은 불필요한 `async return await` 상태머신 없이 endpoint `ValueTask`를 그대로 반환한다.
  - 재검토: public context interface를 구현하는 activation 메서드는 아직 남아 있어 C6 자체는 완료가 아니다. 내부 default-interface 구현으로 억지 이동하면 구체 activation의 public 계약 구현이 흐려질 수 있어, 남은 중복은 별도 설계 단위로 유지한다.
  - 진행: `IZLinkCurrentSpotActivation`에서 fluent outbound(`Publish`/`SendToChannel`/`RequestToChannel`) 표면을 제거하고 `Outbound` property만 노출했다. ambient `ZLinkSpotOutboundService`는 현재 activation의 `Outbound` endpoint를 직접 사용한다. 두 activation 파일에 있던 fluent forwarding 메서드와 사용되지 않는 address 기반 `SendToSpot`/`RequestToSpot` forwarding 메서드는 삭제했다.
  - 재검토: `Outbound`는 이미 public context 계약(`IZLinkSpotCommonContext`)의 깊은 표면이므로 내부 current interface가 같은 endpoint를 참조해도 새 helper가 생기지 않는다. 남은 raw async forwarding은 handler turn 안에서 encoded parts를 보내는 내부 runtime 경계라, endpoint concrete를 current interface에 노출해 억지로 default 구현으로 옮기면 새 pass-through 계층이 된다. 따라서 C6 전체는 아직 완료 처리하지 않는다.
  - 진행: 호출처가 없는 단일 `Message` raw overload(`RequestToChannelAsync`, `SendToChannelAsync`, `RequestToSpotAsync`, `PublishCurrentAsync`, `SendToSpot`)를 activation partial, outbound endpoint, outbound transport에서 제거했다. typed outbound call은 이미 envelope parts를 만든 뒤 current activation interface로 들어오므로 parts 기반 경로만 남는다.
  - 재검토: 새 base class나 default-interface 구현으로 남은 parts 기반 메서드를 억지로 접지 않았다. 두 activation이 public context와 runtime current activation을 동시에 구현하는 구조가 남아 있어 C6 전체는 아직 완료 처리하지 않는다.
  - 완료: `ZLinkSpotActivationOutbound.cs`와 `ZLinkEntrySpotActivationOutbound.cs` 파일을 삭제했다. `IZLinkCurrentSpotActivation`은 parts 기반 outbound 메서드를 직접 구현하지 않고, 실제 책임 객체인 `ZLinkSpotOutboundEndpoint`를 내부 current outbound 경계로 노출한다. typed outbound call은 envelope parts를 만든 뒤 endpoint로 직접 들어간다.
  - 재검토: endpoint는 public `IZLinkSpotOutbound` call 객체 생성, channel shared-client request/send, routed spot request/send, current publish를 실제로 소유한다. activation에 새 base class나 default-interface forwarding을 만들지 않았고, 두 activation에는 public context property(`Handlers`, `Outbound`)만 남겨 같은 pass-through 파일이 다시 생기지 않는다.
  - 검증: 관련 unit filter `FullyQualifiedName~Spot|FullyQualifiedName~Outbound|FullyQualifiedName~Channel|FullyQualifiedName~Request|FullyQualifiedName~EntrySpot|FullyQualifiedName~Actor` 111 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C7. per-factory 리플렉션 헬퍼 + `SpotActorContract` 중복** (code-motion)
  - `ZLinkSpotActorAttributedDescriptorFactory.cs:239-272` / `ZLinkSpotDescriptorFactory.cs:194-220` — `EnumerateInterfaceMethods`/`GetSpotActorContract`/`SpotActorContract` record를 각자 재구현(후자는 `IZLinkSpot<>`도 수용하는 차이만). 공유 spot-contract inspector 추출.
  - `ZLinkSpotActorContractInspector`로 interface method 순회와 generic contract 추출을 통합했다. surface-specific 계약과 spot-or-entry 계약은 별도 public helper로 유지했고, non-generic interface가 `null` alternate와 매치되는 회귀는 targeted test에서 잡아 수정했다.
- [x] **C8. request-submit + `ZLinkRawReplyCompletion.Complete` 보일러플레이트 3벌** (없음)
  - `ZLinkSpotOutboundEndpoint.cs:82-95`(1) / `ZLinkSpotOutboundTransport.cs:28-43,55-70`(2) — `SubmitRequestAsync<IReadOnlyList<Message>>(parts, (pending,complete,fail)=>X.Request(…ZLinkRawReplyCompletion.Complete(…)…, DontWait, timeout), ct)` 형태가 channel-request·spot-request(Message)·spot-request(parts)로 복붙. native `Request` delegate 받는 private 헬퍼 1개.
  - `ZLinkRawRequestSubmitter`로 submitter 호출, native request delegate, `ZLinkRawReplyCompletion.Complete` 연결을 모았다. 실패 메시지의 result 포맷은 helper 안에서만 처리하고, `ZLinkRawReplyCompletion`은 reply 완료/실패 처리 책임만 유지해 새 범용 포맷터가 되지 않게 했다.
  - 검증: 관련 unit filter `FullyQualifiedName~Spot|FullyQualifiedName~Outbound|FullyQualifiedName~Request|FullyQualifiedName~Channel|FullyQualifiedName~Route` 103 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C9. Actor 프레임/헤더 지식 중복** (벤치, per-reply)
  - `ZLinkActorClient.cs:157-183`이 `ZLinkStreamFrameCodec.cs:7`의 역(6B prefix + slice offsets)을 인라인 재구현 → 레이아웃 변경 시 client 조용히 깨짐. `ZLinkStreamFrameCodec.TryDecode` 추가 후 `DecodeReply`가 호출.
  - "request 헤더→response 헤더" 생성이 3곳(`ZLinkSessionStreamTransport.cs:79-86`, `ZLinkSessionStreamCalls.cs:128-136`, `ZLinkActorReply.cs:67-75`). 그중 2곳(`ZLinkSessionStreamCalls.cs:135`, `ZLinkActorReply.cs:74`)은 "Echo the request's correlation id" 주석까지 동일, 세번째(`ZLinkSessionStreamTransport.cs:79-86`)는 **구조만 동일**(해당 주석은 형제 메서드 `ReplyErrorAsync`의 line 117에 있음). `ZlinkStreamHeader.CreateResponse(requestHeader, codec, flags, metadata)` 팩토리 1개.
  - `ZLinkStreamFrameCodec.TryDecode`가 6바이트 prefix와 header/payload slice 계산을 소유하게 하고, actor client는 codec 결과만 사용하게 했다. request reply header 조립은 `ZLinkStreamReplyHeaders.CreateForRequest`로 모아 `HasRequestSeq`, request name, correlation id echo 규칙을 한 곳에 둔다.
  - 검증: 관련 unit filter `FullyQualifiedName~Actor|FullyQualifiedName~Stream|FullyQualifiedName~Session|FullyQualifiedName~Reply|FullyQualifiedName~Frame` 289 passed(넓은 매칭), `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C10. SpotNode-by-RoutingId 조회 루프 중복(4벌)** (없음)
  - `ZLinkActorEntrySpotJoinCoordinator.cs:255-268`(`TryFindSpotNode`), `ZLinkActorEntrySpotRouteInternalPacketDispatcher.cs:83-94`(`FindSpotNode`) + `ZLinkSpotRouteRouterDispatcher`에 2벌 더 — 모두 `state.SpotNodes.Values` 순회·`RoutingId == nodeRid`. `ZLinkFrameworkRuntimeState.TryFindSpotNode(rid)` 단일 헬퍼.
  - 현재 코드 기준 중복 루프는 3벌이라 `ZLinkFrameworkRuntimeState.TryGetSpotNodeByRoutingId`로 모았다. router-capable 여부와 actor-route-specific 예외는 caller 정책으로 유지해 helper가 역할별 의미를 흡수하지 않게 했다.
- [x] **C11. native actor bind/unbind 가드+호출 중복 / `ToNative()` 래퍼 5벌** (없음)
  - `ZLinkActorSessionStreamBinding.cs:65-104` / `ZLinkSessionActorCoordinator.cs:205-229` — `stream is not ZLinkManagedStream` 가드 + `Bind/UnbindActorAsync(동일 timeout)`. `ZLinkManagedStream`로 집약.
  - `return actorRef.ToNative();` 트리비얼 래퍼 5곳: `ZLinkActorManagerService.cs:85`, `ZLinkSessionActorCoordinator.cs:186`, `ZLinkActorEntrySpotJoinCoordinator.cs:306`, `ZLinkFrameworkActorFacade.cs:263`, `ZLinkActorRemoteJoiner.cs:269` — 전부 `ZLinkBackendActorRef.ToNative()` 직접 호출로 대체.
  - native stream actor bind/unbind는 `ZLinkNativeActorStreamBinding`으로 모아 `ZLinkManagedStream` type check와 timeout 전달을 한 곳에 둔다. actor/session 쪽의 ownership·exception 정책은 caller에 남겼다.
- [x] **C12. Location 해석기 지식 중복** (없음/벤치)
  - mesh-scan 해석기: `ZLinkLocationAddressResolvers.cs:15-47` vs `ZLinkLocationSpotRouteRefResolver.cs:10-45`(`ZLinkSpotLocationRidResolver`) — 동일 `SpotNodes→SpotMeshChannelName ?? SpotNodeName + SpotMeshChannels.Keys` mesh-name 집합 + 동일 "loop meshes, `ResolveSpotRowAsync`, first hit" 스캔(둘 다 live). 공유 mesh-name provider + 스캔 헬퍼.
  - live-row 필터(observed-generation + lease-live): `ZLinkStoreLocationResolvers.cs:172-189`(`FilterLiveAsync`, 인접 `ResolveAsync:140-166`과 함께) vs `ZLinkLocationRuntimeQueryService.cs:251-275` — 각자 재구현(per-read hot). `ZLinkLiveRowFilter` 추출.
  - `ZLinkSpotMeshLocationResolver`가 mesh-name 집합과 spot row scan을 소유하게 하고, address resolver와 route-ref resolver는 caller별 반환 정책만 남겼다. `ZLinkLiveLocationRows`가 observed-generation guard와 owner lease join을 맡아 store resolver와 runtime query service의 live-row 판정을 공유한다.
  - 검증: 관련 unit filter `FullyQualifiedName~Location|FullyQualifiedName~AutoConnect` 102 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C13. 2-part reply submit + `DisposeAll` 패턴 중복** (없음)
  - `ZLinkSpotActivationDispatcher.cs:267-277,292-302`(success/error 두 번), `ZLinkSpotRouteDispatcher.cs:226-229,250-253`(`replyParts[0]`+`[1]` 리터럴 = "reply는 항상 2파트" 불변식 누출). `SubmitTwoPartReply(received, parts)` 추출 + 리스트 순회.
  - `ZLinkSpotReplySubmitter.SubmitAndDispose`로 두-part reply submit과 `DisposeAll`을 한 곳에 모았다. 어떤 reply envelope를 만들지와 dispatch error 보고 정책은 caller에 남기고, helper는 builder 계약과 lifetime 정리만 소유한다.
  - 검증: 관련 unit filter `FullyQualifiedName~SpotRoute|FullyQualifiedName~SpotActivation|FullyQualifiedName~Reply|FullyQualifiedName~Dispatch|FullyQualifiedName~InternalRoute` 39 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C14. 두 개의 병렬 message-flow 로깅 서브시스템** (없음)
  - `Runtime/Diagnostics/ZLinkMessageFlowTracer.cs`(correlation-keyed) vs `ZLinkMessageFlowLogger.cs`+`ZLinkTelemetry.TraceFlowEvent`(event-keyed) — 동일 필드셋(surface/kind/packetName/channelName/actorId/spotRid)을 독립 파라미터 리스트·독립 렌더러로 재-plumb. `ZLinkMessageFlowEvent` record로 통일해 Logger 경로가 Tracer가 이미 나르는 struct 방출.
  - 완료: `ZLinkTelemetry.TraceFlowEvent`와 `ZLinkMessageFlowLogger`가 모두 `ZLinkMessageFlowEvent`를 받도록 맞췄다. `ZLinkMessageFlowLogger` 안의 문자열 surface/kind 재해석(`ResolveSurface`/`ResolveKind`)과 별도 `CreateFlow` 경로를 제거했고, channel/route/spot/actor dispatch의 log+report 호출부는 `ZLinkDispatchFlowScope`가 만든 같은 event를 사용한다. report가 없는 entry-spot actor join reject 경로만 event를 직접 만들어 logger에 넘긴다.
  - 재검토: `ZLinkDispatchFlowScope`는 dispatch 결정이나 reply 정책을 소유하지 않고 관측 event와 error report 생성만 소유한다. 따라서 C14 해소 결과가 새 dispatcher adapter가 되지 않는다. surface/kind display name은 metric/log tag 호환을 위해 남겼지만, enum 변환과 flow 필드 구성은 한 곳의 `ZLinkMessageFlowEvent` 생성으로 모였다.
  - 검증: 관련 unit filter `FullyQualifiedName~MessageFlow|FullyQualifiedName~DispatchErrorReporter|FullyQualifiedName~UnhandledDispatch|FullyQualifiedName~EntrySpotActorDispatch|FullyQualifiedName~SpotRoute|FullyQualifiedName~SpotActivation|FullyQualifiedName~Actor` 63 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed, `git diff --check` passed. E2E는 실행하지 않았다.
- [x] **C15. `ChannelFacade`/`SpotFacade`가 Runtime 메서드 표면 중복** (없음)
  - `ZLinkFrameworkChannelFacade.cs`/`ZLinkFrameworkSpotFacade.cs` — 각 메서드가 facade(=`getState()`+manager 전달)와 Runtime partial(`ZLinkFrameworkRuntimeChannels.cs`/`Spots.cs`, facade로 전달) 두 파일에 동일 시그니처. facade 소비처는 Runtime뿐(grep 확인). §D1과 함께 정리.
  - 완료: channel/spot facade 파일을 삭제하고 runtime partial이 `_channels`/`_spots` manager에 `GetOrStartState()`를 직접 넘기게 했다. `ZLinkFrameworkRuntimeComponentFactory`도 더 이상 facade를 만들거나 component record에 싣지 않는다.
  - 재검토: `GetOrStartState()` 바인딩 책임은 원래 runtime 생명주기 책임이므로 runtime partial에 남겼다. 새 helper 없이 facade 파일 자체를 삭제해 pass-through 결과물이 다시 남지 않는다.
  - 검증: 관련 unit filter `FullyQualifiedName~Runtime|FullyQualifiedName~Channel|FullyQualifiedName~Spot|FullyQualifiedName~Host|FullyQualifiedName~Monitoring` 133 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C16. HttpClient 대소문자 무시 헤더 조회 3벌 + 불필요 스캔** (없음)
  - `ZLinkHttpRequestBuilder.FindHeader`(`:320`)/`RequestPerformer.FindRequestHeader`(`:183`)/`ResponseBodyReader.FindHeader`(`:80`) 바이트 동일 선형 스캔. 그중 둘은 이미 `OrdinalIgnoreCase` 딕셔너리를 스캔 → `TryGetValue`로 충분. 헬퍼 1개+중복 스캔 제거. `RequestError` 팩토리(`RequestPerformer.cs:192`/`ResponseBodyReader.cs:107`)도 동일.
- [x] **C17. Stream.Connector 크기 한도·metadata 크기 이중 계산** (벤치, 소)
  - payload-size 한도: `ZlinkStreamFrameSender.cs:145-149`가 `Decompress`(`Compression/...cs:14-18`)가 이미 던지는 `FrameTooLarge`를 재검사. metadata payload size: `ZlinkStreamHeaderCodec.cs:40` 계산 후 `ZlinkStreamFrameSender.cs:162`가 재계산(그것도 헤더 인코드 **후**). 1회 계산·인코드 전 검증.
  - metadata payload limit은 header encoder가 이미 size를 계산하는 위치에서 검증하게 옮기고, sender의 encode 후 재계산을 제거했다. payload decompression 후 size check는 custom compression codec이 `maxDecompressedPayloadSize`를 지키지 않는 경우를 막는 public extension 방어로 확인되어 유지했다.
  - 검증: `dotnet test tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --nologo` 72 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **C18. Backend peer-weight 기본값 "100" 중복** (없음, drift 위험)
  - `ZLinkBackendDealerSocketWrapper.cs:9`가 core 기본을 하드코딩(Router 래퍼는 native read). core가 기본 바꾸면 조용히 drift. 주석 있으나 native read로 통일 검토.

---

## D. 구조 개선 — POSD red flag (pass-through / shallow / temporal, 기회 될 때)

- [x] **D1. Host 이중-파사드 pass-through 붕괴** (없음, 최대 구조 정리)
  - `ZLinkFrameworkRuntime` partial → `*Facade` → manager 2-홉. Channel/Spot 파사드는 `getState()` 바인딩 외 무가치(runtime은 `GetOrStartState` 보유). `ZLinkFrameworkRuntimeChannels.cs:5-18`/`Spots.cs:5-31` + `ZLinkFrameworkChannelFacade.cs`/`SpotFacade.cs` 인라인 1-홉으로. (Actor facade는 `_entrySpotJoin`/`_remoteJoiner` 소유로 정당하나 `:77-250` 평이 delegator는 pass-through.)
  - `ZLinkFrameworkSessionBindings.cs` — 4메서드 전부 `ZLinkSessionActorBindingTable`로 verbatim 전달(interface ≡ impl), 사용처 1필드(`ZLinkFrameworkRuntime.cs:19`). 드롭하고 table 직접 보유하거나 실제 behavior 부여.
    - 진행: channel/spot facade는 C15에서 삭제했다. session binding wrapper도 삭제하고 `ZLinkFrameworkRuntime`이 `ZLinkSessionActorBindingTable`을 직접 보유하게 했다. actor facade의 평이 delegator도 제거해 runtime partial이 `_actorSessionManager`를 직접 호출한다. actor facade에는 `_entrySpotJoin`/`_remoteJoiner`를 조합하는 join 경로만 남겼다.
    - 재검토: runtime partial에 남은 메서드는 runtime 내부 협력 객체가 호출하는 host 경계이며, manager를 각 dispatcher/service로 흩뿌리지 않는다. 따라서 facade 삭제 결과가 새 얕은 모듈이 되지 않는다. actor facade도 단순 전달자가 아니라 entry-spot join과 remote join 선택을 숨기는 깊은 모듈로 남는다.
    - 검증: 관련 unit filter `FullyQualifiedName~Actor|FullyQualifiedName~Session|FullyQualifiedName~Spot|FullyQualifiedName~Runtime|FullyQualifiedName~EntrySpot` 133 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed, `git diff --check` passed. E2E는 실행하지 않았다.
- [x] **D2. Spot outbound/dispatcher shallow pass-through 다층** (없음/벤치)
  - `ZLinkSpotActivationOutbound.cs:1-152` 전체 — ~20메서드가 `_outboundEndpoint`로 1:1, 직접-send 오버로드는 endpoint→transport→nativeSpot 3홉. 직접 `SendToSpot`/`RequestToSpot`/publish는 activation이 transport 직접 보유, endpoint 우회 드롭.
    - 진행: activation→outbound surface→endpoint 3단 위임 중 surface 층을 제거했다. endpoint는 `IZLinkSpotOutbound`를 직접 구현하므로 context의 `Outbound` 표면은 별도 pass-through 객체를 만들지 않는다. activation에 남은 public context 구현은 계약 표면 유지 목적이라 D2의 잔여 항목으로 남긴다.
    - 재검토: 새 helper나 adapter를 만들지 않았고, endpoint가 public outbound 동작과 transport/runtime 연결을 함께 소유한다. `IZLinkCurrentSpotActivation`에는 이미 존재하는 `Outbound` 표면만 남겼기 때문에 이번 변경 자체는 새 shallow wrapper가 되지 않는다.
    - 진행: endpoint→transport→nativeSpot로 이어지던 단일 `Message` raw overload 사슬을 삭제하고, 내부 outbound는 envelope parts 경로 하나로 축소했다.
    - 재검토: parts 경로는 framework envelope 불변식을 숨기는 내부 경계라 유지한다. 단일-part 편의를 다시 helper로 감싸지 않았기 때문에 삭제 결과가 새 얕은 모듈이 되지 않는다.
  - **[벤치] `ZLinkSpotActivationDispatcher.cs:361`** — `reply.ToFrame(streamHeader)`(byte[] 할당, `ZLinkActorReply.cs:62`)를 무조건 계산하나 `isNoBind` 분기의 `ReplyNoBind`가 `reply.ToFrame(requestHeader)`(`:432`)로 재직렬화·`frame` 미사용 → **모든 no-bind reply 이중 직렬화**. `frame` 계산을 `else`(bound-session) 분기로 이동.
    - 완료: no-bind 분기 앞의 `reply.ToFrame(streamHeader)` 호출을 제거하고, bound-session 전송 분기에서만 frame을 만든다. `ReplyNoBind`는 no-bind reply frame을 한 번만 만든다.
    - 검증: 관련 unit filter `FullyQualifiedName~Spot|FullyQualifiedName~Actor|FullyQualifiedName~Session|FullyQualifiedName~NoBind|FullyQualifiedName~Dispatch` 104 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `ZLinkSpotActivationDispatcher.cs:168-198` `DispatchActorPacketAsync`/`ForReplyAsync` — `_actorPacketDispatcher` 전달인데 같은 클래스가 `:352,384`에서 직접 호출 → 얇은 표면 불일치.
    - 완료: `ZLinkSpotActivationDispatcher`의 actor packet pass-through 메서드를 삭제했다. `ZLinkSpotActorDispatchSubmitter`는 serial execution과 payload copy/dispose만 맡고, 실제 dispatch는 `ZLinkSpotActorPacketDispatcher`를 직접 호출한다.
    - 재검토: submitter가 packet dispatch 정책을 흡수하지 않고 mailbox turn/lifetime만 소유하므로 새 dispatcher adapter가 되지 않는다. `ZLinkSpotActivationDispatcher.ActorPackets`는 이미 생성한 책임 객체를 activation 생성부에 넘기는 내부 collaborator 노출이다.
  - `ZLinkSpotRuntimeManager.cs` Notify* **4개**가 `_entrySpotActors` 전달(`:173`/`:184`/`:199`/`:209`); `TryNotifyJoinedSpotActorDisconnectedAsync`(`:217-232`)는 자체 `SpotNodes` 루프라 별개. `:261-266` `GetActivationBySpotRid`=private `GetActivation` alias. `ZLinkSpotServices.cs:5-10,29-35` `ZLinkSpotManagerService` 2메서드가 불필요 `async return await`(형제는 task 직반환).
    - 완료: `ZLinkSpotManagerService`의 request 없는 `CreateAsync`/`GetOrCreateAsync`는 runtime `ValueTask`를 그대로 반환하게 해 불필요한 async state machine을 제거했다. `GetActivationBySpotRid`는 private alias를 없애고 직접 activation lookup을 수행한다.
    - 완료: `ZLinkSpotRuntimeManager`의 entry-spot actor 전달 메서드를 삭제하고 `EntrySpotActors` 책임 객체를 직접 노출했다. runtime partial과 actor entry-spot join coordinator는 `ZLinkEntrySpotActorRouter`를 직접 호출한다. `TryNotifyJoinedSpotActorDisconnectedAsync`는 created actor fast-path와 spot-node scan을 직접 수행하므로 manager에 남겼다.
    - 재검토: `ZLinkEntrySpotActorRouter`는 entry-spot actor dispatch, reply dispatch, lifecycle notification, disconnected notification을 실제로 소유한다. manager에 동일 시그니처 wrapper를 남기는 대신 책임 객체를 드러냈으므로 리팩토링 결과가 또 다른 pass-through 메서드 묶음이 되지 않는다.
    - 검증: 관련 unit filter `FullyQualifiedName~Spot|FullyQualifiedName~Actor|FullyQualifiedName~Activation|FullyQualifiedName~Manager|FullyQualifiedName~Create` 83 passed. 추가 관련 unit filter `FullyQualifiedName~Spot|FullyQualifiedName~Outbound|FullyQualifiedName~Channel|FullyQualifiedName~Request|FullyQualifiedName~EntrySpot|FullyQualifiedName~Actor` 111 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D3. Handler/Coordinator shallow·temporal** (없음)
  - `ZLinkHandlerDispatcher.RebindContext`(`:118-141`) — kind별로 **동일 필드값** 새 context 생성(재-scope 없음, 정보-무 복사) + `switch`가 다른 subtype에 `"Unknown handler context type"` throw(잠재 함정). immutable 원본 통과 또는 새 인스턴스 필요 이유 문서화.
  - `ZLinkSpotActorLifecycleCoordinator` — 죽은 `LeaveAsync` 제거 후 `JoinAsync`만 남음. leave가 이미 사는 `ZLinkSpotActivationActors`로 접어 join/leave를 한 모듈에(temporal decomposition 해소).
  - `ZLinkRemoteActorJoinPackets.cs:41-49` `GetJoinRequestActorId`/`ActorType` — record 프로퍼티 그대로 반환(얕은 간접). caller 인라인. `ZLinkSessionContext.cs:36` internal `BoundActors` = public `IZLinkSessionActors.Bound`(`:193`) 중복 노출.
  - 완료: handler pipeline은 read-only context를 재복사하지 않고 원본 `ZLinkHandlerContext`를 그대로 사용한다. `ZLinkSpotActorLifecycleCoordinator`는 제거하고 join commit 로직을 `ZLinkSpotActivationActors`에 접어 join/leave lifecycle 책임을 같은 partial에 둔다. remote actor join request의 actor id/type은 record property를 직접 읽고, session context의 internal `BoundActors` 중복 표면은 제거했다.
  - 검증: 관련 unit filter `FullyQualifiedName~Handler|FullyQualifiedName~Actor|FullyQualifiedName~Session|FullyQualifiedName~Spot|FullyQualifiedName~Filter` 120 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D4. Channels 잔여 shallow/무의미 간접** (없음/code-motion)
  - `ZLinkChannelMessagePump.cs` — 2메서드가 `ZLinkChannelReceiveLoop`로 1:1(실 작업은 ctor의 dispatcher 빌드뿐). `ReceiveLoop`로 접기.
    - 완료: `ZLinkChannelMessagePump`를 제거하고 `ZLinkFrameworkRuntimeComponentFactory`가 `ZLinkChannelPacketDispatcher`/`ZLinkChannelReceiveLoop`를 직접 구성해 `ZLinkChannelRuntimeManager`에 주입한다. manager는 receive loop를 직접 실행한다.
    - 검증: 관련 unit filter `FullyQualifiedName~Channel|FullyQualifiedName~Dispatch|FullyQualifiedName~Runtime|FullyQualifiedName~MessageFlow|FullyQualifiedName~Route` 124 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `ZLinkRoutePacketDispatcher.cs:141-146,228-233` `DispatchInternalRequestAsync`가 받는 `Func<RoutingId,…>`를 일부 무시(단일 caller가 computed `RoutingId` 버리고 `received` 재-close). 인라인.
    - 완료: internal routed request dispatch는 callback을 받지 않고 `internalPackets.DispatchRequestAsync`를 직접 호출한다. computed `sourceRid`는 reply/error/flow reporting에만 쓰이고, dispatch 입력으로 전달하는 무의미한 간접은 제거했다.
    - 검증: 관련 unit filter `FullyQualifiedName~Route|FullyQualifiedName~Channel|FullyQualifiedName~Request|FullyQualifiedName~InternalRoute|FullyQualifiedName~Dispatch` 76 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `ZLinkRouteChannelRuntime.cs:183-191` `MarkAutoConnectManaged` — behavior 주장 doc 주석 있는 빈 메서드(호출: `ZLinkLocationAutoConnectHost.cs:57`). vestigial: 호출 제거 또는 no-op 명시. `:215-271` request/send 전달자 중 `:241-271`은 `async/await …ConfigureAwait(false)`가 plain return 위 state machine — ValueTask 직반환.
    - 완료: `ZLinkRouteChannelRuntime.RequestAsync`/`RequestPartsAsync`는 `_calls`가 반환한 `ValueTask`를 그대로 반환하게 해 추가 state machine을 만들지 않는다. 상태를 바꾸지 않는 빈 `MarkAutoConnectManaged` hook과 호출은 제거했다.
    - 검증: 관련 unit filter `FullyQualifiedName~Route|FullyQualifiedName~Channel|FullyQualifiedName~Request|FullyQualifiedName~AutoConnect` 92 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `ZLinkRouteConnectionSet.cs:8-11` `Connect(string)`이 rid 오버로드(`:20-33`)가 잡는 `_connectGate` 미보유 → 같은 소켓에 동시성 계약 불일치. reconcile 루프와 경합 불가 여부 확인.
    - 완료: `Connect(string)`도 `_connectGate` 안에서 router connect를 호출하게 해 rid-aware connect와 같은 socket 접근 계약을 공유한다.
    - 검증: 관련 unit filter `FullyQualifiedName~Route|FullyQualifiedName~Channel|FullyQualifiedName~Request|FullyQualifiedName~AutoConnect` 92 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D5. Config/Location 얕은 파사드·잠재 불변식** (없음)
  - `ZLinkFrameworkOptionsBuilder.cs:188-274` `ZLinkSpotMeshBuilder` — 약 15개 `DefaultNode().Xxx(...)` 전달 메서드, `IZLinkSpotMeshBuilder : IZLinkSpotNodeBuilder`(`Contracts/Configuration/Builders.cs:157`)가 무추가. lazy-default-node 외 얕은 래퍼(로직 없음 인지).
    - 완료: `ZLinkSpotMeshBuilder` wrapper를 삭제하고 `ZLinkSpotNodeBuilder`가 `IZLinkSpotMeshBuilder`도 직접 구현하게 했다. `AddSpotMesh(...)`는 public 반환 타입을 유지하면서 실제 동작을 가진 node builder를 바로 반환한다.
    - 재검토: `IZLinkSpotMeshBuilder` public 타입 이름은 계약 문서와 예제의 의미 구분을 위해 남기지만, 내부 구현은 별도 전달 객체를 만들지 않는다. node builder가 이미 mesh node의 모든 설정 책임을 소유하므로 새 abstraction이나 pass-through 결과물이 남지 않는다.
    - 검증: 관련 contract filter `FullyQualifiedName~Builder|FullyQualifiedName~Configuration` 7 passed, 관련 unit filter `FullyQualifiedName~Registration|FullyQualifiedName~NodesAndServices|FullyQualifiedName~Channels|FullyQualifiedName~Location|FullyQualifiedName~AutoConnect` 140 passed.
  - optional `observed`가 `?? new ZLinkObservedLocationGenerations()` 기본 → caller 생략 시 공유 monotonic guard 대신 private guard(교차-표면 불변식 무력화). 해당 사이트는 `ZLinkStoreLocationResolvers.cs:41`·`ZLinkLocationRuntimeQueryService.cs:41` 2곳뿐 → 이 둘만 non-optional로. (`events`는 `?? ZLinkLocationEventEmitter.Disabled`로 양성 기본이라 무관, `ZLinkAutoConnectReconciler`에는 `observed` 파라미터 자체가 없음 — 원 리뷰 과확대 정정.)
    - 완료: `ZLinkStoreLocationResolvers`와 `ZLinkLocationRuntimeQueryService`는 `ZLinkObservedLocationGenerations`를 필수 생성자 인자로 받는다. 내부 `observed ?? new ...` 기본값을 제거했고, DI/test fixture가 runtime 단위 observed guard를 명시적으로 넘기게 했다.
    - 검증: 관련 unit filter `FullyQualifiedName~Location|FullyQualifiedName~AutoConnect` 102 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D6. Diagnostics/Host 벤치 hot 항목** (벤치, opt-in)
  - `ZLinkFrameworkRuntime.cs:160-167` `RunSpotRouteBridgeDrainLoopAsync` — bridge 유무·트래픽 무관 런타임 생애 내내 `Task.Delay(10ms)`+`DrainSpotRouteBridges()` 고정율 wakeup. event-driven(enqueue 시 signal) 가능하면 상시 timer 제거.
    - 진행: bridge가 없는 런타임에서 먼저 `spot-route-bridge-drain` listener task 생성을 중단했고, 이어 bridge가 있는 런타임에서도 전용 safety loop를 제거했다.
    - 재검토: 새 readiness callback이나 public option을 추가하지 않았다. route socket `OnSendReady`는 submitter가 이미 소유하고 있고 backend bridge에는 독립 readiness 신호가 없으므로, bridge flush는 기존 route receive pump와 send/request/spot route dispatch 후 명시 drain 경로에 둔다.
    - 완료: `spot-route-bridge-drain` 전용 listener와 `RunSpotRouteBridgeDrainLoopAsync`를 삭제했다. bridge가 붙은 route channel은 attach 뒤 항상 `ZLinkRouteReceivePump`를 시작하고, 그 pump가 receive/no-data 경로에서 bridge를 drain한다. `TrySendViaSpotRouteBridge`/`TryRequestViaSpotRouteBridge`와 spot route dispatch 후 명시 drain도 유지해 별도 10ms safety loop 없이 기존 route/spot 진행 경로에서 flush한다.
    - 재검토: backend bridge에 없는 readiness callback을 새 public/internal 계약으로 억지 추가하지 않았다. 기존 route receive pump의 책임 안에서 drain 기회를 사용하므로 새 shallow task wrapper나 sleep 기반 workaround가 생기지 않는다.
    - 검증: 관련 unit filter `FullyQualifiedName~CoverageCriticalRuntime|FullyQualifiedName~LocationRuntime|FullyQualifiedName~EntrySpotActorDispatch|FullyQualifiedName~SpotRoute|FullyQualifiedName~Route|FullyQualifiedName~Runtime|FullyQualifiedName~Spot` 128 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `ZLinkMessageFlowTracer.cs:67` — observer 구성 시 traced 이벤트마다 fire-and-forget `Task.Run`(모드 게이트로 off는 무비용). 고샘플+observer 시 per-message closure+task. bounded channel/queue로.
    - 완료: observer가 설정된 tracer는 첫 flow event에서 `ZLinkMessageFlowObserverDispatcher`를 한 번 만들고, 이후 event는 bounded channel에 넣는다. observer 호출은 단일 drain worker에서 순서대로 처리하므로 per-message `Task.Run` 생성이 사라진다.
    - 재검토: queue와 worker는 tracer 내부 구현 세부이며 public option이나 호출자 책임을 늘리지 않는다. observer resolve 정책은 dispatcher 내부에 남겨 호출자가 스케줄링, backpressure, DI resolve 순서를 알 필요가 없다. 따라서 hot-path 리팩토링 결과가 새 public 설정이나 얕은 adapter가 되지 않는다.
    - 검증: 관련 unit filter `FullyQualifiedName~MessageFlow|FullyQualifiedName~DispatchErrorReporter|FullyQualifiedName~UnhandledDispatch|FullyQualifiedName~EntrySpotActorDispatch` 18 passed. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D7. Stream.Connector shallow·hot** (없음/벤치)
  - **[벤치]** `ZlinkStreamReceiveDispatcher.cs:70-72` — `ZlinkStreamMessage<ZlinkStreamEncodedPayload>`(Record용) + 비제너릭 `ZlinkStreamMessage`(`Contracts/ZlinkStreamModels.cs:8`) 동시 생성 후 `ZlinkStreamTypedHandlerRegistry.Add` 래퍼(`:12-18`)가 `object? payload` 캐스팅으로 **세 번째** 제너릭 재구성. registry는 encoded-payload 핸들러만 → 타입-소거 `object?`는 특수/범용 혼합. `Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>,…>` 직저장.
    - 완료: `ZlinkStreamTypedHandlerRegistry.TypedHandler`가 `Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, ...>`를 그대로 저장한다. receive dispatcher는 record용 typed message 하나를 만들고, 같은 인스턴스를 received-message buffer와 typed handler dispatch에 함께 사용한다. 비제너릭 `ZlinkStreamMessage` 재구성과 `object? payload` 캐스팅은 제거했다.
    - 재검토: registry가 encoded-payload handler라는 실제 책임을 타입으로 직접 표현하므로 새 adapter 계층이 생기지 않았다. handler dispatch에서 메시지 재구성이 사라져 리팩토링 결과가 다시 shallow wrapper가 되지 않는다.
    - 검증: 관련 Stream.Connector unit filter `FullyQualifiedName~TypedHandler|FullyQualifiedName~Dispatch|FullyQualifiedName~Compression|FullyQualifiedName~Send|FullyQualifiedName~Connector` 72 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - `Runtime/Protocol/ZlinkStreamDefaultCodecFactory.cs`(3메서드 `new X()`), `ZlinkStreamInboundObserverDispatcher.cs:28`(observer 없어도 ctor에서 `DrainAsync` 상시 spawn → lazy on first `Add`), `ZlinkStreamConnector.cs:377`(`None => null` 도달 불가 arm, `:371`에서 이미 반환).
    - 완료: `ZlinkStreamDefaultCodecFactory`는 삭제했다. production은 기본 `ZlinkStreamPacketNameResolver`와 `ZlinkStreamHeaderCodec`를 직접 생성하고, 테스트도 실제 header/LZ4 codec 타입을 직접 사용한다.
    - 재검토: factory 삭제 뒤 같은 목적의 새 factory/helper를 만들지 않았다. 테스트의 직접 codec 생성은 테스트 fixture 구성이고, production 기본값은 각각의 소유 위치(`ZlinkStreamConnectorOptions`, `ZlinkStreamConnector`)에 남아 얕은 중간 모듈을 만들지 않는다.
    - 검증: `dotnet test tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --nologo` 72 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
    - 완료: `ZlinkStreamInboundObserverDispatcher`는 생성자에서 drain task를 시작하지 않고, 첫 `Add`에서 한 번만 시작한다. observer가 없는 connector는 inbound observer background task를 만들지 않는다.
    - 재검토: `_drainStarted`는 task 생성을 한 번으로 제한하는 내부 상태이며 별도 public 설정이나 helper를 추가하지 않는다. observer dispatch 책임은 여전히 dispatcher 내부에 있고, task runner 사용 지식이 호출자 쪽으로 새지 않는다.
    - 검증: 관련 Stream.Connector unit filter `FullyQualifiedName~InboundObserver|FullyQualifiedName~Connector|FullyQualifiedName~Dispatch` 70 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
    - 완료: `ZlinkStreamConnector.CreateCompressionCodec`의 `ZlinkStreamCompression.None => null` arm은 앞선 `None` 분기에서 이미 반환되므로 제거했다.
    - 재검토: unreachable arm 삭제만 수행했고, compression codec 생성 경로에는 새 분기나 helper를 만들지 않았다. 같은 D7 묶음의 default codec factory와 inbound observer lazy start 항목은 위 변경으로 함께 해소했다.
    - 검증: 관련 Stream.Connector unit filter `FullyQualifiedName~TypedHandler|FullyQualifiedName~Dispatch|FullyQualifiedName~Compression|FullyQualifiedName~Send|FullyQualifiedName~Connector` 72 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D8. AspNetCore / HttpClient / Protobuf 잔여** (없음/벤치)
  - AspNetCore: `ZLinkFrameworkServiceRegistrar.cs:224-232` `HasSpotNode`≡`HasSpotPublisherClient`(둘 다 `SpotNodes.Count>0`) — 발산 의도면 인코드, 아니면 단일 술어. `ZLinkRuntimeEventDispatcher.cs:7-13` `PublishAsync`→`DispatchAsync` pass-through. `ZLinkMonitoringPollingTasks.cs`(`List<Task>` 얇은 래퍼).
    - 완료: `HasSpotPublisherClient`를 제거하고 같은 조건은 `HasSpotNode`로 직접 판정한다. `ZLinkRuntimeEventDispatcher`는 공개 계약인 `PublishAsync`에 실제 dispatch를 담고, monitoring hosted service도 `IZLinkRuntimeEventPublisher`에 의존하게 했다. `ZLinkMonitoringPollingTasks` 얇은 래퍼는 삭제하고 polling runner의 로컬 `List<Task>`로 접었다.
    - 재검토: 새 helper나 public API를 추가하지 않았다. `PublishAsync`가 실제 작업을 수행하므로 pass-through는 사라졌고, polling task 컬렉션은 한 메서드의 지역 상태로만 남아 새 POSD 대상이 되지 않는다.
    - 검증: 관련 unit filter `FullyQualifiedName~Monitoring|FullyQualifiedName~Registration|FullyQualifiedName~Event` 22 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - HttpClient: `Runtime/HttpClientCodecRegistry.cs:44-48` `AddStreamCodec` 빈 no-op(HTTP client가 못 쓰는 stream 개념 강제 구현·조용히 swallow = ISP 누출). verb 단축 7개가 `ZLinkHttpClient.cs:39-72`/`ZLinkHttpClientBuilder.cs:182-215` 중복(의도적·cpp 미러, 우선순위 낮음). `Runtime/StreamingBodyContent.cs`가 `ProviderReadStream` 정의(파일명≠타입), `HttpTransportFactory.cs:58` pinned `X509Certificate2` 미-dispose.
    - 완료: `StreamingBodyContent.cs`는 실제 타입명에 맞춰 `ProviderReadStream.cs`로 옮겼다. `HttpTransportFactory`는 `HttpTransport`를 만들고, `HttpClientRuntime`이 그 transport를 소유하게 했다. TLS callback과 mTLS 컬렉션이 handler 생애 동안 인증서를 참조하므로, 인증서는 handler dispose 뒤에 같은 transport에서 함께 dispose한다.
    - 완료: `HttpClientCodecRegistry.AddStreamCodec`는 더 이상 빈 no-op이 아니다. built-in codec extension처럼 같은 content type의 HTTP payload serializer를 먼저 등록한 경우만 허용하고, stream codec만 단독 등록하는 extension은 `ZLinkConfigurationException`으로 실패시켜 설정 오류가 조용히 숨지 않게 했다.
    - 재검토: HTTP client는 stream payload를 직접 송수신하지 않으므로 stream codec mapping을 새 public 설정이나 런타임 경로로 끌어오지 않았다. 같은 registry가 serializer 존재 여부를 검증하므로 새 adapter 계층도 만들지 않는다.
    - 재검토: verb shortcut은 `ZLinkHttpClient`와 `ZLinkHttpClientBuilder`의 서로 다른 public receiver에 남긴다. C++ 계약도 `client_t`와 `client_builder_t` 양쪽에 `get`/`post` 계열을 제공하고, spec은 `client_t::create(url).post(...)`처럼 `build()` 생략 shortcut을 public 산출물로 적는다. .NET builder shortcut은 request builder가 client factory를 들고 terminal operation에서 client를 만들도록 연결되어 있어 built client shortcut과 생애가 다르다. 공통 helper를 추가해도 두 public method는 그대로 남고 helper는 constructor 호출만 감싸는 pass-through가 되므로 새 POSD 대상이 된다.
    - 검증: 관련 HttpClient unit filter `FullyQualifiedName~Codec|FullyQualifiedName~Protobuf|FullyQualifiedName~MessagePack|FullyQualifiedName~Validation` 5 passed.
    - 검증: 관련 HttpClient unit filter `FullyQualifiedName~ProviderReadStream|FullyQualifiedName~Trusts_test_certificate|FullyQualifiedName~Untrusted_https_certificate|FullyQualifiedName~Presents_client_certificate` 6 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
  - Protobuf: `ZLinkProtobufCodec.cs:48` stream-codec `Decode`가 `Activator.CreateInstance`, 형제 `Deserialize`(`:105-118`)는 compiled-factory 캐시(`:102-104` 주석) — 동일 지식 발산 성능. 캐시 재사용(벤치, 소).
    - 완료: stream payload `Decode<TPayload>`와 message serializer `Deserialize`가 같은 `CreateMessage(Type)` 경로를 사용한다. compiled factory cache를 `ZLinkProtobufCodec` 단위로 올려 두 decode 경로가 같은 생성 계약과 cache를 공유하게 했다.
    - 재검토: helper는 생성 계약을 한 곳에 모으는 내부 구현 세부이며 public API나 호출자 설정을 늘리지 않는다. 기존 serializer 내부 cache를 바깥 공통 cache로 옮긴 것이므로 새 병렬 추상화는 만들지 않았다.
    - 검증: Protobuf 관련 unit filters passed(`Zlink.Framework.UnitTests` 22, `Zlink.HttpClient.UnitTests` 1, `Systems.Zlink.Stream.Connector.Tests` 1), `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.
- [x] **D9. Streams 특수/범용 혼합(낮은 우선)** (없음)
  - `Runtime/Streams/ZLinkStreamProtocolDefaults.cs:23-96` — static `Lz4Compress`/`Lz4Decompress` + 그걸 재-wrap하는 nested `Lz4CompressionCodec` + 제너릭 `Compress/Decompress(codec,…)`. 특수(Lz4)와 범용(codec) 압축 관심사가 한 유틸에 얽힘.
  - 완료: LZ4 구현은 `ZLinkLz4StreamCompressionCodec`로 분리하고, `ZLinkStreamProtocolDefaults`는 기본 LZ4 codec 생성과 범용 compression guard만 맡게 했다. nested LZ4 codec과 defaults 안의 LZ4 static 구현은 제거했다.
  - 재검토: 새 타입은 `IZlinkStreamCompressionCodec` 구현체 자체이며 단순 wrapper가 아니다. LZ4 알고리즘 선택과 payload 크기 guard는 codec 내부에 모이고, defaults는 호출자 설정 표면을 늘리지 않는다.
  - 검증: 관련 unit filter `FullyQualifiedName~StreamProtocol|FullyQualifiedName~Compression|FullyQualifiedName~StreamPayload|FullyQualifiedName~SpotHandler` 10 passed, `dotnet test Zlink.Framework.sln --nologo` passed, sample build loop passed. E2E는 실행하지 않았다.

---

## E. 아키텍처 책임 분리 (DDD — codex 리뷰 반영)

> A~D가 입자 단위 red-flag라면, E는 **도메인 책임 경계** 수준의 큰 재설계다. 공개 API는 추가하지 않고 내부
> 타입만 늘린다. 각 항목은 별도 작업 단위/브랜치로 진행하며 완료 조건에 테스트 국소화를 둔다.

- [x] **E1. `ZLinkLocationLifecycle` 3개 책임 분리** (없음, 대형 재설계)
  - 한 클래스가 actor 위치 claim + activation 실패 rollback(`ZLinkLocationLifecycle.cs:73`), spot 위치 claim(`:290`), actor-session route 등록/해제(`:349`), 소유권 상실 callback(`:418`), takeover scope를 전부 처리. takeover 정책이 `AsyncLocal` scope로 숨겨져 caller(`ZLinkActorEntrySpotRouteInternalPacketDispatcher.cs:37`, `ZLinkFrameworkRuntimeActors.cs:99`)가 직접 scope를 열어야 함 → "remote handoff는 takeover claim을 쓴다"는 정책 지식이 lifecycle 밖으로 누출.
  - **POSD**: 소유권 write 정책이라는 한 결정이 actor 생성·routed join dispatcher·runtime actor facade에 분산(정보 은닉·변경 증폭). **DDD**: actor 생명주기 / spot 소유권 / actor-session route는 서로 다른 런타임 도메인 개념인데 하나의 "location lifecycle" 이름 아래 묶임.
  - 진행: remote actor handoff caller가 `ZLinkLocationLifecycle.EnterActorTakeoverScope()`로 숨은 `AsyncLocal` scope를 직접 여는 구조를 제거했다. `CreateLocalActorForHandoffAsync`가 handoff 의도를 actor creation 경계로 전달하고, `ZLinkLocationLifecycle`은 `ZLinkActorClaimMode.TakeoverExistingOwner`를 받은 경우에만 conflict 이후 takeover write를 시도한다.
  - 진행: actor-session route row 책임을 `ZLinkActorSessionRouteLifecycle`로 분리했다. route generation 추적, bind/rebind takeover, remove, route ownership-loss 정리는 새 타입이 소유하고 runtime/session 경로는 `LocationLifecycle.ActorSessionRoutes`를 통해 그 책임 객체를 직접 사용한다.
  - 진행: spot location claim/release 책임을 `ZLinkSpotLocationLifecycle`로 분리했다. spot generation 추적, same-node restart takeover, release, spot ownership-loss deactivation 선택은 새 타입이 소유한다.
  - 완료: actor claim/takeover handoff/ref 공개/상실 시 deactivation 책임을 `ZLinkActorOwnershipCoordinator`로 분리했다. `IZLinkActorLocationLifecycle` DI 등록도 `ZLinkLocationLifecycle` container가 아니라 `ActorOwnership` 객체를 반환한다.
  - 재검토: takeover 여부를 ambient 전역 상태로 숨기지 않고 actor creation 호출의 내부 모드로 명시했다. public API는 늘리지 않았고, caller가 location lifecycle의 scope 구현을 알 필요가 없어졌다. actor/route/spot 메서드를 `ZLinkLocationLifecycle`에 pass-through로 남기지 않고 책임 객체를 직접 노출해 새 얕은 wrapper를 만들지 않았다. `ZLinkLocationLifecycle`는 runtime ownership-loss 이벤트 구독과 row-kind별 책임 객체 라우팅만 맡는다.
  - 검증: 관련 unit filter `FullyQualifiedName~LocationLifecycle|FullyQualifiedName~Actor|FullyQualifiedName~EntrySpot|FullyQualifiedName~Session` 67 passed. route/spot 분리 포함 filter `FullyQualifiedName~LocationLifecycle|FullyQualifiedName~Actor|FullyQualifiedName~EntrySpot|FullyQualifiedName~Session|FullyQualifiedName~Spot` 93 passed. actor ownership 분리 포함 filter `FullyQualifiedName~LocationLifecycle|FullyQualifiedName~Actor|FullyQualifiedName~EntrySpot|FullyQualifiedName~Session|FullyQualifiedName~Spot|FullyQualifiedName~NodesAndServices` 99 passed. 후속 관련 E2E 게이트는 아래 검증 게이트 실행 기록에서 `SpotService`/`YieldDispatch` 통과를 확인했다.
  - 관련 파일: `ZLinkActorCreationCoordinator.cs`, `ZLinkActorEntrySpotRouteInternalPacketDispatcher.cs`, `ZLinkFrameworkRuntimeActors.cs:383+`(session binding이 actor state·전역 static index·route write를 한 번에).
- [x] **E2. `ZLinkRedisLocationStore` 내부 책임 분해** (없음, 대형·외부 Redis 필요)
  - 공개 store 구현이 connection lifecycle + Lua 인자 구성 + key schema + row filtering/paging + Redis row→계약 객체 복원을 전부 보유. write 조립(`ZLinkRedisLocationStore.cs:332`), remove(`:369`), paging(`:403`), row decode(`:472`), key schema(`:520+`). `Kind<TRow>`가 row별 정책 일부만 숨기고 Lua 인자 순서/key schema는 본체에 잔존.
  - **POSD**: store 인터페이스는 충분히 깊으나 내부에서 "location 계약"과 "Redis schema/script protocol" 결정이 한 파일에 혼재 → Lua 인자 순서 변경 시 store 대부분 재독. **DDD**: row 계약과 Redis 저장 방식은 다른 계층.
  - 방향: `ZLinkRedisLocationKeys`(prefix/row hash/generation/lease/owner index/stamp key), `ZLinkRedisLocationCommandExecutor`(`ScriptEvaluateAsync`+인자 순서)로 이동. store엔 공개 메서드·connection 획득·row 필터만.
  - 완료 조건: store에서 Redis key 문자열 조합 소멸 + Lua 인자 순서 변경 시 executor/script test만 보면 됨 + Redis↔in-memory 동등성 테스트 그대로 통과. (내 리뷰 A-ST2 죽은 `RemoveByOwner` Lua도 이때 제거.)
  - 완료: Redis key schema를 `ZLinkRedisLocationKeys`로 분리했다. `ZLinkRedisLocationStore`에는 `row:`/`gen:`/`keys:`/`own:`/`lease:`/`stamp:` 문자열 조합이 남지 않고, store는 key 객체가 제공하는 schema method만 사용한다.
  - 완료: Lua script 실행과 `KEYS`/`ARGV` 순서를 `ZLinkRedisLocationCommands`로 분리했다. write/remove/owner lease/remove-all/list lease/change stamp의 `ScriptEvaluateAsync` 호출은 command 객체가 소유하고, store는 public store operation과 connection 획득만 맡는다.
  - 완료: Redis row hash field 목록과 row materialize/load를 `ZLinkRedisLocationRows`로 분리했다. store-issued generation/UpdatedAt 적용 규칙은 row reader가 소유하고, row kind별 key/owner/generation/finalize 계약은 `ZLinkRedisLocationKinds` catalog에 모았다.
  - 재검토: 새 타입들은 단순 전달자가 아니라 각각 Redis schema, script protocol, row materialization이라는 변경 축을 소유한다. `ZLinkRedisLocationStore`가 이들을 다시 같은 시그니처로 감싸지 않고 public contract 메서드, connection lifecycle, filter/paging만 남겼으므로 분해 결과가 또 다른 POSD 리팩토링 대상이 되지 않는다.
  - 검증: `dotnet test framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj --nologo` 26 passed, 6 skipped. `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` passed, sample build loop passed. 후속 관련 E2E 게이트는 아래 검증 게이트 실행 기록에서 `SpotService`/`YieldDispatch` 통과를 확인했다.
- [x] **E3. SPOT route router request 처리 공통화** (없음, 테스트 범위 좁음)
  - `ZLinkSpotRouteRouterDispatcher.cs`의 `SpotNodeRouterTarget.RequestAsync`와 `RouteChannelTarget.RequestAsync`가 request 완료 처리·timeout token source·cancellation 등록·reply→TCS 예외 매핑을 대상마다 반복한다. A-CH1에서 서버 bundle bridge 경로는 제거되어 공통화 대상에서 빠졌다.
  - **POSD**: 대상별 분리는 좋으나 request 완료 정책이 흩어져 timeout/cancellation 의미가 대상 구현 세부로 누출. **DDD**: route 대상은 전송 수단, request 생명주기는 dispatcher 공통 정책.
  - 방향: 내부 `ZLinkSpotRouteRequestCompletion` 보조 타입에 timeout/cancellation registration·callback completion·reply dispose 규칙을 집약. 대상별 코드는 native `Request`/route bridge `TryRequest` 시작 + 대상별 error mapping(`NotFound`=stale spot address는 `RouteChannelTarget`에 result mapper로)만.
  - 완료 조건: 두 경로에서 TCS·cancellation/timeout 중복 소멸 + `RequestTargetNotFound`/`SpotRouteNotFound`/timeout 의미 불변.
  - 관련: `ZLinkFrameworkRuntimeChannels.cs`, `ZLinkRouteChannelRuntime.cs`.
  - 완료: `ZLinkSpotRouteRequestCompletion`이 request TCS, timeout source, cancellation registration, raw reply completion 연결을 소유한다. `SpotNodeRouterTarget`와 `RouteChannelTarget`는 request 시작과 대상별 실패 의미만 남겼고, route bridge 미구성 실패에서도 request parts를 `finally`로 해제한다.
  - 재검토: helper는 route request 생명주기 정책만 담고 전송 대상 선택이나 `NotFound` 의미 변환을 가져오지 않는다. 따라서 target별 도메인 판단을 숨기는 새 범용 모듈이 아니라, 중복된 completion lifecycle을 한 곳에 모은 내부 모듈이다.
  - 검증: 관련 unit filter `FullyQualifiedName~Route|FullyQualifiedName~Spot|FullyQualifiedName~Request|FullyQualifiedName~LocationLifecycle` 98 passed.

---

## 정리 목록 — 추적되지 않는 빌드 산출물 (codex 리뷰 반영)

- [x] **CLEAN1. `src/**/bin`·`src/**/obj` 산출물 정리** (없음, 소스 변경 아님)
  - `git ls-files 'framework/languages/dotnet/src/**/obj/**' '…/bin/**'` = 추적 0건이나 작업트리에 ~415개 산출물 잔존(7 src 프로젝트 전부). 삭제해도 소스 변경 아님. 삭제 전 실행 중 IDE/빌드 프로세스 없는지 확인, 이후 `git status --short framework/languages/dotnet/src`로 소스 변경과 산출물 분리.
  - **보류 근거(codex)**: 파일명 기반 무참조로 보이는 상당수는 공개 계약 묶음·`GlobalUsings`·`AssemblyInfo`·빌드 자동 판독 파일이라 삭제 대상 아님. 소스 `.cs` 삭제는 §A(grep+InternalsVisibleTo 검증분)에 한정.
  - 완료: `git ls-files 'framework/languages/dotnet/src/**/bin/**' 'framework/languages/dotnet/src/**/obj/**'` 추적 파일 0건을 재확인했다. VS Code C# DevKit/MSBuild 노드가 일부 ignored 산출물을 즉시 재생성할 수 있어 `dotnet build-server shutdown --msbuild --vbcscompiler` 후 `framework/languages/dotnet/src` 아래 `bin`/`obj` 디렉터리만 제거했다. 소스 `.cs` 파일은 이 항목에서 삭제하지 않았다.

---

## 진행 중 변경 확인 (커밋 전 확인, codex 리뷰 반영)

작업트리에 이미 아래 .NET framework 변경이 있으니 위 작업과 충돌·중복 없는지 확인한다.

- `ZLinkClientCallCodec`가 envelope error code로 `OperationCanceledException`/`TaskCanceledException` 복원.
- `ZLinkStreamSessionRuntime`가 dispatch 중 `OperationCanceledException` 수신 시 session close.
- `EnvelopeCodecTests`에 cancellation error 복원 테스트 추가.

→ cancellation error가 request caller와 stream session lifecycle 양쪽에서 같은 의미인지 unit+E2E로 확인.

---

## 권장 작업 순서

1. **A 트랙(삭제) 먼저** — 특히 A-CH1(서버번들 bridge)·A-SP1(peer disconnect 사슬)·A-SP3(구독 지표)은 대형 vestigial. 빌드+테스트로 충분(§A-SP3만 per-publish라 벤치). 표면 축소가 이후 B/C/D 분석을 가볍게 함.
2. **B 트랙(결함)** — B1(빈 metadata round-trip)·B2(이중 압축)·B4(프레임 헤더 배치 중단+누수)·B5(replyParts 누수)는 실제 버그. B2·B4는 hot 경로라 벤치.
3. **C1(stream wire 중복)** — 최대 유지보수 impact, 그러나 packaging 제약이 있으니 "공유 상수 + 단일 spec test 게이트"부터 착수.
4. **C2/C3/C4(dispatch 파이프라인 통합)** — per-message hot, 벤치 게이트 필수. 가장 큰 지식 중복 소거.
5. **D1(Host 이중 파사드)** — 구조 정리 최대건, control-plane이라 위험 없음.
6. **CLEAN1(bin/obj 산출물)** — 아무 때나, 소스 변경과 분리해 먼저 처리하면 이후 `git status`가 깨끗.
7. **E 트랙(아키텍처 책임 분리)** — 별도 브랜치/작업 단위. E1(LocationLifecycle)을 먼저: actor 소유권·session route가 최근 E2E 변경과 맞물려 책임 경계 정리가 이후 디버깅 비용을 줄인다. E3(router request)은 작고 테스트 범위 좁음(단 §A-CH1 먼저 결정). E2(Redis)는 외부 Redis 환경 필요하니 독립 브랜치.
8. 나머지 C/D는 기회 될 때. hot 표기(벤치) 항목은 baseline vs patched 실측 후에만 커밋.

## 검증 게이트

```bash
dotnet build framework/languages/dotnet/src/Zlink.Framework/Zlink.Framework.csproj --no-restore
dotnet test  framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-restore
dotnet test  framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore
```

위성/E2E 관련 변경 시 추가:

```bash
dotnet test framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --no-restore
dotnet test framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj --no-restore
timeout 420s framework/languages/dotnet/e2e/SpotService/run_e2e.sh
timeout 420s framework/languages/dotnet/e2e/YieldDispatch/run_e2e.sh
```

---

## F. 2026-07-10 재리뷰 — A~E 구현 완료 후 fresh pass

> 아래 상세 설명은 A~E 완료 직후 발견한 문제의 당시 상태를 기록한다. 2026-07-10 후속 작업에서
> F-BUG, F-DEAD, F-DUP, F-POSD를 모두 처리했고 체크박스를 닫았다. 파일:라인은 발견 시점 기준이며,
> 현재 상태와 검증 근거는 이 절 끝의 `F 완료 결과`와 `후속 검증 기록`을 기준으로 판단한다.

### F-BUG. 결함 (correctness)

- [x] **F-BUG1. ⭐core `ZLinkStreamMetadataCodec`가 자기 출력을 디코드 못 함 — B1 수정이 core에 미미러** (correctness, drift)
  - `Runtime/Streams/ZLinkStreamMetadataCodec.cs:40`이 `DecodeString(..., "value")` 호출, private `DecodeString`(`:83`)은 key/value 공히 `length == 0`에서 throw. 그러나 `Write`/`CalculatePayloadSize`(`:62`)는 **빈 key만** 거부 → 빈 value는 `valueLength=0`으로 인코드됨. 위성 connector의 byte-twin codec은 B1에서 `allowEmpty: true`로 고쳤으나(`Systems.Zlink.Stream.Connector/Runtime/Protocol/ZlinkStreamMetadataCodec.cs:41,88`) **core copy는 안 고쳐짐**. 메인 루프 grep으로 확인: core `:83`은 `if (length == 0 || …) throw`, connector `:88`은 `if ((!allowEmpty && length == 0) || …)`.
  - 결과: 빈 value 메타데이터를 실은 stream 헤더를 `ZLinkStreamHeaderCodec.Encode`(`:78`)가 만들고 `Decode`(`:142`)가 `"Metadata value is invalid"` throw → framework↔framework(및 ↔connector) 라운드트립 실패. C1 drift gate(`StreamWireInteropTests.cs:58-60`)는 `.With("optional","")`를 만들지만 encode-parity와 frame-prefix codec만 검증하고 `CoreHeaderCodec.Decode`는 안 태워서 이 drift가 게이트에 안 걸린다.
  - 방향: decode에서 zero-length value 허용(key는 ≥1 유지), connector와 대칭화. `StreamWireInteropTests`에 실제 `CoreHeaderCodec.Encode → Decode` 라운드트립 + connector→core 헤더 decode 케이스 추가로 게이트 사각 메움. 신뢰도 High.
- [x] **F-BUG2. entry-spot actor send 경로의 동일 분기 — 죽은 조건 분기** (correctness/dead)
  - `ZLinkEntrySpotActorDispatcher.cs:200-208` — `if (actorState.LiveActivation is not null)` 양쪽이 byte-identical `SubmitActorAsync(...)`. 형제 reply 경로(`:161-198`)는 같은 조건에서 non-null→`SubmitActorForReplyAsync`, null→`SubmitEntryOrCurrentActorForReplyAsync`(entry-spot actor router 먼저 시도)로 **비대칭**. send에는 대응 entry-spot 라우팅이 없음 → (a) 순수 dead 분기라 단일 호출로 접거나, (b) live activation 없는 relay send가 `TrySubmitEntrySpotActor…` 라우팅을 놓치는 잠재 결함. 리더가 판별 불가. 먼저 send-vs-reply 의도 확정. 신뢰도 High(분기 중복)/Medium(라우팅 갭).
- [x] **F-BUG3. `ZLinkBackendSpotNodeWrapper.EntrySpot()` 비원자 `??=` 레이스** (race, 낮은 빈도·심각)
  - `Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs:79,86-94` — 주석이 "매 native `EntrySpot()`는 새 facade 등록 → 중복 배포 시 bound-actor session relay를 조용히 black-hole"이라 경고하는데 가드는 lock 없는 `_entrySpot ??= new …`. 동시 최초 호출 2개가 각자 native `EntrySpot()` 호출·2 facade 등록 가능(정확히 주석이 경고한 실패). 초기화가 대개 단일 스레드로 warm-up(`ZLinkSpotNodeRuntime.cs:85/95/103`)하지만 warm 안 되는 router-capable 노드(entry-spot 타입 없음/`RoutingId.Size==0`)는 동시 dispatch에서 최초 구현. 방향: lazy init lock 또는 node init에서 강제 warm-up. 신뢰도 Low(창 좁음)/심각도 High.
- [x] **F-BUG4. Stream.Connector request timeout 예산 2회 순차 적용(~2×)** (defect, 낮음)
  - `Systems.Zlink.Stream.Connector/Runtime/ZlinkStreamConnector.cs:284-300` — `timeoutCts.CancelAfter(timeout)`가 `SendPacketAsync`(`:287-292`)를 감싸고, 이어 `_pending.WaitAsync(pending, timeout, ct)`(`:300`)가 **또** `CancelAfter(timeout)`(`ZlinkStreamPendingRequests.cs:47-48`). 두 창이 순차라 느린 send는 최대 `timeout`, reply 대기 추가 `timeout` → 실효 데드라인 ~2×. 방향: send+wait에 단일 데드라인 공유. 신뢰도 Low-Medium.
- [x] **F-BUG5. publish fan-out이 한 endpoint decode 실패에 전체 중단** (defect, 낮음)
  - `Runtime/Channels/ZLinkChannelPublishDispatchPipeline.cs:47-56` — `foreach(endpoint)` 안 `scope.TryDecode` 실패 시 `return`으로 루프 종료. 같은 name이 서로 다른 message type으로 해석되는 구독자들에서 한 type decode 실패가 나머지 구독자(정상 type 포함) 배달을 조용히 스킵. 핸들러 예외 arm(`:75-83`)은 `continue`로 진행 → fan-out 의미 불일치. name→type 대개 1:1이라 영향 제한. 방향: decode 실패도 `continue`. 신뢰도 Low.

### F-DEAD. 삭제 트랙 (대부분 A~E 삭제의 잔여물, 전부 grep 무참조 확인)

- [x] **F-DEAD1. 직접-native spot outbound 클러스터(C6/D2 재배선 잔여)** (없음)
  - `ZLinkSpotOutboundTransport.cs:19-41`(`RequestToSpotAsync`)·`:54-61`(`SendToSpot` bool), `ZLinkSpotOutboundEndpoint.cs:114-121`(`SendToSpot` bool). C6/D2가 spot↔spot outbound를 runtime router-channel로 재배선한 뒤 직접-native 경로가 phantom화. 삭제 시 `ZLinkSpotOutboundTransport`는 publish-only로 축소. 신뢰도 High.
- [x] **F-DEAD2. `AddPublisherBundle` 무호출 pass-through 쌍** (없음)
  - `ZLinkSpotNodeRuntime.cs:119-122` → `ZLinkSpotNodeBundleRegistry.cs:25-31`. bundle은 항상 lazy `GetOrCreatePublisherBundle`로만 생성 → add 경로 무참조. 신뢰도 High.
- [x] **F-DEAD3. `SubmitResolved…` 사슬(host+router) 무호출** (없음)
  - `ZLinkEntrySpotActorRouter.cs:102-114`(`SubmitResolvedAsync`, `actor` 인자도 미read) + 유일 소비처 `Runtime/Host/ZLinkFrameworkRuntimeSpots.cs:101`(`SubmitResolvedEntrySpotActorAsync`) 무호출. 둘 다 삭제. 신뢰도 High.
- [x] **F-DEAD4. `ZLinkSpotActorFrameReader.DisposeFrame` 무참조** (없음)
  - `ZLinkSpotActorFrameReader.cs:54-61` — B4가 `TryRead` 실패 경로 정리를 강화한 뒤 남은 stale 병렬 정리 진입점. frame teardown 지식을 `TryRead`에만 두도록 삭제. 신뢰도 High.
- [x] **F-DEAD5. 액터 generation write-only 사슬(A-AS1 잔여)** (없음)
  - `Runtime/Actors/ZLinkActorRuntimeState.cs:9`(`_actorGeneration`)·`:279-288`(`EnsureActorGeneration`) + 호출부 `ZLinkActorCreationCoordinator.cs:143`. A-AS1이 유일 reader `CurrentActorGeneration`를 지웠으나 counter/mutator/call은 잔존 → 매 activation마다 write-only 유지. 필드+메서드+call 삭제. 신뢰도 High.
- [x] **F-DEAD6. `ZLinkSessionContext.CleanupActorBindingsAsync` 무참조** (없음)
  - `Runtime/Streams/ZLinkSessionContext.cs:95-98`. live 정리 경로는 `ZLinkStreamSessionRuntime.cs:231→_context.CleanupAsync→ActorCoordinator.CleanupAsync`. 삭제. 신뢰도 High.
- [x] **F-DEAD7. `GetSpotRouteBridgeOwner` + write-only `SpotRouteBridgeOwners` 딕셔너리(A-CH1/2 잔여)** (없음)
  - `Runtime/Host/ZLinkFrameworkRuntime.cs:84-95`(private, 무호출), dict `ZLinkFrameworkRuntimeState.cs:38`, 유일 writer `Runtime/Channels/ZLinkRouteChannelInitializer.cs:95`(`.Add`). 딕셔너리는 매 route-channel init에서 채워지나 read 없음(live한 `SpotRouteBridges` list와 다름). 메서드+property+add 삭제. 신뢰도 High.
- [x] **F-DEAD8. `ZlinkStreamTaskRunner` `name` 파라미터 전 호출부에서 폐기** (없음)
  - `Systems.Zlink.Stream.Connector/Runtime/ZlinkStreamTaskRunner.cs:5-22` — `TaskState`에 `Name` 없음, 6개 호출부가 진단 라벨(`"stream-receive-loop"` 등) 전달하나 폐기. 폐기 대신 fault catch에서 사용하거나 파라미터+리터럴 6개 제거. 신뢰도 High.
- [x] **F-DEAD9. Redis key codec decode/parse 방향 production-dead(A-LO1 병렬 인스턴스)** (없음)
  - `Zlink.Framework.Locations.Redis/ZLinkRedisLocationKeyCodec.cs:48-89,157-175` — store는 encode만(16곳). `DecodeKey`/`Decode*Key`/`ParseAutoConnectType`/`ParseRole` 전부 production 무참조. store가 `IZLinkLocationWatchStore` 미구현(`ZLinkFrameworkServiceRegistrar.cs:253` 분기 미도달)이라 key 역복원 소비처 없음. `DecodeKey`+`DecodeActorKey`만 `RedisLocationStoreTests.cs:41`(Actor arm)이 자기 코드 살림 = A-SP2 안티패턴. 방향: decode 방향 제거(테스트는 encode-only 단언) 또는 spec-required 주석+게이트. 신뢰도 High.
- [x] **F-DEAD10. Stream.Connector typed-handler dispatch 도달불가 catch** (없음)
  - `Systems.Zlink.Stream.Connector/Runtime/ZlinkStreamReceiveDispatcher.cs:74-88` — `DispatchUserCallbackAsync`가 핸들러 예외를 밖으로 안 흘림(`ZlinkStreamConnectorCallbacks.cs:101-116,188-198`에서 catch+report/swallow)이라 `"Typed message handler failed."` catch 도달 불가. 중복 try/catch 제거하고 callback layer report에 의존. 신뢰도 Medium-High.
- [x] **F-DEAD11. `ZLinkClientCallCodec.DecodeJsonReply<TReply>` 무참조** (없음)
  - `Runtime/Messaging/ZLinkClientCallCodec.cs:74` — 전 트리 호출부 0. 삭제. 신뢰도 High.
- [x] **F-DEAD12. 미사용 `DecodeBody(Message,…)` 단일-메시지 오버로드(A-CH3 잔여)** (없음)
  - `Runtime/Messaging/ZLinkEnvelopeCodec.cs:214`(3-arg, 완전 무참조), `:209`(2-arg, `EnvelopeCodecTests`만). :214 삭제, :209는 테스트를 4-arg core로 옮기고 드롭. 신뢰도 High(:214)/Medium(:209).
- [x] **F-DEAD13. `HttpHeaderLookup.Find` OrdinalIgnoreCase fallback 루프 도달불가(C16 잔여)** (없음, 방어적)
  - `Zlink.HttpClient/Runtime/HttpHeaderLookup.cs:9-14` — 모든 production 호출부가 이미 `OrdinalIgnoreCase` dict 전달 → `TryGetValue`가 해결, 선형 스캔 미실행(테스트만 case-sensitive dict로 태움). 계약 방어로 유지+주석 or 제거. 신뢰도 Low.

### F-DUP. 중복 / 지식 누출

- [x] **F-DUP1. stale-address `RequestTargetNotFound` 정책 4곳 손복제** (없음/posd-leak)
  - `Runtime/Host/ZLinkFrameworkRuntimeChannels.cs:57-67,90-100,110-116,139-145` — "mesh가 node rid 모름 ⇒ stale address(미수렴 아님) ⇒ `RequestTargetNotFound` throw + 동일 메시지 포맷"이 catch-rewrap 2곳 + if-preguard 2곳으로 4벌. dispose 포함 단일 헬퍼(`ThrowStaleRouteTarget`/`TryGuardKnownPeer`)로. 신뢰도 Medium.
- [x] **F-DUP2. reply-envelope 해석 프로토콜 2개 디코더 중복** (없음/posd-leak)
  - `Runtime/Messaging/ZLinkClientCallCodec.cs:42-56`(`DecodeEnvelopeReply`, throwing) vs `ZLinkEnvelopeReplyCompletion.cs:12-45`(`Complete<TReply>`, callback) — empty⇒throw, `Kind==Error`⇒`ZLinkEnvelopeErrorMapper.CreateException`, else null-body 가드 후 `DecodeBody`가 독립 2벌(둘 다 live). "reply envelope 읽는 계약"을 단일 헬퍼로 추출, wrapper는 throw-vs-callback+dispose만. 신뢰도 Medium.
- [x] **F-DUP3. `ZLinkSpotPeerConnector` 동일 connect 헬퍼 2벌** (없음)
  - `ZLinkSpotPeerConnector.cs:37-47`(`ConnectPeer`) vs `:49-59`(`ConnectRouterPeer` string) — byte-identical "connect + Busy swallow", 같은 backend `ConnectPeer(string)` 호출. router-vs-pubsub 이름이 존재 않는 구분 암시. 두 string 헬퍼 통합(rid 오버로드는 분리 유지). 신뢰도 High.
- [x] **F-DUP4. 중복 `TryGetPublisherBundle` pre-check + pass-through 표면** (벤치, per-publish)
  - `ZLinkSpotRuntimeManager.cs:28-33`가 `TryGetPublisherBundle`→있으면 반환, 없으면 `GetOrCreatePublisherBundle` 호출. 후자(`ZLinkSpotNodeBundleRegistry.cs:43-54`)가 이미 동일 존재체크 수행 → 매 external publish마다 이중 dict 조회. body를 `GetOrCreatePublisherBundle` 단일 호출로, 고아 `TryGetPublisherBundle`(node+registry) 삭제. 신뢰도 Medium-High.
- [x] **F-DUP5. `actorType`가 ownership-coordinator 거의 전 메서드에서 dead param** (없음/posd-leak)
  - `Runtime/Locations/ZLinkActorOwnershipCoordinator.cs`(:164,178,196,215,232,254,291) + `IZLinkActorLocationLifecycle.cs:22`. actor-location key는 `actorId`만(`EncodeActorKey`), `actorType`는 claim 경로에서만 row에 stamp. `Publish/Notify*/Release/Owns/Renew`는 `actorType`를 안 읽거나 discard하는 `RenewActorAsync`로 전달만. "actor 정체성=(type,id)" 오해 유발(key schema 정보 누출). claim 진입점만 남기고 나머지 시그니처에서 드롭. 신뢰도 High.
- [x] **F-DUP6. Stream.Connector metadata UTF-8 byte-count 이중 계산** (벤치, per-send·metadata時)
  - `Systems.Zlink.Stream.Connector/Runtime/Protocol/ZlinkStreamMetadataCodec.cs` — `CalculatePayloadSize`(`:54-76`)가 entry별 `GetByteCount(key/value)`로 버퍼 사이즈 계산, `Write`(`:13-28`)가 배치용으로 재계산. 공통 경로에선 metadata 비어 short-circuit. 단일 패스 또는 per-entry 길이 재사용. 신뢰도 Medium.
- [x] **F-DUP7. Stream.Connector name 검증 send당 2~3회 재실행** (벤치, per-send)
  - `ResolveName`(`ZlinkStreamConnector.cs:316`)·`BuildOutboundFrame`(`ZlinkStreamFrameSender.cs:20`)·`headerCodec.Encode`(`ZlinkStreamHeaderCodec.cs:19`)가 각각 `ValidateName`(reserved-prefix + `GetByteCount`) 실행. public 경계 1회 검증 후 codec/sender는 신뢰(또는 debug assert). 신뢰도 Medium.
- [x] **F-DUP8. `ListLivePeersAsync` reconcile tick당 이중 필터·이중 할당** (벤치, per-tick)
  - `Runtime/Locations/ZLinkStoreLocationResolvers.cs:51-70` + `ZLinkLiveLocationRows.cs:21-37` — `fresh` 리스트(IsKnown+observed) 만든 뒤 2-arg `FilterAsync`(lease 필터)가 `live` 재구성. 3-arg 오버로드(`:39-58`)는 observed+lease를 1패스로 접고 query service가 사용. 2-arg는 이 caller 전용. unknown-row 진단 로그만 predicate화 걸림돌. predicate 통합 후 2-arg `FilterAsync` 삭제. 신뢰도 Low-Medium.
- [x] **F-DUP9. `ZLinkLocationSpotRouteRefResolver` stale 이중 `<summary>`(C12 잔여)** (없음, doc)
  - `Runtime/Locations/ZLinkLocationSpotRouteRefResolver.cs:3-14` — mesh-name 도출을 서술하는 첫 summary는 C12가 그 지식을 `ZLinkSpotMeshLocationResolver.cs:13-17`로 옮긴 뒤 lying comment. 첫 summary 삭제, "store 위 default resolver" summary만 유지. 신뢰도 Medium.

### F-POSD. shallow / pass-through / 구조

- [x] **F-POSD1. actor/entry-spot runtime partial의 단일-await tail-call이 불필요 async state machine(D2/D4 미완 잔여)** (벤치, 일부 per-actor-message hot)
  - `Runtime/Host/ZLinkFrameworkRuntimeActors.cs`(`SubmitActorAsync:178`, `SubmitActorForReplyAsync:308`, `SubmitActorByIdAsync:317`, `CreateActorAsync:269`, `DestroyActorAsync:78`, `JoinActorToSpotAsync:154`, `AttachActorAsync:162`, `DisconnectActorAsync:170`, `FindActorAsync:278`, `NotifyActorDisconnectedByIdAsync:326` 등) + `ZLinkFrameworkRuntimeSpots.cs:63-79,81-99`. 본문이 단일 `await _x.Y(...)`(post-await 작업 없음)라 ValueTask 위 state machine만 생성. D4(`RouteChannelRuntime`)·D2(`ZLinkSpotManagerService`)가 지운 바로 그 패턴. ValueTask 직반환. hot submit 경로는 baseline-vs-patched 벤치 후 커밋. 신뢰도 High(패턴)/Medium(perf 재질).
- [x] **F-POSD2. `transportName`이 command/request 파이프라인의 스레드된 상수(C5 잔여)** (없음)
  - `Runtime/Channels/ZLinkChannelCommandDispatchPipeline.cs:16`·`ZLinkChannelRequestDispatchPipeline.cs:15` — 유일 caller `ZLinkChannelPacketDispatcher`(`:82,128`)가 항상 리터럴 `"Channel"`. publish 파이프라인(`:22`)은 이미 하드코딩 → 불일치. 파라미터 제거·`"Channel"` 하드코딩(또는 공유 상수). 신뢰도 Medium.
- [x] **F-POSD3. `ZLinkEntrySpotActorRouter.TryAsync`가 2계층 통과 `runtimeState` 폐기** (없음/posd-shallow)
  - `ZLinkEntrySpotActorRouter.cs:9-17`(`_ = runtimeState;`) ← `ZLinkFrameworkRuntimeSpots.TrySubmitEntrySpotActorAsync:63-79` ← `ZLinkActorDispatchRouter.cs:120`. turn 재진입이라 serialize 안 해 state 불필요한데 3 시그니처를 타고 전달만. `TryAsync`+host wrapper에서 드롭. 신뢰도 Medium.
- [x] **F-POSD4. `LocationLifecycle`가 service-locator property로 bind/unbind마다 재-resolve** (없음/ddd)
  - `Runtime/Host/ZLinkFrameworkRuntimeActors.cs:375-376`(property, `Services.GetService(...) as`), read `:418`/`:447`. 다른 collaborator는 ctor-주입 필드인데 이것만 매 접근 컨테이너 조회 + `as` null이면 route 알림이 조용히 optional화. 1회 resolve(캐시/ctor) 또는 의존을 명시. optional 설계면 의도를 코드로. 신뢰도 Medium.
- [x] **F-POSD5. session cleanup 3-홉 pass-through** (없음)
  - `Runtime/Streams/ZLinkSessionActorCoordinator.cs:116-121`(`CleanupAsync`→`CleanupBindingsAsync`)→`:109-114`(→`_bindings.CleanupAsync`). F-DEAD6 제거 후 `context.CleanupAsync→coordinator.CleanupAsync→CleanupBindingsAsync→registry.CleanupAsync` 3무가치 홉. 단일 메서드로 접음. 신뢰도 Medium.
- [x] **F-POSD6. `ZLinkEntrySpotActorDispatch`가 `Activation` 위 near-pure pass-through veneer** (없음, 낮음)
  - `ZLinkEntrySpotActorDispatch.cs` — 11멤버 중 9개가 `ZLinkEntrySpotActivation` 메서드로 1:1 forward(5개 `TryResolveX`는 동일 null-guard 반복). 실가치는 `Attach` one-shot 불변식·`RequireActivation`만. entry-spot actor 연산 추가마다 2곳 수정(change amplification). router가 단일 `TryGetAttachedActivation(out …)`로 `Activation` 직접 조회 검토. 신뢰도 Low.
- [x] **F-POSD7. 잔여 소형 shallow(기회 될 때)** (없음)
  - `ZLinkSpotOutboundEndpoint.cs:33-40,42-48` public→private 단일-caller pass-through 쌍(인라인); `ZLinkSessionActorCoordinator.cs:26-36` `BindActorAsync(ActorRef)` 얕은 forwarder(`BindActorCoreAsync` 인라인); `Zlink.Framework.AspNetCore/ZLinkFrameworkServiceRegistrar.cs:138-149` 동일 술어 `if(HasSpotNode)` 블록 2연속(D8이 `HasSpotPublisherClient` 제거 후 남음, 병합); `ZLinkActorOwnershipCoordinator.cs:58` 활성화 rollback을 항상-참 `catch when(Status==Claimed)`로 가림(B7류, plain catch/assert). 신뢰도 Low.
- [x] **F-POSD8. `ZLinkSpotSubscriptionPump` 20ms 고정율 poll이 event-driven subscribe와 중복** (벤치)
  - `ZLinkSpotSubscriptionPump.cs:5,39-64`(20ms `Task.Delay` 루프) vs 네이티브 `subscribeReadable` 배선(`ZLinkSpotActivationExecution.cs:84`→`ZLinkSpotNativeDispatchRouter.cs:26-28`이 동일 `DispatchSubscriptionsAsync` drain). D6가 spot-route bridge에서 제거한 fixed-rate safety loop와 동형 — 구독 있는 activation마다 런타임 생애 상시 wakeup. native edge 신뢰성 검증 후 poll 제거 or safety net 명문화. **삭제 전 동작 확인 필수.** 신뢰도 Medium.

- [x] **F-POSD9. 초기화 실패 정리의 sync-over-async와 런타임 hot path lazy 생성** (correctness/POSD)
  - 후속 재검토에서 channel·route·stream 초기화 실패 경로가 `DisposeAsync().AsTask().GetAwaiter().GetResult()`로
    정리되고, client/publisher bundle이 실제 호출 시점에 만들어지는 문제를 추가로 확인했다. 초기화 사슬을
    `ValueTask`로 전파하고 실패 정리를 `await`하도록 바꿨다. 등록된 client/publisher bundle은 시작할 때 모두
    만들며, 호출 경로는 이미 초기화된 bundle만 조회한다.

### F 완료 결과

- **F-BUG1~5**: 빈 metadata value의 core decode를 connector와 맞추고 양방향 wire 회귀 테스트를 추가했다.
  동일한 entry-spot send 분기는 단일 호출로 접었고, `EntrySpot()` 최초 생성은 lock으로 보호했다. request는
  send와 reply 대기에 하나의 timeout token을 공유하며, publish fan-out decode 실패는 다음 endpoint로 진행한다.
- **F-DEAD1~13**: 직접-native spot outbound 잔여, publisher bundle add 경로, `SubmitResolved` 사슬,
  frame dispose 진입점, write-only actor generation, session cleanup 별도 진입점, route bridge owner map,
  폐기하던 task 이름, Redis key 역변환, 도달 불가 catch, 미사용 reply/body decode, HTTP header fallback을 삭제했다.
- **F-DUP1~9**: stale route 예외 생성과 reply envelope decode를 각각 한곳으로 모았다. peer connect의 string 경로는
  하나로 합치고 routing-id 경로는 의미가 다른 backend 오버로드라 같은 이름의 오버로드로 유지했다.
  publisher 이중 조회, ownership의 죽은 `actorType`, metadata write의 두 번째 byte-count, send 경로의 반복 name 검증,
  live peer 이중 필터·할당, stale summary를 제거했다.
- **F-POSD1~6**: 후처리 없는 `ValueTask` wrapper는 직접 반환하고, 상수 `transportName`과 폐기하던 `runtimeState`를
  제거했다. location lifecycle/topology 협력자는 런타임 생성 시 한 번만 resolve해 하위 구성요소에 전달한다.
  session cleanup 홉을 접고 `ZLinkEntrySpotActorDispatch` veneer를 삭제해 runtime과 router가 activation을 직접 사용한다.
- **F-POSD7**: outbound endpoint의 단일 호출 private wrapper를 인라인하고, AspNetCore의 인접 조건 블록과 ownership
  rollback catch를 정리했다. `BindActorCoreAsync`는 `BindActorAsync`와 `BindOrGetActorAsync` 두 실제 호출자가 공유하는
  native bind 불변식이므로 삭제하지 않고 유지했으며, 얕은 public wrapper만 직접 반환으로 바꿨다.
- **F-POSD8~9**: 고정 20ms subscription pump를 삭제하고 native readable callback 한 경로만 남겼다. channel·route·stream
  초기화와 실패 정리를 끝까지 비동기로 연결하고 등록된 outbound bundle을 시작 단계에서 생성해 hot path의 lazy
  생성과 sync-over-async를 함께 제거했다.

### F 적용 순서 기록

1. F-BUG1의 core codec 대칭화와 decode 회귀 테스트를 먼저 적용했다.
2. F-BUG2를 죽은 분기로 판정해 접은 뒤 나머지 correctness 항목을 처리했다.
3. F-DEAD를 삭제해 표면을 줄인 다음 F-DUP의 정책·계약 중복을 통합했다.
4. F-POSD를 적용하고 hot path smoke와 subscription 관련 E2E를 마지막 게이트로 실행했다.

### 2026-07-10 후속 검증 기록

- 핵심 회귀 filter 통과: metadata interop, entry-spot dispatch/concurrency, publish fan-out을 포함해 23 passed.
- `dotnet test framework/languages/dotnet/Zlink.Framework.sln --nologo` 통과: 511 passed, 0 failed, 0 skipped.
- sample runner 통과: TicTacToe, Bingo, SupportChat, ShoppingMall, DeliveryDispatch, GameQuest. 각 runner가
  `result/evidence=completed`까지 확인했으며 build는 0 warnings, 0 errors였다.
- E2E runner 10개 통과: LocationMessaging, PubSub, RegistrationCodec, ResilienceLifecycle, RuntimeMonitoring,
  SpotActorTransfer, SpotService, StoreFailure, ToActorMessaging, YieldDispatch. 장시간 연속 실행 중 PubSub PS-B2 timeout,
  SpotActorTransfer actor-b 시작 지연, StoreFailure Redis 연결 실패가 각각 한 번 발생했으나 해당 runner 전체 또는
  실패 시나리오를 독립 환경에서 다시 실행했을 때 모두 통과해 코드 회귀가 재현되지 않았다.
- 전체 솔루션 build는 0 errors이며, E2E 샘플의 기존 compiler warning 8건도 함께 제거해 0 warnings로 확인했다.
- 1024B, 2초 local smoke에서 `zlink-framework-dotnet`은 request-window 129.20 KOPS,
  send-saturation 366.80 KMSG/s였다. 짧은 smoke라 절대 성능 판정에는 쓰지 않지만, 같은 문서에 남은 이전
  2초 smoke의 93.15 KOPS / 229.84 KMSG/s보다 낮아지는 회귀는 관측되지 않았다.
- 제거 대상 식별자는 production/test `.cs` 전 범위에서 no-hit을 확인했고 `git diff --check`도 통과했다.
