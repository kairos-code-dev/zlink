# Actor 모델 — 공통 스펙

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Spot Actor](23-spot-actor.ko.md) · [Session Actor Dispatch](31-session-actor-dispatch.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 Actor의 identity, 위치, 메시지 queue, lifecycle과 session binding의
공통 공개 계약을 정의한다. 이 문서는 “Actor가 어느 Spot에 속하더라도 업무 payload와 membership 제어를
각각 어느 실행 문맥에서 처리하는가?”라는 질문에 답한다.

MeshNode route와 admission은 [21 MeshNode](21-mesh-node.ko.md), Spot membership의 transaction과 relocation은
[23 Spot Actor](23-spot-actor.ko.md), STREAM session 연동은
[31 Session Actor Dispatch](31-session-actor-dispatch.ko.md)가 소유한다. payload와 metadata는
[03 메시지 모델](../03-message-model.ko.md), callback 실행과 completion은
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 소유한다.

## 2. Identity와 상태 축

Actor는 Location Store namespace 전체에서 전역인 논리 `ActorId`로 식별되는 stateful object다. `ActorId`는
UTF-8 1..255 bytes의 case-sensitive exact value다. Framework는 Unicode normalization이나 case folding을
적용하지 않는다. MeshName은 최초 placement를 선택하는 attribute이며 identity key가 아니다. 같은
`ActorId`를 MeshName별로 중복 생성할 수 없다.

Actor type은 UTF-8 1..255 bytes의 stable name이며 create가 사용할 factory를 선택한다. 언어 class 이름이나
generic type 이름을 Store와 wire identity로 사용하지 않는다. 같은 server에 같은 stable type을 중복
등록하면 startup 오류다.

`ActorRef`는 `ActorId`, non-zero unsigned 63-bit `ObjectGeneration`, 현재 `MeshName`, 현재 `NodeRid`를 담는
immutable location snapshot이다. `ActorRef`는 메시지 target이 아니며 위치가 바뀌면 stale할 수 있다.
ObjectGeneration은 JSON에서 decimal string으로 표현한다. 별도 `ActorRefSnapshot` public type은 제공하지
않는다.

Actor의 상태는 다음 두 축을 독립적으로 관리한다.

| 축 | 상태 | 의미 |
|---|---|---|
| Spot membership | Entry Spot, user Spot, 이동 중 | Actor의 논리 위치와 membership을 나타낸다. |
| STREAM binding | unbound, bound | 현재 client session으로 push하거나 session payload를 받을 수 있는지를 나타낸다. |

user Spot membership은 bound session을 요구하지 않는다. session bind와 unbind도 Actor의 현재 Spot을
바꾸지 않는다. 한 Actor는 동시에 하나의 session에만 bind할 수 있고, 한 session은 여러 Actor를 bind할
수 있다.

## 3. Actor queue

모든 Actor 업무 payload는 target Actor의 application queue에 직접 제출한다. Actor가 Entry Spot 또는
user Spot에 있거나 remote MeshNode에 있더라도 이 규칙은 같다.

- 같은 Actor에 수락된 payload는 Actor queue claim에서 FIFO로 하나씩 처리한다. Actor queue claim을 보유한
  handler가 끝나기 전에는 같은 Actor의 다음 payload를 시작하지 않는다.
- Entry Spot과 `PerActor` User Spot에서는 서로 다른 Actor가 독립적으로 실행된다. `SpotWide` User Spot에서는
  각 Actor queue head가 User Spot 공통 execution gate도 얻어야 하므로 member Actor, Spot handler, timer와
  lifecycle callback이 한 번에 하나만 실행된다.
- Actor send/request, STREAM session relay와 Actor 간 호출은 같은 Actor queue로 합류한다.
- Actor payload를 Spot application queue에 넣거나 Spot callback으로 변환하지 않는다.

`SpotWide` Member Actor가 `Yield`하면 Actor queue claim은 유지하고 User Spot gate만 반납한다. 다른 Actor,
Spot handler와 timer는 실행할 수 있지만 같은 Actor의 다음 payload는 실행할 수 없다. Terminal continuation이
같은 User Spot gate를 다시 얻어 현재 payload를 완료한 뒤에만 Actor claim을 해제한다. `PerActor`와 Entry
Spot Actor에서는 `Yield`를 허용하지 않으며 공통 call type의 호출은 operation submit 전에
`InvalidConfiguration`으로 끝난다.

Actor handler는 Actor 자신의 상태를 소유한다. Actor handler가 room·stage·zone 같은 Spot 소유 상태를
읽거나 바꾸려면 명시적인 Spot send/request를 제출해야 한다. 그 작업은 target Spot turn에서 실행된다.
Actor handler에 mutable Spot object를 직접 제공해서 두 실행 문맥의 직렬성 경계를 우회하지 않는다.

`SpotWide` Member Actor가 같은 User Spot gate가 필요한 Spot 또는 다른 member Actor의 request를 `Async`로
기다리면 target을 실행할 수 없으므로 Framework는 resolve 뒤 outbound admission 전에
`InvalidConfiguration`으로 거부한다. 현재 User Spot을 떠나는 `JoinSpot(...).Async`도 source lifecycle
callback이 같은 gate를 얻어야 하므로 operation submission과 queue 변경 전에 같은 오류로 거부한다.
같은 Actor 자신에 대한 awaited request도 Actor queue claim 때문에 실행할 수 없으므로 `Async`와 `Yield`
모두 거부한다. 같은 Actor에 보내는 one-way payload는 현재 payload 뒤 FIFO에 넣을 수 있으며 inline 또는
재진입 handler 호출로 바꾸지 않는다.

ready notification, request completion, relocation barrier와 session-binding progress 같은 infrastructure
작업은 Actor application claim과 분리한다. application handler가 대기 중이어도 infrastructure progress가
계속되어야 한다.

## 4. Spot control claim

Spot은 Actor 업무 payload를 처리하지 않는다. Spot turn에 전달하는 Actor 관련 작업은 membership과
lifecycle control로 제한한다.

| Control 작업 | Spot 쪽 의미 |
|---|---|
| join | membership 허용 여부를 판단하고 Spot 소유 membership을 갱신한다. |
| leave | membership을 해제하고 Spot 소유 정리를 수행한다. |
| relocation prepare·commit·abort | 이동 transaction에서 Spot이 소유한 상태를 일관되게 바꾼다. |
| Actor lifecycle notification | 생성·종료와 연결된 Spot 소유 후속 작업을 실행한다. |

각 control 작업은 target Spot의 control claim으로 실행되며 같은 Spot의 다른 Spot-owned callback과
직렬화된다. Actor 쪽 상태 변경도 Actor control claim으로 직렬화한다. 두 owner를 함께 바꾸는 순서와
fencing은 [23 Spot Actor](23-spot-actor.ko.md)가 정한다.

## 5. 메시징

Actor send/request는 global `ActorId`만 대상으로 받는다. Framework는 positive route cache 또는 Location
Store에서 current Ready incarnation과 owner route를 resolve하고, 선택한 ObjectGeneration과 owner fence를
target admission에 고정한다. local과 remote Actor의 handler 및 completion 의미는 같다.

- 호출자는 MeshName, `ActorRef`, owner RID나 현재 Spot ID를 messaging target으로 넘기지 않는다.
- Missing, Creating과 Store failure는 negative cache에 저장하지 않는다. Positive Ready cache도 current owner
  lease의 local admission deadline과 공개 `RouteCacheMaxAge` 안에서만 사용한다.
- Higher StoreVersion, stale result 또는 Store recovery event를 확인하면 positive cache를 즉시 invalidate한다.
- Resolve 뒤 destroy와 recreate가 발생해도 진행 중인 이전 generation operation을 새 generation으로
  retarget하지 않는다.
- request를 보낸 뒤 timeout이나 실행 여부를 알 수 없는 실패가 발생하면 자동 재전송하지 않는다.
- Actor direct 메시징은 bound session을 만들거나 바꾸지 않는다.

handler 선택은 Actor type, message kind와 packet name을 사용한다. 같은 Actor handler namespace에 같은
key를 중복 등록하면 startup 오류다. handler 등록의 정확한 타입과 시그니처는 언어별 공개 인터페이스
문서가 정한다.

## 6. Lifecycle

Object Server는 Actor stable type, factory와 `Disabled`, `Recreate`, `Snapshot` 중 하나의 relocation policy를
함께 등록한다. 생략 policy overload와 compatibility default는 제공하지 않는다. Snapshot policy는
Actor type에 맞는 `ActorRelocationAdapter`를 같은 등록에서 요구한다. Adapter는 application이 해석하는
opaque byte sequence만 capture·restore하며 Framework는 별도 state contract ID를 관리하지 않는다.

Actor manager의 `Create`와 `GetOrCreate`는 required `ActorId`와 stable Actor type을 받는 single-use fluent
call이다. `InMesh`, encoded creation request와 timeout만 선택 항목이다. Caller가 target RID, predicate,
factory class 또는 별도 placement selector를 지정할 수 없다. 같은 option을 두 번 설정하면
`InvalidConfiguration`, terminal submit을 두 번 실행하면 `AlreadySubmitted`다. Terminal submit을 시작할 때
resolve, reservation, factory와 Ready barrier 전체에 적용할 end-to-end deadline 하나를 고정한다.

`InMesh`를 지정하면 해당 Mesh를 사용한다. 생략했을 때 object Client 또는 Server role을 가진 Mesh가 하나면
자동 선택한다. 후보가 0개이면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한 Mesh가
없으면 `MeshNotFound`로 끝난다. Framework는 role, registered stable type과 typed population
capacity를 먼저 검사하고 남은 후보를 node-wide placement weight로 선택한다. Caller가 target node나
endpoint를 고르지 않는다.

Encoded creation request는 최대 1 MiB다. Reservation 전에 immutable content reference와 hash를 durable
creation intent에 기록하며 Ready 또는 fenced failure cleanup까지 유지한다. Authority CAS winner만 request를
factory에 전달한다. Factory는 `(ActorId, ObjectGeneration, creation attempt)` 기준 at-least-once로 실행될 수
있으므로 retry-safe해야 한다. Target은 factory가 만든 staging Actor를 Entry Spot의 Actor creation callback에
전달한다. Callback은 생성 승인 여부와 최대 1 MiB의 optional reply를 반환하며 application rejection과 callback
exception은 서로 다른 결과다. Callback exception은 기존 typed creation failure로 끝난다.

Exclusive `Create`에서 같은 type의 Ready object가 있으면 `ActorAlreadyExists`, 다른 type이면
`ActorTypeMismatch`다. 새 attempt는 `Created` 또는 `Rejected` terminal result를 반환한다. `Created`는 Ready로
commit한 `ActorRef`와 callback reply를, `Rejected`는 Actor 없이 callback reply를 가진다.
`GetOrCreate`는 같은 type의 Ready Actor를 찾으면 factory와 creation callback을 호출하지 않고 `Existing`과
`ActorRef`를 반환한다. 같은 type의 Creating attempt가 있으면 authority 변경을 기다린 뒤 다시 조회한다.
기존 attempt가 Ready로 commit됐으면 `Existing`을 반환한다. 기존 attempt가 거절되거나 실패하여 Creating
authority가 정리됐으면 남은 deadline 안에서 새 reservation을 경쟁하고, winner가 자신의 creation request로
factory와 callback을 실행한다. 서로 다른 operation은 앞선 attempt의 `Rejected` state나 application reply를
공유하지 않는다. 동일한 `OperationId`가 재전달된 경우에만 operation terminal record에서 기존 결과를
재전송한다.

Creation callback이 승인하면 Framework는 initial Entry membership과 Ready authority, reserved-to-active
capacity와 `Created` terminal record를 하나의 commit으로 공개한다. 이 최초 생성 과정에서는
`OnActorJoin`과 joined notification을 호출하지 않는다. 거절하면 staging Actor를 폐기하고 Ready publication과
message admission을 열지 않은 채 exact Creating authority와 pending capacity를 정리하며 `Rejected` terminal
record를 같은 transaction에서 공개한다. 거절된 Actor는 `Find`에서 조회되지 않고 active capacity와 destroy
lifecycle에 포함되지 않는다. 정리가 끝난 뒤 같은 Actor ID의 새 `GetOrCreate`는 새 reservation ID로 attempt를
시작할 수 있다. 이전 operation의 중복 전달만 operation retention 동안 이전 terminal result를 읽는다.

Manager `Find(ActorId)`는 current Ready authority의 `ActorRef`를 반환하며 creation을 시작하지 않는다. 별도
Actor directory는 제공하지 않는다.

Actor를 user Spot으로 옮기는 join·leave·relocation은
[23 Spot Actor](23-spot-actor.ko.md)의 fencing과 barrier를 따른다. 이동 중에 수락한 payload를 이전 Spot
callback으로 보내지 않으며, Actor queue가 순서를 유지한다.

Actor 종료는 신규 payload admission을 닫고 session binding과 location ownership을 정리한다. bound
session의 연결 종료만으로 Actor를 자동 종료하거나 Spot에서 자동 leave하지 않는다. lifecycle 종료의
정확한 허용 상태와 transaction은 [23 Spot Actor](23-spot-actor.ko.md)가 소유한다.

Actor destroy는 exact `ActorRef`를 받는다. Actor가 user Spot에 있으면 먼저 leave 또는 Entry Spot join을
완료해야 한다. Destroy는 membership 이동이 아니므로 성공 과정에서 `OnLeaveActor`를 다시 호출하지 않는다.
신규 payload admission을 닫고 진행 중인 lifecycle 작업을 정리한 뒤 session binding, location ownership과
registry를 제거한다. 같은 incarnation이 이미 없으면 idempotent `false`, 같은 ID의 다른 generation이 있으면
`ActorGenerationStale`, 이동 seal 중이면 `ActorMoving`으로 끝난다. Framework는 current ref를 다시 찾아 새
incarnation을 종료하지 않는다.

## 7. Session binding

session binding은 Actor와 현재 STREAM session 사이의 runtime 관계다. binding token은 재연결과 늦게
도착한 이전 session 작업을 구분한다. Actor handler는 현재 bound session을 통해 client로 one-way push를
보내거나 연결 종료를 요청할 수 있다.

session inbound Actor payload도 Actor queue로 직접 제출한다. Spot membership 조회는 route와 lifecycle
검증에 사용할 수 있지만 payload dispatch 위치를 Spot callback으로 바꾸지 않는다. bind, rebind,
disconnect와 request correlation의 전체 계약은
[31 Session Actor Dispatch](31-session-actor-dispatch.ko.md)가 정한다.

## 8. 실패와 관측

- logical ID의 Ready authority가 없으면 Actor target 오류다. Exact-ref operation에서 mapping이 없으면
  `ActorLocationStale`, generation이 다르면 `ActorGenerationStale`, pre-commit seal 중이면 `ActorMoving`이다.
- handler 없음, decode 실패와 application 예외는 request이면 복원 가능한 reply route로 오류를
  반환하고, one-way이면 runtime 관측 경로에 기록한다.
- bound session이 필요한 작업에 유효한 binding이 없으면 session-not-bound 오류다.
- drain 중에는 신규 Actor 생성과 신규 membership 배정을 막고 이미 수락한 Actor turn과 control
  transaction은 deadline까지 진행한다.

관측 정보는 current MeshName, Actor type, queue와 control backlog, generation, membership state,
session-binding state와 dispatch 결과를 구분해야 한다. Actor ID는 metric label로 사용하지 않는다.

## 9. 검증 요구

- Entry Spot과 user Spot의 Actor payload가 모두 Actor queue로 직접 전달된다.
- Actor payload가 Spot callback이나 Spot application queue를 거치지 않는다.
- join·leave·relocation과 lifecycle control만 Spot control claim을 사용한다.
- 같은 Actor의 payload가 ingress 종류와 무관하게 Actor queue 수락 순서대로 실행된다.
- Actor handler가 mutable Spot state에 직접 접근하지 않고 명시적인 Spot 호출을 사용한다.
- session bind와 Spot membership이 독립적으로 바뀌며 서로를 암묵적으로 변경하지 않는다.
- 같은 ActorId를 서로 다른 MeshName에 중복 생성하지 않는다.
- Actor messaging이 ActorId만 받고 owner route와 generation을 application에 요구하지 않는다.
- concurrent create의 CAS loser가 현재 callback과 동시에 factory를 실행하지 않으며, Ready면 `Existing`,
  rejection cleanup이면 새 reservation으로 자신의 callback을 실행한다.
- destroy가 exact generation을 검사하고 새 incarnation으로 retarget하지 않는다.
