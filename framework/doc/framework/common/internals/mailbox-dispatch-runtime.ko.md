# Mailbox and dispatch runtime

[내부 구조 목차](README.ko.md) · [Service runtime architecture](service-runtime-architecture.ko.md) ·
[Service wire protocol](service-wire-protocol.ko.md)

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

## 1. Dispatch record

Protocol decoder는 검증된 multipart를 immutable dispatch record로 만든다. Record는 domain, command, logical
target, owner fence, operation ID, metadata와 retained payload를 포함한다. Raw socket handle, mutable receive
buffer와 poller event는 포함하지 않는다. Typed decode가 실패하면 handler를 호출하지 않고 request terminal
error 또는 one-way diagnostic으로 끝낸다.

Dispatch record가 queue에 있는 동안 Framework가 retained payload를 소유한다. Handler callback에는 읽기 전용
message context와 typed payload view만 전달하고, callback completion 뒤 reply correlation, route envelope와
retained payload를 함께 해제한다. Application callback에 native message storage나 dispose 책임을 전달하지 않는다.

## 2. Mailbox domain

Runtime은 application과 infrastructure work를 별도 bounded mailbox로 관리한다.

| domain | 처리 대상 | progress 규칙 |
|---|---|---|
| application | handler turn, Spot·Actor message, application timer, session callback | owner별 순차 실행과 quota 적용 |
| infrastructure | peer admission, send-ready, completion, lease, relocation, recovery, termination, STREAM fence | application handler 대기와 무관하게 진행 |

두 mailbox는 message count와 retained byte 한도를 가진다. Infrastructure mailbox는 reply completion, lease expiry와
shutdown을 위한 reserve를 따로 둔다. Operation을 만들 때 terminal completion slot과 promise state를 함께
예약한다. Reserve를 얻지 못하면 raw transport 제출 전에 backpressure로 끝낸다.

## 3. Admission gate

한 gate가 queue limit과 owner fence를 같은 critical section에서 확인하고 record를 enqueue한다.

1. Host와 topology의 domain admission을 확인한다.
2. Global target identity와 ObjectGeneration·membership·authority fence를 확인한다.
3. Relocation seal, creation barrier와 STREAM binding barrier를 확인한다.
4. Queue message·byte budget과 pending admission capacity를 확인한다.
5. Record를 enqueue하고 empty에서 non-empty로 바뀌면 ready index를 갱신한다.

검사와 enqueue 사이에 owner가 바뀌지 않는다. 실패한 record는 application queue에 나타나지 않으며 request는
원인을 stable terminal result로 받는다.

## 4. Ready index와 claim

Ready index는 edge notification이 아니라 현재 work가 있는 owner 집합을 나타내는 level state다. 같은 owner는
entry 하나만 가진다. Executor는 ready owner를 claim하고 message count, byte와 elapsed-time budget 안에서 batch를
처리한다. Work가 남으면 다시 ready로 두고 비었으면 entry를 제거한다. Claim은 재사용하지 않는 process-local
serial을 사용해 late completion의 ABA를 막는다.

한 owner의 application claim은 동시에 하나만 존재한다. Infrastructure executor는 별도이므로 application handler가
await 중이어도 request timeout과 lease fence가 진행된다. Runtime integration은 blocking drain, callback wakeup,
poller signal 중 wakeup mode 하나만 startup에서 선택한다. Wakeup 뒤에는 항상 level-ready index를 다시 확인한다.

Host `Retiring` publication 뒤 coordinator는 relocation unit의 infrastructure mailbox에 intent notification을 넣는다.
Application callback으로 변환하지 않는다. Notification은 current application turn이 끝난 queue boundary에서
outbound·inbound·callback·byte permit을 nonblocking으로 얻는다. 모두 얻으면 같은 boundary에서 application gate를
seal하고, 하나라도 실패하면 seal 없이 다음 notification을 예약한다. 따라서 permit waiter는 ready index의 application
claim을 점유하지 않는다.

Seal 전 application queue에 있지만 아직 claim하지 않은 record는 frozen relocation payload로 옮긴다. Seal 뒤 ingress는
별도 bounded hold queue에 넣는다. Precommit abort는 hold를 source application queue 뒤에 arrival order로 연결하고,
owner commit은 hold record의 operation identity와 fence를 유지해 target relay queue로 옮긴다.

## 5. One-way admission

One-way submit은 bounded pending admission operation 하나를 만든다. Local queue 또는 raw transport가 바로
수락하면 `Submitted`다. 첫 시도가 backpressured면 signal을 기다리되 pending 공간도 가득 차면
`Backpressured`, deadline까지 수락되지 않으면 `TimedOut`이다. Admission 뒤 다른 target에 재제출하지 않으며
완료는 remote handler 실행을 뜻하지 않는다.

## 6. Request와 terminal completion

Source runtime은 transport 제출 전에 operation table entry, deadline task와 correlation route를 준비한다. Full
multipart가 raw transport에 수락되면 committed로 바꾼다. 이 시점부터 target 실행 여부가 불명확할 수 있으므로
다른 owner에 숨은 재제출을 하지 않는다. Target은 request마다 owner fence가 포함된 one-shot reply capability를
만들고 첫 reply, error 또는 terminator만 소비한다.

Operation state는 `Pending`, `Committed`, terminal 중 하나다. Reply, timeout, cancellation, transport failure,
relocation relay와 shutdown이 atomic transition으로 terminal owner를 경쟁한다. 승리한 path만 caller continuation을
완료한다. Table lock 안에서 application callback을 실행하지 않고 scheduler cancel과 payload dispose도 lock 밖에서
수행한다. Late reply는 diagnostic counter만 갱신한다.

Host terminal result를 publish하기 전에 pending operation을 모두 terminal로 바꾼다. Terminal host 뒤에는 새
completion record를 enqueue하지 않는다.

## 7. Logical Multicast

Publish 시작 시 local subscription과 eligible remote peer의 immutable snapshot을 만든다. Local subscriber마다
record 하나를 admission하고 remote node마다 wire record 하나만 보낸다. Remote node가 자신의 local subscriber
snapshot에 fan-out한다. Target 결과는 독립적이며 일부가 수락된 뒤 다른 target이 실패해도 rollback하지 않는다.
Top-level result는 accepted, backpressured, not connected와 dropped count를 보존한다.

## 8. 검증 기준

- Admission fence 검사와 enqueue 사이에 owner가 바뀌지 않는다.
- Application handler 대기가 infrastructure completion과 shutdown을 막지 않는다.
- Level-ready owner는 동시에 한 executor만 claim한다.
- Request terminal completion과 reply capability 소비가 각각 한 번만 일어난다.
- Logical Multicast가 remote node마다 record 하나를 보내고 partial acceptance를 rollback하지 않는다.
