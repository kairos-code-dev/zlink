# S3 Channel amendment 독립 문서 리뷰 — iteration 1 (Claude Sonnet)

- provider: Anthropic
- model: claude-sonnet-5 (Sonnet 5)
- session id: 99fd015e-2fcd-4edd-8812-bcd0c1b81413
- 시작 hash 확인: `sha256sum -c scope-files.sha256` → 57/57 OK. `scope-files.sha256` aggregate =
  `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d` (prompt 값과 일치).
  `scope-files.txt` aggregate = `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428`
  (prompt 값과 일치). SNAPSHOT DRIFT 없음.
- 57개 scope 파일(core spec 2, framework 공통 spec 6, framework server spec 12, 언어별 exact
  interface 25, Config 12 E2E·fixture·inventory·verifier 6, 공통 sample 문서 7개 + fixture 1,
  검증 스크립트 1)을 전부 처음부터 읽었다. 다른 reviewer의 결과나 coordinator 해석은 참고하지
  않았다.

## Finding

[계약][medium] framework/doc/framework/spec/server/41-location-store-redis.ko.md:46-110 —
같은 문서가 MeshNode descriptor(§2.1)와 Actor location(§2)에는 각각
`[mesh-node-descriptor-v1.json]`, `[actor-location-v2.json]` byte-for-byte fixture 링크를
명시하지만, 이번 amendment로 신설된 ClientServer server descriptor(§2.1.1, `channel-server` key
kind, `ChannelName+ServerRid` length-prefix row key, 고정 field 순서 JSON)에는 canonical JSON
예시만 있고 대응하는 byte-for-byte fixture 링크가 없다. `framework/testdata/location/redis/`
디렉토리에도 `channel-server` 계열 fixture 파일이 실제로 존재하지 않는다(`find`로 확인:
`mesh-node-descriptor-v1.json`, `actor-transfer-v1.json`, `actor-location-v2.json` 세 개뿐). —
근거: `doc/principal/documentation/documentation-principles.ko.md` 원칙 5("규칙에는 강제 장치를
함께 만든다" — 원본·미러 동기 규칙에는 일치 검사가 있어야 한다)와 이 문서 자신이 다른 두 record
kind에 세운 선례(§2.1의 `mesh-node-descriptor-v1.json`, §2의 `actor-location-v2.json`). 다섯 언어
공식 Redis extension이 같은 key·HASH field·JSON을 byte-for-byte로 만들어야 한다는 요구(§2.1.1
"모든 공식 extension은 이 row와 key 형식을 동일하게 사용한다")가 있는데 이를 언어 간에 실제로
고정할 검증 fixture가 없다. — 제안: `framework/testdata/location/redis/`에
`channel-server-descriptor-v1.json` 계열 fixture를 추가하고 41장 §2.1.1에 링크를 단다. 이후
`route-mesh-v10-contract-inventory.json`의 `redis_fixtures`에도 hash-lock 항목을 추가해야 다섯
언어 구현이 실제로 이 fixture를 참조하도록 강제할 수 있다(현재 `redis_fixtures`에는 mesh-node·
actor-location·actor-transfer 세 개만 있다).

[원칙][medium] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:2298-2531
— node/04-location-store.ko.md:8-227 — Node.js `02-handler-interfaces.ko.md` §2.31("Location
authority 보충 계약")이 `ZLinkLocationOptions`, `ZLinkMeshNodeDescriptor`,
`ZLinkClientServerServerDescriptor`, `ZLinkSpotLocation`, `ZLinkActorLocation`,
`ZLinkMeshNodeLocationStore`, `ZLinkClientServerLocationStore`, `ZLinkSpotLocationStore`,
`ZLinkActorLocationStore`, `ZLinkOwnerLeaseStore`, `ZLinkActorTransferStore`,
`ZLinkLocationStore`까지 location store 공개 계약 전체를 필드 단위로 다시 선언한다. 그런데 같은
계약을 `node/04-location-store.ko.md`가 "MeshNode·ClientServer descriptor, location·lease·transfer
authority와 공식 Redis 구현"이라는 이름으로 별도 소유 문서로 독립 정의한다(README.ko.md의 문서
목차 표도 두 문서 모두를 계약 owner처럼 나열한다). 두 선언을 diff한 결과 필드 순서만 다르고 현재
드리프트는 없지만, 완전히 같은 타입 집합을 두 문서가 각각 원본처럼 들고 있어 한쪽만 고치면 조용히
어긋나는 구조다. .NET·Java·Kotlin·C++는 location store 타입을 `05-route-mesh`/`02-handler-interfaces`
같은 등록 문서에 반복하지 않고 전용 location-store 문서(`06-location-store.ko.md`,
`03-location-store.ko.md`) 하나에만 둔다 — Node만 이 구조에서 벗어나 있다. — 근거:
`doc/principal/documentation/documentation-principles.ko.md` 원칙 2("중복을 없애되... 원본을
명시하고 함께 고칠 목록에 넣는다" — 여기서는 원본 지정도, 상호 참조도 없다)와
`90-implementation-gap.ko.md` §14 "문서 소유권 중복"이 이미 같은 유형의 문제(두 문서가 같은
계약을 각각 소유)를 카탈로그화하고 있음에도 이 Node 사례는 그 표에 없다 — 알려진 문제 패턴의
미등재 사례다. — 제안: `02-handler-interfaces.ko.md` §2.31을 `04-location-store.ko.md`로 옮기고,
02번 문서에는 "location store capability는 04가 소유한다"는 참조 문장만 남긴다. 또는 반대로
04번을 삭제하고 02 한 곳에 모은 뒤 README 목차를 갱신한다.

[원칙][low] framework/doc/framework/common/sample/event/shoppingmall.ko.md:267 —
`framework/doc/framework/common/sample/fixtures/channel-topology.json`의 ShoppingMall
`clientServer` 항목과 `channels` key가 `"shoppingmall.workflow.owner.*"`라는, 리터럴
와일드카드 문자 `*`를 포함한 문자열을 ChannelName처럼 사용한다. sample README 267행은 이를
"owner별 ClientServer Client"로만 설명하고, 이 이름이 (a) `*`까지 포함한 리터럴 고정
ChannelName 하나인지, (b) 문서가 주문마다 동적으로 만들어지는 ChannelName 계열을 표기하기 위해
쓴 축약 표기인지 명시하지 않는다. 공통 계약(`10-channel-topology.ko.md` §2, §8)은 ChannelName
Client·Server role set이 "startup 뒤 변경할 수 없다"고 고정하므로, (b)로 읽히면 이 샘플이 정식
계약 밖의 동적 ChannelName 생성을 암시하는 것처럼 보인다. GameQuest fixture의
`"gamequest.mission.*"`도 같은 표기를 쓰지만 해당 sample 문서 본문에는 이 이름에 대한 설명이
전혀 없다. — 근거: `documentation-principles.ko.md` 원칙 4("예제의 API는 실재해야 한다" 및
검증 가능성)와 `10-channel-topology.ko.md` §2("Client와 Server role set은 startup 뒤 변경할 수
없다"). — 제안: sample README 또는 fixture 주석에 `*`가 리터럴 문자인지, 아니면 문서 표기
관례(예: 여러 채널을 대표하는 placeholder)인지 한 문장으로 명시한다. 실제로는 정황상(§6 "Workflow
server는 자기 owner Channel의 handler와 내부 RouteMesh의 Spot·projection 처리를 소유" — 즉 어느
Workflow server가 요청을 받아도 내부 Spot addressing으로 실제 owner Spot에 다시 라우팅되므로
ChannelName 자체는 정적일 가능성이 높다) 계약 위반이 아니라 표기 모호성일 가능성이 크므로 severity를
low로 둔다.

## 종료

- 종료 hash 확인: `sha256sum -c scope-files.sha256` → 57/57 OK. `scope-files.sha256` aggregate =
  `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d`. `scope-files.txt` aggregate =
  `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428`. 시작 값과 동일 — SNAPSHOT
  DRIFT 없음.
- 위 세 finding 모두 medium 이하이며 새 public API를 요구하거나 §1~§10 질문이 묻는 핵심 계약(무
  membership MeshNode, ChannelName 단일 주소, RouteMesh/ClientServer 역할 분리, 방향 제한,
  request completion 정확히 한 번, BindHost/AdvertiseHost 분리, 다섯 언어 exact interface, Config
  12, 정식 spec의 현재-상태-전용 서술)에서 모순이나 원칙 위반을 발견하지 못했다. 57개 문서는
  서로 강하게 정합적이었고, `90-implementation-gap.ko.md`가 기록한 미구현 항목(§12.33, §12.39
  등)은 모두 "구현이 아직 안 됨"만 기록할 뿐 목표 계약 자체를 축소하지 않았다.

DOC REVIEW NOT CLEAN
