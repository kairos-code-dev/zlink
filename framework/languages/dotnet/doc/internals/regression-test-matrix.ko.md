<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework .NET Lifecycle And Failure Semantics](./lifecycle-and-failure-semantics.ko.md) | [다음: ZLink Framework .NET Implementation Scope And Non-Goals](./implementation-scope-and-nongoals.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md) | [use case validation](../../../../doc/spec/usecase-validation.ko.md)

# ZLink Framework .NET Regression Test Matrix

## 1. 목적

use case validation 문서는 설계 설명이 어디까지 닿아 있는지를 보는 문서다.
반면 이 문서는 결이 다르다. 구현이 바뀌더라도 "무엇이 깨지면 회귀로 본다"는
기준을 테스트 항목 단위로 못 박는 데 목적이 있다.

## 2. CI 계층

회귀 테스트는 다음 세 계층으로 나누어 둔다.

| 계층 | 목적 | 예시 |
|------|------|------|
| `unit` | registration validation, dispatch lookup, option parsing | 중복 등록, builder validation |
| `integration-single-process` | 같은 호스트 안에서 runtime 조합이 정상 동작하는지 확인 | channel request/send, embedded registry, monitoring attach |
| `integration-multi-process` | 실제 topology[^topology]와 reconnect 동작 확인 | 원격 registry query, discovery 변화, spot peer 변화 |

## 3. 최소 CI 매트릭스

| 항목 | 기준 |
|------|------|
| target framework | `net8.0`, `net10.0` |
| runtime RID[^rid] | `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`, `osx-arm64` |
| test mode | debug, release |

최소 지원 버전이 `net8.0` 이므로, 회귀 테스트도 위 두 target framework 를
함께 돌려야 한다.

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
| 같은 channel 이름을 client-server와 fanout capability로 동시에 등록 | `unit` | startup validation 예외 |
| server capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| `AddClientServerChannel(...).EnableClient(c => c.UseManualConnections(...))` | `integration-single-process` | manual request/send 성공 |
| `AddClientServerChannel(...).EnableClient(...)` + 전역 `UseDiscovery(...)` | `integration-single-process` | discovery 기반 request/send 성공 |
| `AddFanoutChannel(...).EnableSubscriber(s => s.UseManualConnections(...))` | `integration-single-process` | manual 기반 subscribe 성공 |
| client capability에 peer acquisition 경로 없음 | `unit` | startup validation 예외 |
| 같은 capability에서 discovery/manual 혼용 | `unit` | startup validation 예외 |
| publisher capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| publisher 전용 channel | `integration-single-process` | publish submit 성공 |
| subscriber discovery attach | `integration-multi-process` | 원격 publish 수신 |
| handler group mapping | `unit` | `AddZLinkHandlers...()`만으로는 전역 dispatch 대상이 되지 않고, `channel.MapHandlerGroup("...")`로 매핑한 그룹의 handler만 해당 채널에서 dispatch된다 |
| 같은 channel server에 handler 중복 | `unit` | 같은 `kind + packetName` handler가 둘 이상이면 startup validation 예외 |
| 다른 channel server에 같은 packet handler | `integration-single-process` | 같은 `kind + packetName`을 서로 다른 channel에 매핑해도 각 채널이 독립적으로 dispatch된다 |
| 같은 그룹을 여러 채널에 매핑 | `integration-single-process` | 같은 `[ZLinkHandlerGroup("api")]`를 두 채널에 `MapHandlerGroup`으로 노출해도 채널마다 dispatch namespace가 독립이다 |
| `MapHandlerGroup`이 가리키는 그룹 없음 | `unit` | 매핑한 그룹에 handler가 하나도 없으면 startup validation 경고 또는 오류 |
| event handler group mapping | `unit` | `channel.MapHandlerGroup("...")`로 매핑한 그룹의 publish handler만 해당 subscriber channel에서 dispatch된다 |
| HTTP handler에서 `IZLinkClient` 사용 | `integration-single-process` | route handler와 동일한 DI[^di] 컨테이너에서 정상 동작 |
| send async submit backpressure[^backpressure] | `integration-single-process` | HWM[^hwm]에 도달해도 caller thread를 block하지 않고, ready 이후에 완료된다 |
| publish async submit backpressure | `integration-single-process` | `NoDrop` 또는 HWM 조건에서 thread를 block하지 않고 `SendTimeout` 정책에 따라 완료 또는 실패 |
| request submit/reply timeout 분리 | `integration-single-process` | request packet의 submit 지연은 `SendTimeout`으로, reply 대기는 `Timeout(...)`으로 판정 |
| pending request 정리 | `unit` | submit 실패, timeout, cancellation, runtime stop이 일어날 때 request sequence가 pending map에서 제거된다 |
| ready callback batch drain | `integration-single-process` | socket이 ready된 뒤 pending send/publish를 batch로 처리하고, 같은 frame을 중복 전송하지 않는다 |
| channel wire multipart[^wire-multipart] | `integration-single-process` | 서버 간 channel send/request/reply가 `header`와 `payload`를 별도 message part로 보내고, handler dispatch는 header part만 보고 packet을 고른다 |
| publish wire multipart | `integration-single-process` | `PUB/SUB` publish도 framework header와 payload를 별도 part로 유지하고, subscriber handler에는 typed payload만 전달된다 |

## 4.1 DI Capability Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| actor factory without SpotNode | `unit` | actor factory 만 등록하면 startup validation 예외 |
| actor manager without SpotNode | `unit` | SpotNode 없는 구성에서는 `IZLinkActorManager` 가 DI 에 없다 |
| actor manager with SpotNode only | `unit` | SpotNode 만 있고 actor factory 가 없으면 `IZLinkActorManager` 가 DI 에 없다 |
| actor manager with SpotNode and actor factory | `unit` | SpotNode 와 actor factory 가 모두 있으면 `IZLinkActorManager` 가 DI 에 등록된다 |
| Spot service without SpotNode | `unit` | SpotNode 없는 구성에서는 `IZLinkSpotManager`, `IZLinkSpotClient`, `IZLinkSpotConnectionManager` 가 DI 에 없다 |
| Spot service with SpotNode | `unit` | SpotNode 가 있으면 Spot service 가 DI 에 등록된다 |
| Spot publisher without publisher capability | `unit` | SpotNode 가 있어도 publisher capability 가 없으면 Spot publisher service 는 DI 에 없다 |
| Spot publisher with publisher capability | `unit` | Spot publisher capability 가 있으면 `IZLinkSpotPublisherClient` 와 `IZLinkSpotMeshPublisherClient` 가 DI 에 등록된다 |
| session proxy without binding store | `unit` | binding store 없이는 `IZLinkSessionProxyFactory`, `IZLinkActorSessionClient` 가 DI 에 없다 |
| binding store without route mesh | `unit` | binding store 가 있지만 route mesh channel 이 없으면 startup validation 예외 |
| Spot route resolver without SpotNode | `unit` | route 정보만 제공하는 서버는 SpotNode 없이 `IZLinkSpotRouteResolver` 를 등록할 수 있다 |
| Spot client with resolver only | `unit` | Spot route resolver 만 있고 SpotNode 가 없으면 `IZLinkSpotClient` 는 DI 에 없다 |
| route channel missing at call time | `unit` | `IZLinkRouteClient` 호출 시 route channel 이 없으면 `ZLinkConfigurationException` |
| channel client missing at call time | `unit` | `IZLinkClient` 호출 시 channel client capability 가 없으면 `ZLinkConfigurationException` |

## 5. Spot Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate `spotName` factory | `unit` | startup validation 예외 |
| duplicate `AddEntrySpot<TEntrySpot>()` | `unit` | 같은 `SpotNode` 안에서 Entry Spot[^entry-spot] registry를 중복 등록하면 startup validation 예외 |
| `AddSpotMesh(...)`에 `UseDiscovery(...)` 없음 | `unit` | discovery 기반 mesh를 만들 수 없으므로 startup validation 예외 |
| `AddSpotMesh(channel, configureMesh)` | `integration-single-process` | mesh 빌더 한 호출로 discovery, node, spot factory 등록을 한 번에 끝낸다 |
| standalone `AddSpotNode(...)` + local-only spot factory | `integration-single-process` | discovery mesh 없이 단일 local SpotNode runtime을 시작한다 |
| `AddSpotNode(...)` + 별도 `UseSpotDiscovery(channel, ...)` 분리 호출 | `integration-single-process` | 호환 경로로만 유지하며, 새 샘플은 `AddSpotMesh(...)`를 사용한다 |
| `CreateAsync(spotName)` | `integration-single-process` | `SpotId`, `SpotName`, `Created` 값이 일관되게 유지된다 |
| `CreateAsync(spotName)` empty create payload | `integration-single-process` | payload 없는 생성도 빈 multipart payload로 `IZLinkSpot.OnCreateAsync(...)`를 한 번 호출한다 |
| `CreateAsync(spotName, createParts)` multipart | `integration-single-process` | create payload part 수와 순서가 유지되어 `IZLinkSpot.OnCreateAsync(...)`로 한 번 전달된다 |
| `GetOrCreateAsync(spotName, spotId, createParts)` existing | `integration-single-process` | 같은 `spotId`가 이미 ready 상태면 `Created = false`이고 새 `createParts`는 `OnCreateAsync(...)`로 전달되지 않는다 |
| `GetOrCreateAsync(...)` concurrent create payload | `integration-single-process` | 같은 `spotId` 동시 생성에서는 첫 생성 요청의 multipart payload만 `OnCreateAsync(...)`로 전달되고 callback은 한 번만 실행된다 |
| `GetOrCreateAsync(...)` spotName mismatch | `integration-single-process` | 같은 `spotId`에 서로 다른 `spotName`을 사용하면 `SpotTypeMismatch`로 실패하고 새 `OnCreateAsync(...)`를 호출하지 않는다 |
| spot create lifecycle failure | `integration-single-process` | `OnCreateAsync(...)` 또는 `OnInitializeAsync(...)` 실패는 `SpotCreateFailed`로 전파되고 failed entry는 제거되어 다음 생성 요청이 재시도할 수 있다 |
| `GetAsync(...)`, `ListAsync(...)` | `integration-single-process` | manager 조회 결과가 일관된다 |
| `Configure()` handler registration | `integration-single-process` | `Context.AddPacket(...)`, `Context.AddActorPacket(...)`, `Context.AddActorJoined(...)`, `Context.AddActorLeft(...)`, `Context.AddSubscribe(...)`, `Context.AddActorJoin(...)` 등의 등록이 descriptor에 반영된다 |
| Entry Spot handler registration | `integration-single-process` | `AddEntrySpot<TEntrySpot>()`로 등록한 `Context.AddPacket(...)`, `AddSubscribe(...)`, `AddActorPacket(...)`, `AddActorJoined(...)`, `AddActorLeft(...)`가 Entry Spot registry에 반영된다 |
| Entry Spot packet callback concurrency | `integration-single-process` | Entry Spot 일반 packet handler는 user Spot과 같은 등록 표면을 쓰지만 Entry Spot 전체 실행 줄에 직렬화되지 않는다 |
| `OnInitializeAsync(...)` handler resolve | `integration-single-process` | spot마다 분리된 DI scope가 정상 동작한다 |
| `OnClosingAsync(...)` 정상 remove callback | `integration-single-process` | `RemoveAsync(...)` 호출 시 spot 실행 문맥에서 한 번 호출된다 |
| local spot publish | `integration-single-process` | subscriber가 정상 수신한다 |
| SPOT timer metadata | `integration-single-process` | timer handler가 callback 번호, 예정/시작 시각, 지연, skip metadata를 받는다 |
| SPOT timer overrun policy | `integration-single-process` | `SkipLateTicks`, `CatchUpBounded`, `DelayNextTick` 정책이 각각 skip, bounded catch-up, fixed-delay 의미를 지킨다 |
| SPOT timer exception policy | `integration-single-process` | handler 예외가 monitoring event로 기록되고, `StopOnUnhandledException`이 켜진 timer는 중단된다 |
| Entry Spot timer execution context | `integration-single-process` | Entry Spot timer는 전체 Entry Spot callback을 전역으로 막지 않고, 같은 timer callback은 겹쳐 실행하지 않는다 |
| SPOT timer cancel | `integration-single-process` | `CancelAsync()` 뒤 managed timer loop가 추가 callback을 실행하지 않는다 |
| outbound 전용 외부 publish client | `integration-multi-process` | target SPOT[^spot] channel에 publish가 성공한다 |
| spot 제거 후 scope 정리 | `integration-single-process` | 이후 callback이 발생하지 않고 dispose도 정상 완료된다 |
| actor join 이후 dispatch 문맥 | `integration-single-process` | `IZLinkSpotContext.AddActorPacket(...)`으로 등록한 handler가 join된 `Spot` 실행 문맥에서 실행된다 |
| Entry Spot actor mailbox dispatch | `integration-single-process` | Entry Spot actor packet이 Entry Spot 전체 실행 큐에 막히지 않고, actor별 mailbox 순서를 따른다 |
| local actor mailbox dispatch | `integration-single-process` | user Spot에 들어가지 않은 actor packet도 actor별 mailbox 순서를 따른다 |
| user Spot actor dispatch serialization | `integration-single-process` | 같은 user Spot 안의 여러 actor packet이 Spot 실행 queue에서 순서대로 처리되어 Spot 상태가 보호된다 |
| runtime task exception observation | `unit` | detached runtime task와 fire-and-forget handler에서 발생한 예외가 unobserved exception으로 묻히지 않고 runtime error sink 또는 logger로 관찰된다 |
| execution queue cancellation semantics | `unit` | queue enqueue/wait cancellation이 이미 queue에 들어간 work item의 순서를 깨거나 중간에 제거하지 않는다 |
| spot route resolver 경로 | `integration-single-process` | spot name/id 기반 호출이 `IZLinkSpotRouteResolver` 결과를 사용해 target node와 spot id를 찾고, routed message를 보낸다 |
| actor manager 생성 중복/타입 충돌 | `integration-single-process` | `IZLinkActorManager.CreateAsync(...)` 중복 생성은 `ActorAlreadyExists`, `GetOrCreateAsync(...)` actor type 충돌은 `ActorTypeMismatch` 로 실패한다 |
| local actor bind 생성 금지 | `integration-single-process` | `BindActorHandleAsync(...)` 는 local actor 가 없을 때 factory 를 호출하지 않고 `ActorRouteNotFound` 로 실패한다 |
| remote actor dispatch 생성 금지 | `integration-single-process` | routed actor dispatch 수신 경로는 local actor 가 없을 때 factory 를 호출하지 않고 dispatch 를 실패시킨다 |
| session actor relay bridge | `integration-single-process` | `BindActorHandleAsync(...)`, `RelayToActorAsync(IZLinkActorRef, ...)`, `IZLinkActorRef.NotifyDisconnectedAsync(...)`가 public session 표면에서 동작한다 |
| session actor dispatch ordering | `integration-single-process` | stream session에서 actor로 relay된 packet이 actor별 순서를 보장하고, 현재 actor 위치에 맞는 handler 실행 경로로 넘어간다 |
| actor dispatch location after mailbox wait | `integration-single-process` | 같은 actor의 앞선 packet이 join을 끝낸 뒤, 대기 중이던 다음 packet이 이전 위치가 아니라 새 user Spot 위치로 dispatch된다 |
| session actor dispatch wire multipart | `integration-single-process` | Session 서버와 Play 서버 사이의 actor dispatch가 route header, actor metadata, stream header, payload를 별도 part로 유지하고, payload를 JSON envelope 안의 `byte[]`로 재직렬화하지 않는다 |
| session actor reconnect 재사용 | `integration-single-process` | 같은 actor id가 새 stream session에서 다시 bind되면 기존 actor 인스턴스와 spot membership을 유지하고, session binding token[^binding-token]만 갱신된다 |
| session actor binding rollback | `integration-single-process` | actor-session binding 갱신이 실패하면 helper도 실패하고, local binding table의 같은 token entry도 제거된다 |
| stale session binding token guard | `integration-single-process` | 이전 stream에서 늦게 도착한 unbind나 stale `SessionProxy` 메시지가 새 binding을 지우거나 사용하지 못한다 |
| stale session proxy send | `integration-single-process` | 이미 닫힌 stream이나 stale binding으로 향하는 one-way push가 route receive loop와 host shutdown을 실패시키지 않는다 |
| session proxy wire multipart | `integration-single-process` | Play 서버에서 Session 서버로 가는 `SessionProxy` send/request가 route header, proxy metadata, payload를 별도 part로 유지하면서, client STREAM에는 단일 stream packet으로 쓴다 |
| session proxy disconnect local actor | `integration-single-process` | local actor 가 actor id 없이 `IZLinkSessionProxy.DisconnectAsync(...)` 를 호출하면 binding 이 정리되고 session disconnect callback 은 다시 호출되지 않는다 |
| session proxy disconnect remote actor | `integration-single-process` | remote actor 가 actor id 없이 `IZLinkSessionProxy.DisconnectAsync(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다 |
| session context close | `integration-single-process` | `IZLinkSessionContext.CloseAsync(...)`가 현재 stream client 연결을 서버 쪽에서 끊고, 이어서 disconnect callback으로 연결된다 |
| actor join 직후 packet dispatch | `integration-single-process` | join이 끝난 뒤 들어온 packet이 새 `Spot` 실행 문맥에서 실행된다 |
| actor spot 이동 직후 packet dispatch | `integration-single-process` | 이전 `Spot` 문맥으로 stale dispatch가 발생하지 않는다 |
| actor context channel request 경로 | `integration-single-process` | join 전 `Context.RequestChannel(...)`은 일반 channel client 경로를, join 후에는 현재 `Spot`의 channel client 경로를 사용한다 |
| actor context stream send API | `integration-single-process` | actor는 `Context.Send(...)`로 client stream에 push하고, `IZLinkStream`을 직접 노출받지 않는다 |
| actor request handler reply | `unit` | actor request packet은 actor request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| Spot actor request handler reply | `unit` | Entry Spot/user Spot actor request packet은 request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| local actor request relay reply | `integration-single-process` | local session actor relay도 actor request handler 반환값으로 stream response를 작성한다 |
| actor reply public surface 제거 | `unit` | actor context Reply와 actor stream client 계약이 public surface에 다시 노출되지 않는다 |

## 6. Stream Regression 항목

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

## 7. Registry / Monitoring Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| embedded registry 시작 순서 | `integration-single-process` | framework discovery가 registry bind 이후에 시작된다 |
| registry query DI | `integration-single-process` | `IZLinkRegistryQuery` resolve와 snapshot 조회가 성공한다 |
| 원격 query client | `integration-multi-process` | topology snapshot 조회가 성공한다 |
| monitoring source 이름 불일치 | `unit` | startup validation 예외 |
| registry polling diff | `integration-multi-process` | topology, status, service summary event가 발생한다 |
| spot polling diff | `integration-multi-process` | status, peers, subjects event가 발생한다 |

## 8. Release Gate

릴리스로 보내려면 다음 다섯 가지를 모두 만족해야 한다.

1. `unit`, `integration-single-process`, `integration-multi-process` 전부 통과
2. `net8.0`, `net10.0` 양쪽 모두 통과
3. 위 여섯 runtime RID 전체에서 CI gate 통과
4. happy-path 샘플과 대표 failure-path가 각각 한 번 이상 커버되어 있음
5. `behavior-matrix.ko.md`에 정리한 비허용 조합이 모두 테스트로 고정되어 있음

즉 샘플이 한 번 실행되는 것만으로는 충분하지 않다. startup validation 과
runtime failure 의미까지 테스트로 같이 고정되어 있어야 한다.

또한 native backend 가 이미 해당 플랫폼을 지원하더라도, framework 는 그 위에
registration, lifecycle, DI, monitoring 계층을 더 쌓는다. 그래서 플랫폼 gate 는
backend gate 와 별도로 유지한다.

## 9. 문서별 회귀 테스트 단락

이 디렉토리의 각 draft 문서는, 자기 항목이 어떤 테스트로 고정되어 있는지 짧은
`회귀 테스트` 단락을 갖고 있어야 한다. 중앙 matrix 만 갱신해서는 곤란하다.
세부 문서의 독자가 어떤 테스트를 봐야 하는지 놓치기 쉽기 때문이다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `DocumentationRegressionTests.DotNetDraftDocuments_AllExposeRegressionTestSection` | 아래 문서가 모두 `회귀 테스트` 단락을 가진다. |
| `DocumentationRegressionTests.DotNetRegressionMatrix_References_AllDraftDocuments` | 이 matrix가 아래 문서 파일명을 모두 참조한다. |

대상 문서는 다음과 같다.

- `README.ko.md`
- `registry-backed-routing-defaults.ko.md`
- `spot-timer-policy.ko.md`
- `handler-interfaces.ko.md`
- `aspnet-core-channel-messaging.ko.md`
- `aspnet-core-spot.ko.md`
- `stage-wrapper-on-spot.ko.md`
- `aspnet-core-stream.ko.md`
- `aspnet-core-actor.ko.md`
- `session-actor-dispatch.ko.md`
- `streaming-client.ko.md`
- `stream-open-items.ko.md`
- `aspnet-core-monitoring.ko.md`
- `aspnet-core-registry.ko.md`
- `behavior-matrix.ko.md`
- `di-capability-exposure-policy.ko.md`
- `regression-test-matrix.ko.md`
- `lifecycle-and-failure-semantics.ko.md`
- `implementation-scope-and-nongoals.ko.md`
- `backend-dependency-policy.ko.md`
- `channel-messaging-samples.ko.md`
- `spot-samples.ko.md`
- `stream-samples.ko.md`
- `tictactoe-game-sample.ko.md`
- `bingo-game-sample.ko.md`

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
