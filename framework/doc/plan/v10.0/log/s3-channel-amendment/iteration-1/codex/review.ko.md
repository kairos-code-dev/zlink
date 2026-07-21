# S3-CH Channel amendment iteration 1 — Codex 독립 문서 리뷰

## Snapshot과 범위

- 시작 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- 시작 파일별 확인: frozen scope 57/57 `OK`
- 시작 파일 집합 SHA-256: `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d`
- 시작 파일 목록 SHA-256: `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428`
- 루트 `AGENTS.md`, 두 원칙 문서, manifest, scope 목록과 frozen scope 57개 전체를 처음부터 읽고 교차 검토했다. 다른 reviewer 결과는 확인하지 않았다.

## Findings

[계약][blocker] framework/doc/framework/spec/05-framework-api.ko.md:240 — classic fanout subscriber의 같은 ChannelName publisher 자동 발견 계약이 없고 정식 계약은 subscriber가 하나 이상의 manual endpoint를 설정한다고 반대로 고정한다 — `server/40-location-runtime.ko.md:126-137`은 fanout-only host에 Redis가 불필요하다고 하고 `server/13-network-listener-identity.ko.md:58-62`는 publisher endpoint를 location store에 게시하지 않으며, 언어별 exact builder도 자동 발견의 source·identity·lifecycle을 표현하지 않는다. Node의 `ZLinkLocationAutoConnectType.Fanout`(`server/languages/node/02-handler-interfaces.ko.md:570-574`)만 남아 있어 의미가 서로 맞지 않고 CH-E2E-09(`common/e2e/config-12-channel-egress-routing.ko.md:141-146`)도 port 0 연결만 요구할 뿐 같은 ChannelName publisher 자동 발견·재시작을 검증하지 않는다 — fanout 전용 publisher descriptor 또는 별도 discovery owner, ChannelName 일치 규칙, lifecycle/port 0 갱신, store 필요 조건을 정식 spec에 고정하고 다섯 언어 exact interface 및 publisher 시작·재시작 자동 연결 E2E를 추가하라. MeshNode나 ClientServer descriptor를 재사용해서는 안 된다.

[계약][high] framework/doc/framework/spec/server/20-spot-messaging.ko.md:56 — Logical Multicast 대상이 `(MeshName, ChannelName, topic)`으로 정의되어 ChannelName이 process-local 송신 경로를 유일하게 고른다는 계약과 충돌한다 — `server/10-channel-topology.ko.md:41-48`과 이 문서 40-43행은 ChannelName만으로 topology를 선택한다고 하고 공통 call object도 ChannelName과 topic만 요구하지만, 공통 API 표 111행과 .NET 576-582행, C++ 1261-1266행, Java 907-912행, Node 1786-1788행의 exact publisher client는 MeshName을 다시 받는다. Kotlin은 Java 표면을 재사용하므로 동일한 영향이 있다 — Logical Multicast egress도 유일한 local ChannelName index로 선택하도록 공통 계약을 하나로 정하고 모든 exact signature에서 MeshName을 제거한 뒤 compile fixture와 E2E를 맞춰라.

[계약][high] framework/doc/framework/common/e2e/config-12-channel-egress-routing.ko.md:129 — CH-E2E-07이 Server-only ChannelName에는 Client 역할이 없어 outbound egress가 없다고 전제한다 — 정식 topology 계약은 Server가 송신 경로도 등록하고 같은 ChannelName으로 outbound 호출을 시작할 수 있어 Client 중복 등록이 불필요하다고 명시한다(`server/10-channel-topology.ko.md:27-31`). 또한 공통 API는 미등록 route/target snapshot 없음의 `RequestTargetNotFound`와 알려진 target pipe 미준비의 `RouteNotConnected`를 구분한다(`spec/05-framework-api.ko.md:118-121`) — 미등록 ChannelName, Server가 시작하는 정상 outbound, 등록된 경로의 pipe 미준비를 서로 다른 검증으로 나누고 두 오류를 timeout 증가 없이 각각 단정하라.

[계약][high] framework/doc/framework/common/sample/bingo/README.ko.md:223 — Bingo가 API→Play 호출을 필수 흐름으로 선언하면서 API process에는 Play 대상 Channel의 Client 역할을 등록하지 않는다 — 연결 표 124행은 API→Play select-one을 요구하지만 예제에서 API는 `bingo.api` Server만 등록하고 Play는 `bingo.api` Client와 `bingo.room` Server만 등록한다(223-240행). fixture도 `bingo.room` client를 빈 배열로 고정한다(`common/sample/fixtures/channel-topology.json:7-8`). 미등록 ChannelName은 다른 MeshNode에서 찾아 relay하지 않으므로 API→Play가 성립하지 않는다 — API에 실제 Play 대상 Channel의 Client 역할을 등록하고 흐름·예제·fixture·언어별 sample을 같은 이름으로 맞춰라.

[계약][high] framework/doc/framework/common/sample/tictactoe/README.ko.md:308 — TicTacToe 상세 문서는 API→Play와 Play→API 각각에 수동 peer 연결을 설정해 reciprocal 중복 연결을 요구한다 — 공통 sample 기준과 fixture는 API-A/B→Play-A/B 및 Play-A→Play-B만 initiator로 두고 Play→API 요청은 이미 설정된 양방향 ROUTER pipe를 재사용하며 전용 connect를 금지한다(`common/sample/README.ko.md:90-92`, `common/sample/fixtures/channel-topology.json:14-17`). 상세 문서 113·123행도 양쪽 endpoint 설정을 반복한다 — Channel Client 역할과 물리 connect initiator를 분리해 설명하고 Play→API 전용 peer 설정을 113·123·309행에서 제거하라.

[계약][high] framework/doc/framework/common/sample/zoneworld/README.ko.md:652 — Node direct 호출을 `zoneworld.ops.<NodeId>`라는 노드별 ChannelName으로 선택한다고 정의해 존재하지 않는 공개 주소 방식을 만든다 — 정식 API에서 Node direct 대상은 MeshName context와 target RID이고 Channel call만 ChannelName을 사용한다(`spec/05-framework-api.ko.md:105-112`). 그런데 780·787-788행은 이 이름으로 특정 node만 선택하며 일반 ChannelName처럼 분산되지 않는다고 설명하고 topology fixture에는 해당 Channel 등록도 없다(`common/sample/fixtures/channel-topology.json:56-64`) — 관측한 target RID와 MeshName을 사용하는 Node direct로 고치고 `zoneworld.ops.*` Channel 표기와 암묵적 NodeId→route 변환을 제거하라.

[계약][high] framework/doc/framework/common/sample/fixtures/channel-topology.json:43 — executable topology fixture가 `shoppingmall.workflow.owner.*`와 `gamequest.mission.*`를 실제 ClientServer/ChannelName 값으로 고정하지만 정식 계약에는 wildcard ChannelName이나 startup 뒤 동적 역할 등록 의미가 없다 — ChannelName은 startup 시 immutable role set과 하나의 process-local topology에 등록되어야 한다(`spec/server/10-channel-topology.ko.md:37-54`). ShoppingMall은 CommerceApi가 owner별 ClientServer Client라고 하면서 OrderId를 어느 startup 등록 ChannelName으로 바꾸는지 정의하지 않는다(`common/sample/event/shoppingmall.ko.md:266-269,291`) — `*`가 문서용 placeholder라면 fixture에서 제거하고 유한한 shard/owner ChannelName 생성·등록·선택 규칙을 정식 owner에 먼저 정의하라. 그런 계약이 없으면 이미 정식 계약에 있는 RouteMesh/Spot 주소 방식을 사용하도록 sample topology를 다시 정하라.

[계약][medium] framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:1597 — Java exact 문서가 수동 연결을 `channel + capability` 단위로 관리한다고 규정한다 — 같은 언어의 system structure는 RouteMesh 수동 연결이 MeshNode peer intent이며 모든 ChannelName이 ROUTER peer 연결 하나를 공유하고 channel/capability별 연결 집합을 금지한다(`server/languages/java/01-system-structure.ko.md:114-119`). 공통 topology도 ChannelName 수가 peer 연결 수를 늘리지 않는다고 고정한다(`server/10-channel-topology.ko.md:62-64`) — 1597-1600행의 낡은 규칙을 MeshNode `peerConnections()` 하나의 runtime connect/disconnect/list 계약으로 교체하고 classic fanout subscriber endpoint 집합만 별도임을 명시하라.

[계약][high] framework/doc/framework/spec/server/languages/cpp/60-http-hosting.ko.md:32 — C++ 정식 예제가 `request_client_t.request(mesh_name, channel_name, ...)`를 호출해 같은 exact 문서 집합의 ChannelName-only signature와 맞지 않는다 — `request_client_t` 선언은 `request(channel_name, request, options)`만 제공한다(`server/languages/cpp/02-framework-interfaces.ko.md:1269-1278`). HTTP handler 138-141행과 interface 문서 예제 2169-2173행도 제거된 MeshName 인자를 반복하므로 독자가 그대로 컴파일할 수 없다 — 모든 C++ 정식 예제를 ChannelName-only 호출로 고치고 이 예제들을 inventory가 확인하는 compile code fixture에 포함하라.

[원칙][high] scripts/verify-framework-doc-contracts.sh:257 — verifier가 공통 sample topology fixture의 일곱 sample 이름과 파일 hash만 확인하고 channel 역할·peer initiator·wildcard 부재·문서 일치를 전혀 검증하지 않는다 — 현재 Bingo의 누락된 API→Play client, TicTacToe의 중복 peer 설명, wildcard pseudo ChannelName이 모두 통과한다. 같은 방식으로 inventory의 단편 문자열 확인은 Logical Multicast의 MeshName 재노출과 fanout 자동 발견 계약·E2E 누락을 고정하지 못한다 — sample별 routeMeshes/clientServer/channel clients·servers/manualPeerInitiators를 semantic assertion으로 비교하고, ChannelName-only multicast exact signature와 fanout discovery owner·다섯 언어 API·E2E scenario를 구조적으로 확인하는 gate를 추가하라. Hash는 review 승인 추적에만 쓰고 계약 의미 검증을 대신하지 않게 하라.

## 종료 Snapshot

- 종료 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- 종료 파일별 확인: frozen scope 57/57 `OK`
- 종료 파일 집합 SHA-256: `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d`
- 종료 파일 목록 SHA-256: `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428`
- 문서 리뷰만 수행했으며 build와 runtime test는 실행하지 않았다.

DOC REVIEW NOT CLEAN
