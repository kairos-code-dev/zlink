<!-- framework-adapter-nav:start -->
[스펙 목차](../README.ko.md) | [이전: Stream Connector](../stream-connector/32-stream-connector.ko.md) | [다음: Location Store](41-location-store-redis.ko.md)
<!-- framework-adapter-nav:end -->


# Location Runtime

이 문서는 framework의 **분산 위치 관리(location runtime)** 언어 중립 공통 스펙이다. 위치 모델
(peer/spot/actor/route row), owner lease와 generation, store/resolver 계약, 자동 연결 규칙,
watch/polling, 운영 조회 모델의 의미는 이 문서가 소유한다. 다른 spec 문서는 이 문서를
링크하고 세부 모델을 반복하지 않는다. 네이밍은 [framework API](../05-framework-api.ko.md)와
[공개 계약 관리 §4](../00-public-contract-governance.ko.md#4-언어별-표현-원칙)의 언어별 표현 원칙을 따른다.

> core discovery/registry는 framework 표면에서 제거됐다. 위치 정보의 기준 저장소는 사용자가
> 등록하는 **location store**(공식 Redis extension 또는 사용자 구현체)이며, framework runtime이
> 그 위에서 lease join, generation guard, 자동 연결을 수행한다.

## 0. 용어 읽는 법

이 문서의 영어 용어는 API 이름과 여러 언어 구현이 함께 맞춰야 하는 계약 이름이다. 본문에서는 아래
뜻으로 읽는다.

| 용어 | 뜻 |
|------|----|
| row | store에 저장된 위치 정보 한 건이다. peer row, actor row처럼 종류별로 나뉜다. |
| owner lease | row를 쓴 runtime이 아직 유효한지 판단하기 위한 임대 기록이다. |
| generation | 같은 key가 다시 등록될 때 이전 row와 새 row를 구분하는 세대 값이다. |
| lease join | row의 `OwnerId`와 owner lease 목록을 대조해서 row의 owner lease가 유효한지 확인하는 과정이다. |
| generation guard | owner와 generation이 맞을 때만 갱신·삭제를 허용해 오래된 write를 막는 규칙이다. |
| watch | store가 변경을 알려 주는 방식이다. 지원하지 않는 store도 있을 수 있다. |
| polling | runtime이 주기적으로 store를 다시 읽어 변경 여부를 확인하는 방식이다. watch가 없어도 동작해야 하는 기본 경로다. |
| change stamp | 특정 범위의 row가 바뀔 때마다 증가하는 번호다. 변경이 없으면 전체 목록 조회를 건너뛰기 위해 쓴다. |
| fail-static | store 장애 중 마지막으로 성공한 연결 판단을 유지하고 새 connect/disconnect 계산을 멈추는 정책이다. |

## 1. 기본 원칙

- **store는 저장만, 정책은 runtime이.** store 구현체는 row 저장/조회/원자적 write만 책임진다.
  owner lease join, generation guard, 자동 연결 diff는 framework runtime의 책임이다.
- **임의 TTL cache 없음.** 새 handle을 만드는 resolver와 운영 조회는 store에 도달한다.
  메시징 handle의 주소 snapshot은 location event와 안전한 stale 실패에서 framework가
  갱신하며 application에 cache TTL이나 재조회 순서를 노출하지 않는다.
- **생존은 owner lease로.** row 자체는 생존을 증명하지 않는다. row owner의 lease가 만료되면
  그 owner의 모든 row는 성공 결과에서 제외된다. 이런 row를 stale row, 즉 더 이상 성공 결과로 쓰면
  안 되는 오래된 row라고 부른다. 물리 삭제는 background cleanup의 책임이고 계약 대상이 아니다.
- **시간은 store 기준.** lease 만료 판정에 application node의 wall clock을 쓰지 않는다.
  호출자는 TTL만 전달하고 절대 만료 시각은 store가 자기 시계로 계산한다.
- **fail-static.** store 장애는 즉시 연결 해제로 번역되지 않는다. 마지막으로 성공한 연결 대상
  목록을 유지하고, 이미 수립된 연결의 메시징은 store와 독립적으로 계속 동작한다.

## 2. 위치 모델

네 종류의 location row가 있다. 모든 row는 owner-bound다: `OwnerId`(framework runtime instance
id)와 `Generation`(fencing token)을 가진다.

### 2.0 닫힌 값 집합

location row, Redis row JSON, 운영 조회가 쓰는 enum 값은 언어별 ordinal에 맡기지 않고 아래
숫자로 고정한다. 저장소 key가 문자열을 써야 할 때는 표의 "공통 문자열"만 사용한다. 공통 문자열은
모든 언어 구현이 store에 같은 값을 쓰도록 고정한 문자열이다.

| enum | 값 | 공통 문자열 |
|------|-----|------------------|
| `LocationAutoConnectType` | `Invalid=0`, `RouteMesh=1`, `ClientServer=2`, `DealerMesh=3`, `Fanout=4`, `SpotMesh=5` | `route-mesh` / `client-server` / `dealer-mesh` / `fanout` / `spot-mesh` |
| `LocationRole` | `Invalid=0`, `Spot=2`, `Router=3`, `Dealer=4`, `Pub=5`, `Sub=6` | `spot` / `router` / `dealer` / `pub` / `sub` |
| `RouteKind` | `Invalid=0`, `ActorSession=1`, `SpotName=2`, `FrameworkRoute=3` | - |
| `LocationKind` | `Invalid=0`, `Peer=1`, `Spot=2`, `Actor=3`, `Route=4` | `peer` / `spot` / `actor` / `route` |
| `WriteIntent` | `NewClaim=1`, `Renew=2`, `Takeover=3` | - |
| `WriteStatus` | `Stored=1`, `IgnoredStale=2`, `RejectedConflict=3` | - |
| `LocationChangeType` | `Upserted=1`, `Removed=2`, `Expired=3` | - |
| `LocationTopologyState` | `Discovered=1`, `Connecting=2`, `Ready=3`, `Lost=4`, `Error=5`, `Stopped=6` | - |
| `SpotKind` | `Invalid=0`, `Entry=1`, `User=2` | - |

`LocationRole`은 core service role의 `uint16` 값과 맞춘다. 값 `1`은 제거된 gateway role의
예약 결번이며 다시 쓰지 않는다. 이 숫자는 core wire와 Redis row JSON에 드러나므로 변경할 수 없다.

**`LocationKind` enum과 actor row의 `LocationKind` 필드는 서로 다른 값 집합이다.** 혼동하지 않는다.

| 이름 | 용도 | 값 |
|------|------|-----|
| `LocationKind` **enum** | store row의 **종류 태그**. watch·stamp·key prefix 선택에 쓴다 | `Peer=1`, `Spot=2`, `Actor=3`, `Route=4` |
| actor row의 `LocationKind` **필드**(§2.3) | 그 actor가 들어 있는 **spot의 종류**. 타입은 `SpotKind`다 | `Entry=1`, `User=2` |

`LocationAutoConnectType.DealerMesh`는 location 계층에만 존재한다. channel 등록 API는 이 값을
받지 않으므로 application이 dealer mesh channel을 만들 수는 없다
([10 channel topology §5](10-channel-topology.ko.md)).

### 2.1 peer location

자동 연결에 필요한 node endpoint 정보. `AutoConnectType`(route mesh, client/server, dealer
mesh, fanout, spot mesh), `MeshName`, `NodeRid`, `Role`(router/dealer/pub/sub/spot),
`Endpoint`, `Weight`(0..100), `Draining`(bool, 기본 false), `Value`, `Metadata`, `Capabilities`,
`OwnerId`, `Generation`, `UpdatedAt`. node lifecycle과 heartbeat가 자동 갱신한다.

`AutoConnectType=RouteMesh`인 row의 `Role`은 항상 `Router`다. endpoint가 없는
RouteMesh 구성원도 `Dealer`가 아니라 endpoint 없는 `Router` row를 게시한다.
`Dealer`는 다른 auto-connect 유형에서 사용할 수 있는 값이며 RouteMesh row에서는
유효하지 않다. runtime은 RouteMesh의 dealer row를 호환 입력으로 받아들이지 않는다.

**`Draining` 마커.** node가 우아한 종료(graceful drain)에 들어가면 자기 peer row의 `Draining`을
true로 갱신한다. 이 마커는 **"신규 배치 제외"와 "기존 연결 유지"를 분리**하기 위한 것이다 — peer row를
삭제하면 §6의 자동 연결 diff가 기존 연결을 끊어 in-flight reply·actor 핸드오프가 깨지므로, 삭제 대신
마커를 쓴다. 마커의 소비 규칙:

- **drain handoff 대상 노드 선택**과 **remote user Spot으로의 actor join**은 `Draining=true` peer를
  제외한다. 그 밖의 경로(로컬 spot `GetOrCreate`, 호출자가 노드를 지정하는 Entry Spot join, 기존
  owner routing)는 이 마커를 읽지 않는다 — 자세한 경계는
  [54 §3.1](54-graceful-drain-handoff.ko.md)이 소유한다.
- **자동 연결(§6)**은 마커만으로 disconnect하지 않는다 — draining peer로의 기존 연결을 유지한다.

`Draining`은 기본값 false인 필수 typed 필드다. 이번 framework 계약 교체에서는 구형 row decoder나
metadata fallback을 두지 않으며 store row schema와 모든 언어 codec을 함께 바꾼다. 전체 drain 수명주기 계약은
[Graceful Drain & Handoff](54-graceful-drain-handoff.ko.md) §3이 소유한다.

**actor 배치 capability.** Spot mesh의 Spot 역할 peer row는 그 노드가 actor factory로 수용할 수 있는
actor type마다 `Capabilities`에 `actor:<actor-type>`을 하나씩 기록한다. 여기서 `<actor-type>`은
언어 runtime의 클래스 이름이 아니라 `AddActorFactory` 계열 등록에서 application이 지정한 문자열이다.
framework는 값을 중복 없이 문자열 순서로 기록한다. actor 배치와 drain 핸드오프는 요청 actor type과
정확히 일치하는 capability가 있고 `Draining=false`인 노드만 대상으로 선택한다. capability가 없으면 해당
노드의 Entry Spot을 시험 호출해 호환 여부를 추측하지 않는다. 이 값은 framework가 소유하는 예약
capability이며 application metadata로 대신 기록하지 않는다.

### 2.2 spot location

`spot rid`가 어느 node에 있는지. `MeshName`, `SpotRid`, `SpotType`(선택), `NodeRid`,
`SpotKind`(§2.0의 `SpotKind` enum, **숫자로 직렬화**한다), `RouteEndpoint`(선택), `OwnerId`,
`Generation`, `UpdatedAt`. spot lifecycle이 자동 갱신한다.

### 2.3 actor location

actor가 어느 node/spot에 있는지. `ActorId`, `ActorType`, `ActorRef`(nullable), `NodeRid`,
`SpotRid`(nullable), `LocationKind`(**타입은 `SpotKind`** — `Entry=1` / `User=2`, 숫자로
직렬화한다), `SpotMeshName`, `OwnerId`, `Generation`, `UpdatedAt`. actor lifecycle이 자동
갱신한다. actor row에는 `SpotKind`라는 이름의 별도 필드를 두지 않는다 — spot 종류는 이
`LocationKind` 필드 하나로 표현한다.

**`ActorRef`가 비어 있는 row는 아직 commit되지 않은 join이다.** target이 join commit 전에
소유권 claim으로 먼저 쓴 row가 이 상태이며, resolver는 이 row를 **성공 결과로 반환하지 않고
miss로 취급한다**(§5). commit이 끝나면 `ActorRef`, `LocationKind`, `SpotRid`가 함께 채워진다.
별도의 pending 상태 필드는 두지 않는다.

### 2.4 route location

actor/spot으로 직접 표현되지 않는 framework route. `RouteKind`는 framework가 정의한 닫힌
집합(`ActorSession`, `SpotName`, `FrameworkRoute`)이며, application이 임의 key-value 저장소로
쓰는 표면이 아니다.

### 2.5 owner lease

runtime instance당 lease row 하나. `OwnerId`, `NodeRid`, `LeaseExpiresAt`(store가 계산),
`RenewedAt`. runtime start 때 만들어지고 heartbeat interval마다 upsert로 갱신된다. heartbeat
write는 runtime instance당 1회이므로 store 부하는 node 수에 비례한다.

## 3. Store 계약

책임별 interface는 peer/spot/actor/route store + owner lease store로 나뉜다. 그러나 public
등록 표면은 **한 물리 저장소 인스턴스**를 등록하는 통합 계약만 제공한다. extension과 사용자
구현체는 통합 계약(`IZLinkLocationStore` — 5종 결합)을 구현한 인스턴스 하나를
`AddLocationStore(instance)`로 등록한다(§8). owner lease와 row가 같은 저장소에 있어야
`NewClaim`의 "기존 row owner lease 만료" 판정을 원자적으로 할 수 있기 때문이다.

각 store 역할은 공통으로 다음을 제공한다.

- `Update...(row, intent)` / `Remove...(key, ownerToken)` /
  owner별 일괄 제거 작업
- 단건 `Resolve...(key)` (peer 제외) / filter 기반 `List...(filter[, page])`
- peer list와 owner lease list는 **pagination 없는 단일 snapshot**이다. 자동 연결에서 "연결되어
  있어야 하는 대상 목록"을 계산하려면 한 시점의 전체 목록이 필요하므로 page 간 시점 불일치를
  허용하지 않는다.
  mesh당 peer row 수천 상한을 전제로 하며 이를 넘으면 mesh를 분할한다.
- spot/actor/route 목록은 `ZLinkPageRequest`(page size, opaque continuation token)와
  `ZLinkLocationPage<T>`를 사용한다. 기본 page 크기는 `list page size` option을 따른다.

### 3.1 write 결과와 intent

update/remove는 `ZLinkLocationWriteResult`를 반환한다. 성공 시 store가 확정한 `Generation`과
`UpdatedAt`을 함께 돌려준다.

| 결과 | 의미 |
|------|------|
| `Stored` | 저장 또는 교체 성공 |
| `IgnoredStale` | 구세대 owner token의 update/remove라 무시. row 불변 |
| `RejectedConflict` | 유효한 owner row가 있는 key에 대한 new claim 실패(동시 claim 패배) |

| intent | 성공 조건 | generation |
|--------|-----------|------------|
| `NewClaim` | 현재 row가 없거나 row owner의 lease가 만료된 경우에만 | store가 key별로 원자적으로 증가시킨 새 token 발급 |
| `Renew` | 현재 row와 같은 `OwnerId + Generation` 제시 | 불변 |
| `Takeover` | 유효한 owner row를 새 owner가 명시적으로 교체(framework가 의도한 이동 전용) | 새 token 원자 발급 |

`ZLinkLocationOwnerToken`은 `OwnerId + Generation`이다. read API와 write API는 store 장애를
infrastructure error로 던진다.

### 3.2 owner lease store

`RenewOwnerLease(ownerId, nodeRid, leaseTtl)`는 upsert다. 호출자는 절대 만료 시각이 아니라
TTL을 전달한다. `ListOwnerLeases()`는 lease 목록과 store 기준 현재 시각(`StoreNow`)을 한
snapshot으로 반환한다. 만료 판정은 `LeaseExpiresAt - StoreNow`와 조회 이후의 local monotonic
경과 시간으로 계산한다. `RemoveOwnerLease`는 owner 자신의 shutdown 경로와 운영 복구 도구
전용이다.

### 3.3 optional change stamp / watch

- **change stamp**(`IZLinkLocationChangeStampStore`): kind/mesh별 단조 증가 stamp를 싸게 읽어
  변경 유무를 감지한다. 구현하면 polling tick이 stamp만 읽고 변화가 있을 때만 목록을 읽는다.
- **watch**(`IZLinkLocationWatchStore`): 변경 이벤트 stream으로 reconcile을 깨운다.

둘 다 더 빨리 반응하고 조회 부하를 줄이기 위한 최적화일 뿐이다. **polling이 올바른 연결 상태에
도달하기 위한 기본 경로**이며, stamp/watch 이벤트가 유실돼도 다음 polling 결과로 같은 상태에 도달해야
한다.

## 4. Owner/Generation 규칙

- generation은 store가 key별로 원자적으로 발급하며, **어떤 경로로도 node 사이에 배포되지
  않는다.** 승자는 자신의 write 응답에 담겨 온 generation을 owner token으로 보관하고 이후
  `Renew`/remove에 제시한다.
- 구 owner는 자기 token이 `IgnoredStale`로 거부되는 순간 소유권 상실만 알게 된다. token
  비교는 항상 store 안에서 일어나므로 generation 배포 protocol 없이 fencing이 성립한다.
- **claim-then-activate**: 위치 claim(`NewClaim`)이 성공한 쪽만 instance를 활성화한다. 패자는
  instance를 만들지 않고 재조회로 승자의 위치를 사용한다.
- 계획된 이동의 기본 경로는 **deactivate-first**(구 instance를 먼저 멈춘 뒤 `Takeover`)다.
  `Takeover`를 구 owner 무응답 fencing 경로로 쓸 때는, 구 owner가 `IgnoredStale`을 관찰할
  때까지 두 instance가 겹칠 수 있음을 감수한다.

## 5. Resolver 계약

resolver는 runtime과 application-facing client의 읽기 표면이다. 새 handle을 만드는 조회는
store에 도달하고 owner lease join으로 유효성을 판정한다.

| resolver | 표면 | 용도 |
|----------|------|------|
| peer location resolver | live peer list 조회 | 자동 연결의 live peer list 조회 |
| Spot handle resolver | spot rid로 `SpotHandle?` 조회 | 메시징 조회: spot rid → 전송 handle |
| actor Spot handle resolver | actor id로 `SpotHandle?` 조회 | 메시징 조회: actor → 그 actor가 위치한 spot의 전송 handle |
| route 단건 조회 | store SPI/운영 조회 | owner-bound route 단건 조회는 public resolver로 노출하지 않음 |

- 메시징 resolver는 **전송 handle**을 반환한다. handle이 내부 `SpotRef` snapshot과
  안전한 1회 갱신을 소유하며 caller는 주소 수명을 관리하지 않는다. 상세는
  [spot 주소 메시징](24-spot-address-messaging.ko.md)이 정본이다.
- location store 기반 Spot handle resolver는 spot row의 mesh 이름으로 route mesh
  channel을 고른다. spot mesh 이름과 route mesh channel 이름이 다르면 location option에
  `spot mesh -> route mesh channel` 매핑을 등록해야 한다. 매핑이 없으면 같은 이름을 사용한다.
  이 매핑은 전송 channel 선택만 정하고, store row key나 spot lifecycle mesh 이름을 바꾸지 않는다.
- spot/actor/route resolver는 단건 resolve만 노출한다. 목록 조회는 peer resolver의 peer list,
  운영 조회 표면, store interface에만 둔다.
- generation이 필요한 lifecycle 흐름(재연결/없으면 생성 판단, takeover)은 resolver가 아니라
  store/runtime 경로를 사용한다.

### 5.1 stale row 제외

단건 resolve와 목록 조회의 성공 결과는 stale row를 포함하지 않는다. stale row는 (a) owner
lease가 만료된 owner의 row, (b) 같은 key에 대해 이 runtime이 이미 관찰한 generation보다
오래된 generation의 row(복제 지연 guard)다. 조건 (b)를 위해 runtime은 관찰한 최신 generation을
key별로 기억한다.

**actor row는 조건 (c)를 하나 더 갖는다: `ActorRef`가 비어 있는 row**(§2.3, commit 전 claim)는
성공 결과로 반환하지 않고 resolve miss로 처리한다. 이 규칙이 pending join을 성공한 join으로
오인하지 않게 막는다([23 §7](23-spot-actor.ko.md)).

## 6. 자동 연결

framework runtime은 store를 등록한 배포에서 자동 연결 가능한 socket(연결형 client socket)의
mesh별 reconcile 루프를 돌린다.

1. **탐지**: change stamp(있으면) 또는 polling interval마다 peer list + owner lease snapshot을
   읽는다.
2. **desired set**: role 허용/target 매칭으로 dial 대상을 계산한다. RouteMesh 구성원은 모두
   `Router`다. endpoint가 없는 router는 pairwise initiator 순서와 무관하게 endpoint가 있는
   remote router를 항상 dial한다. 양쪽 router에 endpoint가 있으면 pairwise initiator 규칙으로
   한쪽만 dial한다. 상대가 나를 dial하는 peer는 desired set에 없어도 **mesh 구성원**이다(fail-fast 분류는
   desired set이 아니라 구성원 snapshot 기준). peer의 `Draining=true`(§2.1)는 desired set에서
   **제외하지 않는다** — 연결은 유지하고, 신규 배치 결정(spot/actor placement)만 그 peer를 제외한다.
3. **diff 적용**: 새 target은 connect, desired set에서 빠진 target은 disconnect. 같은 peer
   key의 endpoint 또는 연결 식별 정보가 바뀌면 disconnect 후 connect한다. 재시작한 peer가 같은
   endpoint를 새 owner로 다시 등록하면 transport가 끊어진 연결을 이미 새로 연결하므로, reconcile
   루프는 owner 정보만 갱신하고 같은 endpoint를 중복으로 disconnect/connect하지 않는다.
4. **crash 전파**: row write 없이 죽은 node는 owner lease 만료만으로 전파된다. lease join에서
   그 owner의 row가 제외되고 desired set에서 빠져 disconnect된다.

manual endpoint 연결(`EnableClient(endpoint)`)은 auto reconcile이 끊지 않는다.

### 6.1 store 장애와 복구

- 장애 중: fail-static. 기존 연결 유지, diff 계산 중단, `StoreFailure` 관측(§9).
  owner lease heartbeat는 backoff로 재시도한다.
- `store failure grace` 초과: 새 outbound connect만 중단하고, 이미 ready인 연결은 transport가
  owner lease가 유효한 동안 유지한다.
- 복구: 각 노드는 **조회보다 먼저** 자기 owner lease와 local row를 재등록한다. disconnect
  diff는 heartbeat interval 1회 유예 뒤에 적용한다. 유효한 peer가 재등록을 마치기 전에
  한꺼번에 끊는 것을 막는다.

## 7. 운영 조회 (runtime query)

`IZLinkLocationRuntimeQuery`는 운영 도구와 E2E의 조회 표면이다. 모든 조회가 store를 직접
읽는다.

| API | 반환 |
|-----|------|
| runtime 상태 조회 | store health, watch enabled, polling interval, last refresh, last error, owner lease 갱신 상태 |
| peer location 목록 조회 | owner lease가 유효한 raw peer row snapshot |
| Spot/actor/route location 목록 조회 | owner lease가 유효한 raw row page |
| topology 조회 | location row + connection state + lease + generation을 runtime이 합성한 projection |
| service summary 조회 | mesh/type/role별 count summary |

topology/summary는 store가 아니라 **관찰 host 자신의 location runtime projection**이다. store가
topology 의미를 결정하지 않는다.

## 8. 등록 API와 option

### 8.1 등록

아래 코드는 등록 의미를 보여 주는 비규범 `.NET` 투영 예시다. 정확한 이름과 타입은
언어별 스펙에서 고정한다.

```csharp
// extension 또는 사용자 구현체 통합 등록.
// 같은 인스턴스가 peer/spot/actor/route row와 owner lease를 모두 담당한다.
options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
    .SetConnectionString("...")
    .SetKeyPrefix("zlink:app")));

// 단일 process 개발, unit test, sample smoke test 전용.
options.UseInMemoryLocationStores();
```

`AddLocationStore(instance)`는 통합 계약 `IZLinkLocationStore`(store 5종 결합)를 구현한
인스턴스 하나를 등록한다. 같은 인스턴스가 optional 계약(change stamp, watch)도 구현하면
자동으로 인식된다. extension은 전용 등록 함수를 만들지 않는다.
`AddLocationStore(instance)`와 `UseInMemoryLocationStores()`를 함께 쓰는 것은 검증 오류다.

### 8.2 option

| option | 의미 | 기본값 후보 |
|--------|------|------------|
| heartbeat interval | owner lease 갱신 주기 | 5s |
| owner lease TTL | lease 만료 기준. 만료된 owner의 row는 전부 stale | 15s |
| polling interval | watch/stamp가 없거나 이벤트가 없을 때 store 재조회 주기 | 1s |
| list page size | 목록 조회 기본 page 크기 | 1000 |
| store failure grace | store 장애 중 신규 outbound connect를 허용하는 완충 시간 | 30s |
| spot router channel map | spot mesh 이름과 route mesh channel 이름이 다를 때 쓰는 전송 channel 매핑 | 빈 map |

cache 관련 option은 없다(캐시 제거 결정).

## 9. 관측 (event source)

registry/discovery event source는 location runtime event source로 대체됐다. 각 source의
kind는 닫힌 enum이다. 상세 계약은 [메시지 흐름 추적](52-message-flow-tracing.ko.md)의 observer
계약과 [비동기 실행 정책](../04-async-execution-policy.ko.md)을 따른다.

| source | kind |
|--------|------|
| `location-runtime` | `StatusChanged`, `TopologyChanged`, `ServiceSummaryChanged`, `StoreFailure`, `StoreRecovered` |
| `location-peer` | peer row update/remove, auto-connect desired set 변경 |
| `location-spot` | spot row update/remove, spot resolve miss |
| `location-actor` | actor row update/remove, actor reconnect resolve miss |
| `location-route` | route row update/remove, route resolve miss |

`location-runtime` source는 runtime query 표면의 polling diff로 발행된다. store 장애는 source를
죽이지 않는다 — 조회 실패는 `StoreFailure` 이벤트로 강등되고 복구 시 `StoreRecovered`가
발행된다(fail-static).

## 10. 오류 규칙

| 상황 | 결과 |
|------|------|
| store 연결 실패 (read) | infrastructure error |
| store 연결 실패 (write) | infrastructure error(예외, 원인 보존) — 경합 상태값(`IgnoredStale`/`RejectedConflict`)과 분리한다. 기존 connection은 fail-static |
| 위치 없음 | not found |
| stale row만 있음 | not found |
| owner/generation 충돌 | conflict 또는 rejected |
| 잘못된 actor id/spot rid | validation error |

## 11. 구현체 요구와 참조 구현

store 구현체가 지켜야 하는 최소 요구:

- `NewClaim`/`Takeover`의 generation 발급과 lease-만료 판정이 **원자적**이어야 한다(동시
  claim에서 정확히 하나만 `Stored`).
- owner lease와 location row가 같은 물리 저장소를 공유해 "row owner의 lease 만료" 판정이
  원자적으로 가능해야 한다.
- `UpdatedAt`/`LeaseExpiresAt`은 store 기준 시각으로 기록한다.
- continuation token은 opaque하며 filter가 같은 연속 호출에서만 유효하면 된다.

공식 참조 구현은 Redis extension([location-store-redis](41-location-store-redis.ko.md))이다.
framework 본체는 특정 store 제품에 의존하지 않으며, in-memory 구현은 단일 process 테스트
전용이다. store 제품 자체의 HA/복제는 store 구현체 책임이고 framework가 검증하지 않는다.

## 12. routing id slot allocation

자동 routing id 할당은 기존 location store에 선택 capability를 추가하는 방식으로 제공한다. 사용자가
등록한 **같은 store 인스턴스**가 location row, owner lease와 slot allocation을 함께 구현해야 한다.
별도 slot store 등록 API는 제공하지 않는다. 자동 할당을 설정했는데 capability가 없으면 startup
전에 설정 오류로 실패한다.

acquire는 group의 정렬된 member·prefix 목록과 slot count를 처음 호출에서 고정한다. 이후 구성이
다르면 재시도하지 않고 configuration mismatch를 반환한다. 같은 owner의 재시도는 같은 slot과
generation을 반환하며, 서로 다른 owner의 동시 acquire는 유효한 slot 하나를 중복 반환해서는 안
된다. 빈 slot은 `1..slotCount`에서 가장 작은 번호를 선택한다. owner lease가 만료된 slot은 물리 row
삭제 여부와 관계없이 다시 사용할 수 있으며 generation을 증가시킨다. 이전 generation의 release는
현재 owner를 변경하지 않는다.

framework startup 순서는 다음과 같다.

1. 모든 allocation 설정과 lease 시간을 검증한다.
2. 첫 slot과 owner lease를 확보하고 heartbeat와 fencing deadline 감시를 시작한다.
3. group 이름 순서로 나머지 slot을 확보한다. 어느 group이 소진되면 앞서 확보한 slot을 반환하고
   bind 없이 다시 대기한다.
4. 확정한 routing id를 각 member에 적용한 뒤 socket을 만들고 bind한다.
5. location row를 게시한 뒤 readiness와 할당 결과 provider를 완료한다.

정상 종료는 readiness 차단과 drain 뒤 socket, location row, slot, owner lease 순으로 정리한다. bind
실패도 확보한 slot을 반환한다. owner lease renew를 확인하지 못하면 마지막 성공 응답의 store 시각과
local monotonic 경과 시간으로 안전 기한을 계산한다. 기한까지 회복되지 않으면 host 종료를 요청해
lease 만료 전에 관련 socket을 닫는다. 임의의 새 slot으로 전환하지 않는다.

| option | 기본값 | 제약 |
|--------|-------:|------|
| heartbeat interval (`H`) | 10s | 0보다 커야 한다 |
| owner lease TTL (`T`) | 30s | 0보다 커야 한다 |
| routing id fencing margin (`M`) | 5s | 0보다 커야 한다 |
| owner lease renew timeout (`R`) | 3s | 0보다 커야 한다 |

네 값은 `H + R < T - M`을 만족해야 한다. 자동 할당을 사용하지 않는 runtime에는 새 fencing
정책을 적용하지 않는다. application은 readiness 이후 allocation group 이름으로 확정된 slot과
member별 routing id를 조회할 수 있지만, slot 선택·갱신·반환은 framework가 소유한다.
