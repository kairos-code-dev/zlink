<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Runtime Execution](runtime-execution.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[.NET 묶음](../README.ko.md) | [Runtime Lifecycle](runtime-lifecycle.ko.md) | [Runtime Execution](runtime-execution.ko.md) | [Backend Policy](backend-dependency-policy.ko.md) | [공통 E2E](../../common/e2e/README.ko.md)

# ZLink Framework .NET Regression Test Matrix

## 1. 목적

이 문서는 구현이 바뀌더라도 무엇이 깨지면 회귀로 보는지를 테스트 항목 단위로
정리한다. 공개 동작의 의미는 책임 spec이 소유하고, 이 문서는 그 계약을 검증하는
테스트와 release gate만 소유한다.

## 2. CI 계층

회귀 테스트는 다음 세 계층으로 나누어 둔다.

| 계층 | 목적 | 예시 |
|------|------|------|
| `unit` | registration validation, dispatch lookup, option parsing | 중복 등록, builder validation |
| `integration-single-process` | 같은 호스트 안에서 runtime 조합이 정상 동작하는지 확인 | channel request/send, in-memory location store, monitoring attach |
| `integration-multi-process` | 실제 topology[^topology]와 reconnect 동작 확인 | location runtime query, store row 변화, spot peer 변화 |

## 3. 최소 CI 매트릭스

| 항목 | 기준 |
|------|------|
| target framework | `net8.0`, `net10.0` |
| runtime RID[^rid] | `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`, `osx-arm64` |
| test mode | debug, release |

현재 저장소의 기본 빌드(`ZLinkFrameworkTargetFrameworks` 기본값)는 `net8.0` 단일 TFM
이므로, 회귀 테스트는 `net8.0`으로 실행한다. `net10.0`은 아래처럼 회귀 matrix 보고용
multi-target 빌드에서 추가로 다룬다.

- 현재 저장소의 기본 빌드는 `net8.0` 단일 TFM 이다.
- `net10.0` 은 회귀 matrix 보고용 multi-target 빌드에서 추가로 컴파일·실행하는
  형태로 다룬다.

한편 저장소의 `bindings/dotnet/runtimes/` 패키징 대상과
`.github/workflows/build.yml` 이 만들어 내는 native artifact 조합은 위 여섯
runtime RID 를 기준으로 한다. framework CI gate[^ci-gate] 도 같은 범위를
기본으로 본다.

즉 `.NET` framework 회귀 테스트는 특정 OS 하나만 대표로 돌리고 끝내지 않는다.
현재 계획 기준으로 반드시 통과해야 하는 플랫폼은 다음과 같다.

- Windows x64
- Windows ARM64
- Linux x64
- Linux ARM64
- macOS x64
- macOS ARM64

## 4. Channel Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate channel 이름 등록 (`AddClientServerChannel`, `AddFanoutChannel`) | `unit` | startup validation 예외 |
| 같은 channel 이름을 client-server와 fanout 역할로 동시에 등록 | `unit` | startup validation 예외 |
| server 역할에 bind endpoint 없음 | `unit` | startup validation 예외 |
| `AddClientServerChannel(...).EnableClient(endpoint)` | `integration-single-process` | manual request/send 성공 |
| `AddFanoutChannel(...).EnableSubscriber(endpoint)` | `integration-single-process` | manual 기반 subscribe 성공 |
| client 역할에 peer acquisition 경로 없음 | `unit` | startup validation 예외 |
| location store가 있는 역할에 manual endpoint 명시 | `unit` | 명시한 역할은 manual 연결을 사용하고 다른 역할의 자동 연결에는 영향을 주지 않는다 |
| publisher 역할에 bind endpoint 없음 | `unit` | startup validation 예외 |
| publisher 전용 channel | `integration-single-process` | publish submit 성공 |
| subscriber location-store attach | `integration-multi-process` | 원격 publish 수신 |
| handler group mapping | `unit` | `AddZLinkHandlers...()`만으로는 전역 dispatch 대상이 되지 않고, `channel.AddHandlerGroup("...")`로 매핑한 그룹의 handler만 해당 채널에서 dispatch된다 |
| handler exposure 없는 server channel | `unit` | scan 된 handler 가 있어도 `AddHandlerGroup(...)` 또는 `Add...Handler(...)`가 없으면 application handler 가 자동 노출되지 않는다 |
| empty fanout subscriber validation | `unit` | publish handler exposure 없는 fanout subscriber 는 빈 수신자로 허용하지 않고 startup validation 오류다 |
| typed handler registration | `unit` | channel 의 `Add...Handler(...)`로 직접 등록한 handler 는 group mapping 없이도 해당 channel 에 노출된다 |
| channel type handler compatibility | `unit` | client-server 는 send/request, fanout subscriber 는 publish, route mesh 는 route send/request handler 만 허용하고 dealer mesh 는 handler registration 을 노출하지 않는다 |
| incompatible handler group mapping | `unit` | channel type 과 맞지 않는 handler 가 group 안에 섞이면 일부만 제외하지 않고 startup validation 오류로 실패한다 |
| route mesh handler group mapping | `integration-single-process` | route mesh channel 의 `AddHandlerGroup(...)`은 route send/request handler group 을 실제 routed dispatch 대상으로 노출한다 |
| 같은 channel server에 handler 중복 | `unit` | 같은 `kind + packetName` handler가 둘 이상이면 startup validation 예외 |
| 다른 channel server에 같은 packet handler | `integration-single-process` | 같은 `kind + packetName`을 서로 다른 channel에 매핑해도 각 채널이 독립적으로 dispatch된다 |
| 같은 그룹을 여러 채널에 매핑 | `integration-single-process` | 같은 `[ZLinkHandlerGroup("api")]`를 두 채널에 `AddHandlerGroup`으로 노출해도 채널마다 dispatch namespace가 독립이다 |
| `AddHandlerGroup`이 가리키는 그룹 없음 | `unit` | 매핑한 그룹에 handler가 하나도 없으면 startup validation 오류 |
| event handler group mapping | `unit` | `channel.AddHandlerGroup("...")`로 매핑한 그룹의 publish handler만 해당 subscriber channel에서 dispatch된다 |
| HTTP handler에서 `IZLinkChannelClient` 사용 | `integration-single-process` | route handler와 동일한 DI[^di] 컨테이너에서 정상 동작 |
| channel handler에서 `IZLinkChannelClient` 사용 | `integration-single-process` | 일반 request handler가 같은 DI 컨테이너의 `IZLinkChannelClient`로 다른 channel 에 request 하고 reply 를 받는다 |
| channel handler에서 fanout publish | `integration-single-process` | 일반 request handler가 같은 DI 컨테이너의 `IZLinkFanoutClient`로 fanout event 를 publish 하고 subscriber handler가 수신한다 |
| send async submit backpressure[^backpressure] | `integration-single-process` | HWM[^hwm]에 도달해도 caller thread를 block하지 않고, ready 이후에 완료된다 |
| publish async submit backpressure | `integration-single-process` | ROUTER HWM 조건에서 thread를 block하지 않고 `SendTimeout` 정책에 따라 완료 또는 실패 |
| request submit/reply timeout 분리 | `integration-single-process` | request packet의 submit 지연은 `SendTimeout`으로, reply 대기는 `Timeout(...)`으로 판정 |
| pending request 정리 | `unit` | submit 실패, timeout, cancellation, runtime stop이 일어날 때 request sequence가 pending map에서 제거된다 |
| ready callback batch drain | `integration-single-process` | socket이 ready된 뒤 pending send/publish를 batch로 처리하고, 같은 frame을 중복 전송하지 않는다 |
| channel wire multipart[^wire-multipart] | `integration-single-process` | 서버 간 channel send/request/reply가 `header`와 `payload`를 별도 message part로 보내고, handler dispatch는 header part만 보고 packet을 고른다 |
| publish wire multipart | `integration-single-process` | `PUB/SUB` publish도 framework header와 payload를 별도 part로 유지하고, subscriber handler에는 typed payload만 전달된다 |

## 4.1 Dispatch Error Observer Regression 항목

| ID | 계층 | 테스트 위치 | 통과 기준 |
|----|------|-------------|-----------|
| DERR-001, DERR-007, DERR-011, DERR-014 | `unit` | `Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs` | channel request handler 없음은 error reply와 observer event, channel send handler 없음은 drop과 observer event, observer 예외는 원래 dispatch 결과를 깨지 않음 |
| DERR-002, DERR-008 | `unit` | `Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs` | route request handler 없음은 error reply, route send handler 없음은 drop으로 끝나며 observer event가 남음 |
| DERR-003, DERR-004, DERR-009, DERR-010, DERR-016 | `unit` | `Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs` | SPOT route, subscription, actor dispatch 실패가 request면 error reply 또는 caller-visible error, one-way면 drop과 observer event로 끝남 |
| DERR-005, DERR-006, DERR-013, DERR-015 | `unit` | `Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs` | decode 실패와 handler 예외는 error reply 또는 관측 가능한 drop으로 끝나며, observer 미등록 시에도 기본 로그와 metric이 남음 |

## 4.2 DI Capability Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| actor factory without SpotNode | `unit` | actor factory 만 등록하면 startup validation 예외 |
| actor manager without SpotNode | `unit` | SpotNode 없는 구성에서는 `IZLinkActorManager` 가 DI 에 없다 |
| actor manager with SpotNode only | `unit` | SpotNode 만 있고 actor factory 가 없으면 `IZLinkActorManager` 가 DI 에 없다 |
| actor manager with SpotNode and actor factory | `unit` | SpotNode 와 actor factory 가 모두 있으면 `IZLinkActorManager` 가 DI 에 등록된다 |
| Spot service without SpotNode | `unit` | SpotNode 없는 구성에서는 `IZLinkSpotManager` 가 DI 에 없다 |
| Spot service with SpotNode | `unit` | SpotNode 가 있으면 Spot service 가 DI 에 등록된다 |
| Spot publisher without publisher 역할 | `unit` | SpotNode 가 있어도 publisher 역할이 없으면 Spot publisher service 는 DI 에 없다 |
| Spot publisher with publisher 역할 | `unit` | Spot publisher 역할이 있으면 `IZLinkSpotPublisherClient` 가 DI 에 등록된다 |
| bound session factory registration | `unit` | `IZLinkBoundSessionFactory` 는 framework runtime 과 함께 등록된다 |
| Spot handle resolver without SpotNode | `unit` | location store가 있는 서버는 SpotNode 없이 `IZLinkSpotHandleResolver`를 제공할 수 있다. |
| Spot outbound with resolver only | `unit` | Spot ref resolver 만 있고 SpotNode 가 없으면 `IZLinkSpotOutbound` 는 DI 에 없다 |
| route channel missing at call time | `unit` | `IZLinkRouteClient` 호출 시 route channel 이 없으면 `ZLinkConfigurationException` |
| channel client missing at call time | `unit` | `IZLinkChannelClient` 호출 시 channel client 역할이 없으면 `ZLinkConfigurationException` |

## 5. Spot Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate Spot factory type | `unit` | startup validation 예외 |
| duplicate `AddEntrySpot<TEntrySpot>()` | `unit` | 같은 `SpotNode` 안에서 Entry Spot[^entry-spot] registry를 중복 등록하면 startup validation 예외 |
| `AddSpotMesh` 호출 | `integration-single-process` | mesh 빌더 한 호출로 mesh channel, node, spot factory 등록을 한 번에 끝낸다 |
| location store 없이 local-only spot factory | `integration-single-process` | store endpoint 없이 단일 local SpotNode runtime을 시작한다 |
| `CreateAsync<TSpot>()` | `integration-single-process` | `SpotId`, create `State`, create reply 값이 일관되게 유지된다 |
| `CreateAsync<TSpot>()` empty create payload | `integration-single-process` | payload 없는 생성도 빈 `ZLinkMessage`로 `IZLinkSpot.OnCreateAsync(...)`를 한 번 호출한다 |
| `CreateAsync<TSpot>(request)` payload | `integration-single-process` | create request `ZLinkMessage`가 `IZLinkSpot.OnCreateAsync(...)`로 한 번 전달된다 |
| `GetOrCreateAsync<TSpot>(spotRid, request)` existing | `integration-single-process` | 같은 `spotId`가 이미 ready 상태면 `State = Existing`이고 새 `request`는 `OnCreateAsync(...)`로 전달되지 않는다 |
| `GetOrCreateAsync(...)` concurrent create payload | `integration-single-process` | 같은 `spotId` 동시 생성에서는 첫 생성 요청의 `ZLinkMessage`만 `OnCreateAsync(...)`로 전달되고 callback은 한 번만 실행된다 |
| `GetOrCreateAsync<TSpot>(...)` same type | `integration-single-process` | 같은 `spotId`를 같은 Spot 타입으로 다시 확보하면 기존 spot을 반환하고 새 `OnCreateAsync(...)`를 호출하지 않는다 |
| `CreateAsync<TSpot>(request)` create rejected | `integration-single-process` | `OnCreateAsync(...)` reject는 `State = Rejected`와 reply `ZLinkMessage`로 반환되고 spot은 등록되지 않는다 |
| spot create lifecycle failure | `integration-single-process` | `OnCreateAsync(...)` reject는 `State = Rejected`로 반환되고, `OnCreateAsync(...)` 또는 `OnInitializeAsync(...)` 예외는 `SpotCreateFailed`로 전파되며 failed entry는 제거되어 다음 생성 요청이 재시도할 수 있다 |
| `GetAsync(...)`, `ListAsync(...)` | `integration-single-process` | manager 조회 결과가 일관된다 |
| `Configure()` handler registration | `integration-single-process` | `Context.AddPacket(...)`, `Context.AddHandler(...)`, `Context.AddActorPacket(...)`, `Context.AddSubscribe(...)` 등의 등록과 Spot 멤버 lifecycle callback 이 descriptor에 반영된다 |
| Entry Spot handler registration | `integration-single-process` | `AddEntrySpot<TEntrySpot>()`로 등록한 `Context.AddPacket(...)`, `AddSubscribe(...)`, `AddHandler(...)`, `AddActorPacket(...)`, `OnDisconnectActorAsync(...)`와 Entry Spot 멤버 lifecycle callback 이 Entry Spot registry에 반영된다 |
| Entry Spot packet callback concurrency | `integration-single-process` | Entry Spot 일반 packet handler는 user Spot과 같은 등록 표면을 쓰지만 Entry Spot 전체 실행 줄에 직렬화되지 않는다 |
| `OnInitializeAsync(...)` handler resolve | `integration-single-process` | spot마다 분리된 DI scope가 정상 동작한다 |
| `OnClosingAsync(...)` 정상 close callback | `integration-single-process` | `CloseAsync(...)` 호출 시 spot 실행 문맥에서 한 번 호출된다 |
| `IZLinkSpotContext.CloseAsync(...)` self close | `integration-single-process` | timer/handler 실행 중 현재 Spot 종료를 요청하면 현재 callback 이후 close가 진행되고 manager 조회에서 사라진다 |
| `CloseAsync(...)` with joined actors | `integration-single-process` | join된 actor가 남은 user Spot은 close를 거부하고 `false`를 반환한다 |
| local spot publish | `integration-single-process` | subscriber가 정상 수신한다 |
| SPOT timer metadata | `integration-single-process` | timer handler가 callback 번호, 예정/시작 시각, 지연, skip metadata를 받는다 |
| SPOT timer overrun policy | `integration-single-process` | `SkipLateTicks`, `CatchUpBounded`, `DelayNextTick` 정책이 각각 skip, bounded catch-up, fixed-delay 의미를 지킨다 |
| SPOT timer exception policy | `integration-single-process` | handler 예외가 monitoring event로 기록되고, `StopOnUnhandledException`이 켜진 timer는 중단된다 |
| Entry Spot timer execution context | `integration-single-process` | Entry Spot timer 정합성은 actor packet mailbox 작업과 분리해 검증한다. 같은 timer callback은 겹쳐 실행하지 않는다 |
| SPOT timer cancel | `integration-single-process` | `CancelAsync()` 뒤 managed timer loop가 추가 callback을 실행하지 않는다 |
| outbound 전용 외부 publish client | `integration-multi-process` | target SPOT[^spot] channel에 publish가 성공한다 |
| Spot route channel acceptance | `unit` | fanout/dealer mesh/ambiguous/missing router/missing peer source 구성을 startup validation에서 거부한다 |
| Spot route channel transport | `unit` | caller가 명시한 local egress channel이 channel type에 맞는 ROUTER 또는 DEALER socket을 bridge endpoint로 사용해 target Spot으로 routed send/request를 보낸다 |
| route mesh Spot egress target peer 선택 | `unit` | source process가 target route channel 을 local registration 으로 갖지 않아도, 수동 연결과 registry metadata 의 target SpotNode ingress channel / ROUTER `RoutingId`로 route mesh egress target peer 를 선택한다 |
| Spot route egress 역할 validation | `unit` | routed Spot egress 는 client-server client 역할 또는 route mesh transport 에서만 켤 수 있고 fanout/dealer mesh 에서는 startup validation 오류다 |
| spot 종료 후 scope 정리 | `integration-single-process` | 이후 callback이 발생하지 않고 dispose도 정상 완료된다 |
| actor join 이후 dispatch 문맥 | `integration-single-process` | `IZLinkSpotContext.AddHandler(...)`로 등록한 actor handler가 join된 `Spot` 실행 문맥에서 실행된다 |
| Entry Spot actor mailbox dispatch | `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot actor packet이 actor별 입력 순서를 보존하고, 서로 다른 actor handler 시작은 Entry Spot 실행 queue에 막히지 않는다 |
| local actor mailbox dispatch | `integration-single-process` | user Spot에 들어가지 않은 actor packet도 actor별 mailbox 순서를 따른다 |
| user Spot actor dispatch serialization | `integration-single-process` | 같은 user Spot 안의 여러 actor packet이 Spot 실행 queue에서 순서대로 처리되어 Spot 상태가 보호된다 |
| runtime task exception observation | `unit` | detached runtime task와 fire-and-forget handler에서 발생한 예외가 unobserved exception으로 묻히지 않고 runtime error sink 또는 logger로 관찰된다 |
| execution queue cancellation semantics | `unit` | queue enqueue/wait cancellation이 이미 queue에 들어간 work item의 순서를 깨거나 중간에 제거하지 않는다 |
| explicit egress channel Spot route 경로 | `integration-single-process` | routed Spot 호출은 target 정보만으로 egress transport를 고르지 않고, caller가 명시한 local egress channel, egress 설정의 target SpotNode ingress channel, `RoutingId` target으로 routed message를 보낸다 |
| actor manager 생성 중복/타입 충돌 | `integration-single-process` | `IZLinkActorManager.CreateAsync(...)` 중복 생성은 `ActorAlreadyExists`, `GetOrCreateAsync(...)` actor type 충돌은 `ActorTypeMismatch` 로 실패한다 |
| local actor bind 생성 금지 | `integration-single-process` | `BindAsync(...)` 는 local actor 가 없을 때 factory 를 호출하지 않고 `ActorRouteNotFound` 로 실패한다 |
| session actor bind resolver fallback 없음 | `integration-single-process` | `BindAsync(...)` 는 application resolver fallback 없이 logical actor handle 을 등록한다 |
| remote actor dispatch 생성 금지 | `integration-single-process` | routed actor dispatch 수신 경로는 local actor 가 없을 때 factory 를 호출하지 않고 dispatch 를 실패시킨다 |
| session actor relay bridge | `integration-single-process` | `BindAsync(...)` 와 `IZLinkSessionActor.RelayAsync(...)` 가 public session 표면에서 동작한다 |
| session actor explicit disconnect notification | `contract`, `integration-single-process` | session disconnect 는 bound actor 전체에 자동 전파되지 않고, `NotifyDisconnectedAsync(...)` 또는 runtime 명시 호출 시 현재 Spot 의 `OnDisconnectActorAsync(...)` callback 이 호출된다 |
| session actor dispatch ordering | `integration-single-process` | stream session에서 actor로 relay된 packet이 actor별 순서를 보장하고, 현재 actor 위치에 맞는 handler 실행 경로로 넘어간다 |
| actor dispatch location after mailbox wait | `integration-single-process` | 같은 actor의 앞선 packet이 join을 끝낸 뒤, 대기 중이던 다음 packet이 이전 위치가 아니라 새 user Spot 위치로 dispatch된다 |
| session actor dispatch wire multipart | `integration-single-process` | Session 서버와 Play 서버 사이의 actor dispatch가 route header, actor metadata, stream header, payload를 별도 part로 유지하고, payload를 JSON envelope 안의 `byte[]`로 재직렬화하지 않는다 |
| session actor reconnect 재사용 | `integration-single-process` | 같은 actor id가 새 stream session에서 다시 bind되면 기존 actor 인스턴스와 spot membership을 유지하고, session binding token[^binding-token]만 갱신된다 |
| session actor binding rollback | `integration-single-process` | actor-session binding 갱신이 실패하면 helper도 실패하고, local binding table의 같은 token entry도 제거된다 |
| stale session binding token guard | `integration-single-process` | 이전 stream에서 늦게 도착한 unbind나 stale bound session 메시지가 새 binding을 지우거나 사용하지 못한다 |
| Spot route resolver 등록 | `unit` | custom resolver 등록 없이 제거된 registry-backed resolver API가 다시 노출되지 않는다 |
| actor-bound session route 등록 | `integration-single-process` | actor-session route 는 session bind 시 actor runtime state 에 저장된다 |
| location store resolver 대체 | `unit` | custom resolver 를 등록하면 기본 location store resolver 대신 custom resolver 가 DI 에 노출된다 |
| location store Spot RID route | `integration-single-process` | `IZLinkSpotManager.CreateAsync(...)` 으로 만든 Spot 을 resolver 경로로 찾고 종료 후 not found 를 반환한다 |
| stale session unbind guard | `integration-single-process` | 이전 binding token 으로 도착한 disconnect 가 새 actor-session binding 을 지우지 않는다 |
| actor-session store 없이 동작 | `unit` | Bingo 샘플이 actor-session store 없이 actor-bound session 을 사용한다 |
| SessionGateway 변형 없음 | `unit` | TicTacToe SessionGateway 변형이 sample tree 와 solution 에 남아 있지 않다 |
| stale bound session send | `integration-single-process` | 이미 닫힌 stream이나 stale binding으로 향하는 one-way push가 route receive loop와 host shutdown을 실패시키지 않는다 |
| bound session gateway relay | `integration-single-process` | Play 서버에서 Session 서버로 가는 bound session send가 core session relay binding 을 통해 client STREAM에 단일 stream packet으로 도착한다 |
| bound session disconnect local actor | `integration-single-process` | local actor 가 actor id 없이 `IZLinkBoundSession.DisconnectAsync(...)` 를 호출하면 binding 이 정리되고 session disconnect callback 은 다시 호출되지 않는다 |
| bound session disconnect remote actor | `integration-single-process` | remote actor 가 actor id 없이 `IZLinkBoundSession.DisconnectAsync(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다 |
| session context close | `integration-single-process` | `IZLinkSessionContext.CloseAsync()`가 현재 stream client 연결을 서버 쪽에서 끊고, 이어서 disconnect callback으로 연결된다 |
| actor join 직후 packet dispatch | `integration-single-process` | join이 끝난 뒤 들어온 packet이 새 `Spot` 실행 문맥에서 실행된다 |
| actor spot 이동 직후 packet dispatch | `integration-single-process` | 이전 `Spot` 문맥으로 stale dispatch가 발생하지 않는다 |
| spot context channel request 경로 | `unit` | `Spot.Context.Outbound.RequestToChannel(...)`이 현재 Spot 에 설정된 channel egress bridge 경로를 사용한다 |
| spot context routed send/request 표면 | `contract`, `integration-single-process` | `IZLinkSpotOutbound`가 `SendToSpot`, `RequestToSpot`, `Publish`, `SendToChannel`, `RequestToChannel`을 모두 노출하고, `Spot.Context.Outbound.SendToSpot(...)` / `RequestToSpot(...)`이 route transport를 사용한다 |
| actor bound session send API | `integration-single-process` | actor는 `Context.BoundSession.Send(...)`로 client stream에 push하고, `IZLinkStream`을 직접 노출받지 않는다 |
| actor request handler reply | `unit` | actor request packet은 actor request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| Spot actor request handler reply | `unit` | Entry Spot/user Spot actor request packet은 request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| local actor request relay reply | `integration-single-process` | local session actor relay도 actor request handler 반환값으로 stream response를 작성한다 |
| actor reply public API 표면 없음 | `unit` | actor context Reply와 actor stream client 계약이 public API 표면에 다시 노출되지 않는다 |

## 6. Stream Regression 항목

별도 client package의 정확한 lifecycle, dispatch, transport와 observer 계약은
[.NET Stream Connector 공개 계약](../../spec/stream-connector/languages/dotnet/03-stream-connector.ko.md)을
따른다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| 같은 node에 session 중복 등록 | `unit` | startup validation 예외 |
| header session node | `integration-single-process` | `OnDispatchAsync(...)` 호출 확인 |
| `OnConnectedAsync(...)` | `integration-multi-process` | `ConnectionReady` 이후 1회 호출 |
| `OnErrorAsync(...)` 범위 | `integration-multi-process` | transport error만 session callback으로 전달된다 |
| peer metadata 표면 | `integration-single-process` | `SessionId`, `RoutingId`, `LocalAddr`, `RemoteAddr` 값 확인 |
| session callback task dispatch | `integration-single-process` | transport callback에서 user callback을 직접 호출하지 않고, managed task 경로로 호출한다 |
| session callback 직렬성 | `integration-single-process` | stream socket이 보존한 같은 session frame 순서대로 lifecycle/packet callback이 직렬 실행되며, 서로 병렬로 겹치지 않는다 |
| session callback 직접 호출 우회 방지 | `unit` | runtime 내부 transport 진입점은 enqueue API만 사용한다 |
| handler `ValueTask<T>` 결과 await | `unit` | handler invoker가 generic `ValueTask<T>`를 실제 결과 값으로 변환하고, 값 타입 boxing 오류를 내지 않는다 |
| abstract wire payload validation | `unit` | converter 없는 abstract/interface payload가 node 경계 DTO에 포함되면 등록 시점 또는 첫 submit 직전에 configuration 오류로 실패한다 |

## 7. Location / Monitoring Regression 항목

routing id 자동 할당의 정확한 `.NET` public interface와 lifecycle 증거는
[routing id 자동 할당 공개 계약](../../spec/server/languages/dotnet/04-routing-id-allocation.ko.md)을
따른다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| location store 시작 순서 | `integration-single-process` | framework runtime 이 location store 등록 뒤 자동 연결을 시작한다 |
| 원격 query client | `integration-multi-process` | location topology snapshot 조회가 성공한다 |
| socket/Spot monitoring source 이름 불일치 | `unit` | startup validation 예외 |
| location monitoring runtime 누락 | `unit` | location source를 등록했지만 location runtime이 없으면 startup validation 예외 |
| registry polling diff | `integration-multi-process` | topology, status, service summary event가 발생한다 |
| spot polling diff | `integration-multi-process` | status, peers, subjects event가 발생한다 |

## 8. Release Gate

릴리스로 보내려면 다음 다섯 가지를 모두 만족해야 한다.

1. `unit`, `integration-single-process`, `integration-multi-process` 전부 통과
2. `net8.0`, `net10.0` 양쪽 모두 통과
3. 위 여섯 runtime RID 전체에서 CI gate 통과
4. happy-path 샘플과 대표 failure-path가 각각 한 번 이상 커버되어 있음
5. 책임 spec에 정의한 비허용 조합이 모두 테스트로 고정되어 있음

즉 샘플이 한 번 실행되는 것만으로는 충분하지 않다. startup validation 과
runtime failure 의미까지 테스트로 같이 고정되어 있어야 한다.

또한 native backend 가 이미 해당 플랫폼을 지원하더라도, framework 는 그 위에
registration, lifecycle, DI, monitoring 계층을 더 쌓는다. 그래서 플랫폼 gate 는
backend gate 와 별도로 유지한다.

## 9. Explicit turn terminator regression

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `SerialExecutorTests.SerialExecutionQueue_DefaultAwait_Holds_Gate_Until_Work_Completes` | 일반 callback은 완료될 때까지 같은 실행 줄의 다음 작업을 시작하지 않는다. |
| `WorkerPoolTests.RunCpuWorker_Async_Holds_Serial_Turn_Until_Work_Completes` | `Async(...)`를 기다리는 동안 Spot turn을 유지하여 다음 callback이 시작되지 않는다. |
| `WorkerPoolTests.RunCpuWorker_Yield_Releases_And_Resumes_Through_Serial_Turn` | `Yield(...)`가 Spot turn을 반납하고 완료 continuation을 같은 실행 줄의 큐에서 재개한다. |
| `SerialExecutorTests.SerialExecutionQueue_AutomaticTurn_Fault_Cleans_Pending_Turn` | `Yield(...)` 대상의 실패 뒤 pending turn을 정리하고 실행 줄을 계속 사용할 수 있다. |
| `SerialExecutorTests.SerialExecutionQueue_AutomaticTurn_Cancellation_Cleans_Pending_Turn` | `Yield(...)` 대상의 취소 뒤 pending turn을 정리하고 다음 작업을 실행한다. |
| `E2E:ATD-B3` | actor join의 `Yield(...)`를 기다리는 동안 다른 actor 요청이 먼저 완료되고 continuation이 원래 actor mailbox로 돌아온다. |
| `E2E:ATD-A4` | worker의 `Yield(...)`를 기다리는 동안 Spot turn을 반납하고 continuation이 원래 Spot 실행 줄에서 재개된다. |

## 10. 문서별 회귀 테스트 단락

이 디렉토리의 각 계약·sample·internals 문서는, 자기 항목이 어떤 테스트로 고정되어 있는지 짧은
`회귀 테스트` 단락을 갖고 있어야 한다. 중앙 matrix 만 갱신해서는 곤란하다.
세부 문서의 독자가 어떤 테스트를 봐야 하는지 놓치기 쉽기 때문이다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegressionTests.DotNetContractDocuments_AllExposeRegressionTestSection` | 아래 문서가 모두 `회귀 테스트` 단락을 가진다. |
| `RegressionTests.DotNetRegressionMatrix_References_AllContractDocuments` | 이 matrix가 아래 문서 파일명을 모두 참조한다. |

대상 문서는 다음과 같다.

- `README.ko.md`
- `01-system-structure.ko.md`
- `02-handler-interfaces.ko.md`
- `03-stream-connector.ko.md`
- `04-routing-id-allocation.ko.md`
- `05-route-mesh.ko.md`
- `06-location-store.ko.md`
- `dotnet-http-client.ko.md`
- `regression-test-matrix.ko.md`
- `runtime-lifecycle.ko.md`
- `runtime-execution.ko.md`
- `backend-dependency-policy.ko.md`

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^regression]: regression(회귀) 은 이전 버전에서 잘 동작하던 기능이 새 변경 때문에 다시 깨지는 현상을 가리킨다. regression test 는 그런 일을 막기 위해 항상 돌리는 테스트 묶음이다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^rid]: RID(Runtime Identifier) 는 `.NET` 이 OS·CPU 조합을 식별하는 문자열이다. 예: `win-x64`, `linux-arm64`.
[^ci-gate]: CI gate 는 새 변경을 머지하거나 배포하기 전에 통과해야 하는 자동 검증 단계(빌드, 테스트 등)의 묶음을 가리킨다.
[^di]: DI(Dependency Injection) 는 객체가 필요한 의존성을 직접 만들지 않고 외부 컨테이너에서 주입받도록 하는 패턴이다. `.NET` 에서는 `IServiceCollection`/`IServiceProvider` 기반 컨테이너가 표준이다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^hwm]: HWM(High Water Mark) 은 송신 큐에 쌓을 수 있는 최대 메시지 수를 가리키며, 이 한계에 도달하면 backpressure 가 발동한다.
[^wire-multipart]: wire multipart 는 한 논리 메시지를 header, payload 등 여러 message part 로 나누어 전송하는 방식이다. 한쪽만 떼어 살펴봐도 라우팅이 가능해진다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^binding-token]: session binding token 은 actor 와 stream session 의 연결 상태를 식별하는 토큰으로, 재연결 시 어느 binding 이 최신인지 구분하는 데 쓰인다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Runtime Execution](runtime-execution.ko.md) | [다음: Backend Dependency Policy](backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:bottom:end -->

## 11. 공개 계약 문서에서 이관한 회귀 테스트 항목

언어별 spec을 3문서(시스템 구조 · 인터페이스 · connector)로 압축하면서, 삭제한 기능별 계약
문서가 소유하던 회귀 항목을 이 절로 옮겼다. 계약의 의미는 공통 스펙이 소유한다.

### Channel

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ChannelsTests.AddZLinkFramework_Throws_WhenChannelNameIsDuplicated` | 같은 channel 이름을 중복 등록하면 startup validation 예외가 난다. |
| `ChannelsTests.AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath` | client 역할에 자동 연결(store)이나 수동 연결이 없으면 시작 전에 실패한다. |
| `E2E:RM-A2` | 수동 endpoint 연결 경로에서 client request marker를 검증한다. |
| `E2E:RM-C1` | client/server request와 send가 실제 프로세스 사이에서 모두 처리된다. |
| `E2E:RM-A1` | store 자동 연결 기반 client가 request를 실제 다중 프로세스에서 처리한다. |
| `ZLinkAsyncSubmitterTests.Async_DrainsPendingItemFromReadyCallback` | async submitter가 ready callback에서 pending item을 비우고 중복 전송하지 않는다. |

### SPOT

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes` | 같은 Spot factory 타입을 중복 등록하면 startup validation 예외가 난다. |
| `NodesAndServicesTests.AddZLinkFramework_AllowsStandaloneLocalSpotNode` | location store 없이도 local-only SpotNode 구성은 시작할 수 있다. |
| `E2E:SM-A6` | Spot initialize와 명시적 close lifecycle이 실제 runtime에서 한 번씩 완료된다. |
| `E2E:SM-E2` | Spot timer tick이 등록된 handler에 전달된다. |
| `E2E:SM-E3` | idle timer가 Spot을 닫고 이후 요청이 실패해 timer와 lifecycle 종료를 함께 검증한다. |
| `E2E:SM-E4` | timer overrun 정책이 늦은 tick을 계약에 맞게 제한한다. |
| `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures` | handler 예외가 계속 실행되는 실패와 timer 중단 실패로 구분되어 monitoring event에 반영된다. |
| `E2E:SM-C4` | 외부 Spot publisher client가 target SPOT channel로 publish한다. |
| `E2E:SM-B7` | actor 생성, join과 packet dispatch가 현재 Spot lifecycle 순서로 실행된다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot actor packet은 같은 actor 순서를 보존하지만, 서로 다른 actor는 Entry Spot 직렬 실행 줄 때문에 시작이 막히지 않는다. native actor readable 경로와 같은 actor dispatch 경계에서도 actor별 mailbox 가 실행 순서의 기준이다. |
| `E2E:ATD-C2` | 같은 timer의 다음 tick은 이전 callback의 continuation과 완료 뒤 실행되어 재진입하지 않는다. |

### Actor

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| --- | --- |
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenActorFactoryNameIsDuplicated` | actor factory 이름이 중복되면 startup validation에서 예외로 막는다. |
| `E2E:SM-B7` | actor 생성, user Spot join과 actor packet dispatch 순서가 실제 노드에서 이어진다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot actor packet은 actor별 mailbox 순서를 따르며, 서로 다른 actor handler 시작은 Entry Spot 직렬 실행 줄에 막히지 않는다. |
| `E2E:SM-G2` | logical owner 변경 뒤 actor packet이 새 owner에서만 처리되어 이전 Spot으로 dispatch되지 않는다. |
| `E2E:SM-D2` | stream session에서 원격 bound actor로 request가 전달되고 reply가 같은 session으로 돌아온다. |
| `E2E:SM-D1` | local actor bind와 relay가 request/reply를 같은 session에서 완료한다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_NoBindRequest_RepliesViaNoBind_AndDoesNotBindSession` | Entry Spot actor request가 request handler 결과로 응답하고 send handler나 session bind 경로로 바뀌지 않는다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_ActorReply_And_StreamClientContracts` | actor context Reply 와 actor stream client 계약이 public API 표면에 다시 노출되지 않는다. |

### STREAM

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions` | 같은 node에 session을 중복 등록하면 startup validation 예외가 발생한다. |
| `StreamSessionForcedCleanupTests.Stream_node_preserves_typed_routing_id_from_backend_callback` | transport callback에서 받은 typed routing id가 session dispatch까지 정보 손실 없이 전달된다. |
| `E2E:SM-D7` | stream 인증과 dispatch가 실제 connector와 session node 사이에서 완료된다. |
| `E2E:SM-D8` | stream 종료로 pending request가 실패하고 새 session의 인증과 bind 뒤 messaging이 재개된다. |

### Monitoring

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `CoverageCriticalRuntimeTests.MonitoringEventMapper_MapsAndFiltersSocketEvents` | socket runtime event 를 public monitoring event 로 매핑하고 내부 event 는 밖으로 내보내지 않는다. |
| `CoverageCriticalRuntimeTests.SpotTimerFailureEventFactory_MapsStoppedAndContinuingFailures` | timer handler 예외가 계속 실행되는 실패와 timer 중단 실패를 구분해 typed event 로 만들어진다. |
| `MonitoringTests.AddZLinkMonitoring_RequiresPositivePollingIntervals` | Spot과 location runtime polling interval이 0보다 커야 한다. |
| `MonitoringTests.MonitoringSourceValidator_RequiresLocationRuntimeForLocationSources` | location source를 등록했지만 location runtime이 없으면 시작 전에 거부한다. |
| `MonitoringTests.AddZLinkMonitoring_Throws_WhenSpotSourceDoesNotMatchRegisteredSpotNode` | Spot source 이름이 등록된 SpotNode 이름과 정확히 일치하지 않으면 시작 전에 거부한다. |
| `MonitoringTests.AddZLinkMonitoring_UsesExplicitSpotSourceWithoutAutoDiscovery` | Spot source는 자동으로 추가되지 않으며 명시한 SpotNode 이름만 등록한다. |
| `MonitoringTests.SpotPollingEventDiff_EmitsSealedVariantsOnlyForChangedSnapshotParts` | 최초 snapshot과 후속 차이를 sealed Spot event variant로 정확히 발행한다. |
| 공통 개념 | `.NET` 타입 / 멤버 |
| 로그 모드 | `ZLinkMessageFlowLogMode` { `Off`, `ErrorsOnly`(기본), `KeyTransitions`, `Verbose`, `Diagnostic` } |
| outcome | `ZLinkMessageFlowOutcome` { `Received`, `Dispatched`, `Replied`, `Dropped`, `Sent`, `ReplyReceived`, `Error` } |
| event | `ZLinkMessageFlowEvent`(record): `Outcome`, `Surface`, `MessageKind`, `PacketName`, `ChannelName`, `Topic`, `CorrelationId`, `SourceRid`, `LocalRid`, `PeerRid`, `SocketRole`, `SpotRid`, `ActorId`, `MessageSize`, `FlowId`, nullable `FlowOrigin`, 오류 필드 |
| observer | `IZLinkMessageFlowObserver.OnMessageFlowAsync(ZLinkMessageFlowEvent, CancellationToken)` |
| 진단 옵션(read-only) | `IZLinkDispatchOptions.Diagnostics` → `IZLinkDiagnosticsOptions` { `MessageFlow`, `EffectiveMessageFlow`, `SampleRate`, `IncludeMessageSizes`, `LogFile`, `Label` } |
| 런타임 토글 | `IZLinkMessageFlowControl.SetMessageFlowMode(...)` / `MessageFlowMode` (DI singleton) |
| 공통 개념 | `.NET` |
| meter 이름(상수) | `ZLinkMeters.Framework` = `"zlink.framework"` (meter/scope 이름은 언어 간 바이트 동일, 공통 §11) |
| 계기 방출 | `System.Diagnostics.Metrics.Meter("zlink.framework")` — `Counter`/`UpDownCounter`/`ObservableGauge`/`Histogram` |
| 앱 연결(공통 케이스) | OTel `MeterProviderBuilder.AddMeter(ZLinkMeters.Framework)` — 이게 전부다 |
| 비-OTel/테스트 수집 | .NET 표준 `MeterListener`가 `ZLinkMeters.Framework`를 직접 구독 — zlink 전용 listener interface 없음 |
| 공통 개념 | `.NET` |
| 생성 gate | 기존 `MessageFlow` mode가 `Off`가 아니면 create-if-absent 자동 생성 |
| event 필드(추가) | `string ZLinkMessageFlowEvent.FlowId`, `ZLinkFlowOrigin? ZLinkMessageFlowEvent.FlowOrigin` — dispatch 오류 이벤트에도 동일 |
| 공통 개념 | `.NET` |
| 자동 drain(기본) | framework hosted service가 `IHostApplicationLifetime` 종료에 참여, `StopAsync`에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `UseDrainPolicy(ZLinkSpotDrainPolicy.{DrainNatural(기본)/ReleaseAndRecreate})` |
| terminal result | abstract `ZLinkDrainResult` + sealed `Drained`, `ForceStopped(ZLinkDrainForceReason Reason)`; reason은 `DeadlineExceeded`, `DrainingStatePublishFailed`, `OwnerCleanupFailed`, `TeardownFailed` |
| 명시 제어(선택) | `IZLinkDrainControl` { `ValueTask<ZLinkDrainResult> DrainAsync(TimeSpan deadline, CancellationToken)`, `DrainAsync(CancellationToken)`(30초), `AwaitDrainedAsync(CancellationToken)`, `bool IsReady { get; }` } (DI singleton) |
| readiness probe | `IZLinkDrainControl.IsReady` 또는 `IHealthChecksBuilder.AddZLinkDrainHealthCheck()` |
| 상태 관측 | 기존 `IZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.State` { `Serving`/`Draining`/`Drained`/`ForceStopping` }, `SourceName` = 고정값 `"drain"` |

### Session actor dispatch

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:SM-D2` | session callback에서 원격 actor request를 relay하고 reply를 같은 stream session으로 되돌린다. |
| `E2E:SM-D5` | client close와 명시적 actor disconnect notification의 차이를 실제 lifecycle marker로 검증한다. |
| `E2E:SM-D1` | local actor binding과 relay가 별도 원격 route fallback 없이 동작한다. |
| `E2E:SM-D6` | bound session push가 지정한 client session에만 도달한다. |
| `EntrySpotActorDispatchTests.EntrySpotActorDispatch_ConcurrentActors_StartsOutsideEntrySpotSerialLine_AndKeepsSameActorOrdering` | Entry Spot actor packet은 같은 actor 순서를 보존하고, 서로 다른 actor handler 시작은 Entry Spot 직렬 실행 줄에 막히지 않는다. |
| `SerialExecutorTests.ActorDispatchMailbox_Runs_Waiters_In_Fifo_Order` | user Spot에 들어가지 않은 actor packet도 actor별 mailbox 등록 순서를 따른다. |
| `E2E:SM-G2` | actor owner가 바뀐 뒤 대기 중이던 요청도 새 owner에서 처리되고 이전 위치로 전달되지 않는다. |
| `E2E:SM-B7` | actor join 이후 packet dispatch가 현재 Spot lifecycle 순서로 실행된다. |
| `E2E:SM-D8` | 이전 stream 종료 뒤 새 session에서 재인증·재bind하고 messaging을 재개한다. |
| `StreamSessionForcedCleanupTests.Rejected_terminal_work_starts_disposal_and_releases_the_session_scope` | session 종료 작업이 queue에서 거절되어도 stream close와 session scope 정리가 완료된다. |
| `SerialExecutorTests.StreamSessionSerialExecutor_Continues_After_Work_Exception` | session queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_Continues_After_Queued_Work_Exception` | Spot queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_ExecuteAsync_Propagates_Work_Exception` | Spot queue에서 완료를 기다리는 실행 경로는 handler 예외를 호출자에게 그대로 돌려준다. |
| `SerialExecutorTests.SerialExecutionQueue_RunAsync_Propagates_Work_Exception` | 공통 serial queue의 `RunAsync(...)`가 work 예외를 error sink에 기록하면서 호출자에게도 전파한다. |
| `SerialExecutorTests.SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work` | 공통 serial queue에서 completion wait가 취소되더라도 이미 queue에 들어간 work item은 제거되지 않는다. |
| `SerialExecutorTests.ActorDispatchCancellation_Does_Not_Stop_Current_Or_Later_Dispatch` | actor dispatch 대기를 취소해도 현재 실행 중인 dispatch나 이후 dispatch가 중단되지 않는다. |
| `RegressionTests.DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards` | 중앙 regression matrix가 실행 직렬화 관련 회귀 항목을 유지한다. |

### SpotNode

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:SM-A1` | Entry Spot routing id를 사용한 request가 실제 Entry Spot handler에 도달한다. |
| `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` | 제거된 route 계약이 public API 표면으로 다시 노출되지 않고 actor/session 계약은 유지된다. |

### Stage wrapper

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:SM-B7` | actor join 뒤 stage 역할의 Spot에서 packet이 lifecycle 순서에 맞게 처리된다. |
| `E2E:SM-E3` | stage tick으로 쓰는 timer가 Spot 종료 뒤 추가 callback을 만들지 않는다. |
| `E2E:SM-A5` | application stage wrapper가 Spot request, timer와 lifecycle을 public API로 실행한다. |

### Location

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `NodesAndServicesTests.AddZLinkFramework_AddLocationStores_ResolvesEveryStoreRoleToOneInstance` | 하나의 store 인스턴스가 모든 location 역할로 등록된다. |
| `NodesAndServicesTests.AddZLinkFramework_Throws_WhenAddLocationStoreIsCombinedWithInMemoryStore` | 외부 store와 in-memory store를 함께 등록하면 시작 전에 실패한다. |
| `LocationResolverTests.Rows_Of_Expired_Owner_Are_Not_Returned` | owner lease가 만료된 위치 row를 resolver가 반환하지 않는다. |
| `AutoConnectReconcilerTests.Reconcile_Connects_New_Targets_And_Disconnects_Vanished_Ones` | 자동 연결이 새 peer를 연결하고 사라진 peer를 연결 집합에서 제거한다. |
| `RedisInMemoryParityTests.Same_Operation_Sequence_Yields_Identical_Statuses_And_Generations` | in-memory와 Redis 구현이 같은 write status와 generation을 반환한다. |
| `E2E:RM-A1`, `E2E:RM-A4`, `E2E:RM-B1`, `E2E:RM-B2` | store 기반 자동 연결, failover, scale-out과 scale-in을 실제 프로세스에서 검증한다. |
| `E2E:SF-B1`, `E2E:SF-B2`, `E2E:SF-C1`, `E2E:SF-C2`, `E2E:SF-D3` | store 장애 중 연결 유지, 복구, owner lease 만료와 정상 종료 정리를 검증한다. |
| `RegressionTests.DotNet_Samples_Do_Not_Use_Legacy_Registry_Discovery` | .NET sample 이 제거된 Registry/Discovery API 를 다시 사용하지 않는다 |
