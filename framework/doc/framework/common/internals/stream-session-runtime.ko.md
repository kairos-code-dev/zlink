# STREAM session runtime

[내부 구조 목차](README.ko.md) · [Stateful maintenance](stateful-maintenance-runtime.ko.md) ·
[Mailbox and dispatch](mailbox-dispatch-runtime.ko.md)

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

## 1. Connection aggregate

Session runtime은 physical STREAM connection과 Actor route를 분리한다. Connection aggregate는 connection ID,
binding generation, inbound sequence, pending request와 current exact ActorRef를 소유한다. Actor authority는 global
ActorId, ObjectGeneration, current owner와 current Spot identity를 소유한다.

Binding generation은 같은 physical connection에서 bind state가 바뀌는 순서를 구분한다. Reconnect는 새 connection
aggregate와 generation을 사용한다. Actor dispatch는 Object `Client` 또는 `Server` role과 Location Store가 있을
때만 활성화하며 Store가 없는 hidden local binding 경로를 만들지 않는다.

## 2. Bind와 dispatch

Bind request는 current connection, caller가 넘긴 exact ActorRef와 Actor Ready authority를 함께 확인한다. Stale ref와
moving authority는 typed failure로 끝내며 global ActorId로 fresh incarnation을 다시 찾지 않는다. Authority CAS가
필요하면 commit 뒤에만 connection route를 공개한다.

Application record는 connection ID, binding generation, ActorRef와 current authority fence를 가진다. Callback
직전에 모두 다시 확인해 stale route frame이 successor owner callback에 전달되지 않게 한다. Application callback과
infrastructure completion은 서로 다른 queue에서 처리한다. Disconnect, timeout과 reply가 경쟁해도 operation ID별
terminal result 하나만 완료한다.

## 3. Relocation barrier

Relocation은 User Spot과 member Actor를 generic aggregate로 묶는다. Target reservation은 exact participant inventory와
target owner fence를 고정한다. Aggregate commit은 Actor owner, AuthorityOwnerGeneration과 target Spot membership을
원자적으로 바꾸며 session route는 committed authority만 사용한다.

Connection-bound source에서 수락한 request는 capture 전에 terminal drain하고 durable journal로 이동하지 않는다.
Deadline 안에 끝나지 않으면 pre-capture abort로 source binding과 admission을 복원한다. Target factory·restore와
journal staging은 owner commit 전에 끝내고, commit 뒤 membership callback과 replay, source ingress hold relay와
durable cleanup을 완료한 다음 route command를 보낸다.
Route ACK와 steady normalization 전에는 target application admission을 열지 않는다.

## 4. Abort와 reconnect

Commit 전 abort는 durable Aborted authority를 먼저 기록한다. Session abort route와 ACK, target reservation cleanup과
source normalization 뒤 binding을 다시 연다. Commit 뒤에는 source로 rollback하지 않는다.

Physical connection이 끊기면 해당 aggregate와 pending waiter를 terminal 처리한다. Reconnect는 current Actor
authority와 exact ref를 caller 계약에 따라 다시 bind하고 새 binding generation을 발급한다. 이전 connection의
reply, ACK와 timer는 새 connection state를 변경하지 않는다.

## 5. 검증 기준

- Connection ID, binding generation, exact ActorRef와 authority fence가 모두 일치할 때만 callback을 시작한다.
- Stale·moving ActorRef bind가 fresh incarnation으로 hidden retry되지 않는다.
- Connection-bound request는 capture 전 terminal drain하며 frozen journal에 들어가지 않는다.
- Aggregate owner와 membership commit 뒤에만 session route를 갱신한다.
- Route ACK와 normalization 전 target admission이 닫혀 있다.
- Reconnect가 이전 connection operation과 generation을 재사용하지 않는다.
