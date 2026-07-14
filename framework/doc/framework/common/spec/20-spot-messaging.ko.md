# SPOT 메시징 — 공통 스펙

[스펙 목차](README.ko.md) | [이전: Channel 메시징](11-channel-messaging.ko.md) | [다음: SpotNode](21-spot-node.ko.md)

> 이 문서는 **SPOT의 개념 위치와 메시징 축의 언어 중립 정본**이다. outbound 축의 분리,
> publish·subscribe 모델, dispatch 실패 정책, route ingress 규칙, startup validation을 소유한다.
>
> SPOT 위에 상위 실행 모델을 얹는 계약은 [stage-wrapper-on-spot](25-stage-wrapper-on-spot.ko.md),
> actor 이동은 [spot-actor](23-spot-actor.ko.md), 노드 등록은 [spot-node](21-spot-node.ko.md),
> spot 주소는 [spot-address-messaging](24-spot-address-messaging.ko.md)이 소유한다.
>
> 언어별 등록 표면과 시그니처는 `languages/<lang>/`의 SPOT 문서가 고정한다.

## 1. SPOT을 무엇으로 보는가

**SPOT은 pub/sub helper가 아니다. 주소를 가질 수 있는 논리 인스턴스다.** room, stage, 채팅방,
MMORPG zone이 대표적이다.

**"토픽 시스템"이 아니라 먼저 "논리 대상 인스턴스"로 설명해야 한다.** publish/subscribe는 그 안에서
쓸 수 있는 한 가지 활용 방식일 뿐이다.

요소 사이의 관계:

- **spot은 특정 service에 종속되지 않는다. SpotNode에 종속된다.**
- **SpotNode는 channel 이름을 직접 소유하지 않는다. channel view를 공급한다.** 그 view가 같은
  channel에 속한 peer mesh의 범위를 닫는다.
- **같은 SpotNode에는 active SPOT channel view를 하나만 둔다.**
- SpotNode의 router와 pub/sub mesh는 **같은 channel에 속한 다른 SpotNode와만** 연결된다.
- 다른 channel 호출은 SpotNode router가 아니라 **route bridge가 참조하는 channel runtime
  socket**을 탄다.

**SPOT은 pub/sub만으로 설명하면 부족하다.** 세 가지를 함께 설명해야 한다 — 논리 인스턴스 모델,
channel publish/send/request, SpotNode가 spot 인스턴스를 생성·소유하는 lifecycle.

### 1.1 direct call과의 관계

framework는 direct channel call만 제공하는 계층이 아니다. **SPOT도 동등한 축이다.**

| 축 | 용도 |
|---|---|
| channel 이름 기반 일반 channel messaging | 서비스 간 요청 |
| SPOT 기반 current channel publish/subscribe, channel send/request | 논리 인스턴스 상태 |

**상태를 소유한 대상으로 보내는 application 호출은 spot handle을 쓴다.** route mesh channel에
node rid를 직접 지정하는 `SendToNode`/`RequestToNode` 표면은 존재하지만, **infra 계층과 owner
일관 라우팅용**이며 room·actor 같은 상태 대상 요청의 기본 API로 쓰지 않는다
([channel topology §3.1](10-channel-topology.ko.md)).

## 2. Outbound 축

SPOT의 outbound 호출은 **세 축으로 갈라진다.** 각 축이 쓰는 표면이 다르다.

| 축 | 대상 | 전송 경로 |
|---|---|---|
| **topic publish** | 현재 SPOT channel의 subscriber | 현재 channel의 pub/sub mesh |
| **channel send / request** | 다른 channel | **route bridge가 참조하는 channel runtime socket** |
| **spot send / request** | 특정 spot 인스턴스 | spot 주소(handle) 기반 routed 경로 |

**규칙:**

- **spot 대상 호출은 호출자가 resolve해서 보관한 spot handle을 받는다.** framework가 위치 변경
  event와 주기적 조회로 handle의 주소를 갱신한다. 요청 도중 주소가 무효화되면 **안전한 경우에
  한해** 주소를 다시 조회하고 **한 번 재전송한다.** **one-way send는 이미 전달됐을 수 있으므로
  자동 재전송하지 않는다**([spot-address-messaging](24-spot-address-messaging.ko.md)).
- **`targetRid + spotRid`를 낱개로 받는 raw 호출은** 하부 바인딩에 남아 있더라도 **application의
  기본 API로 문서화하지 않는다.** application은 handle을 resolver로 얻고, handle 안의 위치값을
  낱개로 풀어 쓰지 않는다.
- **spot outbound 표면을 일반 channel client 위에 얹은 것으로 설명하면 안 된다.** 두 표면은 하부에서
  **서로 다른 C API를 감싼다.**

| 표면 | 책임 |
|---|---|
| **channel client** | 일반 channel messaging |
| **spot outbound** | current SPOT channel publish, 다른 channel send/request, spot-routed send/request |

- **timer는 outbound의 callback scheduler로 두지 않는다.** spot lifecycle 등록 표면 하나로
  통일한다([stage-wrapper-on-spot §4](25-stage-wrapper-on-spot.ko.md)).

## 3. Publish 모델

**두 경우를 분리해서 설명한다.**

| 경우 | 표면 |
|---|---|
| **local spot 안에서 현재 channel로 publish** | spot context의 outbound publish |
| **local spot이 없는 외부 노드에서 특정 SPOT channel로 publish** | **spot publisher client** — target channel 이름을 명시한다 |

### 3.1 topic과 spot rid는 역할이 다르다

| 개념 | 의미 |
|---|---|
| **spot rid** | 특정 room·stage·zone **인스턴스**를 가리키는 논리 주소 |
| **topic** | 여러 subscriber가 함께 듣는 **fan-out 주제 이름** |

### 3.2 subscribe와 packet dispatch는 다르다

둘 다 문자열을 키로 쓰지만 **dispatch 의미가 다르다.**

- **packet은 header의 packet name을 기준으로 targeted dispatch된다.**
- **SPOT subscribe는 topic subscription으로 consumer 등록된다.**

**subscribe handler를 router request handler와 같은 종류의 매핑으로 보면 안 된다.**

### 3.3 SPOT subscribe와 channel fanout은 다른 표면이다

두 pub/sub 표면을 혼동하지 않는다.

| 표면 | 구독 키 | dispatch 키 |
|------|---------|-------------|
| **SPOT subscribe** | **topic**. 등록 시 topic을 지정하고 그 topic의 consumer로 붙는다 | topic + packet name |
| **channel fanout** | 없음. subscriber는 그 fanout channel의 전량을 받는다 | **packet name만.** transport 수준 topic 필터를 노출하지 않는다 |

channel fanout에서 발행자가 topic 개념을 쓰고 싶으면 그 값을 메시지 payload나 publish context에
담고 **handler가 application 수준에서 필터**한다. fanout subscriber에 transport topic 필터를
추가하는 것은 공개 계약 확장이므로 [00 §3](00-public-contract-governance.ko.md)의 절차를 먼저
거친다.

## 4. 등록 모델

SPOT handler는 **두 경로로 등록한다.** spot 객체가 **구성 단계에서 직접 등록**하는 명시 경로가
기준이고, 선언적 metadata(attribute·annotation·decorator) 스캔에 의한 **자동 등록도 함께
지원하며 기본으로 켠다**([framework API §3.3](05-framework-api.ko.md)). 두 경로는 같은 registry로
수렴한다.

등록 표면의 축:

| 축 | 의미 |
|---|---|
| **packet handler** | request와 send packet을 함께 등록한다. dispatch key는 **packet 타입의 packet name** |
| **subscribe handler** | topic consumer 등록. topic은 등록 호출 인자 또는 선언적 metadata로 준다 |
| **timer** | 현재 spot lifecycle 안에 등록. overrun 정책과 handler 예외 정책을 함께 정한다 |

- **packet name은 registration descriptor가 정한다.** 선언적 packet metadata가 있으면 그 이름을,
  없으면 nominal type 이름을 쓴다. **codec은 packet name에 관여하지 않는다** — codec을 Protobuf로
  바꿔도 dispatch key는 그대로다([framework API §2.2](05-framework-api.ko.md)).
- handler는 별도 타입으로 두고 spot에는 핵심 로직만 남길 수 있다.
- framework는 **per-spot scope**를 만들고 등록된 handler 타입을 그 scope에서 resolve한다.
- **Entry Spot과 user Spot은 등록 표면을 맞춘다**(packet, subscription, timer, channel outbound,
  actor handler). **실행 직렬화 정책만 서로 다르다.**
- **actor의 구성 단계는 message handler를 등록하지 않는다.** actor handler는 spot의 registry에
  등록한다.

### 4.1 실행 직렬화 — Entry Spot과 user Spot

**등록 표면은 같지만 실행 직렬화 정책이 다르다.**

| 대상 | 실행 줄 |
|---|---|
| **user Spot의 packet · request · subscription · timer · actor join** | **user Spot 실행 queue 하나** |
| **user Spot에 머무는 actor의 packet** | **user Spot 실행 queue 하나** |
| **Entry Spot의 initialize · closing · actor lifecycle callback** | **Entry Spot 실행 문맥** |
| **Entry Spot actor의 packet** | **actor별 mailbox** |

- **user Spot은 room·game·stage 같은 하나의 상태 객체다.** user Spot 실행 queue는 drain loop가
  하나이므로 **두 handler 본문이 물리적으로 동시에 실행되지는 않는다.** 다만 handler가 request,
  join, worker의 **framework terminator를 await하면 그 지점에서 실행 줄을 양보**하므로, 같은 user
  Spot의 다른 callback이 그 대기 중에 끼어들 수 있다. 따라서 **await를 가로지르는 spot 상태 불변식은
  보장되지 않는다** — 자세한 규칙은 [04 비동기 실행 정책](04-async-execution-policy.ko.md) section 1이
  소유한다([stage-wrapper-on-spot §3](25-stage-wrapper-on-spot.ko.md)).
- **Entry Spot은 특정 room 상태를 소유하는 곳이 아니라 모든 actor가 처음 거쳐 가는 공용 입구다.**
  그래서 **Entry Spot actor packet은 actor별 mailbox에서 순서를 보존한다.** 같은 actor의 packet은
  순서대로 실행되지만, **서로 다른 actor의 packet은 Entry Spot 실행 queue 하나 때문에 서로 기다리지
  않는다.**
- **user Spot queue는 native bound actor 경로에서 반드시 필요한 직렬화 경계다.** managed runtime
  경로는 actor별 순서 규칙을 거친 뒤 user Spot queue로 들어갈 수 있지만, **native bound actor
  경로에는 그 앞단이 없다.** 보호는 두 겹이다 — user Spot queue가 **실행 구간**을 직렬화하고,
  actor·timer mailbox가 **terminator 양보를 가로질러서도** 재진입을 막는다. 따라서 같은 actor의
  callback은 await를 가로질러도 재진입되지 않지만, 같은 user Spot의 서로 다른 actor callback은
  terminator await 경계에서 인터리브될 수 있다.
- **Entry Spot actor handler와 user Spot actor handler는 표면이 다르다.** Entry Spot에는 user Spot
  객체가 없기 때문이다. Entry Spot handler는 entry spot·actor·payload를, user Spot handler는
  spot·actor·payload를 받는다.

### 4.2 핫패스 원칙

SPOT의 packet handler 호출은 room의 **핫패스**가 될 수 있다. 일반 channel messaging보다 **더 강한
성능 기준**을 적용한다.

- **reflection은 등록 단계까지만 허용한다.**
- **per-packet allocation, 과도한 DI 재구성, 불필요한 boxing을 피한다.**
- 등록 표면의 비용은 **startup과 spot 구성 단계에서만** 든다.

실제 room 성능을 좌우하는 것은 등록 문법보다 **codec encode/decode 비용, 같은 spot 안의 queue
적체, broadcast fan-out, allocator pressure, lock contention**이다. 원칙은 "class 기반 handler라서
느리다"가 아니라 **"핫패스 구현을 어떻게 캐시하고 어떻게 줄일 것인가"** 다.

## 5. Dispatch 실패 정책

**reply path가 있으면 error reply를 반환하고, 없으면 drop한다.**

| 경로 | 결과 | `action` |
|---|---|---|
| **SPOT route request** — reply 상관관계를 복원할 수 있다 | **error reply를 반환한다** | `ReplyError` |
| **actor request** — reply 상관관계를 복원할 수 있다 | **error reply를 반환한다** | `ReplyError` |
| **request** — **reply 상관관계를 복원할 수 없다** | **drop한다** | `Drop` |
| **reply frame이 없는 경로**(같은 process의 local actor call 등) | **caller를 framework 오류로 완료한다.** Error 로그 + metric | `FailCaller` |
| **SPOT route send** | **drop.** Warning 로그 + metric | `Drop` |
| **actor send** | **drop.** Warning 로그 + metric | `Drop` |
| **subscription** | **drop.** Debug 로그 또는 metric | `Drop` |

**모든 경로가 전역 message flow observer event를 남긴다.** event의 공통 스키마와 `action` 값은
[framework API §2.4.3](05-framework-api.ko.md)이 소유한다.

**handler 예외는 one-way 경로에서도 Error로 기록한다**([channel 메시징 §3.1](11-channel-messaging.ko.md)).

**observer 실패가 dispatch loop나 shutdown을 깨뜨리지 않는다.**

## 6. Route ingress

**SPOT route ingress는 route mesh channel의 서버 소켓만 사용한다.** client/server channel과 fanout
channel은 **SPOT route ingress로 지정할 수 없다.**

- **peer 획득 방식은 role 단위로 하나만 고른다.** 같은 role에 수동 endpoint가 하나라도 있으면 그
  role은 **수동 연결로 확정**되고, location store를 함께 등록했더라도 그 role의 자동 연결 reconcile은
  돌지 않는다. startup 오류가 아니라 **수동이 우선**이다.
- 수동 endpoint가 없으면 framework가 **location store의 peer row를 읽어 자동 연결한다**
  ([location-runtime](40-location-runtime.ko.md)).
- **peer source가 아예 없으면**(수동 endpoint도 store도 없음) startup validation 오류다.
- **handler group이 없어도 transport 전용 channel로 쓸 수 있다.** 반대로 handler group을
  매핑해도 **route ingress가 자동으로 켜지지는 않는다.**
- egress로 route mesh channel을 쓸 때는 **실제 target 서버 소켓에 연결되어 있어야 한다.**
  **주소만 알고 연결하지 않은 상태에서는 routed spot 메시지를 보낼 수 없다.**

**spot callback 밖에서도 target spot으로 보낼 수 있다.** channel handler·HTTP handler·background
service는 DI로 주입된 **외부 route client**로 spot에 send/request한다. 다만 그 표면은 **spot
handle과 메시지만 받는다** — `targetRid + spotRid`를 낱개로 받는 overload는 두지 않는다
([spot-address-messaging §3](24-spot-address-messaging.ko.md)). 현재 spot callback 안에서 다른
spot으로 보낼 때는 spot outbound를 쓰며, 두 표면의 대상 인자는 같은 spot handle이다.

## 7. 결정된 기준

- **route bridge channel socket과 spot publisher client 설정은 역할별 builder 하나로 묶는다.**
  runtime이 소유하는 설정(socket option, manual connection)만 노출하고 **더 세밀한 하위 builder
  트리로 확장하지 않는다.**
- **spot rid는 별도 wrapper 없이 routing id로 노출한다.** node rid와 spot rid는 **이름으로**
  구분한다.
- **Entry Spot의 native lifecycle은 framework가 관리한다.** application registry는 SpotNode 등록
  안에서 붙인다.
- **actor join/leave lifecycle을 spot 메서드 override만으로 설명하지 않는다.** actor packet
  handler, join handler, leave handler는 각 context의 registry에 등록한다.
- **request·join·worker는 완료 terminator를 하나만 제공한다.** framework는 보호 중인 spot/actor
  상태의 직렬성을 유지하면서 **continuation을 원래 실행 문맥에서 재개한다.**
- **spot manager는 생성과 조회를 함께 가진다.** 조회를 별도 query 서비스로 분리하지 않는다
  ([spot-node §3](21-spot-node.ko.md)).
- **subscriber concurrency와 backpressure는 per-handler·per-topic API가 아니라 subscriber 역할
  option에서 노드 단위로 설정한다.**
- **SPOT mesh channel과 top-level node 등록을 분리해 호출하는 public 경로를 제공하지 않는다.**
  SPOT network를 구성하는 local node는 spot mesh 등록과 **동시에** 등록된다.
- **STREAM session relay는 별도 node builder가 아니다.** framework가 router 역할을 켠 SpotNode를
  relay ingress로 사용한다.

## 8. Startup validation

| 구성 | 결과 |
|------|------|
| **둘 이상의 SpotNode가 actor factory를 소유** | **설정 오류** |
| actor factory가 없는 둘 이상의 spot mesh 등록 | 허용 |
| **같은 spot factory 타입 중복** | **설정 오류** |
| **같은 Entry Spot 타입 중복** | **설정 오류** |
| **route bridge에 router-capable channel이 없음** | **설정 오류** |
| **route egress channel에 store 또는 manual peer가 없음** | **설정 오류** |
| **fanout/dealer mesh를 routed SPOT egress로 지정** | **설정 오류** |
| location store 없이 local-only spot factory 등록 | 허용 |
| local spot factory 없이 외부 publish 역할만 등록 | 허용 — spot publisher client를 사용한다 |
| **SpotNode가 router·pub/sub 중 아무것도 켜지 않음** | **설정 오류** — 최소 하나는 켜야 한다 |
| **router 없이 actor factory를 등록** | **설정 오류** — actor는 router 경로를 요구한다 |
| **spot mesh channel 이름이 비어 있음** | **설정 오류** |
| **router·pub/sub 역할을 켰는데 bind endpoint가 비어 있음** | **설정 오류** |
| **등록하지 않은 route channel을 route bridge로 지정** | **설정 오류** |
| **handler group 이름이 비어 있음** | **설정 오류** |
| **subscribe topic이 비어 있음** | **설정 오류** |
| **timer 등록 검증**([stage-wrapper §4.1](25-stage-wrapper-on-spot.ko.md)) | **설정 오류** |

**모든 설정 오류는 host 시작 전에 실패한다.**

## 9. 회귀 테스트

| 항목 | 검증 |
|---|---|
| outbound 세 축 | topic publish, channel send/request, spot send/request가 각자의 경로를 탄다 |
| 외부 publish | local spot이 없는 노드가 spot publisher client로 target channel에 publish한다 |
| dispatch 실패 | reply path 유무에 따라 error reply와 drop이 §5대로 갈린다 |
| route ingress | client/server·fanout channel을 SPOT route ingress로 지정하면 startup에서 실패한다 |
| 수동·자동 혼용 | 같은 route 수신 관계에서 혼용하면 startup에서 실패한다 |
| startup validation | §8의 각 행이 그대로 동작한다 |
