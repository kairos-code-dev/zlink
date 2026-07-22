# Spot과 Actor membership

[스펙 목차](../README.ko.md) · [Actor 모델](22-actor-model.ko.md) ·
[Spot 주소 메시징](24-spot-address-messaging.ko.md) ·
[Session Actor Dispatch](31-session-actor-dispatch.ko.md) ·
[Location runtime](40-location-runtime.ko.md)

이 문서는 ZLink Framework 11.0.0에서 Actor 생성, Spot membership, relocation, aggregate transfer와 recovery를
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

Actor와 User·Instance Spot creation은 generic placement reservation을 사용한다.

1. Manager가 global key, stable type, optional Mesh·placement와 durable creation request를 고정한다.
2. Runtime이 role, type capability, active·pending capacity를 먼저 검사하고 positive node-wide weight 후보를
   선택한다.
3. Store `Reserve`가 `Missing → Creating` authority와 target pending capacity를 하나의 transaction으로
   고정한다.
4. Target activation registry가 exact reservation fence에서 factory와 initialize를 실행한다.
5. Store `Commit`이 같은 fence를 `Ready`로 바꾸고 pending capacity를 active로 전환한다.
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
수행한다. Application은 별도 transfer phase, target node, state adapter와 owner token을 지정하지 않는다.

Join request는 선택 항목이며 생략하면 empty request를 target proposal callback에 전달한다. Request는 승인
판단에만 사용하며 state transfer payload로 재사용하지 않는다. Same-node join은 relocation이 아니므로
`Disabled` policy도 허용한다.

Cross-node join은 다음 순서를 지킨다.

1. Target Spot proposal callback이 Actor identity와 join request를 검증해 accept 또는 reject를 반환한다.
2. Actor factory에 고정한 shared transfer policy와 target capability·capacity를 preflight한다.
3. Source Actor admission과 source membership을 reversible하게 seal한다.
4. `Snapshot`이면 durable state와 accepted work를 capture하고, `Recreate`이면 state payload 없이 boundary를
   고정한다. `Disabled`이면 capture 전에 거부한다.
5. Target reservation과 initialized staging object를 준비하되 application admission은 열지 않는다.
6. Owner와 membership을 하나의 authority commit으로 전환한다.
7. Target restore와 joined callback, source leave notification을 실행하되 target application admission은 계속
   닫아 둔다.
8. Source durable cleanup과 Completed authority CAS, session route switch ACK와 steady target normalization을
   끝낸 뒤 target admission을 연다.

Commit 전 reject, timeout, callback failure와 CAS conflict는 target staging을 폐기하고 source owner, state와
membership을 유지한다. Commit 뒤에는 source로 rollback하지 않고 exact authority와 durable capture에서 target
recovery를 계속한다. ObjectGeneration은 유지하고 AuthorityOwnerGeneration만 증가한다.

## 5. Shared transfer policy

Actor·User Spot·Instance Spot의 Object Server factory는 다음 policy 중 하나를 반드시 등록한다.

| Policy | 의미 |
|---|---|
| `Disabled` | Cross-node relocation을 capture 전에 거부하고 source owner와 admission을 유지한다. |
| `Recreate` | Target factory를 실행하지만 application state payload는 전달하지 않는다. |
| `Snapshot` | Stable state contract의 typed adapter로 application state를 capture·restore한다. |

Join과 host maintenance는 같은 policy와 state contract를 사용한다. Operation별 policy, 생략 overload, 별도
untyped adapter registry를 제공하지 않는다. Policy와 adapter registration은 startup 뒤 바뀌지 않는다.

## 6. User Spot maintenance aggregate

Host `Retire`가 User Spot을 이전할 때 Spot과 seal 시점의 current member Actor 전체를 하나의 aggregate로
처리한다. Application은 participant와 phase를 선택하지 않는다.

Aggregate ID는 non-zero 128-bit value다. Aggregate record는 최대 1024 participants와 encoded 최대 1 MiB이며
각 participant의 object kind, global key, ObjectGeneration, owner fence, policy와 state contract를 보존한다.

1. Source User Spot의 join·leave와 모든 participant admission을 reversible하게 seal한다.
2. Exact participant inventory를 aggregate record에 고정한다.
3. 모든 policy, target type capability, active·pending capacity와 state contract를 preflight한다.
4. 모든 state와 accepted work를 capture하고 target reservation·restore를 admission이 닫힌 상태로 준비한다.
5. Generic Store transaction이 Spot owner, 모든 Actor owner와 membership visibility를 하나의 commit generation으로
   전환한다.
6. Target restore, callback, route ACK와 steady normalization을 완료한 뒤 전체 admission을 연다.

Commit 전 individual owner update는 resolver에 보이지 않는다. Participant 하나라도 commit 전에 실패하면
target staging을 폐기하고 aggregate 전체 source 상태를 유지한다. Commit 뒤에는 일부 participant만 source로
되돌리지 않고 같은 aggregate identity와 transfer root로 전체 target recovery를 계속한다.

## 7. Failure와 recovery

Commit 전 failure는 durable abort, route abort ACK, transfer root·reservation cleanup과 source normalization 뒤
source admission을 연다. Commit 뒤에는 source route로 rollback하지 않는다. Recovery coordinator가 durable
authority와 Transfer Store root에서 target activation을 이어가며 failed target replacement는 새 attempt와 reservation만
발급한다.

Factory, restore와 lifecycle callback은 attempt 사이에서 at-least-once로 실행될 수 있다. Stale attempt는
completion, admission과 cleanup을 commit할 수 없다. Process pause 뒤 재개한 이전 owner도 stale
AuthorityOwnerGeneration, owner lease와 local admission deadline 때문에 message, timer, phase update와 cleanup을
수행하지 못한다.

## 8. Route forwarding

Commit 뒤 source는 `TransferForwardingWindow` 안에서 committed source→target mapping만 사용해 stale route를
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
