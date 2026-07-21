# S3 Channel·fanout amendment 독립 문서 리뷰 — iteration 2

## Snapshot 확인

- 시작 시 `scope-files.sha256` 70개 항목이 모두 `OK`였다.
- `scope-files.sha256` SHA-256은 `72431e8feef1b758a6879fc2f8e866db935f038d32f547cc978ebb1ea633f76f`였다.
- `scope-files.txt` SHA-256은 `0f1172592c455d39fd208e01517133c2b583989ad8deff6e4d66644be71d8208`였다.
- `AGENTS.md`, 문서 원칙, POSD 원칙, manifest와 scope 70개 파일 전체를 읽었다.
- `scripts/verify-framework-doc-contracts.sh`는 `FRAMEWORK DOC CONTRACTS CLEAN`으로 종료했다.

## Findings

[계약][high] framework/doc/framework/spec/02-interaction-model.ko.md:41 — Logical Multicast만 호출자에게 owner `MeshName`을 계속 요구하여 ChannelName이 process-local 송신 경로를 유일하게 선택한다는 공통 계약을 깨뜨린다 — 같은 문서의 35~43행은 호출자가 MeshName을 지정하지 않는다고 하면서 Logical Multicast를 예외로 두고, `05-framework-api.ko.md:111`, `server/20-spot-messaging.ko.md:56` 및 .NET·C++·Java·Node exact interface가 모두 `(MeshName, ChannelName, topic)`을 노출한다 — Logical Multicast도 `ChannelName + topic`만 받고 process-local channel index가 owner MeshNode를 결정하도록 공통 spec과 다섯 언어 exact signature를 함께 정렬하고, verifier에 multicast public signature의 MeshName 금지 검사를 추가한다.

[계약][high] framework/doc/framework/spec/server/languages/dotnet/05-route-mesh.ko.md:147 — manual fanout subscriber의 runtime connection 계약이 다섯 언어 exact interface에서 다르다 — Java `FanoutChannelBuilder.subscriberConnections()`와 Node `ZLinkFanoutChannelBuilder.subscriberConnections()`는 connect·disconnect·list handle을 노출하지만 .NET은 일회성 `ConnectSubscriber(endpoint)`만, C++는 builder `connect(endpoint)`만 제공하며 이 차이는 `90-implementation-gap.ko.md:169`에서도 목표 계약 gap으로 확인된다 — 공통 manual connection 소유·수명·재연결 의미를 먼저 고정하고 .NET·C++ exact builder에 동일 수준의 runtime handle을 반영한 뒤 구현 차이만 gap에 남긴다.

[계약][high] framework/languages/cpp/e2e/PubSub/feature-map.ko.md:15 — Config 3 `PS-A2`의 packet-name typed dispatch 계약을 C++·Java·Kotlin·Node feature map이 topic filtering 시나리오로 바꾸어 기록한다 — 공통 `config-3-pubsub.ko.md:61-71`은 transport topic filter를 public API로 제공하지 않고 서로 다른 packet name의 typed handler가 각각 한 번만 실행되어야 한다. 그런데 Java 18행, Kotlin 9행, Node 6행은 topic accepted/ignored를 `구현`으로 판정하고, verifier 934~948행은 scenario ID 행의 존재만 세어 이 오판을 통과시킨다 — 네 feature map과 runner 목표를 두 packet name·두 typed handler·cross-dispatch 0회로 바꾸고, verifier가 ID 존재 외에 canonical scenario 의미 또는 검증 fixture를 고정하게 한다.

[계약][high] framework/doc/framework/common/e2e/config-3-pubsub.ko.md:157 — `PS-D2`가 store에 존재할 수 없는 subscriber 역할 record를 생성하라고 요구한다 — 정식 `server/40-location-runtime.ko.md:89-92`는 subscriber descriptor를 게시하지 않는다고 규정하고, Redis kind 목록 `server/41-location-store-redis.ko.md:45`에도 subscriber record가 없으며 fanout은 role field를 가진 generic peer record가 아닌 publisher 전용 descriptor를 사용한다 — subscriber-role row를 삽입하는 절차를 제거하고, 다른 ChannelName의 `fanout-publisher`와 유효한 다른 kind row 그리고 schema/compile-negative 검사로 publisher-only model을 검증한다.

[계약][high] framework/doc/framework/common/sample/README.ko.md:90 — TicTacToe가 같은 `tictactoe` MeshName의 네 MeshNode 중 API-A↔API-B 연결을 명시적으로 제외하여 RouteMesh full-mesh 계약과 충돌한다 — `server/10-channel-topology.ko.md:62-64`는 같은 MeshName의 ready MeshNode가 서로 직접 연결되고 각 node가 최대 `N-1` peer를 관리한다고 규정하지만 공통 fixture 14~18행과 verifier 348~357행은 4-node 구성의 6개 pair 중 5개만 고정한다 — API pair에도 단일 canonical initiator를 추가하거나, manual partial graph를 허용하려면 full-mesh 예외·readiness·direct/channel routing 의미를 정식 spec에 먼저 정의한 뒤 sample fixture와 verifier를 같이 바꾸어야 한다.

[계약][medium] framework/doc/framework/spec/server/languages/cpp/60-http-hosting.ko.md:32 — C++ HTTP hosting 흐름이 제거된 `request(mesh_name, channel_name, ...)` 호출을 여전히 표준 예시로 보여 준다 — 같은 exact contract의 `02-framework-interfaces.ko.md:1291-1293`은 `request(channel_name, request, options)`만 선언하며 ChannelName이 egress를 선택하는 공통 계약과도 현재 HTTP 문구가 어긋난다 — diagram의 호출을 exact `request(channel_name, ...)` 시그니처로 교체하고 C++ code fixture가 이 사용 예를 검증하게 한다.

[계약][medium] framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:118 — Java 수동 RouteMesh peer가 remote `RoutingId`를 받지 않는다는 문구가 같은 정식 문서와 exact interface의 expected-RID overload와 충돌한다 — 같은 문서 205~212행은 `connect(RoutingId.from("play-peer"), endpoint)`를 사용하고 endpoint 또는 expected RID+endpoint를 등록한다고 설명하며, exact `ZLinkMeshPeerConnections` 선언도 두 형태를 모두 제공한다 — 118행을 선택적 expected RID가 admission에서 보장하는 공개 의미로 고쳐 exact signature와 일치시킨다.

## 종료 확인

- 종료 hash 검사에서 scope 70개가 모두 `OK`이고 두 목록 hash가 시작 값과 같음을 다시 확인했다.

DOC REVIEW NOT CLEAN
