# [BUG] 분리 토폴로지에서 세션 disconnect가 원격 actor의 spot `OnDisconnectActorAsync`를 호출하지 않음

- **상태**: 수정 완료 (2026-07-01)
- **심각도**: High — actor 생명주기 스펙("disconnect → destroy")이 분리 토폴로지에서 깨짐
- **레벨**: **core C-API 갭** (순수 .NET 바인딩 버그 아님 — §4.4 참조). 정석 수정은 core에서 시작해 전 언어 바인딩 parity 필요
- **영향 범위**: stream 세션 서버와 actor(spot) 서버가 **다른 노드/프로세스**인 모든 구성 + 모든 언어 바인딩(C-API 공통 갭)
- **최초 발견**: SupportChat 샘플(상담원 재접속 시 availability 정리)
- **작성일**: 2026-07-01

---

## 1. 요약

클라이언트 stream이 끊겨 세션의 `IZLinkSession.OnDisconnectedAsync`가 실행되고, 그 안에서
bound actor마다 `IZLinkSessionActor.NotifyDisconnectedAsync()`를 호출해도 — actor가 **원격
노드**(세션 서버와 다른 spot 서버)에 있으면 그 actor가 속한 spot의
`OnDisconnectActorAsync`(및 entry spot의 동일 콜백)가 **호출되지 않는다.**

세션과 spot이 **같은 프로세스**(co-located)인 경우에만 콜백이 발화한다. 그래서 TicTacToe·Bingo
샘플은 이 버그를 밟지 않는다(세션이 spot과 같은 Play 노드에서 호스팅됨).

## 2. 기대 동작 vs 실제 동작

- **기대**: 세션 disconnect → bound actor `NotifyDisconnectedAsync()` → 해당 actor의 home
  노드에서 그 actor가 속한 spot의 `OnDisconnectActorAsync(actor, ct)` 발화(로컬 경로와 동일).
  actor 생명주기 스펙의 "disconnect → destroy"가 토폴로지와 무관하게 성립해야 함.
- **실제**: 분리 토폴로지에서 콜백이 발화하지 않음. 예외도 없이 조용히 유실됨.

## 3. 재현 (실측 완료)

가장 확실한 재현은 SupportChat 샘플이다(세션 서버 ≠ Support 서버, 둘 다 spot mesh 참여).

1. `framework/languages/dotnet/samples/SupportChat`에서 `./run_sample.sh` 실행.
   시나리오 중간에 상담원 client가 `agent.Close.Async()`로 연결을 끊는다.
2. 세션 서버 로그: `SupportChatSession.OnDisconnectedAsync`가 발화하고, bound actor 3개
   (roster `agent-1` + 방별 `agent-1@conversation-1/2`)에 `NotifyDisconnectedAsync()`가
   **예외 없이** 호출됨(진단 로그로 확인: 모두 "NotifyDisconnected OK", throw 없음).
3. Support 서버 로그: `ConversationSpot.OnDisconnectActorAsync`,
   `SupportEntrySpot.OnDisconnectActorAsync` **어느 것도 발화하지 않음**.

재현용 최소 진단(이미 제거함)은 세션 루프에 try/catch + 로그, 각 spot의
`OnDisconnectActorAsync`에 진입 로그를 넣어 확인했다.

### 대조군 (정상 동작)

TicTacToe 샘플은 세션이 Play(spot) 서버와 **같은 프로세스**(`Server/Play/.../Sessions/PlaySession.cs`,
run 스크립트에 별도 session 서버 없음). 여기서는 동일 패턴
(`OnDisconnectedAsync` → `Context.Actors.Bound` 순회 → `NotifyDisconnectedAsync`)이
`TicTacToeGame.OnDisconnectActorAsync` / `PlayEntrySpot.OnDisconnectActorAsync`를 정상 발화시킨다.

## 4. 근본 원인 (코드 경로)

### 4.1 분리(원격) 경로 — 깨진 곳

`src/Zlink.Framework/Runtime/Host/ZLinkFrameworkRuntimeActors.cs`
`NotifyActorDisconnectedAsync(ActorRef actor, …)` (약 315행):

```csharp
var state = GetOrCreateActorState(actor.ActorId);
if (state.Actor is not null
    && state.NativeActorRef is { } localActor
    && localActor.NodeRid == actor.NodeRid
    && localActor.Generation == actor.Generation)
{
    await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken); // (A) 로컬 경로
    return;
}

if (GetActorSpotNode() is not { } node)
{
    await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken); // (B) 스팟노드 없음
    return;
}

// (C) 원격 actor + router-capable 세션 노드
cancellationToken.ThrowIfCancellationRequested();
var backendActor = actor.ToBackend();
_ = Task.Run(async () =>
{
    await node.CloseActorBoundSessionAsync(
        backendActor, Registration.DefaultRequestTimeout, CancellationToken.None);
}, CancellationToken.None);
```

- 세션 서버는 actor를 원격에 두므로 (A)의 로컬 조건 불성립.
- 세션 서버가 spot mesh에 router로 참여(`AddSpotMesh(...).EnableRouter().SetRoutingId()`)하면
  `GetActorSpotNode()`가 노드를 반환 → (B) 건너뛰고 **(C)** 진입.
- **(C)는 네이티브 `CloseActorBoundSession`만 fire-and-forget(`Task.Run`, `CancellationToken.None`)으로
  호출**하고, home 노드의 관리형 disconnect **생명주기(`OnDisconnectActorAsync`)를 라우팅하지 않는다.**
  게다가 `Task.Run` 안이라 예외가 나도 삼켜진다(관찰된 "조용한 유실"과 일치).

참고로 (B) 경로의 `NotifyActorDisconnectedByIdAsync`(308행)는
`src/Zlink.Framework/Runtime/Actors/ZLinkActorDispatchRouter.cs`의
`NotifyDisconnectedByIdAsync`로 이어지는데, 여기서 `state.Actor ?? throw ActorRouteNotFound`
이므로 **원격 actor면 예외**다. 즉 (B)도 원격을 처리하지 못한다.

### 4.2 로컬 경로 — 정상 동작하는 곳 (수정의 참고 모델)

`src/Zlink.Framework/Runtime/Actors/ZLinkActorDispatchRouter.cs`
`NotifyDisconnectedByCurrentLocationAsync` (약 193행):

```csharp
var placement = await state.ExecuteLockedAsync(
    () => state.SelectPlacementLocked(false), cancellationToken);

if (placement.Activation is not null)
{
    await placement.Activation.NotifyActorDisconnectedAsync(actor, cancellationToken); // spot 콜백
    return;
}
await runtime.TryNotifyEntrySpotActorDisconnectedAsync(actor, null, cancellationToken); // entry spot 콜백
```

- 이 관리형 경로가 실제로 `OnDisconnectActorAsync`를 발화시킨다. **문제는 이 경로가 actor의
  home 노드에서 실행되어야 하는데, 분리 토폴로지에서는 (C)가 네이티브 close로 빠져 이 경로가
  home 노드에서 실행되지 않는다는 것.**

### 4.3 네이티브 경계

`node.CloseActorBoundSessionAsync`는 결국
`src/Zlink.Framework/Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs:226`에서
`nativeSpotNode.CloseActorBoundSession(actor.ToNative(), timeout)` — **네이티브 코어 호출**이다.
수신측(actor home 노드)의 bound-session close 처리는 네이티브 코어
(`/home/hep7/project/kairos/zlink/core/src`)에 있고, 이때 home 노드의 관리형
disconnect 생명주기를 콜백으로 올리는 훅이 없다.

### 4.4 근본은 core C-API 갭 (레벨 판정)

C-API 확인 결과 이 문제의 뿌리는 관리형이 아니라 **core C-API에 disconnect 생명주기 개념이
없다**는 것이다.

- `core/include/zlink/service/actor.h`의 actor 생명주기 이벤트 종류는
  **`ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED`(1), `ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT`(2) 둘뿐**이고
  **`DISCONNECTED`가 없다.** 홈 노드의 lifecycle 수신(`zlink_spot_recv_actor_lifecycle`,
  `core/include/zlink/service/spot.h:165`)도 이 둘만 전달한다.
- 따라서 `OnDisconnectActorAsync`(disconnect 콜백)는 **C-API에 없는, 각 언어 바인딩이
  in-process로 합성한 개념**이다(관리형 `NotifyDisconnectedByCurrentLocationAsync`가 직접
  spot 메서드 호출). actor가 세션과 같은 노드일 때만 성립한다.
- cross-node에서 쓰는 유일한 C-API 프리미티브
  `zlink_spot_node_actor_close_bound_session`(`core/include/zlink/service/spot.h:181`)은
  세션↔actor **바인딩만 닫고**, actor의 홈 노드에 disconnect 성격의 lifecycle 신호를
  **emit하지 않는다.** (`ZLINK_EVENT_DISCONNECTED`는 소켓/transport 레벨 이벤트로 actor
  lifecycle과 무관.)

결론: **홈 노드의 disconnect 콜백을 cross-node로 발화시킬 C-API 신호 자체가 없다.** 순수
관리형(.NET) 레벨로는 완전 해결이 불가능하며(§6.1은 core 신호 없이는 막힘), 정석 수정은
core(C-API)에서 시작해야 한다.

## 5. 왜 지금까지 안 드러났나

- TicTacToe·Bingo·기타 spot 샘플은 세션을 spot과 **같은 노드**에서 호스팅하므로 항상 (A) 로컬
  경로를 타서 정상 발화한다.
- 회귀 테스트 `tests/Zlink.Framework.SampleRegressionTests/Regression.cs`의
  `Bingo_And_TicTacToe_Samples_Implement_Actor_Lifecycle_Spec`은 **코드가 생명주기 스펙을
  구현하는지 정적 검사**만 하고, 분리 토폴로지에서 런타임 발화를 검증하지 않는다.
- 즉 **세션 서버 ≠ actor 서버 구성에서의 disconnect 생명주기 전파는 테스트 공백**이었다.

## 6. 수정 방향 (후보)

§4.4에 따라 근본 수정은 **6.1(core C-API)**. 6.2는 core 신호가 생긴 뒤의 바인딩 매핑이다.
6.3은 core 신호 없이 관리형에서 우회 가능한지 검토용(대안).

### 6.1 core C-API — disconnect 신호 추가 (근본, 전 언어 영향)

`core`에서 cross-node로 actor bound session이 닫힐 때(또는 `close_bound_session` 수신 시)
actor의 **홈 노드가 disconnect 성격의 신호를 emit**하도록 추가한다. 설계 선택:

- (a) `zlink_spot_actor_lifecycle_event_kind_t`에 `DISCONNECTED` 종류를 추가하고, 홈 노드의
  `zlink_spot_recv_actor_lifecycle`로 전달. 바인딩이 이를 disconnect 콜백으로 매핑.
- (b) 별도 bound-session-closed 콜백/recv를 신설.

ABI/스펙 변경이므로 공통 스펙 문서 + core 버전 관리 필요.

### 6.2 각 언어 바인딩 매핑 (6.1 이후, parity)

core가 disconnect 신호를 주면 각 바인딩(C++/Java/Kotlin/Node/dotnet)이 이를 자기 disconnect
콜백(dotnet의 `OnDisconnectActorAsync` 등)으로 매핑. 관리형 dotnet의 경우
`NotifyActorDisconnectedAsync` (C) 분기가 네이티브 close에 더해 홈 노드 lifecycle 수신 경로로
`NotifyDisconnectedByCurrentLocationAsync`(§4.2)를 타도록 연결.

### 6.3 (대안 검토) 관리형 우회 — core 변경 없이 가능한지

core에 신호가 없으므로, 관리형이 기존 actor 메시징 경로(`RelayAsync`/`SendActorBoundSession`
계열)로 홈 노드에 "disconnect 생명주기 실행" 제어 메시지를 보내 `NotifyActorDisconnectedByIdAsync`를
트리거할 수 있는지 검토. 가능하면 dotnet 한정 단기 완화책이 되나, 전 언어 parity·정합성 측면에서
6.1이 정석. **어느 경우든 (C) 분기의 fire-and-forget + 예외 삼킴은 교정(최소 로깅)해야 한다.**

## 7. 요구되는 산출물 (담당자 체크리스트)

1. **회귀 테스트(재현)**: 분리 토폴로지에서 세션 disconnect가 원격 actor의
   `OnDisconnectActorAsync`를 발화시키는지 검증하는 테스트. 수정 전 **실패**, 수정 후 **통과**.
   - 단위/통합: `tests/`의 멀티노드 하네스 또는 `testapps/Zlink.Framework.TestHost` 활용.
2. **프레임워크 수정**: 6.1 또는 6.2.
3. **e2e 시나리오 추가·검증**: 세션↔actor 분리 구성에서 stream 끊김 → 원격 spot 정리 콜백
   발화를 확인하는 e2e(예: `e2e/`에 신규 시나리오 또는 SpotService/ResilienceLifecycle 계열
   확장). 서버 로그/관찰 가능 상태로 콜백 발화를 단정.
4. 언어 parity(6.2 선택 시): 공통 스펙 문서 및 4개+ 언어 바인딩 동기화.

## 8. 관련 파일 (앵커)

- `src/Zlink.Framework/Runtime/Host/ZLinkFrameworkRuntimeActors.cs` — `NotifyActorDisconnectedAsync`(315), `NotifyActorDisconnectedByIdAsync`(308), `CloseActorBoundSessionAsync`(472)
- `src/Zlink.Framework/Runtime/Actors/ZLinkActorDispatchRouter.cs` — `NotifyDisconnectedByCurrentLocationAsync`(193, 정상 경로 모델), `NotifyDisconnectedByIdAsync`
- `src/Zlink.Framework/Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs:226` — 네이티브 `CloseActorBoundSession` 경계
- `src/Zlink.Framework/Runtime/Streams/ZLinkSessionActor.cs:26` / `ZLinkSessionContext.cs:84` / `ZLinkSessionActorCoordinator.cs:81` — 세션측 `NotifyDisconnectedAsync` 진입 체인
- 정상 대조 샘플: `framework/languages/dotnet/samples/TicTacToe`(co-located), `.../Bingo`
- 재현 샘플: `framework/languages/dotnet/samples/SupportChat`(분리 토폴로지)

## 9. SupportChat 샘플 상태 (이 버그와의 관계) — 수정 후 검증 완료

- SupportChat은 상담원 disconnect 시 availability를 정리해야 한다(문서 §9).
- **관용적 방식**(bound actor `NotifyDisconnectedAsync` → `SupportEntrySpot.OnDisconnectActorAsync`에서
  `AgentAvailabilityDirectory`에서 제거)으로 코드가 작성되어 있고, **프레임워크 수정(§10) 후
  이 관용 코드가 그대로 동작함을 실측 확인했다.**
- 검증: `samples/SupportChat/run_sample.sh` 3연속 그린(`supportchat=completed` +
  `server-evidence=completed`). 상담원 disconnect(재접속 중간 + 종료) 시 Support 서버 로그에
  `support entry: agent disconnected, availability removed. actor=agent-1`가 발화 —
  전용 Support 서버(세션과 분리된 원격 노드)에서 `OnDisconnectActorAsync`가 cross-node로 발화함을 확인.
  재접속 후 `SetAgentAvailableReq(true)` 재등록과의 타이밍 충돌 없음.
- 과거 임시 우회책이던 채널 요청(`RemoveAgentAvailabilityReq`)은 **제거**했다(관용 코드로 대체).

## 10. 수정 결과 (2026-07-01)

core C-API에 actor lifecycle `DISCONNECTED` 이벤트를 추가했다. 원격 bound session close를 처리하는
home 노드는 이제 actor의 현재 spot에 disconnect lifecycle을 enqueue한다. 각 언어 바인딩은 이 새
값을 public enum에 반영했고, framework runtime은 lifecycle 수신을 각 언어의 actor disconnect
callback으로 연결한다.

Java framework에서는 stream session teardown 경로가 session의 bound actor 목록을 먼저
`notifyDisconnected()`로 통지한 뒤 `onDisconnected()`를 실행하도록 보강했다. 이로써 local
session actor와 cross-node lifecycle event 모두 같은 callback 의미를 갖는다.

회귀 확인:

- `cmake --build core/build --target test_spot_actor_dispatch -j2`
- `core/build/bin/test_spot_actor_dispatch`
- `bindings/dev_sync_local_core_libs.sh`
- `dotnet build framework/languages/dotnet/Zlink.Framework.sln --no-restore`
- `../../gradlew --project-cache-dir "${HOME}/.cache/zlink/java-e2e/SpotService-gradle-cache" --no-daemon --no-parallel --max-workers=1 installDist --quiet`
- `npm --prefix framework/languages/node run build`
- `cmake --build framework/languages/cpp/build --target zlink_framework -j2`
- `framework/languages/dotnet/e2e/SpotService/run_e2e.sh` with `SCENARIO_SET=sm-b6`
- `framework/languages/dotnet/e2e/SpotService/run_e2e.sh` with `SCENARIO_SET=sm-d5`
- `framework/languages/java/e2e/SpotService/run_e2e.sh`
- `framework/languages/node/e2e/SpotService/run_e2e.sh SM-B6`
- `framework/languages/node/e2e/SpotService/run_e2e.sh SM-D5`
- `framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B6`
- `framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D5`
