# SPOT 메시징 — 공통 스펙

[스펙 목차](README.ko.md)

> 이 문서는 **SPOT의 개념 위치와 메시징 축의 언어 중립 정본**이다. outbound 축의 분리,
> publish·subscribe 모델, dispatch 실패 정책, route ingress 규칙, startup validation을 소유한다.
>
> SPOT 위에 상위 실행 모델을 얹는 계약은 [stage-wrapper-on-spot](stage-wrapper-on-spot.ko.md),
> actor 이동은 [spot-actor](spot-actor.ko.md), 노드 등록은 [spot-node](spot-node.ko.md),
> spot 주소는 [spot-address-messaging](spot-address-messaging.ko.md)이 소유한다.
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

**routing id를 직접 넣는 routed 호출은 SPOT의 spot-to-spot 경로에만 남는다.** 특정 channel의
서버 소켓을 routing id로 직접 지정해 호출하는 모델은 **채택하지 않는다.**

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
  자동 재전송하지 않는다**([spot-address-messaging](spot-address-messaging.ko.md)).
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
  통일한다([stage-wrapper-on-spot §4](stage-wrapper-on-spot.ko.md)).

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

- **packet은 header의 message id를 기준으로 targeted dispatch된다.**
- **subscribe는 topic subscription으로 consumer 등록된다.**

**subscribe handler를 router request handler와 같은 종류의 매핑으로 보면 안 된다.**

## 4. 등록 모델

**SPOT handler 등록은 attribute·decorator 기반이 아니다.** spot 객체가 **구성 단계에서 직접
등록**하는 쪽을 기본으로 둔다.

등록 표면의 축:

| 축 | 의미 |
|---|---|
| **packet handler** | request와 send packet을 함께 등록한다. dispatch key는 **packet 타입의 message id** |
| **subscribe handler** | topic consumer 등록 |
| **timer** | 현재 spot lifecycle 안에 등록. overrun 정책과 handler 예외 정책을 함께 정한다 |

- **message id는 codec이 정한다.** Protobuf면 protobuf message 이름, JSON이면 언어의 클래스
  이름이다.
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

- **user Spot은 room·game·stage 같은 하나의 상태 객체다.** 그래서 같은 user Spot 안의 서로 다른
  actor가 같은 상태를 바꾸더라도 **두 handler가 동시에 실행되지 않는다.** application이 spot
  상태를 lock으로 보호하지 않아도 되는 근거다([stage-wrapper-on-spot §3](stage-wrapper-on-spot.ko.md)).
- **Entry Spot은 특정 room 상태를 소유하는 곳이 아니라 모든 actor가 처음 거쳐 가는 공용 입구다.**
  그래서 **Entry Spot actor packet은 actor별 mailbox에서 순서를 보존한다.** 같은 actor의 packet은
  순서대로 실행되지만, **서로 다른 actor의 packet은 Entry Spot 실행 queue 하나 때문에 서로 기다리지
  않는다.**
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

| 경로 | handler 없음 · decode 실패 · handler 예외 · invalid frame |
|---|---|
| **SPOT route request** | **error reply를 반환한다** |
| **actor request** | **error reply를 반환한다** |
| **reply frame이 없는 경로**(같은 process의 local actor call 등) | **caller를 framework 오류로 완료한다** |
| **SPOT route send** | **drop.** Warning 로그와 metric |
| **actor send** | **drop.** Warning 로그와 metric |
| **subscription** | **drop.** Debug 로그 또는 metric, 전역 message flow observer event |

**observer 실패가 dispatch loop나 shutdown을 깨뜨리지 않는다.**

## 6. Route ingress

**SPOT route ingress는 route mesh channel의 서버 소켓만 사용한다.** client/server channel과 fanout
channel은 **SPOT route ingress로 지정할 수 없다.**

- **수동 endpoint와 store 자동 연결을 같은 route 수신 관계에서 섞으면 startup validation
  오류다.**
- 수동 endpoint가 없으면 framework가 **location store의 peer row를 읽어 자동 연결한다**
  ([location-runtime](location-runtime.ko.md)).
- **handler group이 없어도 transport 전용 channel로 쓸 수 있다.** 반대로 handler group을
  매핑해도 **route ingress가 자동으로 켜지지는 않는다.**
- egress로 route mesh channel을 쓸 때는 **실제 target 서버 소켓에 연결되어 있어야 한다.**
  **주소만 알고 연결하지 않은 상태에서는 routed spot 메시지를 보낼 수 없다.**

**spot callback 밖에서 target spot으로 직접 send/request하는 public client를 두지 않는다.**
channel handler·HTTP handler·background service는 **actor 생성이나 entry spot join 같은 도메인
흐름으로 actor ref를 얻는다.** 현재 spot callback 안에서 다른 spot으로 보낼 때만 spot outbound를
쓴다.

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
  ([spot-node §3](spot-node.ko.md)).
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
