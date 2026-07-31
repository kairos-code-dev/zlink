# Node.js service runtime lifecycle

[Node.js 문서](../README.ko.md) · [공개 인터페이스](../../common/spec/server/languages/node/interfaces/README.ko.md) ·
[Host 종료 계약](../../common/spec/28-graceful-drain-handoff.ko.md) ·
[Stateful maintenance 내부 구조](stateful-maintenance-runtime.ko.md)

## 1. 목적

이 문서는 Node.js Framework가 NestJS host, service runtime과 공개 raw Node binding을 연결하는 내부
구조를 설명한다. 공개 상태와 종료 결과는 정식 spec이 소유한다. 이 문서는 구현자가 startup, mailbox,
transport liveness와 teardown 순서를 같은 구조로 구현할 때 참조한다.

Node.js service runtime은 다른 언어 runtime과 구현 코드를 공유하지 않는다. 공통으로 맞추는 대상은
Framework spec, service wire protocol과 contract fixture다. Core service C API나 private addon SPI를
중간 공통 runtime으로 만들지 않는다.

## 2. 모듈 경계

```text
+----------------------------------------------------------+
| NestJS adapter: DI, registration, lifecycle hooks        |
+----------------------------------------------------------+
| Node service runtime: routing, mailbox, owner, relocation |
+----------------------------------------------------------+
| Public Node binding: context, message, raw sockets       |
+----------------------------------------------------------+
| Core raw runtime: transport, poller, monitor             |
+----------------------------------------------------------+
```

NestJS adapter는 configuration과 provider 수명만 조정한다. Service runtime은 ChannelName selection,
Spot·Actor execution, request completion, discovery와 maintenance transaction을 소유한다. Binding adapter는
공개 package root가 제공하는 raw socket API만 호출한다. Native addon의 service symbol, generated internal
type, private handle과 reflection 우회는 사용하지 않는다.

## 3. Startup

Host state는 runtime 객체를 만들기 전에 `Preparing`으로 고정한다.

1. Registration을 immutable snapshot으로 만들고 ChannelName, factory type, state contract와 provider
   capability를 검증한다.
2. Binding context와 raw socket adapter를 만든다.
3. Service runtime에 idle probe 5초와 inbound deadline 15초의 고정 liveness profile을 시작한다. 이 값은
   Framework public option으로 노출하지 않는다.
4. Routing ID를 할당하고 listener를 bind한 뒤 실제 advertise endpoint를 확정한다.
5. Owner lease와 recovery coordinator를 시작하고 미완료 authority transaction을 복구한다.
6. Handler table, application mailbox와 infrastructure mailbox를 준비한다.
7. MeshNode, ClientServer server와 fanout publisher descriptor를 게시하고 peer admission을 시작한다.
8. 모든 required component가 ready인 snapshot 하나를 commit한 뒤 state를 `Serving`으로 바꾼다.

어느 단계에서든 실패하면 아직 게시한 descriptor와 lease를 current fence로 정리하고 raw resource를 역순으로
닫는다. 부분적으로 준비된 runtime을 DI provider에 노출하지 않으며 terminal state는 `Error`다.

## 4. 두 mailbox domain

Raw socket receive callback은 frame 검증과 service envelope decode까지만 수행한다. Application handler를
callback 안에서 직접 호출하지 않는다.

- Application mailbox는 handler turn, 새 Spot·Actor 생성, join, timer와 session binding을 직렬화한다.
- Infrastructure mailbox는 peer admission, request completion, owner lease, relocation recovery, termination
  barrier와 STREAM binding fence를 처리한다.

두 mailbox는 같은 event loop를 사용하더라도 서로 다른 bounded queue와 scheduling flag를 가진다.
Application callback이 Promise를 기다리는 동안 infrastructure mailbox는 completion과 lease deadline을
계속 처리한다. Monitoring observer는 두 mailbox의 진행 조건이 아니며 callback 실패가 runtime state를
바꾸지 않는다.

## 5. Transport liveness

RouteMesh와 ClientServer runtime은 service protocol의 `livenessProbe`와 `livenessAck`을 만들고 검증한다. 두
command는 connection-local non-zero probe ID를 사용하고 metadata와 payload를 포함하지 않는다. Fanout
subscriber runtime은 publisher마다 전용 SUB socket과 receive loop를 두고, 첫 valid application record 또는
[exact two-frame beacon](../../common/internals/service-wire-protocol.ko.md#5-service-liveness)을 받은
뒤에만 해당 publisher를 ready로 만든다. Probe·ACK과 beacon을 application message로 decode하거나 application
queue와 handler에 전달하지 않는다. Core raw runtime은 disconnect·error monitor와 reconnect primitive만 제공한다.

Orderly disconnect monitor event는 해당 pipe를 즉시 not-ready로 만든다. Monitor event가 없는 half-open
RouteMesh·ClientServer 연결은 마지막 유효 inbound service frame부터 15초가 지나면 not-ready로 바뀐다. Fanout은
마지막 valid application record 또는 exact beacon부터 15초가 지나면 해당 publisher의 전용 socket만 not-ready로
바뀐다. Owner lease와 Store polling은 이 판정을 대신하지 않는다. Reconnect가 시작돼도 peer admission 또는
fanout의 first valid receive를 확인하기 전에는 ready target set에 다시 넣지 않는다.

## 6. Retire와 Shutdown

`ZLinkFrameworkRuntime` 하나가 host의 모든 topology를 조정한다. RouteMesh나 ClientServer runtime에 별도
종료 command를 두지 않는다.

`Retire`는 host maintenance barrier를 잡아 local object inventory와 target reservation을 한 snapshot으로
검증한다. Preflight가 실패하면 admission과 `Serving` state를 그대로 유지하고 `Blocked`를 반환한다.
성공하면 barrier 안에서 state를 `Draining`으로 바꾸고 application admission boundary를 한 번만 seal한다.

`Shutdown`은 continuity preflight 없이 같은 barrier에서 admission을 seal한다. NestJS shutdown hook은
진행 중인 termination에 합류하고, 없으면 `Shutdown`을 시작한다. Rolling maintenance controller는 hook
전에 `Retire`를 명시적으로 호출한다.

Draining 뒤 먼저 시작한 intent와 deadline은 shared operation에 고정한다. 뒤의 waiter는 같은 Promise에
합류하되 각자의 AbortSignal은 waiter만 끝낸다. `Blocked`는 terminal result cache에 넣지 않는다.
`Stopped` 또는 `ForceStopped` terminal result는 한 번만 commit하고 이후 호출에 그대로 반환한다.

## 7. Teardown 순서

1. Admission seal 전에 수락한 handler와 request completion을 deadline까지 진행한다.
2. Actor와 Instance Spot relocation, accepted journal replay와 STREAM binding barrier를 terminal phase로 만든다.
3. User Spot과 relocation하지 않는 local resource를 닫는다.
4. Current authority fence로 owner record, descriptor와 lease를 정리한다.
5. ClientServer listener, fanout publisher, peer pipe와 raw socket을 닫는다.
6. Terminal result와 monitoring event를 publish한다.
7. Observer, NestJS registration과 binding context를 닫는다.

Deadline이나 relocation·teardown failure가 발생해도 동일 순서의 bounded cleanup을 한 번 수행한다.
Terminal event를 publish하기 전에 monitoring source를 분리하지 않는다.

## 8. 구현 검증 지점

- Framework package가 Node binding의 public entry point만 import한다.
- Core service symbol과 private native handle을 참조하지 않는다.
- Preparing 중 application admission이 열리지 않는다.
- Application handler가 대기 중이어도 infrastructure mailbox와 service liveness deadline이 진행된다.
- Concurrent Retire·Shutdown이 host barrier와 terminal Promise 하나만 만든다.
- Waiter 취소가 shared termination을 취소하지 않는다.
- NestJS hook이 진행 중인 Retire에 합류하고 별도 Shutdown transaction을 만들지 않는다.
- Terminal event 뒤에만 observer와 binding context를 정리한다.
