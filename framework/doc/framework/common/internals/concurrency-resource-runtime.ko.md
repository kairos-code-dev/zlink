# Concurrency and resource ownership

[내부 구조 목차](README.ko.md) · [Mailbox and dispatch](mailbox-dispatch-runtime.ko.md) ·
[Service monitoring](service-monitoring-runtime.ko.md)

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

## 1. Execution domain

Runtime은 raw receive·monitor callback, deadline·object timer scheduler, application executor와 infrastructure
executor를 분리한다. Raw callback은 record를 복사하거나 retain해 mailbox에 전달한 뒤 즉시 반환한다. Application
handler를 raw I/O thread에서 실행하지 않는다. Infrastructure executor는 application callback이 await 중이어도
reply, lease, recovery와 shutdown을 진행한다.

## 2. Resource tree

| owner | 직접 소유하는 resource |
|---|---|
| host aggregate | raw context, topology registry, provider, executor, scheduler, termination operation |
| topology runtime | raw socket, peer registry, selection snapshot, topology monitor state |
| object aggregate | mailbox, handler scope, timer set, child Actor·session reference |
| operation entry | correlation route, deadline task, completion과 retained request metadata |
| transfer aggregate | reservation, authority snapshot, transfer reference, participant와 journal |
| session aggregate | raw connection route, decoder, outbound FIFO, binding과 reply capability |
| observer | bounded queue, sequence cursor와 cancellation registration |

Resource를 만든 owner가 close한다. Child는 parent resource를 사용하는 동안 strong reference 또는 language-native
lifetime token을 가진다. Public reference는 raw pointer가 아니며 disposed와 generation을 검증한다.

## 3. Synchronization scope

각 aggregate가 자신의 state와 queue를 보호하고 cross-aggregate transition은 immutable snapshot과 authority CAS로
연결한다. Lock이 필요한 구현은 host lifecycle, topology·peer registry, object·session state, operation·transfer
entry, mailbox queue, monitoring reducer 순서를 지킨다.

Raw send serialization lock은 위 state lock을 해제한 뒤 획득한다. Location·Transfer Store provider, application·observer
callback, scheduler cancel과 raw blocking call을 lock 안에서 호출하지 않는다. Serial executor를 사용하는 runtime도
같은 순서를 message ownership 규칙으로 보존한다.

## 4. Lifetime와 close

Registry lookup은 aggregate strong reference와 expected generation을 함께 반환한다. Close는 새 lookup과 admission을
막고 active reference를 bounded하게 기다린 뒤 dispose한다. Late callback은 captured generation과 terminal flag를
확인하고 state를 변경하지 않는다.

Deadline 안에 반환하지 않은 callback reference는 host와 분리한 tombstone aggregate로 옮긴다. Tombstone은
immutable callback input, generation fence와 지연 release reference만 가지며 provider, socket, timer, executor
admission과 authority를 소유하지 않는다. Host는 raw resource를 유한하게 정리해 `ForceStopped`를 완료할 수 있다.
Dispose는 idempotent하고 concurrent close는 첫 operation에 합류한다.

## 5. Operation과 timer ownership

Operation entry가 deadline task를 소유한다. Reply, timeout, cancellation과 shutdown 중 terminal owner가 정해지면
table과 scheduler에서 detach한다. Scheduler cancel과 caller continuation은 table lock 밖에서 실행한다. 128-bit
operation ID는 lifecycle에서 재사용하지 않는다. Wire 64-bit correlation은 connection lifetime과 함께 operation
ID를 찾는 local key로만 사용한다.

Object timer는 owner aggregate가 소유한다. Scheduler는 callback을 직접 실행하지 않고 generation이 포함된 mailbox
record를 만든다. Close 또는 transfer가 timer를 detach한 뒤 발생한 tick은 fence 검사에서 폐기한다.

## 6. Bounded resource

Application·infrastructure mailbox, pending admission, request table, creation follower queue, transfer journal과
participant window, STREAM decode buffer·outbound FIFO, observer queue, protocol frame·metadata와 transfer envelope는
count와 byte limit을 가진다. Allocation 전에 capacity를 예약하고 실패를 public backpressure, target rejection
또는 infrastructure error로 변환한다. Unbounded retry, polling loop와 observer별 전용 thread를 만들지 않는다.

## 7. Shutdown order

1. 신규 application admission과 public operation 시작을 막는다.
2. Accepted owner claim과 request completion을 deadline까지 처리한다.
3. Transfer, Location lease와 STREAM barrier를 terminal state로 만든다.
4. Object timer, session binding과 일반 observer producer를 막고 reserved terminal lane은 유지한다.
5. Peer connector, listener와 raw socket callback을 중지한다.
6. Scheduler와 executor queue를 drain하거나 bounded cancel한다.
7. Provider와 raw context를 닫는다.
8. Final snapshot과 terminal event를 게시하고 observer queue와 waiter를 완료한다.

Terminal result 뒤에는 callback, timer, completion과 event를 새로 시작하지 않는다.

## 8. 검증 기준

- 같은 Spot 또는 Actor의 application turn은 동시에 하나만 실행한다.
- Application handler 대기가 infrastructure progress를 막지 않는다.
- Request와 host termination은 terminal completion 하나만 가진다.
- Store·raw I/O·callback을 state lock 안에서 기다리지 않는다.
- Close 반환 뒤 해당 reference에서 새 callback을 시작하지 않는다.
- Host resource count가 baseline으로 돌아오며 tombstone은 terminal host를 재사용하지 않는다.
