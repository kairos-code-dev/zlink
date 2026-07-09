<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Actor Model](actor-model.ko.md) | [다음: Session Actor Dispatch Usability (Policy)](session-actor-dispatch.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[문서 묶음](../README.ko.md) | [개요](overview.ko.md) | [상호작용 모델](interaction-model.ko.md) | [메시지 모델](message-model.ko.md) | [Actor 모델](actor-model.ko.md) | [Spot Actor Join / Transfer](spot-actor.ko.md) | [Session Actor Dispatch 사용성](session-actor-dispatch.ko.md)

# Spot Actor Join / Transfer 공통 스펙

## 1. 목적

이 문서는 actor가 Entry Spot과 user Spot 사이를 이동할 때 모든 framework 언어가 지켜야 하는
공통 계약을 정의한다. [actor-model.ko.md](actor-model.ko.md)는 actor 개념과 lifecycle 전체를
다루고, 이 문서는 그중 **Spot actor join과 node 간 transfer**의 완료 조건, callback 순서,
장애 처리 기준을 고정한다.

언어별 framework는 이름과 API 모양이 달라도 이 문서의 의미를 바꾸면 안 된다. 현재 구현이 이
문서와 다르면 그 언어의 구현 gap으로 기록하고, 같은 동작을 하도록 맞춘다.

## 2. 용어

| 용어 | 의미 |
| --- | --- |
| source Spot | actor가 이동하기 전에 속한 Spot이다. 생성 직후 actor는 Entry Spot에 있다. |
| target Spot | actor가 `JoinSpot`으로 들어가려는 Spot이다. |
| source node | 이동 전 actor instance를 소유한 SpotNode다. |
| target node | target Spot을 소유한 SpotNode다. 같은 node 이동이면 source node와 같다. |
| admission | target Spot이 actor identity와 request를 보고 받을 수 있는지 확인하는 단계다. 이 단계의 callback은 `OnActorJoin` 계열이다. |
| actor transfer adapter | node 간 transfer에서 source actor의 이동 state를 `ZLinkMessage`로 만들고, target node에서 actor 객체를 materialize하는 actor type별 hook이다. |
| commit | actor membership을 실제로 target Spot으로 바꾸는 단계다. commit 뒤에만 target Spot은 actor가 들어왔다고 볼 수 있다. |
| joined callback | commit 완료를 application에 알리는 callback이다. 이름은 언어별로 다르지만 의미는 `OnJoinedActor` 계열이다. |
| left callback | source Spot membership에서 actor가 빠졌음을 알리는 callback이다. 이름은 언어별로 다르지만 의미는 `OnLeaveActor` 계열이다. |

이 문서에서 `OnActorJoin`, `OnJoinedActor`, `OnLeaveActor`, `OnCreateActor`는 공통 의미를
가리키는 이름이다. 언어별 실제 이름은 `.NET`의 `OnActorJoinAsync`, Node.js의 `onActorJoin`,
C++의 `on_actor_join`처럼 달라도 된다.

## 3. 핵심 원칙

### 3.1 `OnActorJoin`은 admission만 담당한다

`OnActorJoin`은 target Spot이 actor를 받을 수 있는지 판단한다. 이 callback이 `Accept`를 반환해도
actor가 target Spot에 들어온 것은 아니다. application은 `OnActorJoin`에서 room membership,
game participant list, broadcast 대상 목록처럼 "actor가 들어왔다"는 사실을 확정하는 상태를
바꾸지 않아야 한다.

`OnActorJoin` 입력에는 actor instance를 전달하지 않는다. admission 단계에서 필요한 값은 actor type,
actor id, source Spot, request payload, 필요한 경우 source node 같은 identity와 metadata다.

`OnActorJoin`에서 해도 되는 일:

- request payload 검증
- actor type, actor id, 권한, room 상태 확인
- join reply payload 계산
- admission 실패 사유 기록

`OnActorJoin`에서 하면 안 되는 일:

- target Spot membership 확정
- source Spot 상태 정리
- client에게 "입장 완료" event 발행
- actor instance 접근 또는 저장
- actor packet을 target Spot handler로 dispatch 가능하다고 간주

actor instance를 application에 처음 노출하는 lifecycle은 target `OnJoinedActor`다. remote transfer에서
target node는 `OnActorJoin` accept만으로 target actor instance를 만들지 않는다. source node가 actor
transfer adapter로 만든 state message를 commit 요청에 싣고, target node는 actor transfer adapter로
target actor instance를 materialize한다. target `OnJoinedActor`에는 이 actor를
전달한다.

### 3.2 `OnJoinedActor`가 join 완료 신호다

actor가 target Spot에 들어왔다고 application이 볼 수 있는 첫 시점은 target Spot의
`OnJoinedActor` callback이다. `JoinSpot` caller에게 성공을 반환하는 것도 target `OnJoinedActor`
가 정상 완료된 뒤여야 한다.

따라서 다음 문장은 모든 언어에서 참이어야 한다.

- `OnActorJoin`이 `Accept`를 반환했지만 `OnJoinedActor`가 호출되지 않았다면 join은 완료되지 않았다.
- target Spot이 actor를 membership에 넣고 domain state를 갱신하는 기준은 `OnJoinedActor`다.
- public actor location은 target `OnJoinedActor`가 정상 완료된 뒤 target user Spot으로 관찰되어야 한다.
- `JoinSpot` 성공 reply는 `OnJoinedActor` 완료 뒤에만 caller가 받을 수 있다.

### 3.3 `OnLeaveActor`는 target `OnJoinedActor`보다 먼저 관찰되어야 한다

source Spot이 존재하는 이동에서는 target `OnActorJoin`이 accept된 뒤 source `OnLeaveActor`를 먼저
호출하고, 그 다음 target `OnJoinedActor`를 호출한다. 이 순서를 지켜야 한 actor가 같은 시점에 두
room에 들어와 있는 것처럼 보이는 application 상태를 줄일 수 있다.

source Spot이 없거나 이미 target node로 actor ownership이 넘어간 복구 경로에서는 source
`OnLeaveActor`를 호출할 수 없다. 이런 경로는 정상 join 경로가 아니라 recovery 또는 reconcile
경로로 분리하고, feature-map에 명시해야 한다.

### 3.4 join 완료 전 packet dispatch 금지

target Spot은 target `OnJoinedActor`가 정상 완료되기 전까지 actor packet을 user Spot handler로
dispatch하면 안 된다. commit이 먼저 끝나더라도 `OnJoinedActor`가 아직 실행 중이거나 실패했다면
application은 actor가 target user Spot에 들어왔다고 관찰하면 안 된다. source Spot도 actor가 moving
상태에 들어간 뒤 같은 actor packet을 계속 처리하면 안 된다.

구현은 언어별로 actor별 serial queue, moving flag, pending join table, location generation guard
중 적절한 조합을 쓸 수 있다. 어떤 방식을 쓰더라도 외부에서 보이는 계약은 같다.

## 4. 같은 node에서 user Spot으로 join

같은 SpotNode 안에서 Entry Spot actor가 user Spot으로 이동하는 기본 흐름은 다음과 같다.

```mermaid
sequenceDiagram
  autonumber
  participant A as Actor
  participant FW as Framework
  participant E as Entry Spot
  participant R as User Spot
  A->>FW: JoinSpot(roomRid, request)
  FW->>R: OnActorJoin(actorId, request)
  R-->>FW: Accept(reply)
  Note over FW: mark actor dispatch as moving
  FW->>E: OnLeaveActor(actor)
  Note over FW: commit membership to target Spot
  FW->>R: OnJoinedActor(actor)
  Note over FW: publish actor location as target Spot
  FW-->>A: JoinSpot success(reply)
```

`Reject(reply)`이면 source membership은 유지된다. source `OnLeaveActor`, target `OnJoinedActor`,
location row 갱신은 실행하지 않는다.

### 4.1 같은 node join의 완료 조건

같은 node join은 아래 조건을 모두 만족해야 성공이다.

1. target `OnActorJoin`이 `Accept`를 반환했다.
2. source `OnLeaveActor`가 필요한 경우 정상 완료됐다.
3. framework 내부 membership이 target Spot으로 바뀌었다.
4. target `OnJoinedActor`가 정상 완료됐다.
5. actor location row가 target user Spot을 가리키는 committed 상태로 관찰된다.

위 조건 중 하나라도 실패하면 caller에게 성공을 반환하면 안 된다. 실패 시 source membership을
유지하거나, 이미 바뀐 내부 상태를 되돌릴 수 없으면 actor를 recoverable error 상태로 두고
location/runtime reconcile 절차가 처리해야 한다.

## 5. 다른 node의 user Spot으로 transfer

다른 SpotNode의 user Spot으로 actor를 이동할 때도 public lifecycle 의미는 같다. 다만 source node와
target node가 나뉘므로 framework는 admission과 commit을 분리해서 처리해야 한다.

```mermaid
sequenceDiagram
  autonumber
  participant A as Actor @source node
  participant SA as Framework @source node
  participant ES as Source Spot
  participant TB as Framework @target node
  participant R as User Spot @target node
  A->>SA: JoinSpot(roomRid, request)
  SA->>TB: prepare actor transfer
  TB->>R: OnActorJoin(actorId, request)
  R-->>TB: Accept(reply)
  TB-->>SA: admission accepted(reply)
  Note over SA: mark actor dispatch as moving
  SA->>SA: TransferOut(actor) -> state
  SA->>ES: OnLeaveActor(actor)
  SA->>TB: commit actor transfer(state)
  TB->>TB: TransferIn(actorId, state) -> actor
  Note over TB: commit membership to target Spot
  TB->>R: OnJoinedActor(actor)
  Note over TB: publish actor location as target Spot
  TB-->>SA: commit acknowledged
  SA-->>A: JoinSpot success(reply)
  Note over SA: cleanup source ref/session/location
```

이 다이어그램은 public contract를 보여준다. target node는 `OnActorJoin` 전에 actor identity와 request를
담은 pending admission record를 만들 수 있다. 그러나 target actor instance, target membership, public
location row는 만들지 않는다. target actor instance는 source node의 commit 요청에 들어 있는 transfer
state를 `TransferIn`으로 복원한 뒤 materialize하고, target `OnJoinedActor`에서 처음 application에
전달한다.

actor transfer adapter는 domain actor materialization만 담당한다. `TransferOut(actor)`는 source actor의
이동 state를 `ZLinkMessage`로 만들고, `TransferIn(actorId, state)`는 그 message와 actor id로 target actor
instance를 만든다. 이동 state는 비어 있을 수 있다. 이 경우 target actor는 actor id와 actor factory 같은
언어별 actor 생성 경로로 만들어지고, 필요한 domain state는 target `OnJoinedActor`나 actor의 lazy-load
정책에서 별도로 읽어 올 수 있다.
입장 가능 여부, actor id/type 일치성, source/target route 일치성, transfer deadline 같은 검사는
framework와 `OnActorJoin` 책임이다. transfer adapter는 이런 검사를 반복하지 않는다.

### 5.1 remote transfer의 완료 조건

remote transfer는 아래 조건을 모두 만족해야 성공이다.

1. target node가 actor identity와 request로 admission을 준비했다.
2. target `OnActorJoin`이 `Accept`를 반환했다.
3. source node가 actor transfer adapter로 transfer state `ZLinkMessage`를 만들었다.
4. source node가 source `OnLeaveActor`를 정상 완료했다.
5. source node가 transfer state를 포함한 commit 요청을 target node에 보냈다.
6. target node가 actor transfer adapter로 target actor instance를 materialize했다.
7. target node가 actor membership을 target user Spot으로 commit했다.
8. target `OnJoinedActor`가 정상 완료됐다.
9. actor location row가 target user Spot을 가리키는 committed 상태로 관찰된다.
10. target node가 source node에 commit 완료를 응답했다.

caller는 10번 이후에만 성공 reply를 받을 수 있다. 기존 actor ref, bound session route,
source location tracking 정리는 성공 reply 뒤에도 이어질 수 있는 사후 정리다. 이 정리가
실패하더라도 target commit이 이미 완료됐으면 actor ownership은 target node가 가진다. source 정리는
stale owner release처럼 멱등적으로 재시도할 수 있어야 한다.

### 5.2 target `OnJoinedActor` 전에 source node가 죽는 경우

source node가 target admission accept 뒤 commit 전에 죽으면 transfer는 완료되지 않은 것으로 본다.
target node는 source down signal을 기다리지 않는다. target node는 admission accepted 응답을 보낼 때
pending admission deadline을 함께 설정하고, deadline 안에 commit 요청이 오지 않으면 pending record를
스스로 정리한다.

이 경우 target user Spot은 `OnJoinedActor`를 호출하면 안 된다. actor가 source node와 함께 사라졌다면
client는 reconnect, actor recreate, rejoin 같은 상위 복구 흐름으로 들어간다. target node는 아직 transfer
state를 받지 않았고 actor instance를 만들지 않았으므로 pending admission record, admission reply,
temporary route metadata만 제거하면 된다. 이 정리는 actor lifecycle 성공이 아니므로 target
`OnJoinedActor`와 source/target `OnLeaveActor`를 호출하지 않는다.

```mermaid
sequenceDiagram
  autonumber
  participant SA as Framework @source node
  participant TB as Framework @target node
  participant R as User Spot @target node
  SA->>TB: prepare actor transfer
  TB->>R: OnActorJoin(actorId, request)
  R-->>TB: Accept(reply)
  TB-->>SA: admission accepted(reply)
  Note over SA: source node down before commit
  Note over TB: pending admission deadline / cleanup
  Note over R: OnJoinedActor must not run
```

### 5.3 target commit 뒤 source node가 죽는 경우

target node가 membership commit과 `OnJoinedActor`를 완료한 뒤 source node가 죽으면 transfer는 성공한
것으로 본다. source node의 stale location row나 bound session bookkeeping은 generation fencing과
owner lease 정리로 제거한다.

```mermaid
sequenceDiagram
  autonumber
  participant SA as Framework @source node
  participant TB as Framework @target node
  participant R as User Spot @target node
  SA->>TB: commit actor transfer
  Note over TB: actor membership commit
  TB->>R: OnJoinedActor(actor)
  TB-->>SA: commit acknowledged
  Note over SA: source node down during local cleanup
  Note over TB: target ownership remains valid
```

## 6. actor transfer adapter

remote transfer는 actor 객체를 transport로 직접 보내지 않는다. source node는 actor type별 transfer
adapter로 이동 state를 `ZLinkMessage`로 만들고, target node는 같은 actor type의 transfer adapter로
target actor instance를 만든다.

모든 framework 언어는 같은 public contract를 제공해야 한다. 실제 interface 이름, async 반환형,
cancellation 표현, 등록 API는 언어별 spec에서 고정한다. 특정 언어가 아래 의미의 surface를 제공하지
못하면 구현 완료가 아니라 public contract parity gap으로 기록한다.

| 항목 | 공통 계약 |
| --- | --- |
| 등록 단위 | actor type별 하나의 transfer adapter |
| custom 등록 | actor type과 adapter type을 함께 등록한다. |
| stateless 등록 | actor type만 등록해 framework 기본 stateless adapter를 사용한다. |
| source method | `TransferOut(actor, cancellation)` |
| source 반환 | `ZLinkMessage` 또는 그 언어의 framework message type |
| target method | `TransferIn(actorId, state, cancellation)`. actor 생성에 별도 runtime context가 필요한 언어는 언어별 spec에서 해당 context 인자를 추가로 고정한다. |
| target 반환 | target node에서 사용할 actor instance |
| 미등록 정책 | remote transfer 시작 전 실패. source `OnLeaveActor`를 호출하지 않는다. |
| 빈 state 정책 | adapter가 등록되어 있으면 `TransferOut`이 빈 `ZLinkMessage`를 반환해도 정상 transfer로 처리할 수 있다. |
| 같은 node join | transfer adapter를 호출하지 않고 기존 actor instance를 그대로 이동한다. |

`TransferOut`은 source node의 실제 actor instance를 받는다. actor id는 actor 객체가 이미 알고
있으므로 별도 context로 다시 넘기지 않는다. 이 method는 actor의 이동 가능한 상태만 message로 만든다.
이동 state가 필요 없는 actor type은 빈 `ZLinkMessage`를 반환할 수 있다.

`TransferIn`은 target node에서 아직 actor instance가 없을 때 호출된다. framework는 복원할
`actorId`와 source가 보낸 state message를 넘긴다. 이 method는 target node에서 사용할 actor instance를
반환한다. `.NET`처럼 actor 객체가 framework context를 생성자에서 받아야 하는 언어는, 언어별 spec에서
그 context 인자를 함께 정의한다. 이 context는 검증이나 admission 판단을 위한 값이 아니라 actor instance가
기존 actor factory 계약과 같은 runtime context를 노출하게 하려는 생성 인자다.

state가 비어 있으면 adapter는 actor id와 언어별 actor factory 또는 public actor 생성 경로를 사용해
actor instance를 만들 수 있다. 필요한 domain state는 target `OnJoinedActor` 이후 별도 저장소에서 읽어
올 수 있다.

framework는 transfer adapter 미등록과 빈 state transfer를 구분해야 한다. adapter가 없으면 remote
transfer를 시작하지 않고 실패한다. adapter가 등록되어 있고 빈 `ZLinkMessage`를 반환하면 이는 명시적인
stateless transfer로 본다.

각 언어 framework는 actor type별 stateless transfer adapter를 쉽게 등록할 수 있는 기본 등록 API를
제공해야 한다. 이 기본 adapter는 `TransferOut`에서 빈 `ZLinkMessage`를 만들고, `TransferIn`에서 해당
언어의 public actor 생성 경로로 actor instance를 만든다.

transfer adapter가 담당하지 않는 일:

- actor id, actor type, source/target route 일치성 검사
- admission 조건 검사
- transfer id 중복 처리
- membership 등록
- location row 갱신
- bound session route 연결

위 항목은 framework 또는 `OnActorJoin`의 책임이다. transfer adapter에 검사를 반복시키면 actor state
변환 책임과 join 정책 책임이 섞인다.

transfer adapter 등록은 actor type별로 하나만 허용한다. remote transfer 대상 actor type에 transfer
adapter가 없으면 framework는 remote transfer를 시작하지 않고 실패를 반환해야 한다. 같은 node join은
actor instance를 그대로 이동하므로 transfer adapter를 사용하지 않는다.

transfer state의 content type, codec, version은 `ZLinkMessage`의 envelope에 담는다. framework는 state
message를 transport payload로 전달하지만, 그 내부 domain schema는 해석하지 않는다.

actor transfer와 함께 `OnActorJoin`도 모든 언어에서 같은 의미의 admission surface를 가져야 한다.
`OnActorJoin`은 actor instance를 받지 않고, actor identity와 request만 받아야 한다. 실제 method
signature는 언어별 spec에서 고정한다.

`ZLinkActorJoinAdmission`은 actor instance가 아니라 identity와 route metadata만 담는다.

| 필드 | 의미 |
| --- | --- |
| `ActorId` | join하려는 actor id |
| `ActorType` | join하려는 actor type |
| `SourceSpotRid` | actor가 떠나려는 source Spot |
| `TargetSpotRid` | actor가 들어가려는 target Spot |
| `SourceNodeRid` | remote transfer일 때 source node |
| `TargetNodeRid` | target Spot을 소유한 node |

언어별 framework는 위 필드를 다른 이름의 low-level header나 raw message로 application에 노출하면 안
된다. application이 admission에서 보는 입력은 actor identity, source/target, request payload로 제한한다.

## 7. Entry Spot lifecycle와 user Spot transfer

actor 생성과 user Spot transfer는 다른 사건이다.

| 사건 | Entry Spot lifecycle | user Spot lifecycle |
| --- | --- | --- |
| actor가 처음 생성됨 | `OnCreateActor`를 한 번 호출한다. 생성 직후 actor는 Entry Spot에 있다. | 호출하지 않는다. |
| Entry Spot에서 user Spot으로 join 성공 | Entry Spot `OnLeaveActor`를 호출한다. | target user Spot `OnActorJoin` accept 뒤 `OnJoinedActor`를 호출한다. |
| user Spot에서 다른 user Spot으로 join 성공 | source user Spot `OnLeaveActor`를 호출한다. | target user Spot `OnActorJoin` accept 뒤 `OnJoinedActor`를 호출한다. |
| user Spot에서 Entry Spot으로 leave 성공 | target Entry Spot `OnJoinedActor`를 호출한다. | source user Spot `OnLeaveActor`를 호출한다. |
| destroyActor 성공 | `OnLeaveActor`를 호출하지 않는다. actor 수명 종료다. | actor가 user Spot에 있으면 destroy할 수 없다. |

remote transfer에서 target node는 admission 단계에서 actor instance를 만들지 않는다. commit 요청을
받은 뒤 source actor의 논리적 수명을 이어받는 target actor instance를 materialize한다. 이 작업은 새
actor 생성 완료가 아니므로 target Entry Spot `OnCreateActor`를 호출하지 않는다. user Spot membership
완료 신호는 항상 target user Spot `OnJoinedActor`다.

## 8. location row와 generation

actor location row는 actor packet routing과 remote lookup의 기준이다. 따라서 location row는 lifecycle
의미와 모순되면 안 된다.

공통 규칙:

- `OnActorJoin` accept만으로 public actor location을 target user Spot으로 확정하지 않는다.
- target `OnJoinedActor`가 정상 완료된 뒤 actor location row를 target user Spot의 committed location으로 공개한다.
- location row를 먼저 써야 하는 구현은 그 row를 pending 상태로 취급해야 하며, resolver와 actor packet routing은 pending row를 성공한 user Spot join으로 해석하면 안 된다.
- `OnJoinedActor`가 실패하면 target user Spot location으로 공개하지 않는다. 이미 public row가 보였다면 rollback하거나 actor packet dispatch를 중단하고 reconcile 대상으로 격리한다.
- source와 target이 동시에 같은 actor의 live owner라고 주장하지 않도록 generation fencing을 사용한다.
- stale source owner의 release는 target owner의 generation을 지우면 안 된다.
- target commit 뒤 source cleanup이 실패해도 target owner가 이긴다.

구현 편의를 위해 target node가 `OnActorJoin` 전에 claim을 먼저 수행할 수 있다. 이 경우 다음 중 하나를
반드시 만족해야 한다.

- claim이 pending 상태라 public resolver가 성공한 user Spot join으로 해석하지 않는다.
- source가 commit 전에 죽으면 target이 claim을 release하거나 timeout cleanup으로 제거한다.
- generation fencing과 owner lease가 pending/committed 상태를 구분해 stale route가 actor packet을
  잘못 받지 않게 한다.

## 9. bound session transfer

bound session은 actor 위치와 독립된 축이지만, remote transfer가 성공하면 session-bound actor의 push와
reply가 target node의 actor instance로 이어져야 한다.

공통 규칙:

- remote transfer request에는 source bound session route를 전달해야 한다.
- target node는 commit 전에 bound session route를 준비할 수 있다.
- target `OnJoinedActor` 완료 전에는 bound session push를 target user Spot actor로 성공 처리하면 안 된다.
- transfer가 실패하면 source bound session binding은 유지되거나, source node가 죽은 경우 client
  reconnect 흐름으로 복구한다.
- transfer가 성공하면 source node의 old binding release는 멱등적으로 처리한다.

## 10. callback과 transfer 오류 처리

`OnActorJoin`, `OnLeaveActor`, `OnJoinedActor`와 actor transfer adapter는 application 코드이므로 실패할 수
있다. framework는 실패 시점을 기준으로 join 결과를 다르게 처리한다.

| 실패 지점 | join 결과 | 필수 처리 |
| --- | --- | --- |
| target `OnActorJoin` throws | 실패 | target membership을 만들지 않고 caller에 실패를 반환한다. |
| target `OnActorJoin` rejects | rejected | source membership을 유지하고 joined/left callback을 호출하지 않는다. |
| source `TransferOut` throws | 실패 | source `OnLeaveActor`, target `TransferIn`, target `OnJoinedActor`를 호출하지 않는다. source membership을 유지한다. |
| source `OnLeaveActor` throws | 실패 | target `OnJoinedActor`를 호출하지 않는다. 가능한 경우 source membership을 유지한다. |
| target `TransferIn` throws | 실패 | target membership을 만들지 않고 caller에 실패를 반환한다. source actor는 이미 left가 끝났으므로 runtime reconcile 대상으로 기록한다. |
| target commit 실패 | 실패 | target `OnJoinedActor`를 호출하지 않는다. pending state를 cleanup한다. |
| target `OnJoinedActor` throws | 실패 | caller에게 성공을 반환하지 않는다. target membership을 rollback한다. rollback할 수 없으면 actor packet dispatch를 중단하고 actor를 reconcile 대상으로 격리한다. |
| source cleanup 실패 | 성공 가능 | target commit과 `OnJoinedActor`가 완료됐다면 target ownership을 유지하고 source cleanup을 재시도한다. |

`OnJoinedActor` 실패 뒤에는 그 join이 성공한 membership으로 관찰되면 안 된다. 가장 좋은 처리는
target membership과 location row를 rollback하는 것이다. 이미 외부 store write나 actor instance 준비가
섞여 있어 즉시 되돌릴 수 없으면, framework는 해당 actor의 target user Spot packet dispatch를 막고
runtime error event와 reconcile 대상으로 기록해야 한다.

## 11. 언어별 구현 요구 사항

각 언어 framework는 다음 항목을 feature-map 또는 구현 문서에 표시해야 한다.

- 같은 node join에서 callback 순서가 `OnActorJoin`, `OnLeaveActor`, `OnJoinedActor` 순서인지
- remote transfer에서 admission과 commit이 분리되어 있는지
- remote transfer에서 actor type별 transfer adapter를 통해 state message를 전달하거나 빈 state transfer를 명시적으로 처리하는지
- transfer adapter 미등록 actor type의 remote transfer를 명시적으로 실패시키는지
- target commit ack 이후 source cleanup 실패를 join 실패로 되돌리지 않고 멱등 정리 대상으로 남기는지
- source node down signal 없이도 accept / before commit 상태의 pending admission을 deadline으로 정리하는지
- target `OnJoinedActor` 완료 전 caller에게 success를 반환하지 않는지
- target `OnJoinedActor` 완료 전 actor packet dispatch가 target user Spot으로 들어가지 않는지
- location row가 pending join과 committed join을 구분하는지
- bound session transfer가 commit 완료 전 성공으로 노출되지 않는지

이 항목 중 하나라도 빠지면 그 언어는 이 스펙을 완전히 만족하지 않는다. 구현 gap은 "테스트 미구현"이
아니라 public contract parity gap으로 기록한다.

## 12. 회귀 테스트 기준

모든 언어는 최소한 아래 테스트를 가져야 한다.

| 테스트 | 검증 내용 |
| --- | --- |
| local join accept order | target admission accept 뒤 source left가 target joined보다 먼저 관찰된다. |
| local join reject no side effect | reject 시 source left, target joined, location update가 없다. |
| remote join success order | target admission, source left, target joined, commit ack와 success reply 순서가 증거로 남는다. source cleanup은 성공 뒤 멱등 정리 evidence로 분리한다. |
| remote transfer state | source transfer out state가 target transfer in으로 전달되고 복원된 actor가 target joined에 전달된다. |
| remote transfer empty state | 등록된 stateless transfer adapter가 빈 state로 target actor를 만들고, target joined 이후 domain state를 별도로 읽어 올 수 있다. |
| missing transfer adapter | remote transfer 대상 actor type에 transfer adapter가 없으면 source left 없이 실패한다. |
| source down before commit | target admission accept 뒤 commit이 오지 않으면 target joined가 호출되지 않고 pending admission이 deadline으로 정리된다. |
| source down after commit | target joined 완료 뒤 source cleanup이 실패해도 target ownership이 유지된다. |
| joined callback failure | target joined callback 실패 시 caller가 success를 받지 않는다. |
| packet during moving | moving 상태에서 source와 target 양쪽 user Spot handler가 동시에 actor packet을 처리하지 않는다. |
| bound session transfer | remote transfer 성공 뒤 bound session push가 target actor로 도달하고, 실패한 transfer는 성공으로 보이지 않는다. |

테스트는 단순 source grep이 아니라 실제 runner나 fake backend로 callback 순서, location row, actor packet
dispatch 결과를 검증해야 한다.
