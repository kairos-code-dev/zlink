<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Node.js Lifecycle And Failure Semantics](lifecycle-and-failure-semantics.ko.md) | [다음: ZLink Framework Node.js Implementation Scope And Non-Goals](implementation-scope-and-nongoals.ko.md)
<!-- framework-adapter-nav:end -->

[표면 매핑 정책](dotnet-to-node-surface-mapping.ko.md)

[Node.js 묶음](../README.ko.md) | [Behavior Matrix](behavior-matrix.ko.md) | [Lifecycle](lifecycle-and-failure-semantics.ko.md) | [use case validation](../../common/spec/usecase-validation.ko.md)

# ZLink Framework Node.js Regression Test Matrix

> 이 문서는 [표면 매핑 정책](dotnet-to-node-surface-mapping.ko.md)을 따른다.
> 회귀 항목의 **의미·통과 기준·커버리지는 dotnet 과 동일**하고, 테스트 표면만
> Node.js(`node:test` 기반 `*.test.js`, `test(...)`)로 옮긴다. 정식
> 기준은 `framework/languages/dotnet` 의 코드와 dotnet 회귀 matrix 다. 이
> 문서대로 테스트를 구현하면 Node 구현이 .NET 버전과 **동등함을 증명**하는
> 테스트 묶음이 나온다.

## 1. 목적

use case validation 문서는 설계 설명이 어디까지 닿아 있는지를 보는 문서다.
반면 이 문서는 결이 다르다. 구현이 바뀌더라도 "무엇이 깨지면 회귀로 본다"는
기준을 테스트 항목 단위로 못 박는 데 목적이 있다.

dotnet 테스트 프로젝트는 다음 세 묶음이다. Node 테스트 패키지는 이를 1:1로
미러링한다.

| dotnet 프로젝트 | node 테스트 패키지 | 계층 매핑 |
|------|------|------|
| `Zlink.Framework.ContractTests` | `@zlink-systems/framework` contract 테스트(`test/contract/**/*.test.js`) | `unit` 계약 표면 + 일부 `contract` |
| `Zlink.Framework.UnitTests` | unit 테스트(`test/unit/**/*.test.js`) | `unit` |
| `Zlink.Framework.E2ETests` | e2e 테스트(`test/e2e/**/*.test.js`) | `integration-single-process` + `integration-multi-process` |

> dotnet `ContractTests` 의 `Channels/Handlers/Spots/Streams/Configuration/
> Registry/Actors/Timers/Eventing/Codecs` 하위 묶음, `UnitTests` 의
> `Configuration/Registration`, `Contracts`, `Runtime`, `Documentation`,
> `Samples` 묶음, `E2ETests` 의 `Channels/Spot/Stream/Registry/Monitoring/
> Lifecycle/MultiProcess` 묶음을 같은 디렉토리 구조로 재현한다.

## 2. CI 계층

회귀 테스트는 다음 세 계층으로 나누어 둔다.

| 계층 | 목적 | 예시 |
|------|------|------|
| `unit` | registration validation, dispatch lookup, option parsing | 중복 등록, module options validation |
| `integration-single-process` | 같은 호스트(Node process) 안에서 runtime 조합이 정상 동작하는지 확인 | channel request/send, embedded registry, monitoring attach |
| `integration-multi-process` | 실제 topology[^topology]와 reconnect 동작 확인 | 원격 registry query, discovery 변화, spot peer 변화 |

## 3. 최소 CI 매트릭스

| 항목 | 기준 |
|------|------|
| Node.js 런타임 | `node20`(LTS), `node22`(현행) |
| 플랫폼 ABI[^abi] | `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `darwin-x64`, `darwin-arm64` |
| test mode | development(소스 `*.ts`), production(번들·`dist`) |

최소 지원 런타임이 `node20` 이므로, 회귀 테스트도 위 두 런타임 버전을 함께
돌려야 한다.

- 현재 저장소의 기본 빌드는 `node20` 단일 런타임이다.
- `node22` 는 회귀 matrix 보고용 다중 런타임 빌드에서 추가로 컴파일·실행하는
  형태로 다룬다.

한편 저장소의 `@zlink-systems/zlink`(Node 바인딩) prebuilt native artifact 조합과
CI workflow 가 만들어 내는 native artifact 조합은 위 여섯 플랫폼 ABI 를 기준으로
한다. framework CI gate[^ci-gate] 도 같은 범위를 기본으로 본다.

즉 Node framework 회귀 테스트는 특정 OS 하나만 대표로 돌리고 끝내지 않는다.
현재 계획 기준으로 반드시 통과해야 하는 플랫폼은 다음과 같다.

- Windows x64
- Windows ARM64
- Linux x64
- Linux ARM64
- macOS x64
- macOS ARM64

## 3.1 Node Binding Parity Regression 항목

> framework 는 `@zlink-systems/zlink` public API 위에만 올라간다. dotnet
> `Runtime/Backend/DotNet/` 이 `bindings/dotnet` public surface 를 쓰는 것처럼,
> Node backend adapter 도 binding internal/native 경로를 직접 우회하지 않는다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| binding public API gap list | `unit` | P2~P8 에 필요한 channel/spot/stream/registry/monitoring/ActorGateway/bound session API가 목록화되고 gap이 0이다 |
| framework public-api-only import guard | `unit` | framework runtime/adapter package가 binding internal path, native addon symbol, generated private helper를 import하지 않는다 |
| ActorGateway attach public API smoke | `integration-single-process` | stream session relay가 binding public API만으로 ActorGateway에 attach된다 |
| bound session public API smoke | `integration-single-process` | bound session send/disconnect가 binding public API만으로 동작한다 |
| registry query public API smoke | `integration-single-process` | registry query client wrapper가 binding public API만 호출한다 |
| socket monitor public API smoke | `integration-single-process` | socket monitoring source가 binding public API만 호출한다 |
| native artifact freshness guard | `unit` | native addon 산출물이 source보다 오래되면 framework smoke가 실패한다 |

## 4. Channel Regression 항목

> dotnet `ContractTests/Channels`, `ContractTests/Handlers`,
> `ContractTests/Configuration`, `E2ETests/Channels` 미러. node `register*`
> builder channel 등록(`.addClientServerChannel(...)`)과 `ZLinkChannelClient` /
> `ZLinkFanoutClient` 표면을 검증한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate channel 이름 등록 (`channels` 키 / `registerClientServerChannel`, `registerFanoutChannel`) | `unit` | startup validation 예외 |
| 같은 channel 이름을 client-server와 fanout 역할로 동시에 등록 | `unit` | startup validation 예외 |
| server 역할에 bind endpoint 없음 | `unit` | startup validation 예외 |
| `.addClientServerChannel(...)` + `.enableClient(endpoint)` | `integration-single-process` | manual request/send 성공 |
| `registerClientServerChannel(...)` + `enableClient(...)` + 전역 `useDiscovery().addRegistryEndpoint(...)` | `integration-single-process` | discovery 기반 request/send 성공 |
| `.addFanoutChannel(...)` + `.enableSubscriber(endpoint)` | `integration-single-process` | manual 기반 subscribe 성공 |
| client 역할에 peer acquisition 경로 없음 | `unit` | startup validation 예외 |
| 같은 역할에서 discovery/manual 혼용 | `unit` | startup validation 예외 |
| publisher 역할에 bind endpoint 없음 | `unit` | startup validation 예외 |
| publisher 전용 channel | `integration-single-process` | publish submit 성공 |
| subscriber discovery attach | `integration-multi-process` | 원격 publish 수신 |
| handler group mapping | `unit` | handler decorator 등록만으로는 전역 dispatch 대상이 되지 않고, channel 의 `handlerGroups: ['...']`로 매핑한 그룹의 handler만 해당 채널에서 dispatch된다 |
| handler exposure 없는 server channel | `unit` | scan 된 handler 가 있어도 `addHandlerGroup(...)` 또는 `add*Handler(...)`가 없으면 application handler 가 자동 노출되지 않는다 |
| handler exposure 없는 server channel validation | `unit` | handler exposure 없는 server channel 은 `acceptSpotRoutesFromChannel(...)` 명시 참조가 없으면 startup validation 오류다 |
| fanout subscriber handler exposure | `unit` | 현재 NestJS registration 에는 publish handler exposure 표면이 없으므로 정식 dispatch 대상으로 검증하지 않는다 |
| typed handler registration | `unit` | channel 의 `add*Handler(...)`로 직접 등록한 handler 는 group mapping 없이도 해당 channel 에 노출된다 |
| channel type handler compatibility | `unit` | client-server 는 request, route mesh 는 route send/request handler 만 허용하고 dealer mesh 와 fanout subscriber 는 handler registration 을 노출하지 않는다 |
| incompatible handler group mapping | `unit` | channel type 과 맞지 않는 handler 가 group 안에 섞이면 일부만 제외하지 않고 startup validation 오류로 실패한다 |
| route mesh handler group mapping | `integration-single-process` | route mesh channel 의 `addHandlerGroup(...)`은 route send/request handler group 을 실제 routed dispatch 대상으로 노출한다 |
| route mesh packet dispatcher | `integration-single-process` | route mesh `ROUTER` 로 들어온 routed send/request packet 을 handler 로 dispatch 하고 request reply/error 를 돌려주며 빈 probe frame 은 application handler 로 넘기지 않는다 |
| DI route channel inbound handler dispatch | `integration-single-process` | `ZLinkModule.forRoot(...)` route channel 의 `sendHandlers`/`requestHandlers`가 runtime host 시작 후 host-owned `ROUTER` receive loop 에 연결되어 routed send/request 를 처리한다 |
| DI route client host transport | `integration-single-process` | `ZLinkModule.forRoot(...)`가 노출한 `ZLinkRouteClient`가 framework runtime host 시작 이후 host-owned ROUTER transport로 target node RID에 routed send/request/reply를 수행한다 |
| Spot route transport 전용 channel | `integration-single-process` | `acceptSpotRoutesFromChannel(...)`으로만 참조된 router-capable channel 은 handler group 없이도 Spot route transport 로 동작하지만 application handler 를 열지 않는다 |
| 같은 channel server에 handler 중복 | `unit` | 같은 `kind + packetName` handler가 둘 이상이면 startup validation 예외 |
| 다른 channel server에 같은 packet handler | `integration-single-process` | 같은 `kind + packetName`을 서로 다른 channel에 매핑해도 각 채널이 독립적으로 dispatch된다 |
| 같은 그룹을 여러 채널에 매핑 | `integration-single-process` | 같은 `zlinkRequestHandler('api', ...)` group 을 두 채널에 `handlerGroups`로 노출해도 채널마다 dispatch namespace가 독립이다 |
| `addHandlerGroup`이 가리키는 그룹 없음 | `unit` | 매핑한 그룹에 handler가 하나도 없으면 startup validation 오류 |
| event handler group mapping | `unit` | publish handler group mapping 은 subscriber handler registration 표면이 생긴 뒤 추가한다 |
| HTTP(REST controller) handler에서 `ZLinkChannelClient` 사용 | `integration-single-process` | route handler와 동일한 NestJS DI[^di] 컨테이너에서 정상 동작 |
| DI channel client host transport | `integration-single-process` | `ZLinkModule.forRoot(...)`가 노출한 `ZLinkChannelClient`가 framework runtime host 시작 이후 host-owned DEALER transport로 manual channel request/reply를 수행한다 |
| channel handler에서 `ZLinkChannelClient` 사용 | `integration-single-process` | 일반 request handler가 같은 DI 컨테이너의 `ZLinkChannelClient`로 다른 channel 에 request 하고 reply 를 받는다 |
| dealer mesh channel client | `integration-single-process` | `ZLinkChannelClient.sendToChannel(...)`와 `requestToChannel(...)`가 `registerDealerMeshChannel(...)` 등록의 DEALER socket 을 통해 동작한다 |
| channel handler에서 fanout publish | `integration-single-process` | 일반 request handler가 같은 DI 컨테이너의 `ZLinkFanoutClient`로 fanout event 를 publish 하고 subscriber handler가 수신한다 |
| send async submit backpressure[^backpressure] | `integration-single-process` | HWM[^hwm]에 도달해도 caller 실행 흐름을 block하지 않고(`Promise` 미해결 유지), ready 이후에 resolve된다 |
| publish async submit backpressure | `integration-single-process` | `NoDrop` 또는 HWM 조건에서 흐름을 block하지 않고 `sendTimeout` 정책에 따라 resolve 또는 reject |
| request submit/reply timeout 분리 | `integration-single-process` | request packet의 submit 지연은 `sendTimeout`으로, reply 대기는 `timeout(...)`으로 판정 |
| pending request 정리 | `unit` | submit 실패, timeout, cancellation(`AbortSignal`), runtime stop이 일어날 때 request sequence가 pending map에서 제거된다 |
| ready callback batch drain | `integration-single-process` | socket이 ready된 뒤 pending send/publish를 batch로 처리하고, 같은 frame을 중복 전송하지 않는다 |
| channel wire multipart[^wire-multipart] | `integration-single-process` | 서버 간 channel send/request/reply가 `header`와 `payload`를 별도 message part로 보내고, handler dispatch는 header part만 보고 packet을 고른다 |
| publish wire multipart | `integration-single-process` | `PUB/SUB` publish도 framework header와 payload를 별도 part로 유지하고, subscriber handler에는 typed payload만 전달된다 |

## 4.1 DI Capability Regression 항목

> dotnet `ContractTests/Configuration/ConnectionAndConfigContracts`,
> `UnitTests/Configuration/Registration` 미러. NestJS provider token 노출 규칙
> (`di-capability-exposure-policy`)을 검증한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| actor factory without SpotNode | `unit` | actor factory 만 등록하면 startup validation 예외 |
| actor manager without SpotNode | `unit` | SpotNode 없는 구성에서는 `ZLinkActorManager` provider token 이 DI 에 없다 |
| actor manager with SpotNode only | `unit` | SpotNode 만 있고 actor factory 가 없으면 `ZLinkActorManager` 가 DI 에 없다 |
| actor manager with SpotNode and actor factory | `unit` | SpotNode 와 actor factory 가 모두 있으면 `ZLinkActorManager` 가 DI 에 등록된다 |
| Spot service without SpotNode | `unit` | SpotNode 없는 구성에서는 `ZLinkSpotManager` 가 DI 에 없다 |
| Spot service with SpotNode | `unit` | SpotNode 가 있으면 Spot service 가 DI 에 등록된다 |
| Spot publisher without publisher 역할 | `unit` | SpotNode 가 있어도 publisher 역할이 없으면 Spot publisher service 는 DI 에 없다 |
| Spot publisher with publisher 역할 | `unit` | Spot publisher 역할이 있으면 `ZLinkSpotPublisherClient` 가 DI 에 등록된다 |
| bound session factory registration | `unit` | `ZLinkBoundSessionFactory` 는 framework runtime 과 함께 등록된다 |
| Spot remote address resolver without SpotNode | `unit` | remote address 정보만 제공하는 서버는 SpotNode 없이 `ZLinkSpotRemoteAddressResolver` 를 등록할 수 있다 |
| Spot outbound with resolver only | `unit` | Spot remote address resolver 만 있고 SpotNode 가 없으면 `ZLinkSpotOutbound` 는 DI 에 없다 |
| route channel missing at call time | `unit` | `ZLinkRouteClient` 호출 시 route channel 이 없으면 `ZLinkConfigurationError` |
| channel client missing at call time | `unit` | `ZLinkChannelClient` 호출 시 channel client 역할이 없으면 `ZLinkConfigurationError` |

## 5. Spot Regression 항목

> dotnet `ContractTests/Spots`, `ContractTests/Actors`, `E2ETests/Spot` 미러.
> `ZLinkSpotManager`, `ZLinkActorManager`, Entry Spot, bound session 표면을
> 검증한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate Spot factory type | `unit` | startup validation 예외 |
| duplicate `registerEntrySpot(EntrySpotClass)` | `unit` | 같은 `SpotNode` 안에서 Entry Spot[^entry-spot] registry를 중복 등록하면 startup validation 예외 |
| `registerSpotMesh(...)`에 `useDiscovery().addRegistryEndpoint(...)` 없음 | `unit` | top-level discovery endpoint 를 상속하거나 local-only mesh 로 등록된다 |
| `registerSpotMesh(channel, configureMesh)` | `integration-single-process` | mesh 빌더 한 호출로 discovery, node, spot factory 등록을 한 번에 끝낸다 |
| `registerSpotMesh(...)` + 빈 `addRegistryEndpoint` + local-only spot factory | `integration-single-process` | discovery endpoint 없이 단일 local SpotNode runtime을 시작한다 |
| `registerSpotMesh(...)` + router-capable `addNode(...)` + `attachActorGateway(...)` | `integration-single-process` | session relay ingress 를 mesh 소유권 아래 시작한다 |
| `create(spotType)` | `integration-single-process` | `spotId`, `Created` 상태가 일관되게 유지된다 |
| `create(spotType)` empty create payload | `integration-single-process` | payload 없는 생성도 빈 `Message`로 `ZLinkSpot.onCreate(...)`를 한 번 호출한다 |
| `create(spotType, request)` payload | `integration-single-process` | create request `Message`가 `ZLinkSpot.onCreate(...)`로 한 번 전달된다 |
| `getOrCreate(spotType, spotRid, request)` existing | `integration-single-process` | 같은 `spotId`가 이미 ready 상태면 `Existing`이고 새 `request`는 `onCreate(...)`로 전달되지 않는다 |
| `getOrCreate(...)` concurrent create payload | `integration-single-process` | 같은 `spotId` 동시 생성에서는 첫 생성 요청의 `Message`만 `onCreate(...)`로 전달되고 callback은 한 번만 실행된다 |
| `getOrCreate(spotType, spotRid)` same type | `integration-single-process` | 같은 `spotId`를 같은 Spot 타입으로 다시 확보하면 기존 spot을 반환하고 새 `onCreate(...)`를 호출하지 않는다 |
| spot create lifecycle failure | `integration-single-process` | `onCreate(...)` 또는 `onInitialize(...)` 실패는 `SpotCreateFailed`로 전파되고 failed entry는 제거되어 다음 생성 요청이 재시도할 수 있다 |
| `find(...)`, `list(...)` | `integration-single-process` | manager 조회 결과가 일관된다 |
| `configure()` handler registration | `integration-single-process` | `context.handlers.addPacket(...)`, `context.handlers.addHandler(...)`, `context.handlers.addSubscribe(...)` 등의 spot-local 등록이 descriptor에 반영된다 |
| Entry Spot actor handler decorator registration | `integration-single-process` | `zlinkEntrySpotActorRequestHandler(...)` decorator 로 등록한 actor packet handler 가 대상 Entry Spot registry에 반영된다 |
| user Spot actor handler decorator registration | `integration-single-process` | `zlinkSpotActorRequestHandler(...)` decorator 로 등록한 actor packet handler 가 대상 user Spot registry에 반영된다 |
| Entry Spot packet callback concurrency | `integration-single-process` | Entry Spot 일반 packet handler는 user Spot과 같은 등록 표면을 쓰지만 Entry Spot 전체 실행 줄에 직렬화되지 않는다 |
| `onInitialize(...)` handler resolve | `integration-single-process` | spot마다 분리된 DI scope가 정상 동작한다 |
| `onClosing(...)` 정상 close callback | `integration-single-process` | `close(...)` 호출 시 spot 실행 문맥에서 한 번 호출된다 |
| local spot publish | `integration-single-process` | subscriber가 정상 수신한다 |
| SPOT timer metadata | `integration-single-process` | timer handler가 callback 번호, 예정/시작 시각, 지연, skip metadata를 받는다 |
| SPOT timer overrun policy | `integration-single-process` | `SkipLateTicks`, `CatchUpBounded`, `DelayNextTick` 정책이 각각 skip, bounded catch-up, fixed-delay 의미를 지킨다 |
| SPOT timer exception policy | `integration-single-process` | handler 예외가 monitoring event로 기록되고, `stopOnUnhandledException`이 켜진 timer는 중단된다 |
| Entry Spot timer execution context | `integration-single-process` | Entry Spot timer는 Entry Spot actor packet, lifecycle callback, request continuation과 같은 실행 queue에서 처리되고, 같은 timer callback도 겹쳐 실행하지 않는다 |
| SPOT timer cancel | `integration-single-process` | `cancel()` 뒤 managed timer loop가 추가 callback을 실행하지 않는다 |
| outbound 전용 외부 publish client | `integration-multi-process` | target SPOT[^spot] channel에 publish가 성공한다 |
| Spot route channel acceptance | `unit` | fanout/dealer mesh/ambiguous/missing router/missing peer source 구성을 startup validation에서 거부한다 |
| Spot route channel manual connect | `integration-single-process` | `acceptSpotRoutesFromChannel(...)` 수동 endpoint가 binding public API를 통해 router channel peer로 적용된다 |
| Spot route channel transport | `integration-single-process` | caller가 명시한 local egress channel이 channel type에 맞는 ROUTER 또는 DEALER socket으로 egress 설정의 target SpotNode ingress channel을 통해 target Spot으로 routed send/request를 보낸다 |
| route mesh Spot egress target peer 선택 | `integration-single-process` | source process가 target route channel 을 local registration 으로 갖지 않아도, 수동 연결과 registry metadata 의 target SpotNode ingress channel / ROUTER `routingId`로 route mesh egress target peer 를 선택한다 |
| Spot route egress 역할 validation | `unit` | routed Spot egress 는 client-server client 역할 또는 route mesh transport 에서만 켤 수 있고 fanout/dealer mesh 에서는 startup validation 오류다 |
| spot close 후 scope 정리 | `integration-single-process` | 이후 callback이 발생하지 않고 dispose도 정상 완료된다 |
| actor join 이후 dispatch 문맥 | `integration-single-process` | `ZLinkSpotContext.addHandler(...)`로 등록한 actor handler가 join된 `Spot` 실행 문맥에서 실행된다 |
| Entry Spot actor dispatch serialization | `integration-single-process` | Entry Spot actor packet이 actor별 입력 순서를 보존한 뒤 Entry Spot 실행 queue에서 순서대로 처리된다 |
| local actor mailbox dispatch | `integration-single-process` | user Spot에 들어가지 않은 actor packet도 actor별 mailbox 순서를 따른다 |
| user Spot actor dispatch serialization | `integration-single-process` | 같은 user Spot 안의 여러 actor packet이 Spot 실행 queue에서 순서대로 처리되어 Spot 상태가 보호된다 |
| runtime task exception observation | `unit` | detached runtime task와 fire-and-forget handler에서 발생한 예외가 unhandled rejection으로 묻히지 않고 runtime error sink 또는 logger로 관찰된다 |
| execution queue cancellation semantics | `unit` | queue enqueue/wait cancellation이 이미 queue에 들어간 work item의 순서를 깨거나 중간에 제거하지 않는다 |
| explicit egress channel Spot route 경로 | `integration-single-process` | routed Spot 호출은 target 정보만으로 egress transport를 고르지 않고, caller가 명시한 local egress channel, egress 설정의 target SpotNode ingress channel, `routingId` target으로 routed message를 보낸다 |
| actor manager 생성 중복/타입 충돌 | `integration-single-process` | `ZLinkActorManager.create(...)` 중복 생성은 `ActorAlreadyExists`, `getOrCreate(...)` actor type 충돌은 `ActorTypeMismatch` 로 실패한다 |
| local actor bind 생성 금지 | `integration-single-process` | `bind(...)` 는 local actor 가 없을 때 factory 를 호출하지 않고 `ActorRouteNotFound` 로 실패한다 |
| session actor bind resolver 제거 | `integration-single-process` | `bind(...)` 는 application resolver fallback 없이 logical actor handle 을 등록한다 |
| remote actor dispatch 생성 금지 | `integration-single-process` | routed actor dispatch 수신 경로는 local actor 가 없을 때 factory 를 호출하지 않고 dispatch 를 실패시킨다 |
| session actor relay bridge | `integration-single-process` | `bind(...)` 와 `ZLinkSessionActor.relay(...)` 가 public session 표면에서 동작한다 |
| session actor explicit disconnect notification | `contract`, `integration-single-process` | session disconnect 는 bound actor 전체에 자동 전파되지 않고, `notifyDisconnected(...)` 또는 runtime 명시 호출 시 현재 Spot actor disconnected handler 가 호출된다 |
| session actor dispatch ordering | `integration-single-process` | stream session에서 actor로 relay된 packet이 actor별 순서를 보장하고, 현재 actor 위치에 맞는 handler 실행 경로로 넘어간다 |
| actor dispatch location after mailbox wait | `integration-single-process` | 같은 actor의 앞선 packet이 join을 끝낸 뒤, 대기 중이던 다음 packet이 이전 위치가 아니라 새 user Spot 위치로 dispatch된다 |
| session actor dispatch wire multipart | `integration-single-process` | Session 서버와 Play 서버 사이의 actor dispatch가 route header, actor metadata, stream header, payload를 별도 part로 유지하고, payload를 JSON envelope 안의 `Buffer`로 재직렬화하지 않는다 |
| session actor reconnect 재사용 | `integration-single-process` | 같은 actor id가 새 stream session에서 다시 bind되면 기존 actor 인스턴스와 spot membership을 유지하고, session binding token[^binding-token]만 갱신된다 |
| session actor binding rollback | `integration-single-process` | actor-session binding 갱신이 실패하면 helper도 실패하고, local binding table의 같은 token entry도 제거된다 |
| stale session binding token guard | `integration-single-process` | 이전 stream에서 늦게 도착한 unbind나 stale bound session 메시지가 새 binding을 지우거나 사용하지 못한다 |
| Registry Spot route 기본 resolver 등록 | `unit` | `useRegistrySpotRemoteAddresses(...)` 가 custom resolver 없이 기본 `ZLinkSpotRemoteAddressResolver` 와 Spot RID directory 를 등록한다 |
| actor-bound session route 등록 | `integration-single-process` | actor-session route 는 session bind 시 actor runtime state 에 저장된다 |
| Registry route 기본 구현 중복 등록 방지 | `unit` | Registry 기본 구현과 custom resolver 를 함께 등록하면 startup validation 오류가 난다 |
| Registry route 기본 구현 discovery validation | `unit` | `useDiscovery().addRegistryEndpoint(...)` 없이 Registry 기본 route resolver 를 켜면 startup validation 오류가 난다 |
| Registry Spot RID route | `integration-single-process` | `ZLinkSpotManager.getOrCreate(spotType, spotRid, request?)` 로 만든 Spot 을 `find(...)` 로 찾고, `close(...)` 성공 후 not found 를 반환한다 |
| stale session unbind guard | `integration-single-process` | 이전 binding token 으로 도착한 disconnect 가 새 actor-session binding 을 지우지 않는다 |
| sample-only session metadata store 제거 | `unit` | TicTacToe.Ts 와 Bingo.Ts 샘플이 sample-only actor-session store 없이 framework/session 흐름을 사용한다 |
| stale bound session send | `integration-single-process` | 이미 닫힌 stream이나 stale binding으로 향하는 one-way push가 route receive loop와 host shutdown을 실패시키지 않는다 |
| bound session gateway relay | `integration-single-process` | Play 서버에서 Session 서버로 가는 bound session send가 core ActorGateway binding 을 통해 client STREAM에 단일 stream packet으로 도착한다 |
| bound session disconnect local actor | `integration-single-process` | local actor 가 actor id 없이 `ZLinkBoundSession.disconnect(...)` 를 호출하면 binding 이 정리되고 session disconnect callback 은 다시 호출되지 않는다 |
| bound session disconnect remote actor | `integration-single-process` | remote actor 가 actor id 없이 `ZLinkBoundSession.disconnect(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다 |
| session context close | `integration-single-process` | `ZLinkSessionContext.close(...)`가 현재 stream client 연결을 서버 쪽에서 끊고, 이어서 disconnect callback으로 연결된다 |
| actor join 직후 packet dispatch | `integration-single-process` | join이 끝난 뒤 들어온 packet이 새 `Spot` 실행 문맥에서 실행된다 |
| actor spot 이동 직후 packet dispatch | `integration-single-process` | 이전 `Spot` 문맥으로 stale dispatch가 발생하지 않는다 |
| spot context channel request 경로 | `integration-single-process` | `spot.context.outbound.requestToChannel(...)`이 현재 Spot 에 attach 된 channel client 경로를 사용한다 |
| spot context routed send/request 표면 | `contract`, `integration-single-process` | `ZLinkSpotOutbound`가 `sendToSpot`, `requestToSpot`, `publish`, `sendToChannel`, `requestToChannel`을 모두 노출하고, `spot.context.outbound.sendToSpot(...)` / `requestToSpot(...)`이 route transport를 사용한다 |
| actor bound session send API | `integration-single-process` | actor는 `context.boundSession.send(...)`로 client stream에 push하고, `ZLinkStream`을 직접 노출받지 않는다 |
| actor request handler reply | `unit` | actor request packet은 actor request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| Spot actor request handler reply | `unit` | Entry Spot/user Spot actor request packet은 request handler 반환값으로만 reply되고 send handler로 fallback dispatch되지 않는다. send/request 밖 stream kind도 actor packet으로 처리하지 않는다 |
| local actor request relay reply | `integration-single-process` | local session actor relay도 actor request handler 반환값으로 stream response를 작성한다 |
| actor reply public surface 제거 | `unit` | actor context reply와 actor stream client 계약이 public surface에 다시 노출되지 않는다 |

## 6. Stream Regression 항목

> dotnet `ContractTests/Streams`, `E2ETests/Stream` 미러. header 기반 단일
> `onDispatch` session 등록, lifecycle callback, handler invoker 표면을 검증한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| 같은 node에 session 중복 등록 | `unit` | startup validation 예외 |
| header session node | `integration-single-process` | `onDispatch(...)` 호출 확인 |
| `onConnected(...)` | `integration-multi-process` | `ConnectionReady` 이후 1회 호출 |
| `onError(...)` 범위 | `integration-multi-process` | transport error만 session callback으로 전달된다 |
| peer metadata 표면 | `integration-single-process` | `sessionId`, `routingId`, `localAddr`, `remoteAddr` 값 확인 |
| session callback task dispatch | `integration-single-process` | transport callback에서 user callback을 직접 호출하지 않고, managed task(microtask 큐) 경로로 호출한다 |
| session callback 직렬성 | `integration-single-process` | stream socket이 보존한 같은 session frame 순서대로 lifecycle/packet callback이 직렬 실행되며, 서로 병렬로 겹치지 않는다 |
| session callback 직접 호출 우회 방지 | `unit` | runtime 내부 transport 진입점은 enqueue API만 사용한다 |
| handler `Promise<T>` 결과 await | `unit` | handler invoker가 generic `Promise<T>`를 실제 결과 값으로 변환하고, 값 타입 변환 오류를 내지 않는다 |
| abstract wire payload validation | `unit` | converter 없는 abstract/interface payload가 node 경계 DTO에 포함되면 등록 시점 또는 첫 submit 직전에 configuration 오류로 실패한다 |

## 7. Registry / Monitoring Regression 항목

> dotnet `ContractTests/Registry`, `ContractTests/Eventing`,
> `E2ETests/Registry`, `E2ETests/Monitoring` 미러. embedded registry 시동 순서,
> `ZLinkRegistryQuery`, monitoring typed event 를 검증한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| embedded registry 시작 순서 | `integration-single-process` | framework discovery가 registry bind 이후에 시작된다 |
| registry query DI | `integration-single-process` | `ZLinkRegistryQuery` resolve와 snapshot 조회가 성공한다 |
| 원격 query client | `integration-multi-process` | topology snapshot 조회가 성공한다 |
| monitoring source 이름 불일치 | `unit` | startup validation 예외 |
| registry polling diff | `integration-multi-process` | topology, status, service summary event가 발생한다 |
| spot polling diff | `integration-multi-process` | status, peers, subjects event가 발생한다 |

## 8. Release Gate

릴리스로 보내려면 다음 여섯 가지를 모두 만족해야 한다. 로컬에서는
`npm run verify:release` 가 아래 필수 gate 를 한 번에 실행한다. CI 는 같은 기준을
runtime/ABI matrix job 과 cross-language job 으로 나누어 실행한다.

1. `unit`, `integration-single-process`, `integration-multi-process` 전부 통과
2. `npm run verify:runtime-matrix` 로 `node20`, `node22` 양쪽 모두 통과
3. 위 여섯 플랫폼 ABI 전체에서 CI gate 통과
4. happy-path 샘플과 대표 failure-path가 각각 한 번 이상 커버되어 있음
5. `behavior-matrix.ko.md`에 정리한 비허용 조합이 모두 테스트로 고정되어 있음
6. `npm run verify:cross-language` 로 cross-language smoke 필수 경로가 통과되어
   Node 구현이 dotnet/C++/Java 와 같은 wire 계약을 지킨다는 것을 확인함

즉 샘플이 한 번 실행되는 것만으로는 충분하지 않다. startup validation 과
runtime failure 의미까지 테스트로 같이 고정되어 있어야 한다.

또한 native backend 가 이미 해당 플랫폼을 지원하더라도, framework 는 그 위에
registration, lifecycle, DI, monitoring 계층을 더 쌓는다. 그래서 플랫폼 gate 는
backend gate 와 별도로 유지한다.

## 8.1 Sample / Guide / Cross-Language Release 항목

> Phase 9 의 사용성·샘플 축은
> [정본 샘플](../README.ko.md)이 소유한다.
> 아래 항목은 release gate 에서 반드시 실행한다.

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| `npm run verify:release` | `integration-multi-process` | ABI 선언, P0 회귀, sample smoke, Node runtime matrix, cross-language smoke 를 순서대로 실행한다 |
| `npm run verify:samples` | `integration-multi-process` | TicTacToe.Ts, Bingo.Ts 가 모두 self-check 통과 |
| `npm run verify:runtime-matrix` | `integration-multi-process` | 현재 runner 가 Node 20 과 Node 22 에서 build, typecheck, 전체 contract test 를 모두 통과시킨다 |
| `npm run verify:abi-matrix` | `unit` | `framework-node` CI workflow, release 문서, package script 가 `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `darwin-x64`, `darwin-arm64` 와 Node 20/22 gate 를 같은 목록으로 유지한다 |
| `npm run verify:cross-language` | `integration-multi-process` | Node 와 dotnet TestHost 가 channel/stream 필수 경로 여섯 가지를 같은 프로토콜 의미로 통과시킨다 |
| guide chapter map | `unit` | Node guide 12개 장이 dotnet guide 주요 장과 1:1로 매핑된다 |
| sample public API import guard | `unit` | sample 이 framework/connector public API만 import하고 binding internal/native 경로를 직접 쓰지 않는다 |
| sample readiness guard | `unit` | sample 이 sleep-only readiness masking을 사용하지 않고 observable readiness를 기다린다 |
| Node client -> dotnet channel server request/reply | `integration-multi-process` | dotnet request handler가 같은 payload 의미로 reply한다 |
| Node client -> dotnet channel server one-way send | `integration-multi-process` | dotnet send handler가 같은 packet 의미로 처리한다 |
| Node publisher -> dotnet fanout subscriber publish | `integration-multi-process` | dotnet publish handler가 같은 topic/payload 의미로 처리한다 |
| dotnet client -> Node channel server | `integration-multi-process` | Node handler가 dotnet client 요청에 같은 payload 의미로 reply한다 |
| Node stream connector -> dotnet stream server | `integration-multi-process` | header session request/reply와 notification dispatch가 동작한다 |
| dotnet connector -> Node stream server | `integration-multi-process` | dotnet connector가 Node `onDispatch`와 `reply` 경로를 통과한다 |

## 9. 문서별 회귀 테스트 단락

이 디렉토리(및 `spec/`)의 각 구현 기준 문서는, 자기 항목이 어떤 테스트로
고정되어 있는지 짧은 `회귀 테스트` 단락을 갖고 있어야 한다. 중앙 matrix 만
갱신해서는 곤란하다. 세부 문서의 독자가 어떤 테스트를 봐야 하는지 놓치기 쉽기
때문이다.

dotnet 의 문서 회귀 테스트처럼, Node 에서도 구현 기준 문서가 자기 회귀 테스트
단락을 유지하는지 확인한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `documentation-regression.test.js › node guide exposes the 12 required guide chapters` | 아래 guide 문서가 모두 `회귀 테스트` 단락을 가진다. |
| `documentation-regression.test.js › node documentation relative markdown links resolve` | 이 matrix를 포함한 Node 문서의 상대 링크가 모두 유효하다. |

> dotnet 의 narrative guide 와 case-study 문서가 strict 집합에서 제외되는 것과
> 동일하게, node 의 사용자 가이드(usability) 계층은 strict 집합 대상이 아니다.
> 현재 node 묶음은 구현 기준 문서(`spec/`, `internals/`, root plan, sample plan)만
> strict 집합으로 둔다.

대상 문서는 현재 `framework/doc/framework/node` 아래에 실제 존재하는 구현용 문서다.
dotnet `aspnet-core-*` 문서는 node 의 `nestjs-*` 대응 문서로 매핑한다.

- `README.ko.md`
- `nestjs-actor.ko.md` (dotnet `actor-gateway-session-relay.ko.md` /
  `aspnet-core-actor.ko.md` 대응)
- `handler-interfaces.ko.md`
- `nestjs-channel-messaging.ko.md` (dotnet `aspnet-core-channel-messaging.ko.md`)
- `nestjs-spot.ko.md` (dotnet `aspnet-core-spot.ko.md`)
- `stage-wrapper-on-spot.ko.md`
- `nestjs-stream.ko.md` (dotnet `aspnet-core-stream.ko.md`)
- `session-actor-dispatch.ko.md`
- `spot-node.ko.md`
- `stream-open-items.ko.md`
- `nestjs-monitoring.ko.md` (dotnet `aspnet-core-monitoring.ko.md`)
- `nestjs-registry.ko.md` (dotnet `aspnet-core-registry.ko.md`)
- `behavior-matrix.ko.md`
- `di-capability-exposure-policy.ko.md`
- `regression-test-matrix.ko.md`
- `lifecycle-and-failure-semantics.ko.md`
- `implementation-scope-and-nongoals.ko.md`
- `backend-dependency-policy.ko.md`
- `channel-messaging-samples.ko.md`
- `spot-samples.ko.md`
- `stream-samples.ko.md`

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^regression]: regression(회귀) 은 이전 버전에서 잘 동작하던 기능이 새 변경 때문에 다시 깨지는 현상을 가리킨다. regression test 는 그런 일을 막기 위해 항상 돌리는 테스트 묶음이다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^abi]: ABI(Application Binary Interface) 조합은 Node.js native addon 이 동작하는 OS·CPU 아키텍처 조합을 가리킨다. 예: `linux-x64`, `darwin-arm64`. `@zlink-systems/zlink` prebuilt artifact 가 이 조합으로 배포된다.
[^ci-gate]: CI gate 는 새 변경을 머지하거나 배포하기 전에 통과해야 하는 자동 검증 단계(빌드, 테스트 등)의 묶음을 가리킨다.
[^di]: DI(Dependency Injection) 는 객체가 필요한 의존성을 직접 만들지 않고 외부 컨테이너에서 주입받도록 하는 패턴이다. NestJS 에서는 module + provider + token 기반 컨테이너가 표준이다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^hwm]: HWM(High Water Mark) 은 송신 큐에 쌓을 수 있는 최대 메시지 수를 가리키며, 이 한계에 도달하면 backpressure 가 발동한다.
[^wire-multipart]: wire multipart 는 한 논리 메시지를 header, payload 등 여러 message part 로 나누어 전송하는 방식이다. 한쪽만 떼어 살펴봐도 라우팅이 가능해진다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^binding-token]: session binding token 은 actor 와 stream session 의 연결 상태를 식별하는 토큰으로, 재연결 시 어느 binding 이 최신인지 구분하는 데 쓰인다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Node.js Lifecycle And Failure Semantics](lifecycle-and-failure-semantics.ko.md) | [다음: ZLink Framework Node.js Implementation Scope And Non-Goals](implementation-scope-and-nongoals.ko.md)
<!-- framework-adapter-nav:bottom:end -->
