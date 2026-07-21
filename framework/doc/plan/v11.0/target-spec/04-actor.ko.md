# Actor service

이 문서는 ZLink Framework 11.0 Actor의 identity, lifecycle, Spot membership, sequential turn, messaging과 maintenance
transfer 계약을 정의한다. 언어별 exact interface는 같은 의미를 각 언어 관례로 표현한다.

## 1. 책임 경계

Actor는 logical identity, mutable application state와 sequential application turn을 가진다. Actor payload는 Actor
queue로 직접 들어가며 Spot application callback을 경유하지 않는다. Spot은 membership lifecycle과 Spot-owned
state를 처리한다. Framework는 factory, queue, request correlation, durable authority, transfer와 bound-session
route를 조정한다. Core raw transport는 이를 해석하지 않는다.

## 2. Identity와 authority fence

Actor logical key는 `(MeshName, ActorId)`이고 Actor type은 한 object incarnation에서 immutable하다. ActorRef의
generation은 authority의 ObjectGeneration이다. 같은 logical key를 delete 뒤 다시 create할 때만 바뀌며 Spot
membership 변경과 owner node transfer에서는 유지된다.

Current membership과 owner route는 AuthorityOwnerGeneration으로 fence한다.
Authority snapshot은 Actor type과 ID, current Spot RID·ObjectGeneration·kind, owner node RID·lifecycle generation,
ObjectGeneration, AuthorityOwnerGeneration과 host `(OwnerId, LeaseGeneration)` token을 함께 보관한다.

Host-level manager, directory와 client operation은 MeshName을 명시한다. ActorRef만으로 physical mesh나 owner를
추측하지 않는다.

Location Store가 없는 Actor는 runtime-local authority와 same-process handle만 사용한다. Remote resolve,
distributed join, transfer·relocation과 distributed session binding은 `TransferDisabled` 또는 configuration error다.

## 3. Create, resolve와 destroy

Store-backed Create는 NewObject CAS로 final generation과 `Creating` row를 먼저 발급한다. Typed factory,
initialize와 initial Entry Spot membership이 모두 성공한 뒤 same object·owner fence Ready CAS를 수행한다.
Resolver와 remote messaging은 Ready Actor만 사용한다. Framework는 remote MeshNode를 create target으로 고르거나
hidden remote create를 전달하지 않는다.

Factory, initialize, initial membership 또는 Ready commit failure는 local barrier를 failed·sealed로 고정하고 same
StoreVersion·ObjectGeneration·AuthorityOwnerGeneration·owner lease로 row를 fenced delete한다. Ambiguous result는
exact Read로 reconcile한다. Missing 확인 전에 callback을 재실행하지 않고 그 다음 caller만 NewObject의
새 generation을 받는다.

Resolve는 existing owner만 찾는다. Destroy는 새 payload, membership mutation과 bind를 seal하고 accepted turn과
reply를 deadline까지 처리한 뒤 binding, authority, scope와 queue를 정리한다. Old ObjectGeneration의 reference와
late reply는 새 incarnation에 적용하지 않는다.

## 4. Spot membership

Actor는 Entry Spot에서 시작하고 User Spot으로 join·leave할 수 있다. Instance Spot은 membership target이 아니다.
Join은 target Spot RID·ObjectGeneration을 검증하고 target Spot turn에서 application proposal을 처리한다. Callback
`Accept`는 proposal 승인일 뿐이다. Current Actor StoreVersion과 AuthorityOwnerGeneration을 비교하는 NewOwner
CAS가 실제 commit point다.

Reject, timeout, callback failure와 CAS conflict는 current membership을 바꾸지 않는다. Commit 뒤 joined callback,
leave commit 뒤 leave callback, connection membership 종료 뒤 disconnect callback을 호출한다. Actor 업무 payload는
membership callback과 별개인 Actor queue에서 계속 처리한다.

## 5. Messaging과 turn

Actor send·request는 exact ActorRef와 expected AuthorityOwnerGeneration의 current route를 사용한다. Destination은
current authority, owner host lease와 node lifecycle을 mailbox admission 직전에 검증한다. Stale fence는 current
membership을 바꾸지 않고 typed stale result로 끝난다.

같은 sender에서 같은 Actor incarnation으로 accept한 record는 FIFO다. 서로 다른 sender의 global order는 보장하지
않는다. 한 Actor application turn은 한 번에 하나만 실행한다. Request completion, send-ready, transfer와 binding은
infrastructure reserve에서 진행하므로 application callback이 await 중이어도 완료와 shutdown progress가 막히지
않는다.

Request는 original correlation을 한 번만 완료한다. Timeout, cancellation, disconnect, transfer와 shutdown 뒤
다른 owner에게 hidden retry하지 않는다.

## 6. Maintenance transfer

Actor transfer는 host `Retire`만 시작한다. Application이 prepare, commit, activate와 abort를 조합하는 public API는
없다. TransferId는 checkpoint·journal·completion 전체에서 안정적이고 TargetAttemptGeneration은 target replacement
fence일 뿐이다.

1. Preflight는 policy, compatible reader와 bounded target headroom만 확인한다.
2. Source message·timer admission을 reversible하게 seal하고 accepted turn을 완료한다. Connection-bound work와
   bound-session request는 Captured 전 terminal drain한다.
3. Preparing authority CAS 뒤 application state와 journal을 immutable checkpoint에 기록하고 Captured CAS로 연결한다.
4. Exact inventory로 target offer·accept·reservation ACK를 끝낸 뒤 Prepared CAS를 수행한다. Offer는 initialized
   target Entry Spot identity를 고정한다.
5. Prepared→Committed NewOwner CAS가 owner, AuthorityOwnerGeneration과 current membership을 target Entry Spot으로
   atomic commit한다. ObjectGeneration은 유지한다.
6. Target factory·restore 뒤 target Entry Spot joined callback을 실행하고 journal을 replay한다. Application
   admission은 계속 sealed다. Source leave callback과 old Entry membership 제거는 durable source cleanup이다.
7. Durable source cleanup, Completed CAS, bound-session route ACK와 steady authority normalization 뒤 target admission을
   열고 Ready로 publish한다.

`Disabled`는 Actor가 남은 Retire를 차단한다. `Recreate`는 application snapshot 없이 same logical ID의 factory와
accepted journal을 사용한다. `Snapshot`은 typed adapter의 `framework-json-v1` state와 journal을 사용한다. Adapter는
TransferId, target RID, raw frame와 serializer option을 받지 않는다.

Retire transfer는 source Entry Spot current member만 대상으로 한다. User Spot member Actor가 하나라도 있으면
preflight를 `Blocked/TransferDisabled`로 끝내고 state와 admission을 유지한다.

Target replacement는 Prepared부터 Cleaning까지 가능하고 post-commit replacement는 Committed로 재진입한다.
Factory와 restore callback은 attempts 사이 at-least-once이며 stale callback과 겹칠 수 있다. Callback은 retry-safe해야
하고 exactly-once external side effect는 보장하지 않는다. Public callback에 TransferId를 추가하지 않는다. Current
exact owner와 TargetAttemptGeneration만 completion commit과 admission open을 수행할 수 있다.

## 7. Replay, reply와 stale route

Frozen request는 OperationId, original non-zero ReplyRouteId와 exact request-source owner lease·node fence를 보관한다.
OperationId는 dedupe key이며 reply route를 대신하지 않는다. Late completion은 checkpoint root와 compact authority
count를 atomic하게 갱신하고 authenticated `replyRelayAck` 또는 exact request-source host lease expiry 전에는 release할
수 없다. Physical connection close는 terminal proof가 아니다.

Transfer commit 뒤 old Actor route로 들어온 message는 configured forwarding window 안에서 same ObjectGeneration의
target으로 한 번 전달할 수 있다. Source·target AuthorityOwnerGeneration을 exact 검증하고 transfer chain을 따라가지
않는다. Window가 끝나거나 fence가 다르면 stale result다. 기본 window는 5초이고 0은 forwarding disabled다.

## 8. Bound session과 abort

Bound STREAM connection과 session object는 이동하지 않는다. Session owner는 physical connection을 유지하고
binding route를 new Actor owner와 AuthorityOwnerGeneration으로 교체한다. Target은 Completed 뒤 commit route와 routed
ACK를 받고 steady authority가 된 다음에만 packet·push admission을 연다. Old authority owner generation, binding
generation과 session owner token의 packet, reply, push와 close는 current session에 적용하지 않는다.

Precommit abort는 source를 sealed 상태로 유지하고 durable Aborted CAS를 먼저 수행한다. Aborted phase에서만 session
abort route와 routed ACK를 처리하고 reservation·orphan cleanup과 steady source normalization 뒤 source admission을
연다.

## 9. Lease와 종료

모든 transition, message, timer, factory completion과 CAS는 shared host owner lease의 local monotonic deadline을
검사한다. Process pause 뒤 deadline을 넘긴 source, target과 coordinator는 새 authority mutation을 수행할 수 없다.

Shutdown은 새 create, join과 transfer reservation을 막고 accepted turn과 진행 중인 barrier만 deadline까지 처리한다.
Retire는 all-or-none preflight blocker가 있으면 authority와 admission을 바꾸지 않는다.

## 10. 검증 요구

- ActorRef generation은 ObjectGeneration이고 membership·owner 이동은 AuthorityOwnerGeneration으로 fence한다.
- Create·join failure가 partial Actor, callback과 authority mutation을 남기지 않는다.
- Creating Actor를 remote Ready로 관측하지 않고 failure delete 뒤 다음 caller만 새 generation을 받는다.
- Actor payload가 Spot callback을 거치지 않고 sequential Actor queue에서 처리된다.
- Seal 전 accepted record가 유실·역전되지 않고 replay가 OperationId·ReplyRouteId·request-source fence를 보존한다.
- Prepared 전 target callback과 target admission은 0건이다.
- Activated 뒤에도 target은 sealed이며 route ACK와 steady normalization 뒤에만 Ready다.
- Replacement attempt의 stale callback이 commit과 admission을 열지 않는다.
- Source host lease가 유효한 connection loss는 reply terminal proof가 아니다.
- Aborted CAS 전 session route rollback과 source reopen은 발생하지 않는다.
- Bound STREAM connection은 이동하지 않고 route fence만 교체된다.
- User Spot member Actor가 남은 Retire는 `Blocked/TransferDisabled`로 끝난다.
