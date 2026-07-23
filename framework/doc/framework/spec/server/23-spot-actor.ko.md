# Spot과 Actor membership

[스펙 목차](../README.ko.md) · [Actor 모델](22-actor-model.ko.md) ·
[Spot 주소 메시징](24-spot-address-messaging.ko.md) ·
[Session Actor Dispatch](31-session-actor-dispatch.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

이 문서는 ZLink Framework 11.0.0에서 Actor 생성, Spot membership, relocation, aggregate relocation과 recovery를
정의한다. Core는 raw socket과 transport만 제공하며 이 상태와 lifecycle은 각 언어 Framework runtime이
소유한다.

## 1. Identity와 authority

ActorId와 User·Instance Spot RID는 Location Store namespace 전체에서 전역인 logical key다. MeshName은 최초
placement attribute이며 authority key의 일부가 아니다. `ActorRef`와 `SpotRef`의 ObjectGeneration은 non-zero
unsigned 63-bit conceptual value다. 같은 incarnation에서 membership 또는 owner MeshNode가 바뀌어도
ObjectGeneration은 유지하고 provider가 더 큰 AuthorityOwnerGeneration을 발급한다.

Store-backed authority는 current owner, Spot membership, ObjectGeneration, AuthorityOwnerGeneration,
StoreVersion과 exact owner lease를 저장한다. Runtime route cache는 row snapshot이며 단독으로 authority를
결정하지 않는다. Join, leave, relocation, destroy와 close는 expected StoreVersion, generation과 owner lease를
검증하는 transaction만 사용한다.

Object Client 또는 Server role은 Location Store가 필수다. Store가 없으면 startup에서 거부하며 hidden local
Store, runtime-local object manager와 같은 이름의 축소된 의미를 만들지 않는다. Object role이 `None`인 manual
topology는 Node direct와 Channel operation만 사용할 수 있다.

## 2. Creation authority와 Ready barrier

Actor와 User Spot manager create, Instance Spot direct cold activation은 generic placement reservation을 사용한다.
Manager create는 coordinator가 target transport 전에 reservation을 획득한다. Instance Spot은 source가 최초
message를 포함한 activation envelope를 target에 제출하고, target runtime이 local exact instance가 없을 때
자신을 owner로 reservation을 획득한다.

1. Runtime이 global key, stable type, optional Mesh·placement와 durable creation input을 고정하고 role, type
   capability, active·pending capacity를 만족하는 positive node-wide weight 후보를 선택한다.
2. Actor·User Spot manager create는 coordinator가 `Reserve`를 호출한다. Instance Spot은 source가 first-message
   activation envelope를 후보 target에 먼저 제출하고 target activation registry가 `Reserve`를 호출한다.
3. Store `Reserve`가 `Missing → Creating` authority와 target pending capacity를 하나의 transaction으로
   고정한다.
4. CAS winner target만 exact reservation fence에서 factory와 initialize를 실행한다.
5. Store `Commit`이 같은 fence를 `Ready`로 바꾸고 pending capacity를 active로 전환한다. Instance Spot은
   envelope에 포함된 first message를 activation barrier 뒤 local queue에 한 번 제출한다.
6. `Abort`는 exact Creating authority와 pending capacity만 해제한다.

Reservation은 object kind, global key, stable type, target descriptor, capacity delta와 exact owner fence를
포함한다. TTL을 사용하지 않으며 Creating authority와 target owner lease로 recovery, takeover와 abort를
판단한다. Actor·Spot 전용 Store operation을 만들지 않는다.

Encoded creation request는 최대 1 MiB이며 reservation 전에 immutable content reference와 hash를 creation
intent에 기록한다. Ready 또는 fenced failure cleanup까지 유지하며 CAS winner만 factory에 전달한다. Factory와
initialize는 `(logical key, ObjectGeneration, attempt)` 기준 at-least-once로 실행될 수 있으므로 retry-safe해야
한다.

같은 type의 Creating CAS loser는 같은 attempt의 terminal result를 관찰하며 다른 target에서 factory를
시작하지 않는다. Terminal call의 하나의 deadline은 resolve, reservation, factory와 Ready barrier 전체를
포함한다. Deadline이 끝나면 `DeadlineExceeded`이며 다음 call이 exact authority를 reconcile한다. Missing,
Creating과 Store failure는 negative cache에 저장하지 않는다.

Entry Spot은 startup initialization을 마치기 전 descriptor와 resolver에 publish하지 않는다. Actor creation은
initial Entry Spot membership과 Ready barrier를 같은 lifecycle에서 완료한다.

## 3. Entry Spot과 User Spot

Actor를 만들면 selected owner MeshNode의 Entry Spot이 initial membership을 처리한다. Actor 업무 message는
Actor queue로 직접 전달하며 Entry Spot이나 User Spot callback을 경유하지 않는다.

User Spot은 join proposal, joined, leave와 disconnected lifecycle control을 해당 Spot control queue에서
직렬화한다. 같은 Spot의 packet·timer turn과 callback 순서는 Spot turn이 정한다. Instance Spot은 Actor
membership target이 아니다.

일반 User Spot Close는 current Actor membership이 하나라도 있으면 public `false`로 끝나고 admission과
authority를 유지한다. Caller가 명시적 leave 또는 destroy를 끝낸 뒤에만 close할 수 있다. Framework는 Actor를
숨겨서 이동하거나 파괴하지 않는다.

## 4. Join 의미와 commit 순서

`JoinSpot`은 global Spot RID를 받는다. `JoinEntrySpot`은 target node RID를 받지 않는다. Framework가 target
Spot 또는 eligible Entry Spot을 resolve하고 owner node가 다르면 Actor relocation을 join operation 안에서
수행한다. Application은 별도 relocation phase, target node, state adapter와 owner token을 지정하지 않는다.

Join request는 선택 항목이며 생략하면 empty request를 target proposal callback에 전달한다. Request는 승인
판단에만 사용하며 state relocation payload로 재사용하지 않는다. Same-node join은 relocation이 아니므로
`Disabled` policy도 허용한다.

Cross-node join은 다음 순서를 지킨다.

1. Target Spot proposal callback이 Actor identity와 join request를 검증해 accept 또는 reject를 반환한다.
2. Actor factory에 고정한 shared relocation policy와 target capability·capacity를 preflight한다.
3. Source queue의 turn 경계에서 active unit, callback과 예상 payload byte permit을 모두 nonblocking으로 얻은 뒤
   Actor admission과 source membership을 reversible하게 seal한다.
4. `Snapshot`이면 Actor relocation adapter의 `Capture`로 application state를 얻는다. Seal 시점에 실행하지 않은
   message queue, accepted journal, timer logical registration·pending tick과 Framework metadata를 함께
   Relocation Store에 고정한다. `Recreate`이면 adapter를 호출하지 않고 application state 없이 boundary를
   고정한다. `Disabled`이면 `Capture` 전에 거부한다.
5. Target reservation과 staging Actor를 준비한다. `Snapshot`이면 target factory가 만든 Actor에 같은 adapter의
   `Restore`를 호출한다. Accepted journal은 handler를 실행하지 않고 검증해 staging queue에 준비한다.
6. Owner와 membership을 하나의 authority commit으로 전환한다.
7. Target joined callback과 source leave notification을 실행하고 old Entry membership의 durable cleanup을
   완료한 뒤 accepted message·journal을 replay한다. Framework는 logical timer를 자동 복원하되 target
   application admission은 계속 닫아 둔다.
8. Replay 뒤 남은 source resource cleanup과 Completed authority CAS, session route switch ACK와 steady target normalization을
   끝낸 뒤 target admission을 연다.

Commit 전 reject, timeout, `Capture`·`Restore` failure와 CAS conflict는 target staging을 폐기하고 source owner, state와
membership을 유지한다. Commit 뒤에는 source로 rollback하지 않고 exact authority와 durable capture에서 target
recovery를 계속한다. ObjectGeneration은 유지하고 AuthorityOwnerGeneration만 증가한다.

Permit을 얻기 전에는 source queue를 seal하지 않는다. Permit을 얻지 못한 queue는 application message와 timer를
계속 처리한다. Seal 뒤 도착한 ingress는 relocation payload에 합치지 않고 bounded hold에 둔다. Commit 전 abort는
hold를 source queue에 arrival order로 되돌리고, commit 뒤에는 original operation identity와 generation을 보존해
target으로 relay한다.

Application이 요청한 cross-node join은 membership join이므로 target의 `OnActorJoin`, commit 뒤
`OnJoinedActor`, source의 `OnLeaveActor` lifecycle을 사용한다. 물리적으로 Actor를 복원했다는
이유로 maintenance 전용 `OnActorRelocated` callback을 추가로 호출하지 않는다.

Entry Spot 자체는 relocation participant가 아니다. Host `Retire`로 source Entry Spot의 Actor가 target node의
Entry Spot으로 이동하면 Framework는 target Actor의 `Restore`를 끝내고 owner·membership을 commit한 뒤
target Entry Spot의 `OnActorRelocated` callback과 source Entry Spot의 `OnLeaveActor` callback을 호출한다. 두 callback이
완료될 때까지 target Actor dispatch를 열지 않는다. 어느 callback이 실패해도 commit을 되돌리지 않고 current
relocation fence에서 재시도한다. Source process가 종료되면 durable source cleanup이 source callback 완료를 대신해
target recovery가 계속된다. 정확한 callback 이름과 비동기 표현은 언어별 exact interface가 정한다.

Spot의 terminal lifecycle callback은 `OnClosing(ClosingContext)`이다. Actor는 항상 Entry
또는 User Spot에 속하므로 Actor별 closing callback을 제공하지 않는다. `ClosingContext`는 다음 닫힌 reason과
operation의 absolute deadline을 제공한다.

| 값 | Reason | 호출 조건 |
|---:|---|---|
| 0 | `ExplicitClose` | application이 User·Instance Spot의 close를 시작해 해당 local instance를 정상 정리함 |
| 1 | `HostShutdown` | relocation 없이 host `Shutdown`이 local Entry·User·Instance Spot을 정리함 |
| 2 | `RelocationOut` | User·Instance Spot owner commit 뒤 source local instance를 정리함 |

Standalone Actor 이동은 Entry Spot 자체를 닫지 않으므로 Entry Spot의 `OnClosing`을 호출하지 않는다. 기존 target
`OnActorRelocated`와 source `OnLeaveActor`만 사용한다. User Spot에 Actor membership이 남아 explicit close가 거부되면
`OnClosing`을 호출하지 않는다. Host `Shutdown`에서는 accepted handler와 timer turn을 terminal 상태로 만든 뒤,
Actor membership과 local instance가 아직 유효한 상태에서 Spot `OnClosing`을 호출한다. Callback 완료 뒤 Actor·Spot
scope를 dispose하고 Location authority와 resource를 정리한다.

언어 runtime에 표준 cooperative cancellation 표현이 있으면 callback에 남은 cleanup budget을 함께 전달할 수
있다. Spot closing만을 위한 별도 Framework cancellation 타입을 만들지는 않는다. 표준 표현이 없는 언어에서는 `ClosingContext`의
deadline만 전달하고 Framework가 deadline에 callback completion 대기를 끝낸다. Application은 callback 이후
context와 cancellation signal을 보관하지 않는다. `HostShutdown`은 callback failure로 relocation나 rollback을
시작하지 않는다. Callback exception은 `ForceStopped/TeardownFailed`, deadline 만료는
`ForceStopped/DeadlineExceeded`로 끝난다. Process crash와 `SIGKILL`에서는 callback 실행을 보장하지 않는다.
정확한 enum, context와 표준 cancellation 표현은 언어별 exact interface가 정한다.

## 5. Shared relocation policy

Actor·User Spot·Instance Spot의 Object Server factory는 다음 policy 중 하나를 반드시 등록한다.

| Policy | 의미 |
|---|---|
| `Disabled` | Cross-node relocation을 capture 전에 거부하고 source owner와 admission을 유지한다. |
| `Recreate` | Target factory를 실행하지만 application state payload는 전달하지 않는다. |
| `Snapshot` | Object kind에 맞는 relocation adapter로 application state를 opaque byte sequence로 capture·restore한다. |

Actor는 `ActorRelocationAdapter`, User·Instance Spot은 `SpotRelocationAdapter`를 사용한다. 두 adapter의
operation 이름은 `Capture`와 `Restore`다. `Capture`는 source instance를 받아 byte sequence를 반환하고,
`Restore`는 target factory가 만든 instance와 byte sequence를 받아 상태를 적용한다. Instance를
반환하지 않는다.

Application은 byte format, version, compatibility와 migration을 관리한다. Framework는 state contract ID,
generic state type, serialization profile과 message codec을 relocation adapter 계약에 추가하지 않는다. Relocation
Store에는 application bytes를 그대로 opaque payload로 저장하고 Framework root manifest·chunk·checksum만
Framework가 검증한다.

`Capture`가 한 participant에 대해 반환하는 byte sequence는 최대 64 MiB다. 빈 byte sequence는 유효한
application state이고 null result는 adapter contract 위반이다. Callback이 성공하면 Framework가 결과를 즉시
복사하거나 소유권을 넘겨받으므로 application은 그 뒤 결과를 바꾸지 않는다. `Restore`에 전달한 bytes는 callback이
완료될 때까지만 유효하고 callback이 보관하려면 직접 복사해야 한다.

Join과 host maintenance는 같은 factory policy와 adapter registration을 사용한다. `Snapshot` Actor가 다른
node의 User Spot·Entry Spot으로 join하거나 maintenance로 이동할 때 Actor adapter를 호출한다.
User Spot aggregate relocation에서는 `Snapshot`으로 등록한 Spot과 각 member Actor의 adapter를 각각
호출한다. Same-node join, `Disabled` 거부와 `Recreate` relocation에서는 adapter를 호출하지
않는다. Operation별 policy, 생략 overload와 별도 adapter registry를 제공하지 않는다.
Policy와 adapter registration은 startup 뒤 바뀌지 않는다.

## 6. User Spot maintenance aggregate

Host `Retire`가 User Spot을 이전할 때 Spot과 seal 시점의 current member Actor 전체를 하나의 aggregate로
처리한다. Application은 participant와 phase를 선택하지 않는다.

Host가 `Retiring`으로 전환되면 Framework는 aggregate의 Spot control queue에 infrastructure intent notification을
예약한다. 이 notification은 application callback이 아니다. Notification을 처리한 turn 경계에서 permit을 얻지 못하면
seal하지 않고 다음 notification을 예약하므로 Spot과 member Actor는 application message와 timer를 계속 처리한다.

Aggregate ID는 non-zero 128-bit value다. Aggregate record는 최대 1024 participants와 encoded 최대 1 MiB이며
각 participant의 object kind, global key, ObjectGeneration, owner fence와 policy를 보존한다.

1. Spot queue turn 경계에서 aggregate의 active unit, callback과 예상 payload byte permit을 모두 얻은 뒤 source
   User Spot의 join·leave와 모든 participant admission을 reversible하게 seal한다.
2. Exact participant inventory를 aggregate record에 고정한다.
3. 모든 policy, target type·Snapshot adapter capability와 active·pending capacity를 preflight한다.
4. `Snapshot` participant의 모든 state, 실행하지 않은 message queue, accepted journal과 timer logical
   registration·pending tick을 capture하고 target reservation·factory·restore를 admission이 닫힌 상태로 준비한다.
5. Generic Store transaction이 Spot owner, 모든 Actor owner와 membership visibility를 하나의 commit generation으로
   전환한다.
6. Authority commit 뒤 target lifecycle callback, accepted message·journal replay, Framework timer 자동 복원,
   source ingress hold relay, route ACK와 steady normalization을 완료한 뒤 전체 admission을 연다.

4번의 restore는 5번 aggregate commit 전에 끝나야 한다. User Spot aggregate는 logical membership을 그대로
이동하므로 target에서 `OnJoinedActor`·`OnActorRelocated`를 호출하거나 source에서 `OnLeaveActor`를 호출하지
않는다. Spot·Actor adapter의 restore와 Spot lifecycle callback만 target admission 전에 끝낸다.

Commit 전 individual owner update는 resolver에 보이지 않는다. Participant 하나라도 commit 전에 실패하면
target staging을 폐기하고 aggregate 전체 source 상태를 유지한다. Commit 뒤에는 일부 participant만 source로
되돌리지 않고 같은 aggregate identity와 relocation root로 전체 target recovery를 계속한다.

## 7. Failure와 recovery

Commit 전 failure는 durable abort, route abort ACK, relocation root·reservation cleanup과 source normalization 뒤
source admission을 연다. Commit 뒤에는 source route로 rollback하지 않는다. Recovery coordinator가 durable
authority와 Relocation Store root에서 target activation을 이어가며 failed target replacement는 새 attempt와 reservation만
발급한다.

Factory, `Capture`, `Restore`와 lifecycle callback은 attempt 사이에서 at-least-once로 실행될 수 있다. Stale attempt는
completion, admission과 cleanup을 commit할 수 없다. Process pause 뒤 재개한 이전 owner도 stale
AuthorityOwnerGeneration, owner lease와 local admission deadline 때문에 message, timer, phase update와 cleanup을
수행하지 못한다.

`Capture`가 예외나 rejected task로 끝나면 Framework는 relocation root를 publish하지 않는다. Durable `Aborted` CAS,
route abort ACK, cleanup과 steady source normalization을 완료한 뒤에만 source seal을 해제한다. `Restore`가 실패하면
해당 staging instance를 폐기한다. 같은 immutable payload를 다시 적용할 때도 factory가 새 instance를 만들며,
부분적으로 변경된 instance를 재사용하지 않는다. Deadline과 policy가 허용하면 다른 target attempt를 시작한다.
Final owner·membership commit 전에 모든 target이 실패하면 source를 유지하고 `StateIncompatible`로 끝낸다.
Framework가 operation deadline 때문에 callback을 취소하면 `DeadlineExceeded`를 사용하며 cancellation 자체를
state format 오류로 바꾸지 않는다.

Final commit 뒤 lifecycle callback failure는 source rollback 조건이 아니다. Target admission을 닫은 상태로
current authority fence에서 callback을 재시도한다. Adapter와 lifecycle callback은 같은 object generation에
대해 두 번 이상 호출되어도 수렴해야 하며 exactly-once external side effect를 가정하면 안 된다.

## 8. Route forwarding

Commit 뒤 source는 `RelocationForwardingWindow` 안에서 committed source→target mapping만 사용해 stale route를
relay한다. Relay는 Store를 읽거나 application handler를 실행하지 않으며 original operation ID, generation,
payload와 reply route를 보존한다.

Mapping은 global key, ObjectGeneration, source·target AuthorityOwnerGeneration과 owner fence를 exact 검증한다.
Owner generation은 hop마다 증가하며 최대 8 hops다. Mapping 하나의 queue는 1024 messages와 16 MiB 이하이고
negotiated message bound도 지킨다. Window 만료, mapping 없음, generation mismatch, loop 또는 bound 초과는
stale-route error다. Framework는 failed operation을 fresh owner에게 hidden retry하지 않는다.

User Spot aggregate의 Spot과 member Actor forwarding mapping은 같은 commit generation에서 설치한다.

## 9. Bound session

Actor가 이동해도 physical STREAM connection, session identity와 ObjectGeneration은 유지된다. Session owner는
binding token, AuthorityOwnerGeneration과 sequence barrier로 새 Actor owner route를 선택한다. Target은 route
commit ACK와 steady normalization 전 packet·push admission을 열지 않는다. 이전 owner generation, binding token과
sequence의 packet, reply, push와 close는 current binding에 적용하지 않는다.

## 10. 검증 요구

- Object role이 Store 없이 startup하지 않고 hidden local manager를 만들지 않는다.
- Creation reservation이 global key authority와 pending capacity를 atomic하게 고정한다.
- Concurrent create loser가 같은 attempt에 합류하고 다른 factory를 실행하지 않는다.
- Join proposal이 capture보다 먼저 실행되고 commit 전 failure가 source 전체를 유지한다.
- Cross-node join이 shared factory policy를 사용하며 same-node join은 `Disabled`로 차단하지 않는다.
- Actor ObjectGeneration은 create부터 destroy까지 유지되고 relocation에서는 AuthorityOwnerGeneration만 바뀐다.
- User Spot과 member Actor가 bounded aggregate의 commit generation 하나로 함께 전환된다.
- Commit 뒤 failure가 participant 일부를 source로 rollback하지 않는다.
- Forwarding이 bounded committed mapping만 사용하고 operation identity를 보존한다.
- Bound STREAM connection은 이동하지 않으며 authority generation과 sequence barrier로 route만 바뀐다.
