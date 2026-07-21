# Spot과 Actor membership

[스펙 목차](../README.ko.md) · [Actor 모델](22-actor-model.ko.md) ·
[Spot 주소 메시징](24-spot-address-messaging.ko.md) ·
[Session Actor Dispatch](31-session-actor-dispatch.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

이 문서는 ZLink Framework 11.0.0에서 Actor 생성, Spot membership, 이동, ordering과 recovery를 정의한다.
Core는 raw socket과 transport만 제공하며 이 상태와 lifecycle은 각 언어 Framework runtime이 소유한다.

## 1. Identity와 authority

`ActorRef`의 generation은 ObjectGeneration이다. 같은 Actor ID의 생성 수명 동안 유지하며 destroy 뒤 같은 ID를
다시 만들 때만 새 값을 발급한다. Spot membership 또는 owner MeshNode가 바뀌면 ObjectGeneration은 유지하고
provider가 새 AuthorityOwnerGeneration을 발급한다.

Store-backed Actor authority row는 current owner, Spot membership, ObjectGeneration, AuthorityOwnerGeneration,
StoreVersion과 exact owner lease를 저장한다. Runtime route cache는 row snapshot이며 단독으로 authority를 결정하지
않는다. Join, leave, transfer와 destroy는 expected StoreVersion, 두 generation과 owner lease를 검증하는 CAS만
사용한다.

Location Store가 없는 Actor는 runtime-local opaque authority와 same-process handle만 사용한다. Remote directory
resolve, distributed join, transfer·relocation과 distributed session binding은 `TransferDisabled` 또는
startup/configuration error로 거부한다. Hidden local Store나 CAS provider를 만들지 않는다.

## 2. Creating에서 Ready까지

Store-backed Actor Create는 internal `Creating → Ready` publication barrier를 사용한다.

1. NewObject CAS가 final ObjectGeneration, AuthorityOwnerGeneration과 `Creating` row를 먼저 발급한다.
2. 같은 exact fence를 가진 local activation registry에서 typed factory, initialize와 initial Entry Spot membership을
   수행한다.
3. 모든 단계가 성공하면 same object·owner fence의 Ready CAS를 수행한다.
4. Resolver와 remote send·request는 Ready row만 반환하거나 수락한다.

Factory, initialize 또는 initial membership이 실패하면 local barrier를 failed·sealed로 고정한다. Current request는
typed failure로 terminal-once 완료하고 exact StoreVersion, ObjectGeneration, AuthorityOwnerGeneration과 owner lease로
row를 fenced delete한다. Cancellation, timeout과 response loss는 exact Read로 reconcile한다. Missing이 확인될
때까지 같은 failure를 반환하고 callback을 숨겨서 다시 실행하지 않는다. Registry를 정리한 뒤 다음 caller만
NewObject claim으로 새 generation을 발급한다. Late callback은 exact fence가 다르면 아무 상태도 변경하지 못한다.

Entry Spot은 startup initialization을 마치기 전 descriptor와 resolver에 publish하지 않는다. 이 조건은 host
Preparing→Serving gate의 일부다.

## 3. Entry Spot과 User Spot

Actor를 만들면 owner MeshNode의 Entry Spot이 initial membership을 처리한다. Actor 업무 message는 Actor queue로
직접 전달하며 Entry Spot이나 User Spot callback을 경유하지 않는다.

User Spot은 join, joined, leave와 disconnected lifecycle control을 해당 Spot control queue에서 직렬화한다. 같은
Spot의 packet·timer turn과 callback 순서는 Spot turn이 정한다. Instance Spot은 Actor membership target이 아니다.

Store-backed dynamic User Spot도 internal `Creating → Ready` barrier를 사용한다. NewObject CAS가 final
ObjectGeneration과 `Creating` row를 발급하고 factory·configure·initialize가 끝난 뒤 same fence Ready CAS를
수행한다. Resolver와 remote messaging은 Ready만 사용한다. 실패한 creation은 §2와 같은 exact delete·read
reconcile을 수행하고 다음 caller만 새 generation을 발급한다.

일반 User Spot Close는 current Actor membership이 하나라도 있으면 public `false`로 실패하고 admission과
authority를 유지한다. Internal observer는 `InUse/Conflict` reason을 기록할 수 있지만 새 public result type을
추가하지 않는다. Caller가 명시적 leave 또는 destroy를 끝낸 뒤에만 close할 수 있다. Framework는 Actor를
숨겨서 이동하거나 파괴하지 않는다. Host Shutdown과 Retire는 Actor barrier를 Spot cleanup보다 먼저 처리한다.

## 4. Join commit

Join은 다음 순서를 지킨다.

1. Source가 target Spot과 expected ObjectGeneration, AuthorityOwnerGeneration과 owner lease를 확인한다.
2. Target `OnActorJoin`이 Actor identity와 admission payload를 검증해 accept 또는 reject를 반환한다.
3. Accept 뒤 authority NewOwner CAS가 current membership과 새 AuthorityOwnerGeneration을 commit한다.
4. CAS 성공 뒤 source `OnLeaveActor`가 이전 membership을 정리한다.
5. Target membership을 공개하고 `OnJoinedActor`를 실행한다.
6. 새 owner route를 가리키는 같은 ActorRef로 operation을 완료한다.

Accept는 proposal 승인일 뿐이며 CAS 전 current membership을 바꾸지 않는다. Reject 또는 CAS conflict는 source
membership을 유지한다. Stale ObjectGeneration, AuthorityOwnerGeneration 또는 owner lease는 stale-location으로
끝내고 current owner를 추측해 적용하지 않는다.

같은 MeshNode의 다른 User Spot으로 이동할 때도 callback은 각 Spot control queue에 제출한다. Typed Actor instance와
immutable membership snapshot처럼 언어별 callback 표현이 달라도 committed identity와 순서는 같아야 한다.
서로 반대 방향 join이 동시에 시작되어도 local join 전체를 MeshNode global lock으로 직렬화하지 않는다.

## 5. Maintenance policy

다른 MeshNode로 이동하는 transfer는 host `Retire`만 시작한다. Application은 특정 Actor target이나
prepare·commit·activate phase를 선택하지 않는다.

| Policy | Retire behavior |
|---|---|
| `Disabled` | Actor가 남아 있으면 `Blocked/TransferDisabled`로 끝내고 owner와 admission을 유지한다. |
| `Recreate` | Target typed factory로 같은 Actor identity를 만들며 application state payload는 만들지 않는다. |
| `Snapshot` | Typed state adapter로 application state를 capture·restore한다. |

Policy는 Actor type startup registration에 고정한다. Snapshot은 state contract ID와 typed adapter를 같은 등록에서
받고 factory type과 adapter target type을 검증한다. Operation별 policy·adapter와 별도 untyped registry를 제공하지
않는다.

Retire로 transfer할 수 있는 Actor는 source Entry Spot의 current member로 한정한다. User Spot member Actor가 하나라도
있으면 preflight를 `Blocked/TransferDisabled`로 끝내고 state와 admission을 변경하지 않는다.

## 6. Transfer transaction

Actor transfer는 [Location runtime](40-location-runtime.ko.md)의 common maintenance phase를 따른다.

1. Preflight가 type capability, policy와 bounded target headroom을 확인한다.
2. Source admission을 reversible하게 seal하고 pre-seal turn과 timer turn을 완료한다.
3. Bound-session request와 connection-bound accepted work를 Captured 전에 terminal drain한다.
4. Preparing CAS 뒤 exact boundary와 journal을 checkpoint에 쓰고 Captured CAS로 complete root를 연결한다.
5. Target offer가 compatible initialized target Entry Spot RID·ObjectGeneration·kind를 고정하고
   offer·accept·reservation ACK 뒤 Prepared CAS를 수행한다.
6. NewOwner CAS가 owner, AuthorityOwnerGeneration과 current membership을 target Entry Spot identity로 atomic
   commit한다.
7. Target factory·restore를 끝낸 뒤 target Entry Spot `OnJoinedActor`를 실행하고 journal을 replay한다.
   Admission은 sealed 상태로 유지한다.
8. Source `OnLeaveActor`와 old Entry membership 제거를 durable source cleanup에 포함한다. Completed CAS,
   bound-session route ACK와 steady normalization 뒤 Ready와 admission을 연다.

TransferId는 stable durable identity고 TargetAttemptGeneration은 target reservation attempt fence다. Target replacement는
attempt와 reservation만 바꾸며 Actor ObjectGeneration과 immutable checkpoint를 바꾸지 않는다.

Seal 뒤 source handler에는 신규 application message를 제출하지 않는다. Framework가 failed send·request를 새
owner로 숨겨서 재제출하지 않는다. Captured checkpoint에 포함된 lease-backed record만 target에서 replay한다.
Handler completion과 replay cursor CAS 사이 crash로 같은 operation이 다시 실행될 수 있으므로 callback은
retry-safe해야 한다. Request terminal result는 한 번만 확정한다.

## 7. Failure와 recovery

Commit 전 failure는 durable Aborted CAS, bound-session abort route ACK, checkpoint·reservation cleanup과 steady
source normalization 뒤 source admission을 다시 연다. Captured 전 source crash에는 durable replay를 보장하지
않으며 original request는 normal connection failure, timeout 또는 cancellation terminal을 따른다.

Commit 뒤에는 source로 rollback하지 않는다. Recovery coordinator가 durable authority와 checkpoint에서 target
activation을 이어가며 current target이 실패하면 새 TargetAttemptGeneration과 reservation을 CAS한다. Factory와
restore는 attempt 사이에서 at-least-once로 실행될 수 있고 stale attempt와 겹칠 수 있다. Current exact owner와
attempt만 completion과 admission을 commit할 수 있다.

Process pause 뒤 재개한 이전 owner는 stale AuthorityOwnerGeneration, owner lease와 local monotonic admission
deadline 때문에 message, timer, phase update와 cleanup을 수행하지 못한다.

## 8. Stale route와 forwarding

Commit 뒤 이전 ActorRef route로 도착한 message는 bounded forwarding window 안에서 target으로 한 번 전달할 수 있다.
Forwarding entry는 Actor ObjectGeneration과 source·target AuthorityOwnerGeneration을 exact 검증한다. Actor가 다시
이동하면 이전 transfer chain을 따라가지 않고 current authority를 resolve한다.

Window가 끝나거나 fence가 다르면 `ActorLocationStale`로 실패한다. Framework는 send·request를 새 location으로
자동 재제출하지 않으며 실행 여부가 불명확한 timeout도 다시 제출하지 않는다.

## 9. Bound session

Actor가 이동해도 physical STREAM connection, session identity와 Actor ObjectGeneration은 유지된다. Session owner는
binding token, AuthorityOwnerGeneration과 sequence barrier로 새 Actor owner route를 선택한다. Target은 Completed,
route commit ACK와 steady normalization 전 packet·push admission을 열지 않는다. 이전 authority owner generation,
binding token과 sequence의 packet, reply, push와 close는 current binding에 적용하지 않는다.

## 10. 검증 요구

- Store-less Actor는 same-process operation만 허용하며 hidden Store나 distributed transfer를 만들지 않는다.
- Actor와 dynamic User Spot은 Creating row를 Ready로 오인하지 않는다.
- Creation failure가 exact fenced delete로 수렴하고 다음 caller만 새 generation을 발급한다.
- Active membership이 있는 User Spot Close가 `false`로 실패하고 hidden Actor cleanup을 하지 않는다.
- Join reject와 stale object·owner fence가 membership을 변경하지 않는다.
- Actor ObjectGeneration은 create부터 destroy까지 유지되고 이동에서는 AuthorityOwnerGeneration만 바뀐다.
- Disabled Actor가 남은 Retire는 owner와 admission을 바꾸지 않는다.
- User Spot member Actor가 남은 Retire는 `Blocked/TransferDisabled`로 끝나고 Entry Spot member만 target Entry
  membership으로 atomic commit한다.
- Captured 뒤 journal replay가 operation identity를 보존하고 terminal result를 한 번만 완료한다.
- Activated, Cleaning과 Completed에서 target admission이 닫혀 있고 route ACK·steady normalization 뒤 열린다.
- Bound STREAM connection은 이동하지 않으며 AuthorityOwnerGeneration·sequence barrier로 route만 바꾼다.
