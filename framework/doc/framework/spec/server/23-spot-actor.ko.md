# Spot과 Actor membership

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Actor 모델](22-actor-model.ko.md) · [Location runtime](40-location-runtime.ko.md)

이 문서는 ZLink Framework 10.0.0에서 Actor가 Spot에 참여하고 다른 Spot으로 이동할 때의 공개 lifecycle,
ordering과 location authority를 정의한다. 대상 독자는 Spot·Actor runtime과 location store를 구현하는
개발자다.

## 1. Identity와 authority

`ActorRef`의 generation은 같은 Actor ID의 생성 수명을 구분한다. Membership epoch는 같은 generation이
참여하는 Spot 위치가 바뀔 때 증가한다. Spot과 Actor가 같은 MeshNode에 있더라도 두 값의 의미는 달라지지
않는다.

Redis location store의 Actor location row가 분산 owner와 membership epoch의 authority다. Framework
memory의 route cache는 이 row의 snapshot이며 단독으로 owner를 확정하지 않는다. Join, leave와 transfer는
expected generation과 membership epoch를 검증하는 CAS로 location을 갱신한다.

## 2. Entry Spot과 user Spot

Actor를 만들면 owner MeshNode의 Entry Spot이 creation record를 처리한다. Entry Spot은 actor type을 확인하고
initial state를 만든 뒤 user Spot join을 시작할 수 있다. Actor 업무 message는 Actor application queue로
직접 전달되며 Entry Spot이나 user Spot callback을 경유하지 않는다.

User Spot은 join, joined, leave와 disconnected lifecycle control을 처리한다. Lifecycle callback은 해당
Spot의 control claim에서 실행되며 같은 Spot의 일반 packet·timer turn과 정의된 순서로 직렬화된다.

## 3. Join commit

Join은 다음 순서로 완료된다.

1. source가 target Spot과 expected Actor generation을 확인한다.
2. target `OnActorJoin`이 join admission payload와 immutable Actor identity snapshot을 검증해 accept 또는 reject를 반환한다.
3. accept이면 source `OnLeaveActor`가 현재 membership을 정리한다.
4. location authority가 membership epoch를 증가시키는 CAS를 commit한다.
5. target membership을 공개하고 immutable membership snapshot으로 `OnJoinedActor`를 실행한다.
6. source operation을 새 ActorRef location snapshot으로 완료한다.

`OnActorJoin` reject 또는 CAS 실패에서는 target membership을 공개하지 않는다. Join reply의 성공이
membership epoch를 증가시키는 유일한 commit point다. Stale generation 또는 epoch는 stale-location 결과로
완료하며 Framework가 현재 owner를 추측해 적용하지 않는다.

## 4. 같은 MeshNode의 join

Actor handler가 같은 MeshNode의 다른 user Spot으로 이동을 요청하면 caller의 Actor turn은 join completion을
기다릴 수 있지만 Spot callback을 직접 실행하지 않는다. Target `OnActorJoin`, source `OnLeaveActor`와 target
`OnJoinedActor`는 각각 해당 Spot의 control claim으로 제출하며 관찰 순서는 §3과 같다. Actor의
infrastructure claim은 이 control operation의 completion을 Actor turn과 독립적으로 진행한다.

Spot control callback에는 ActorRef, Actor type과 membership epoch를 담은 immutable membership snapshot을
전달한다. callback은 mutable Actor object나 다른 Spot의 실행 흐름을 보유하지 않는다. Actor의 업무 상태를
읽거나 바꿔야 하면 snapshot의 ActorRef로 Actor send/request를 제출해 Actor turn에서 처리한다. 서로 반대
방향인 두 join이 동시에 시작되어도 control claim 사이의 순환 대기가 생기지 않아야 하며 local join 전체를
MeshNode 전역 lock으로 직렬화하지 않는다.

## 5. 다른 MeshNode로 transfer

다른 MeshNode의 Spot으로 이동할 때 location authority는 transfer identity, source·target participant,
expected Actor generation과 membership epoch로 한 operation을 식별한다. Framework는 Core가 발급한 sealed
transfer token과 정확히 다음 membership epoch로 source·target fence를 적용한다. Location store는 Core
token을 저장하지 않고 다음 durable 상태를 원자적으로 기록한다.

| 상태 | 의미 |
|---|---|
| Prepared | source snapshot과 target reserve가 있으며 location owner는 바뀌지 않았다 |
| Committed | location row가 target owner와 새 membership epoch를 가리킨다 |
| Activated | target Actor queue가 새 owner route로 message를 처리할 수 있다 |
| Aborted | target reserve를 해제하고 source owner를 유지한다 |

Commit 전에는 target Actor handler를 실행하지 않는다. Commit 뒤 target activation이 끝나야 새 owner route를
ready로 공개한다. Source route는 설정된 forwarding window가 끝나면 stale target을 거부한다. 세부 queue와
barrier 자료 구조는 Core와 internals가 소유한다.

## 6. Failure와 recovery

Prepare 전에 실패하면 source membership을 유지한다. Prepared 상태에서 deadline이 끝나면 participant가
transfer ID와 recovery lease를 확인해 abort할 수 있다. Committed 상태에서는 source로 rollback하지 않고 target
activation을 복구한다. Successor MeshNode는 Redis의 participant state와 Actor location row를 읽어 같은
결정을 이어서 수행한다. Successor가 authority를 takeover하면 자신의 Core runtime에서 participant를 다시
prepare해 새 opaque token을 얻으며 다른 process의 token을 재사용하지 않는다.

Application callback이 실패한 경우 location row를 임의로 변경하지 않는다. Commit 전 실패는 source
membership을 유지한다. Commit 뒤 실패는 recoverable Actor error로 기록하고 target activation 또는
reconciliation을 계속한다. Callback 재시도는 transfer token과 phase를 기준으로 idempotent해야 한다.

## 7. Session binding

Actor가 bound STREAM session을 가진 상태에서 membership이 바뀌어도 session identity는 유지된다. 새 owner가
session relay authority를 얻기 전까지 application message를 처리하지 않는다. Disconnect와 unbind record는
Actor의 infrastructure queue에서 진행되며 Spot lifecycle callback으로 전달하지 않는다.

## 8. 검증 요구

- join reject, stale generation과 stale epoch가 membership을 변경하지 않는다.
- 같은 MeshNode에서 반대 방향 join 두 개가 서로 기다리지 않고 완료된다.
- transfer의 각 failure point가 Prepared abort 또는 Committed recovery 중 하나로 수렴한다.
- message sequence가 transfer 전후에 중복되거나 역전되지 않는다.
- Actor payload가 Spot callback을 거치지 않고 Actor queue에서 처리된다.
- Redis capability가 없는 location store로 분산 transfer를 시작하면 startup이 실패한다.

## 9. Bound session route handoff

Bound session이 있는 Actor를 다른 MeshNode로 transfer하면 session identity와 client connection은 유지한다.
Commit 전 실패는 source binding을 유지하고 target binding을 만들지 않는다. Commit 뒤에는 target Actor가
session relay authority를 얻은 뒤에만 application packet과 push를 처리한다. Source binding 정리는 target
activation을 막지 않으며 같은 session의 packet 순서를 바꾸지 않는다.

## 10. In-flight packet handoff

### 10.1 Moving 상태의 admission

Prepare가 source Actor admission을 닫은 뒤 도착한 packet은 source application handler에서 실행하지 않는다.
Source infrastructure queue는 packet과 arrival sequence를 transfer backlog로 보존한다. Commit 전 abort하면
backlog를 source Actor queue 앞에 되돌리고, commit하면 target에 전달한다.

### 10.2 Ordering

Transfer는 다음 순서를 보장한다.

1. Moving 구간에 수락한 packet을 유실하거나 중복하지 않는다.
2. Source backlog의 arrival sequence를 target에서도 유지한다.
3. Backlog를 target Actor queue에 넣기 전에 새 owner route를 ready로 공개하지 않는다.
4. 같은 bound session에서 transfer 전후에 보낸 packet은 session FIFO 순서를 유지한다.

### 10.3 Commit과 activation

Target reserve, durable location commit, backlog enqueue, target activation과 route publication 순서를 지킨다.
Commit 뒤에는 source로 rollback하지 않는다. Target activation을 복구하고 동일 transfer ID의 backlog replay를
idempotent하게 처리한다.

### 10.4 Straggler forwarding

Commit 뒤 old ActorRef로 도착한 packet은 설정된 forwarding window 안에서 target으로 한 번 전달한다.
Forwarding entry는 Actor generation과 membership epoch를 함께 검증하며 재이동하면 최신 target으로 갱신한다.
Window가 끝나면 entry를 제거하고 stale packet을 `ActorLocationStale`로 실패시킨다. Framework는 실패한
packet을 저장하거나 새 location으로 자동 재전송하지 않는다.
