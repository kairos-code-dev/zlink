# SpotNode — 공통 스펙

[스펙 목차](../README.ko.md) | [이전: SPOT 메시징](20-spot-messaging.ko.md) | [다음: ZLink Framework Actor Model](22-actor-model.ko.md)

> 이 문서는 **SpotNode 등록과 SpotManager 계약의 언어 중립 정본**이다. Entry Spot 설정, bind
> 순서, spot 생성·조회·종료의 의미, route 색인의 의미를 소유한다.
>
> 언어별 builder 메서드 이름과 시그니처는 `languages/<lang>/spot-node.ko.md`가 고정한다.

## 1. SpotNode 등록

SpotNode는 spot 인스턴스를 호스팅하는 컨테이너 노드다. 등록 표면은 다음 축을 갖는다.

| 축 | 의미 |
|---|---|
| **router 역할** | spot router bind endpoint와 routing id |
| **pub/sub 역할** | spot publisher·subscriber bind endpoint와 routing id |
| **Entry Spot 설정** | native Entry Spot facade의 routing id 등 설정 |
| **Entry Spot 타입 등록** | Entry Spot 구현 타입 등록. actor packet handler는 그 Entry Spot이 등록한다 |
| **spot factory 등록** | 이 node가 만들 수 있는 spot 타입 |
| **channel client** | spot handler의 channel send/request가 공유하는 client |

**Entry Spot 설정과 Entry Spot 타입 등록은 별개다.** 타입을 등록하지 않아도 facade 설정은
적용된다.

**등록 단계의 충돌은 조용히 덮어쓰지 않는다.** 같은 spot factory 타입을 두 번 넣거나 Entry Spot
타입을 두 번 지정하면 **startup 시점에 설정 오류로 실패한다.** 설정 실수를 즉시 드러내는 쪽이
기본 규칙이다.

RouteMesh와 SpotMesh가 같은 프로세스에 있으면 **framework가 route bridge를 자동으로 붙인다.**
discovery는 등록된 location store를 사용한다([location runtime](40-location-runtime.ko.md)).

## 2. Entry Spot

Entry Spot은 **actor가 생성 직후 머무르는 기본 spot**이다. actor가 user Spot에서 leave하면 같은
node의 Entry Spot으로 돌아온다. 따라서 Entry Spot의 routing id는 actor remote location의 현재
spot rid가 될 수 있다([spot-actor](23-spot-actor.ko.md)).

### 2.1 적용 순서

**Entry Spot routing id는 native SpotNode가 bind되기 전에 적용해야 한다.** core가 bind 이후
Entry Spot rid 변경을 잠그기 때문이다.

1. **bind 전에** Entry Spot routing id를 native Entry Spot facade에 적용한다.
2. router·pub bind endpoint를 설정한다.
3. store 자동 연결, manual peer, accepted spot route channel, publisher 같은 node 역할을 붙인다.
4. Entry Spot을 초기화한다(activation과 dispatch pump 포함).
5. 이후 생성되는 actor가 그 Entry Spot rid를 사용한다.

**이 순서는 actor가 생성되기 전에 Entry Spot rid가 확정되도록 하기 위한 것이다.** 순서를 어기면
actor가 잘못된 Entry Spot rid를 갖는다.

## 3. SpotManager

**spot 인스턴스는 SpotNode가 생성하고 소유한다.** application은 manager로 **생성·조회·종료만**
한다. **반환값은 장기 보관용 spot instance handle이 아니다.**

| 작업 | 의미 |
|---|---|
| **create** | spot 타입으로 factory를 고르고 runtime이 **새 spot rid를 발급한다.** 호출자가 넘긴 payload를 spot의 생성 callback에 **한 번** 전달한다. payload 없이 호출하면 **빈 message를 넘긴 것과 같고**, 생성 callback은 빈 message를 받아 한 번 실행된다 |
| **getOrCreate** | **명시적 spot rid가 필요할 때** 쓴다. 아래 §3.1 참조 |
| **find / list** | 현재 존재하는 logical spot rid를 확인하는 조회 표면이다. 결과에는 **spot rid만** 담는다. `find`는 없으면 빈 결과를 반환한다 |
| **close** | 등록된 SpotNode를 훑어 해당 spot rid를 정상 종료한다. **actor가 남아 있는 user Spot은 종료하지 않고 실패를 반환한다** |

### 3.1 getOrCreate의 의미

| 기존 상태 | 동작 |
|---|---|
| 같은 spot rid가 **이미 ready** | `Existing` 상태를 반환하고 **이번 request는 생성 callback으로 전달하지 않는다** |
| 같은 spot rid가 **initializing** | **첫 생성 요청의 생성 callback 완료를 기다린다** |
| 기존 entry의 **spot 타입이 다름** | 같은 logical spot을 다른 framework 타입으로 해석하려는 시도이므로 **타입 불일치 오류로 실패한다** |

### 3.2 생성 lifecycle의 호출 순서

**순서가 계약이다.** wrapper가 이 순서에 의존한다([stage-wrapper §3](25-stage-wrapper-on-spot.ko.md)).

1. **handler 구성**을 마친다.
2. **생성 callback**을 호출한다.
3. **생성 callback이 수락한 경우에만** 초기화 callback을 호출한다. **거절하면 초기화를 건너뛴다.**
4. **종료 callback은 spot 하나당 한 번만** 실행한다. 종료를 여러 번 요청해도 중복 호출하지 않는다.

**세 callback은 모두 그 spot의 실행 문맥에서 직렬로 실행된다.**

### 3.3 생성 결과와 실패

생성 결과는 **spot rid, 상태, 선택적 reply payload**를 담는다. 이후 메시징은 현재 channel
publish 또는 attach된 channel client의 send/request로 푼다.

**factory resolve, activation, 생성 callback, 초기화 callback의 실패는 모두 spot 생성 실패로
분류한다.**

## 4. Route 의미

**메시징 handle은 spot rid만 공개한다.** owner node rid와 spot kind는 **내부 주소 snapshot에
보존하며 application 메시징 표면에 노출하지 않는다.**

- **Spot RID route는 framework가 관리하는 이름 색인이다.** 이 색인은 **spot rid를 찾는 용도로만**
  사용한다.
- **owner node rid와 spot kind는 location store에서 조회한다.** owner lease가 유효한지 확인한
  spot location row를 기준으로 사용한다. 조회와 유효성 판정은
  [location runtime §5](40-location-runtime.ko.md)를 따른다.
- framework가 **운영용** spot location row를 노출할 때는 Entry Spot과 user Spot을 구분한다.

**actor ref를 publish·sync하거나 actor remote location을 조회하는 별도 application public
interface는 추가하지 않는다.** `ActorRef`는 actor manager와 actor directory가 발급하며,
application은 그것을 actor 메시징 표면(`SendToActor` / `RequestToActor`)의 대상 값으로 쓴다
([22 actor 모델 §6](22-actor-model.ko.md)). **spot 대상 메시징**은 `ActorRef`가 아니라 불투명한
spot handle을 사용한다 — 두 표면의 대상 값을 섞지 않는다.

## 5. 회귀 테스트

| 항목 | 검증 |
|---|---|
| Entry Spot routing id | 설정한 rid로 보낸 request가 실제 Entry Spot handler에 도달한다 |
| bind 순서 | bind 이후 Entry Spot rid 변경이 잠긴다 |
| 등록 충돌 | 중복 spot factory·Entry Spot 등록이 startup에서 실패한다 |
| getOrCreate | ready·initializing·타입 불일치 세 갈래가 §3.1대로 동작한다 |
| close | actor가 남은 user Spot을 종료하지 않는다 |
| public 표면 | 제거된 route 계약이 public API로 다시 노출되지 않고 actor·session 계약은 유지된다 |
