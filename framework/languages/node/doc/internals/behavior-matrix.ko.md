<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: .NET → Node.js 표면 매핑 정책](./dotnet-to-node-surface-mapping.ko.md) | [다음: ZLink Framework Node.js DI Capability Exposure Policy](./di-capability-exposure-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node 묶음](../README.ko.md) | [표면 매핑 정책](./dotnet-to-node-surface-mapping.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [channel](../spec/nestjs-channel-messaging.ko.md) | [SPOT](../spec/nestjs-spot.ko.md) | [STREAM](../spec/nestjs-stream.ko.md)

# ZLink Framework Node.js Behavior Matrix

> 이 문서의 **동작(behavior) 행은 백엔드와 무관하며 .NET 버전과 완전히
> 동일하다.** Node.js / NestJS / TypeScript 표면(decorator, options 키, TS 타입
> 등)만 옮겼다. 같은 channel/packet 으로 붙으면 .NET 서비스와 Node 서비스가
> 동일한 판정과 동일한 런타임 동작을 보여야 한다. 표기와 코드가 어긋나면 dotnet
> **코드**(`framework/languages/dotnet/src`)가 기능의 최종 기준이다. 번역 규칙은
> [표면 매핑 정책](./dotnet-to-node-surface-mapping.ko.md)이 소유한다.

## 1. 목적

인터페이스 정의와 샘플 코드만 봐서는 다음 같은 질문에 답하기 어려울 수 있다.

- "이 조합을 허용해도 되는가"
- "무엇을 startup 실패로 봐야 하는가"

이 문서의 목적은 구현자가 registration surface[^registration-surface] 를 만들
때, 같은 입력에 대해 항상 같은 판정을 내리도록 기준을 미리 고정해 두는 것이다.

## 2. 공통 원칙

각 역할 표를 읽기 전에, 표 전체에 공통으로 적용되는 다음 네 가지를 먼저
짚어 둔다.

- 등록 단계에서 이미 판정할 수 있는 설정 오류는 host 가 시작되기 전에
  fail-fast[^fail-fast] 한다.
- 같은 역할 안에서는 `discovery` 기반 자동 연결과 manual 연결을 섞지
  않는다.
- outbound 역할에 manual 연결도 없고 discovery 등록도 없으면 거부한다.
  peer 를 어디서 가져올지 알 길이 없기 때문이다.
- 같은 항목을 중복으로 등록한 경우, 조용히 덮어쓰지 않고 예외로 처리한다.

## 3. Channel Capability Matrix

다음 표는 channel 의 역할 조합별 허용 여부와 기대 동작을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `.enableServer(endpoint)`만 등록 | 허용 | server 역할만 열리고, handler 매핑이 없으면 처리할 packet도 없다 |
| `.enableServer(undefined)`처럼 bind endpoint 없음 | 비허용 | startup validation 오류 |
| `.enableClient()` + 전역 `.useDiscovery()` 있음 | 허용 | outbound request/send runtime을 만든다 |
| `.enableClient(endpoint)`만 등록 | 허용 | manual 기반 outbound request/send runtime을 만든다 |
| `.enableClient()` + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| `.enablePublisher(endpoint)`만 등록 | 허용 | event publish만 가능하다 |
| `.enablePublisher(undefined)`처럼 bind endpoint 없음 | 비허용 | startup validation 오류 |
| `.enableSubscriber()` + 전역 `.useDiscovery()` 있음 | 허용 | discovery 기반 event subscribe runtime을 만든다 |
| `.enableSubscriber(endpoint)`만 등록 | 허용 | manual 기반 subscribe runtime을 만든다 |
| `.enableSubscriber()` + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| 같은 channel에서 `server + client` 함께 등록 | 허용 | inbound와 outbound runtime을 모두 가진다 |
| 같은 channel에서 `publisher + subscriber` 함께 등록 | 허용 | event fan-out과 수신을 모두 가진다 |
| 같은 channel 역할 안에서 discovery + manual 함께 등록 | 비허용 | startup validation 오류. 단, routed Spot route mesh egress 는 수동 연결을 실제 transport 로 쓰고 discovery/query 를 target ROUTER `RoutingId` metadata 조회에만 쓰는 좁은 예외를 둔다 |
| 같은 channel server에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 다른 channel server에 같은 `kind + packetName` handler 등록 | 허용 | channel별로 handler namespace가 분리되어 있다 |
| `handlerGroups: ['...']`로 명시한 그룹의 handler만 그 channel에서 dispatch | 허용 | handler class decorator 의 group 과 channel 매핑을 조합해서 노출 범위를 제한한다 |
| 같은 channel에 여러 그룹 매핑 | 허용 | `handlerGroups`에 여러 그룹을 넣어 그룹들의 합집합을 한 채널에 노출한다 |
| `handlerGroups`가 가리키는 그룹에 handler 0개 | 비허용 | startup validation 오류 |
| client-server channel에서 `spotRouteEgress`만 등록하고 client 역할 없음 | 비허용 | routed Spot egress 는 local client DEALER 가 필요하다 |
| route mesh channel에서 `spotRouteEgress` 등록 | 허용 | target SpotNode ingress channel 로 routed Spot relay 를 보낼 수 있다. 실제 target ROUTER 연결과 target ROUTER `RoutingId` metadata 가 모두 필요하다 |

## 4. Spot Capability Matrix

다음 표는 SPOT 의 등록 조합별 허용 여부와 기대 동작을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `.addSpotNode(name)` | 허용 | 활성 SPOT[^spot] channel view와 node 런타임을 함께 소유한다 |
| spot mesh에 `discovery` 없음 | 허용 | top-level discovery endpoint 를 상속하거나 local-only mesh 로 시작한다 |
| spot mesh + 빈 `discovery` + local-only spot factory | 허용 | discovery endpoint 없이 단일 local SpotNode 하나를 mesh 소유권 아래 띄운다 |
| top-level standalone node 등록 | 비허용 | public 등록 표면에서 제거되었다. SPOT node 는 항상 spot mesh 안에서 등록한다 |
| 분리된 SPOT discovery 등록과 node 등록 | 비허용 | public 등록 표면에서 제거되었다. discovery 와 node 집합은 spot mesh가 함께 소유한다 |
| 같은 mesh에 `nodes` 여러 개 | 허용 | 같은 channel view를 공유하는 여러 SpotNode를 등록한다 |
| 같은 mesh의 router-capable node를 stream ActorGateway 로 참조 | 허용 | session relay ingress 를 일반 SpotNode router 역할로 시작한다 |
| 같은 `SpotNode`에 같은 `spotRid` factory 중복 등록 | 비허용 | startup validation 오류 |
| 같은 `SpotNode`에 Entry Spot[^entry-spot] registry 중복 등록 | 비허용 | startup validation 오류 |
| `router` 역할만 등록 | 허용 | inbound routed call만 받는다 |
| attach된 channel client 역할 등록 + channel discovery/manual 경로 있음 | 허용 | spot 내부에서 outbound channel 호출이 가능하다 |
| attach된 channel client 역할 등록 + channel peer acquisition 경로 없음 | 비허용 | startup validation 오류 |
| local spot factory 없는 외부 publish node는 `ZLinkSpotPublisherClient` 사용 | 허용 | Spot publisher client 역할만 둔 `SpotNode` 로 특정 SPOT channel publish만 수행한다 |

## 5. Stream Node Matrix

다음 표는 stream node 등록의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `registerSession<T>()` 하나 등록 | 허용 | zlink stream header session node를 만든다 |
| 같은 node에 stream session을 둘 이상 등록 | 비허용 | startup validation 오류 |
| bind endpoint 없음 | 비허용 | startup validation 오류 |

### 5.1 Session Actor Dispatch Matrix

다음 표는 session actor dispatch 와 routed channel 의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `.addRouteMeshChannel(name)` + 전역 `.useDiscovery()` 있음 | 허용 | discovery 기반 routed channel node를 만든다 |
| `.addRouteMeshChannel(name).enableClient(endpoint)` 있음 | 허용 | manual 기반 routed channel node를 만든다 |
| `routeMesh` channel + discovery/manual 둘 다 없음 | 비허용 | startup validation 오류 |
| routed channel bind endpoint 없음 | 비허용 | startup validation 오류 |
| 같은 routed channel에 같은 `kind + packetName` handler 중복 | 비허용 | startup validation 오류 |
| 같은 actor type factory 중복 등록 | 비허용 | builder 등록 시점에 오류 |
| session actor attach 중 application resolver fallback 사용 | 비허용 | session relay 는 logical actor handle 을 저장하고, 실제 위치 해석은 core ActorGateway 로 일원화한다 |
| route 없는 `bind(actor, ...)` 로 remote actor bind | 비허용 | 이 overload 는 local actor instance 경로로만 동작하고, local actor 가 없으면 `ActorRouteNotFound` 로 실패한다 |
| 같은 actor id가 새 stream session에서 다시 bind | 허용 | 기존 actor 인스턴스를 재사용하고 session binding token[^binding-token]만 갱신한다 |
| actor factory가 요청 actor id와 다른 id를 반환 | 비허용 | actor route와 binding id가 갈라지므로 actor 생성 오류 |
| `actor.context.boundSession.send(...)` 가 오래된 binding이나 이미 닫힌 stream에 도착 | 허용된 실패 | 해당 push만 실패하고, route loop와 host shutdown은 계속 진행한다 |
| converter 없는 abstract/interface payload를 reply DTO에 포함 | 비허용 | startup validation 또는 첫 submit 직전에 configuration 오류 |
| spot remote address resolver 중복 등록 | 비허용 | builder 등록 시점에 오류 |
| Registry Spot route 기본 구현 + custom Spot remote address resolver 함께 등록 | 비허용 | startup validation 오류 |
| Registry route 기본 구현 + `discovery: {...}` 없음 | 비허용 | startup validation 오류 |
| Registry route 기본 구현 + route mesh channel이 둘 이상이고 channel id 생략 | 비허용 | startup validation 오류 |
| spot rid 기반 routed Spot client 사용 + spot remote address resolver 없음 | 비허용 | service 생성 또는 첫 호출에서 명확한 오류 |
| `ZLinkBoundSession` 사용 + actor-session binding 없음 | 비허용 | 대상 actor에 묶인 session이 없으면 명확한 오류 |

## 6. Monitoring Registration Matrix

다음 표는 monitoring 등록의 허용 조합을 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `monitoring: {...}` + 등록된 socket source 이름 | 허용 | 해당 source에 event handler를 연결한다 |
| `monitoring: {...}` + 등록된 registry source 이름 | 허용 | snapshot diff polling[^snapshot-diff-polling]을 시작한다 |
| `monitoring: {...}` + 등록된 spot source 이름 | 허용 | snapshot diff polling을 시작한다 |
| `monitoring: {...}` + discovery source 이름 | 비허용 | discovery 상태는 registry snapshot/query로 조회하는 것이 원칙이다 |
| 존재하지 않는 source 이름 등록 | 비허용 | startup validation 오류 |
| polling source인데 interval이 0 이하 | 비허용 | startup validation 오류 |

## 7. Registry Matrix

다음 표는 Registry 등록 조합의 허용 여부를 정리한다.

| 조합 | 허용 여부 | 기대 동작 |
|------|-----------|-----------|
| `ZLinkRegistryModule.forRoot(...)`만 등록 | 허용 | standalone registry host가 된다 |
| `ZLinkModule.forRoot(...)` + `ZLinkRegistryModule.forRoot(...)` 함께 등록 | 허용 | embedded registry와 framework runtime을 한 호스트에서 함께 띄운다 |
| embedded 구성에서 `discovery` endpoint를 자동 추론 | 비허용 | endpoint는 반드시 명시적으로 적어야 한다 |
| `ZLinkRegistryQueryClientModule.forRoot(...)`만 등록 | 허용 | 원격 topology[^topology] 조회만 수행한다 |

## 8. Startup Validation Error 목록

아래 항목은 구현체가 공통적으로 예외를 던져야 하는 잘못된 입력이다.

- duplicate channel 이름
- duplicate `spotNodeName`
- duplicate `spotRid` factory
- outbound 역할에 discovery/manual 경로가 둘 다 없는 경우
- 같은 역할 안에서 discovery/manual 혼용
- monitoring 등록 시 존재하지 않는 source를 지정한 경우
- 같은 stream node에 session을 중복 등록한 경우
- bind endpoint가 없는 stream node
- routed channel bind endpoint가 없는 경우
- 같은 channel server에서 `kind + packetName`이 같은 handler 중복
- 같은 routed channel에서 `kind + packetName`이 같은 handler 중복

이 오류들은 런타임에 뒤늦게 드러내지 않고, startup 단계에서 미리 막는 것이
기본이다.

다만 actor resolver 누락처럼 실제로 service 가 사용되는 시점이 되어야 드러나는
항목도 있다. 이런 항목은 해당 service 가 생성되거나 처음 호출되는 시점에, 같은
error family 로 명확하게 실패시킨다.

## 9. 회귀 테스트

Behavior Matrix 는 허용 조합과 비허용 조합을 테스트 이름 단위로 고정한다. 새
역할 조합을 추가할 때는 표만 늘리지 않는다. startup validation 또는
runtime integration 테스트도 같은 변경에 함께 포함시킨다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ChannelsTests.forRoot_Throws_WhenRouteChannelMixesDiscoveryAndManualConnections` | 같은 routed 역할에서 discovery와 manual 연결을 섞으면 실패한다. |
| `HandlerExposureTests.forRoot_Throws_WhenServerHasNoBindEndpoint` | server 역할에 bind endpoint가 없으면 실패한다. |
| `RegistryAndMonitoringTests.forRoot_Throws_WhenPublisherHasNoBindEndpoint` | publisher 역할에 bind endpoint가 없으면 실패한다. |
| `NodesAndServicesTests.forRoot_AllowsStandaloneLocalSpotNode` | discovery mesh 없이 local-only SpotNode를 단독으로 시작할 수 있다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, client, publisher, subscriber)를 가리킨다.
[^startup-validation]: startup validation 은 host가 시작되기 전에 등록된 설정을 검사해 잘못된 조합을 미리 거부하는 단계다.
[^registration-surface]: registration surface 는 DI 컨테이너에 framework 구성 요소를 등록할 때 사용자가 쓰는 module options 묶음을 가리킨다.
[^fail-fast]: fail-fast 는 잘못된 설정이나 상태를 발견하면 즉시 예외를 던지고 실행을 멈추는 전략이다. 늦게 발견되어 더 큰 문제로 번지는 것을 막는다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.
[^binding-token]: session binding token 은 actor 와 stream session 의 연결 상태를 식별하는 토큰으로, 재연결 시 어느 binding 이 최신인지 구분하는 데 쓰인다.
[^snapshot-diff-polling]: snapshot diff polling 은 일정 주기로 전체 스냅샷을 가져온 뒤 직전 스냅샷과 비교해 변경된 부분만 이벤트로 올리는 방식이다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
</content>
</invoke>

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: .NET → Node.js 표면 매핑 정책](./dotnet-to-node-surface-mapping.ko.md) | [다음: ZLink Framework Node.js DI Capability Exposure Policy](./di-capability-exposure-policy.ko.md)
<!-- framework-adapter-nav:bottom:end -->
