<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Channel egress routing](config-12-channel-egress-routing.ko.md) |
[다음: Instance Spot activation](config-14-instance-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Config 13 — One-way submit admission

One-way send·publish·reply가 operation family의 admission boundary에 제출된 뒤 반환 데이터 없이
완료되는지 실제 다중 process 환경에서 검증한다. 비동기 terminal 완료는 원격 handler 실행 완료가 아니다.
일반 send family는 queue가 바로
수락하지 못하면 정해진 send timeout 안에서 writable 상태를 기다리고, Logical Multicast는 bounded executor
direct handoff 뒤 Framework service runtime이 확정한 target snapshot을 한 번 처리한다. 이 config는 새 public
API의 근거가 아니다. 공개 계약은
[async 실행 정책](../spec/05-async-execution-policy.ko.md),
[Framework API](../spec/06-framework-api.ko.md),
[Channel messaging](../spec/08-channel-messaging.ko.md),
[Spot messaging](../spec/12-spot-messaging.ko.md)이 소유한다.

## 1. 목적과 범위

이 config는 다음 경계를 함께 검증한다.

- Public one-way call은 .NET `Async`, Kotlin wrapper `await`, Java·Node.js·C++ `submit`으로
  local outbound admission을 기다린다. 정상 완료 값은 없고 실패는 예외 또는 언어의 비동기 실패로 전달한다.
- 일반 send·classic fanout·bound session·session Actor relay·STREAM send와 reply는 admission queue가 가득 차면
  send-ready 또는 local mailbox capacity signal을 기다리고,
  deadline 전 수락·deadline 만료·cancellation·shutdown 중 먼저 확정된 terminal 하나만 반환한다.
- Logical Multicast는 일반 send-ready retry와 executor 내부 대기 queue를 사용하지 않는다. Direct handoff
  capacity는 bounded admission waiter에서 send timeout까지만 기다린다. `worker slot` 하나를 얻으면 target
  snapshot을 한 번 확정하고 각 snapshot target에 최대 한 번만 admission을 시도한다. Snapshot 처리가
  시작되면 operation이 commit된 것으로 본다.
- 정상 완료는 family admission boundary가 operation을 수락했다는 뜻이며 원격 handler나 subscriber가
  실행됐다는 뜻이 아니다.
- Timeout을 늘리거나 반복 submit해서 성공시키는 절차는 허용하지 않는다.

STREAM 범위는 framework server package가 handler context에 제공하는 bound session과 server session의
send·reply call이다. Stream connector package의 send builder, 반환 type과 retry 동작은 이 config에서
검증하지 않는다. STREAM peer가 필요한 시나리오에서는 기존 공개 connector를 고정된 test peer로 사용할 수
있지만, connector terminal의 반환값을 Config 13의 성공 증거로 사용하지 않는다.

## 2. 서버 구성

한 번의 실행에서 다음 역할을 사용한다. Redis와 gate fixture는 runner가 실행마다 새로 만들고 종료 시 자신이
만든 process와 fixture만 정리한다.

| 역할 | 수 | 책임 |
|---|---:|---|
| location store | 1 | Automatic topology와 global Spot·Actor authority가 공유하는 공식 Redis Location Store. 실행마다 전용 key prefix를 사용한다. |
| `AdmissionCaller` | 1 | Location Store를 등록한 Object Client. 이 host가 source인 RouteMesh·ClientServer Channel, global Spot·Actor와 Logical Multicast call을 시작하지만 factory와 placement target은 제공하지 않는다. |
| `MeshTarget` | 2 | Location Store를 등록한 Object Server. Entry Spot, stable User Spot type `admission.spot`과 Actor type `admission.actor` factory를 명시적 `Disabled` policy로 등록하고 placement weight `100`, Actor total·Spot total·`admission.spot` stable type limit `128`, activation concurrency `32`를 사용한다. RouteMesh Channel handler, local·remote Spot·Actor handler와 User Spot의 Logical Multicast subscription을 제공한다. |
| `ClientServerTarget` | 2 | 전용 ClientServer Channel의 send handler를 제공하고 select-one 재선택 대상을 분리한다 |
| `SessionGateway` | 2 | Location Store를 등록한 Object Client. local·remote bound session, global Actor relay와 server STREAM session을 제공하고 이 family의 public call을 시작하지만 object factory는 제공하지 않는다. |
| `FanoutPublisher` | 1 | classic fanout publisher와 publish endpoint를 제공하고 fanout public call을 시작한다 |
| `FanoutSubscriber` | 1 | classic fanout subscriber handler와 delivery evidence를 제공한다 |
| `StreamPeer` | 1 | server STREAM session에 연결하고 wire 수신 순서와 request sequence를 기록한다 |
| `ReceiverGate` | 경로별 1 | 연결은 유지하되 target 방향 read를 중단·재개하는 runner 소유 TCP gate다 |
| `LocalMailboxGate` | local family별 1 | Local Spot·Actor·bound relay mailbox를 작은 budget과 handler barrier로 포화시키는 in-process fixture다 |
| `EvidenceCollector` | 1 | 각 source host가 종료되기 전에 보낸 operation evidence를 저장하고 source host 종료 뒤에도 조회 endpoint를 유지한다 |

Stateful scenario는 close·destroy 뒤 recreate로 generation을 바꾸고 cross-node relocation은 시작하지 않는다.
따라서 `MeshTarget`의 모든 factory는 `Disabled`를 사용하며 Relocation Store와 relocation adapter를 등록하지
않는다. SA-E2E-06에서 `Relocate`하는 `AdmissionCaller`와 `SessionGateway`는 Object Client라 local object
inventory가 없고, 다른 Channel·session component의 eligible target만 preflight한다. Object operation을
시작하지 않는 역할에는 object role을 추가하지 않는다.

각 역할 server는 그 host가 source인 public call을 시작하는 실제 application endpoint를 가진다. Client는 이
endpoint를 HTTP client wrapper로 호출한다. Framework call을 대신 실행하는 별도 driver server는 두지 않는다.
`EvidenceCollector`는 source host가 보낸 evidence를 보관할 뿐 public Framework call을 실행하지 않는다.

## 3. 결정적 backpressure 구성

### 3.1 HWM과 pending admission

Remote transport backpressure 시나리오는 해당 operation family의 공개 설정에서 sender와 receiver HWM을 1로
둔다. Pending admission capacity도 1로 둔다. Family가 public HWM 설정을 제공하지 않으면 그 언어 구현은
내부 값을 임의로 바꾸지 않고 feature map에 contract gap을 기록한다.

HWM만으로 queue 포화를 추정하지 않는다. 모든 runner는 다음 값을 test fixture 상수로 고정한다.

| 상수 | 값 | 검증 목적 |
|---|---:|---|
| `payloadBytes` | 32768 bytes | 모든 setup·검증 payload의 encoded body 크기를 같게 만든다 |
| `gateForwardBufferBytes` | 4096 bytes | `ReceiverGate`가 방향별로 보관할 수 있는 사용자 공간 byte 수를 제한한다 |
| `socketBufferRequestBytes` | 4096 bytes | Gate 양쪽 socket의 `SO_SNDBUF`·`SO_RCVBUF` 요청값을 고정한다 |
| `gateReadBoundaryBytes` | 0 bytes | Gate를 닫은 뒤 sender 방향 socket에서 payload byte를 더 읽지 않는다 |
| `localMailboxBudget` | 1 operation | Local Spot·Actor·bound relay mailbox가 수락할 수 있는 검증 operation 수를 고정한다 |

Runner는 시작할 때 socket buffer의 실제 적용값을 읽어 checked-in platform manifest의 기대값과 대조한다.
운영체제가 요청값을 조정하는 platform은 기대값을 manifest에 고정하며, 실행마다 측정한 값으로 기대값을
바꾸지 않는다. 값이 다르거나 Gate가 닫힌 뒤 read boundary를 넘으면 setup 실패다.

Payload는 public message-size 제한 안에서 정확히 `payloadBytes`로 encode한다. 각 payload에는 scenario ID와
단조 증가 sequence를 넣고 나머지를 고정 byte로 채운다. Setup payload와 검증 payload를 구분해 handler count와
중복 여부를 직접 대조한다.

Local Spot·Actor·bound relay backpressure에는 TCP `ReceiverGate`를 사용하지 않는다. `LocalMailboxGate`는
`localMailboxBudget`이 1인 실제 runtime mailbox를 사용한다. Application handler barrier를 닫고 blocker
operation의 handler 실행이 시작된 것을 확인한 다음, setup operation 하나로 남은 mailbox slot을 사용한다. 이후
검증 operation이 mailbox-full로 pending인 것을 확인하고 barrier를 연다. Local fixture도 public call만
사용하며 runtime counter를 상태 변경 명령으로 사용하지 않는다.

### 3.2 remote receiver와 local mailbox gate 절차

Remote route를 사용하는 일반 send family는 아래 절차로 queue를 채운다.

1. 모든 route가 ready임을 확인하고 `ReceiverGate`를 닫는다. Gate는 연결을 끊지 않고 target 방향 read만
   중단한다. 닫힘 marker 뒤에는 sender 방향 socket에서 payload byte를 읽지 않았음을 byte counter로 확인한다.
2. 해당 family의 source host endpoint에서 sequence가 다른 setup submit을 시작한다. Endpoint는 submit을 기다리는
   작업의 operation ID를 반환하고, `EvidenceCollector`의 `/evidence/operations/{id}`는 `pending` 또는 terminal을
   반환한다.
3. Pending operation 하나가 확인될 때까지만 setup submit을 추가한다. 최대 64개 또는 3초 안에 pending을
   만들지 못하면 setup 실패다. 이 상한이나 send timeout을 늘려 계속하지 않는다.
4. Pending capacity가 1인 상태에서 다음 검증 operation도 별도 bounded waiter로 무제한 보관하지 않는지
   확인한다. Gate를 열면 먼저 기다리던 operation이 writable wakeup 뒤 한 번만 정상 완료하며, 공간을
   얻지 못한 operation은 send timeout·cancellation·shutdown 중 먼저 확정된 실패로 끝난다.
5. Gate를 연 뒤 handler가 받은 sequence와 terminal completion 수를 대조한다. Setup payload를 성공 payload로
   바꾸거나 같은 operation을 다시 submit하지 않는다.

고정 sleep과 예상 전송 횟수로 queue 포화를 추정하지 않는다. Gate transition, operation pending marker와
terminal marker가 순서를 결정한다.

Local Spot·Actor·bound relay는 같은 절차에서 `ReceiverGate` 대신 `LocalMailboxGate`를 사용한다. Handler
barrier를 닫고 mailbox budget을 사용한 뒤 pending marker를 확인한다. Barrier를 열면 local mailbox capacity
signal로 기존 pending operation이 한 번만 다시 시도된다. TCP byte counter나 socket HWM을 local admission의
증거로 사용하지 않는다.

### 3.3 invocation·transport attempt·commit 구분

한 번의 public call과 내부 transport 재시도, queue 수락을 서로 다른 값으로 기록한다.

| evidence | 증가 시점 |
|---|---|
| `publicInvocationCount` | application이 언어별 public one-way terminator를 호출할 때 증가한다 |
| `invalidInvocationCount` | argument·handle·one-shot state 검증에서 거부한 public invocation일 때 증가한다 |
| `transportAttemptCount` | Runtime이 Core·binding의 non-blocking transport admission primitive 또는 local mailbox admission을 실제 호출할 때 증가한다. `EAGAIN`과 mailbox-full도 한 번으로 센다 |
| `commitCount` | 일반 send는 transport queue가 operation을 수락할 때, Logical Multicast는 확정한 snapshot 처리를 시작할 때 증가한다 |
| `snapshotPassCount` | Logical Multicast가 확정한 target snapshot 전체를 처리하기 시작할 때 증가한다 |
| `targetAttemptCount` | Logical Multicast가 확정한 snapshot의 개별 remote 또는 local target에 admission을 시도할 때 증가한다 |

일반 pending operation은 최초 transport attempt 한 번에서 `EAGAIN` 또는 mailbox-full을 확인한다. Gate가 닫힌
동안 polling·timer로 다시 시도하지 않으므로 추가 attempt는 0이다. 이후 관측한 send-ready 또는 local mailbox
capacity signal 한 번에 transport admission을 정확히 한 번 다시 시도한다. 따라서 수락된 pending operation의
`transportAttemptCount = 2`이고 signal count와 retry attempt count는 각각 1이다. Pending admission capacity가
이미 가득 찬 상태에서 새 operation이 들어오면
유효한 call도 먼저 non-blocking transport 또는 local mailbox admission을 한 번 시도한다. 그 결과가
`EAGAIN` 또는 mailbox-full이고 waiter 공간도 없을 때도 backpressure를 public terminal로 반환하지 않는다.
추가 operation은 bounded capacity가 생기기를 send timeout 안에서 기다리며, 그 전에 공간을 얻지 못하면
timeout으로 실패한다. 최초 transport attempt를 수행했다면 `transportAttemptCount = 1`이다.
Invalid call과 Logical Multicast executor direct handoff 실패만 transport attempt와 snapshot pass가 0이다.
Logical Multicast는 snapshot을 확정한 뒤 각 member에 admission을 최대 한 번 시도하며, 일부 target이
거부되어도 snapshot 전체를 다시 처리하지 않는다. 어떤 counter도 public
call을 다시 호출한 횟수로 대신하지 않는다. 유효한 일반 scenario의 `publicInvocationCount`와 `commitCount`는
각각 1을 넘지 않는다. Duplicate terminator를 검증하는 negative case는 같은 call object 또는 같은 reply
token에서 만든 call object의 public invocation을 별도로 기록한다. Claim loser는 transport attempt와 commit을
추가하지 않는다.

### 3.4 handler gate와 Logical Multicast commit barrier

일반 send family의 정상 완료는 local outbound queue가 operation을 수락했다는 뜻이다. Logical Multicast는
Framework runtime이 고정한 target snapshot을 한 번 처리한 뒤 반환 데이터 없이 정상 완료한다. Snapshot
target이 0개여도 정상 완료한다. Snapshot에 포함된 일부 또는 모든 target이 drop·unreachable이어도 public
결과나 예외로 target별 detail을 반환하지 않는다. 정상 완료와 원격 실행 완료를 구분할 때는 target이
payload를 받은 직후 handler 본문을 진행하지 않도록 application handler gate를 닫는다. Submit terminal을 먼저
확인한 뒤 handler gate를 열어 delivery를 끝낸다.

Logical Multicast는 receiver gate를 target별로 독립 제어한다. Runtime은 Framework publish transaction
경계에서 다음 진단 evidence를 남긴다. 이 evidence는 관측에만 사용하며 scheduling이나 결과를 바꾸지
않는다.

- executor direct handoff와 snapshot 처리 시작 여부
- 한 public operation에 대응하는 snapshot pass 횟수
- snapshot 처리 시작 직전의 commit marker
- snapshot member별 admission attempt 횟수
- remote·local snapshot, admitted, dropped, unreachable count
- snapshot 처리 종료 뒤 terminal completion 횟수

Cancellation·shutdown을 direct handoff·commit 전과 commit 후에 각각 발생시킨다. Commit 전 winner는
snapshot pass와 target admission attempt를 0회로 유지한다. `worker slot`을 바로 얻지 못한 direct handoff
실패도 backpressure를 반환하지 않고 send timeout까지 executor capacity를 기다린다. Capacity가 생기면
snapshot pass를 한 번 시작하고, timeout·cancellation·shutdown이 먼저 확정되면 snapshot pass와 commit 없이
해당 비동기 실패로 끝난다. Commit 뒤에는 snapshot pass 하나를 끝까지 처리하고 반환 데이터 없이 정상 완료한다.

### 3.5 Logical Multicast snapshot barrier

All-unreachable과 snapshot 0을 route 변경 timing에 의존하지 않고 재현하기 위해 검증 build에
`PublishSnapshotBarrier`를 둔다. 이 fixture는 Framework service runtime이 ready target snapshot을 기록한
직후, 각 target에 submit하기 전에 해당 operation만 일시 정지한다. `SA-E2E-13.b` runner는 barrier marker에서
snapshot member를 확인하고 모든 member의 pipe를 종료한 뒤 barrier를 한 번 해제한다. 따라서 Framework
runtime이 이미 확정한 snapshot은 0보다 크지만 target submit은 모두 unreachable로 끝난다.

`PublishSnapshotBarrier`는 E2E instrumentation이며 public API가 아니다. 기존 public publish call과
Framework publish transaction을 그대로 사용하고 raw frame을 만들거나 별도 transport call로 publish를
대신하지 않는다. Fixture는
snapshot 내용과 pause·release 순서만 기록하며 member 선택, target별 admission이나 public completion을
바꾸지 않는다.

## 4. 공통 evidence 형식

모든 언어는 scenario별 JSON evidence와 동일한 의미의 로그를 남긴다. 필드 이름은 언어 관례에 맞게 바꿀
수 있지만 feature map에서 다음 값의 대응을 밝혀야 한다.

| evidence | 의미 |
|---|---|
| `scenarioId`, `operationId`, `sequence` | scenario와 public call 한 번을 연결한다 |
| `family`, `routeKind`, `targetId` | send·publish·reply family와 local·remote target을 구분한다 |
| `gateClosedAt`, `gateOpenedAt`, `pendingAt` | receiver gate 또는 local mailbox gate와 pending admission 순서를 증명한다 |
| `deadlineAt`, `terminalAt`, `terminalKind` | 정상 완료 또는 timeout·cancellation·target·route·shutdown·invalid failure 중 terminal winner를 비교한다. Public status나 result 객체가 아니다 |
| `publicInvocationCount`, `invalidInvocationCount`, `transportAttemptCount`, `commitCount` | public call, local validation 실패, 내부 transport 시도와 queue 수락을 구분한다 |
| `terminalCount` | public operation이 terminal로 완료된 횟수를 확인한다 |
| `handlerEnteredAt`, `handlerReleasedAt`, `handlerCount` | submit completion과 원격 handler 완료를 구분한다 |
| `routeGeneration`, `connectionGeneration` | reconnect 뒤 이전 operation이 다시 제출되지 않았는지 확인한다 |
| `pendingWaiterCount`, `reservationCount`, `callbackCount` | shutdown·disposal 뒤 resource가 남지 않았는지 확인한다 |
| `sendReadySignalCount`, `capacitySignalCount`, `retryAttemptCount`, `pollRetryCount`, `timerRetryCount` | pending operation이 관측한 signal과 그 signal로 시작한 재시도가 일대일인지 확인한다 |
| `snapshotPassCount`, `targetAttemptCount`, `commitAt` | 검증 build 내부 fixture로 Logical Multicast의 snapshot 단일 처리와 member별 단일 attempt를 확인한다. Public monitoring field가 아니다 |
| `snapshotRecordedAt`, `snapshotBarrierReleasedAt`, `snapshotMemberIds` | Logical Multicast snapshot 확정과 target pipe 종료·barrier 해제 순서를 확인한다 |

Runtime 내부 counter를 application의 성공 조건을 만들기 위한 제어 값으로 사용하지 않는다. Client가 받은
Public awaitable의 정상 완료·예외, 역할 server evidence와 사용한 gate 기록을 함께 대조한다. Timestamp는 순서 확인에만 쓰며
느슨한 latency 상한으로 실패를 숨기지 않는다.

## 5. E2E 시나리오

### SA-E2E-01 — 즉시 수락 fast path

- **목적:** Logical Multicast 이외의 ready queue가 one-way call을 반환 데이터 없이 즉시 정상 완료하며 Framework
  scheduler queue를 추가로 거치지 않는지 확인한다.
- **Topology·사전 조건:** Channel, Spot, Actor, ClientServer, classic fanout, bound session, session Actor relay와
  server STREAM route를 모두 ready로 둔다. `ReceiverGate`와 `LocalMailboxGate`의 handler barrier는 열고 HWM과
  mailbox는 비어 있어야 한다.
- **절차:** Family별로 sequence 하나를 submit한다. 시작 전·후 Framework scheduler enqueue count와 public
  awaitable의 즉시 완료 여부를 기록한다. Node.js의 Promise microtask는 Framework scheduler hop으로 세지 않는다.
- **기대 결과:** 각 call은 public invocation 1회, transport attempt 1회, commit 1회, scheduler enqueue 0회,
  terminal 정상 완료 1회다. Public completion에 status나 result payload가 없어야 한다.
- **공통 evidence:** Public completion, operation terminal marker, scheduler enqueue delta, target handler sequence를
  같은 operation ID로 대조한다.

### SA-E2E-02 — writable wakeup 뒤 수락

- **목적:** 일시적으로 가득 찬 queue가 비면 pending call이 polling이나 application retry 없이 수락되는지
  확인한다.
- **Topology·사전 조건:** §3.2 절차로 remote family는 `ReceiverGate`, local Spot·Actor·bound relay는
  `LocalMailboxGate`를 사용해 pending operation 하나를 만든다. Public send timeout은 기본값 또는 family
  설정값을 그대로 사용한다.
- **절차:** 검증 operation이 pending임을 확인한 뒤 deadline 전에 gate를 한 번 연다. 같은 operation을 다시
  submit하지 않는다.
- **기대 결과:** Writable wakeup 뒤 검증 operation이 반환 데이터 없이 정상 완료한다. Public invocation과 commit은 각각
  1회다. 최초 `EAGAIN` 또는 mailbox-full을 확인한 transport attempt는 1회이며, gate를 열기 전 추가 attempt는
  0이다. Send-ready 또는 local capacity signal을 정확히 한 번 관측한 뒤 admission을 정확히 한 번 다시
  시도하므로 최종 transport attempt는 2회다. Poll·timer retry는 0회이고 terminal과 handler delivery도 각각
  한 번이다.
- **공통 evidence:** Gate transition, pending marker, send-ready·capacity signal count, retry attempt count,
  poll·timer retry count, terminal kind와 handler sequence를 시간 순서로 남긴다.

### SA-E2E-03 — bounded pending admission capacity

- **목적:** Transport HWM뿐 아니라 bounded pending admission 공간도 backpressure를 제한하는지 확인한다.
- **Topology·사전 조건:** Receiver gate를 닫고 §3.2에 따라 pending capacity 1을 이미 사용한다.
- **절차:** 두 번째 pending 후보를 시작하고 endpoint 응답과 operation evidence를 확인한다. 첫 operation이
  사용하는 waiter 외에 payload reservation이 무제한 증가하지 않는지 확인하고 gate는 send deadline까지 열지 않는다.
- **기대 결과:** 두 번째 후보는 non-blocking transport 또는 local mailbox admission을 한 번 시도해
  `EAGAIN` 또는 mailbox-full을 확인한다. Backpressure status나 결과 객체를 반환하지 않고 bounded capacity를
  기다리다가 timeout 예외로 한 번 완료되며 첫 pending operation의 상태는 바뀌지 않는다.
- **공통 evidence:** Pending waiter count 1, 두 operation의 서로 다른 ID, 두 번째 terminal time과
  timeout exception kind, public invocation 1회, transport attempt 1회, commit 0회를 남긴다.

### SA-E2E-04 — deadline 만료와 late admission 차단

- **목적:** Queue가 deadline까지 비지 않으면 timeout 예외로 끝나고 이후 writable event가 완료된 operation을
  다시 제출하지 않는지 확인한다.
- **Topology·사전 조건:** Logical Multicast를 제외한 각 family에서 remote route는 `ReceiverGate`, local
  mailbox는 `LocalMailboxGate`를 닫고 검증 operation을 pending으로 만든다. 설정된 send timeout을 기록한다.
- **절차:** 다음 하위 case를 실행한다.
  - `SA-E2E-04.a`: Gate를 닫은 채 deadline terminal을 기다린다. Timeout 예외 확인 뒤 gate를 열고 정상
    operation 하나를 별도 ID로 보낸다.
  - `SA-E2E-04.b`: Public send timeout에 `0`과 `-1`을 각각 설정한다.
  - `SA-E2E-04.c`: `INT_MAX` millisecond를 설정하고 ready queue의 fast path를 한 번 실행한다. 실제 deadline
    만료까지 기다리지 않는다.
  - `SA-E2E-04.d`: 표현 가능한 양수 sub-millisecond 값을 설정하고 ready queue와 pending queue를 각각
    실행한다.
  - `SA-E2E-04.e`: 해당 언어의 public duration type으로 표현 가능한 `INT_MAX + 1`, 양의 infinity와 음의 infinity를
    각각 설정한다. 표현할 수 없는 입력은 호출을 흉내 내지 않고 N/A evidence를 남긴다.
  - `SA-E2E-04.f`: Node.js에서 `NaN`과 정수가 아닌 millisecond 값을 각각 설정한다.
- **기대 결과:** 만료 operation은 timeout 예외로 한 번 완료되며 late admission과 handler delivery는 0이다.
  새 operation만 반환 데이터 없이 정상 완료한다. `0`과 `-1`은 늦어도 host startup에서 거부되고 기본값으로
  바뀌지 않는다. `INT_MAX`는
  유효한 값으로 보존된다. 양수 sub-millisecond 값은 1ms로 올림하며 0으로 줄이지 않는다. `INT_MAX + 1`과
  표현 가능한 infinity, Node.js의 `NaN`과 정수가 아닌 값은 validation에서 거부한다. .NET·Java·C++처럼
  public duration type이 infinity를 표현하지 못하는 언어는 양수·음수 infinity case를
  `not applicable by type contract`로 기록한다. 음수 `-1`과 `INT_MAX + 1`도 해당 type이 값을 표현할 수 있을
  때만 validation case를 실행하고, 표현할 수 없으면 같은 N/A evidence를 남긴다. Node.js의 `NaN`과 정수가
  아닌 millisecond case는 Node.js에서만 실행한다.
- **공통 evidence:** Deadline·terminal timestamp, timeout operation의 terminal count와 handler count 0,
  새 operation의 다른 sequence를 기록한다. Timeout validation case는 입력값, millisecond 정규화값,
  설정 또는 startup 결과와 실제 deadline source를 함께 남긴다.

### SA-E2E-05 — target 부재와 route 미연결 구분

- **목적:** Resolve할 대상이 없는 상태와 알려진 target에 ready route가 없는 상태를 다른 Framework 예외로 전달하는지
  확인한다.
- **Topology·사전 조건:** 첫 호출은 descriptor·location이 없는 ID를 사용한다. 두 번째 호출은 target identity와
  location을 유지하되 receiver gate가 아니라 연결 gate에서 route ready만 차단한다.
- **절차:** 두 상태에서 같은 family call을 한 번씩 시작한다. Settle 반복이나 fallback target을 허용하지 않는다.
- **기대 결과:** 대상 부재는 `TargetNotFound`, 알려진 target의 미연결은 `RouteNotConnected` Framework
  예외다. 둘 다 terminal
  한 번이고 handler 실행은 0이다.
- **공통 evidence:** Resolve snapshot, known target ID, route-ready state, public exception kind와 handler count를 남긴다.

### SA-E2E-06 — Relocate·Shutdown admission 거부

- **목적:** `Relocate` 또는 `Shutdown`의 admission seal 뒤 새 one-way call이 runtime shutdown 예외로 한 번만
  거부되는지 확인한다.
- **Topology·사전 조건:** Target이 아니라 family별 public call의 실제 source host를 `Relocate`와 `Shutdown` 대상으로
  사용한다. RouteMesh·ClientServer Channel, Spot·Actor와 Logical Multicast는 `AdmissionCaller`, classic fanout은
  `FanoutPublisher`, bound session·session Actor relay와 server STREAM은 해당 `SessionGateway`가 source다. 미리
  만든 public call 작업은 각 source Framework admission barrier 뒤에서 기다리게 한다. Receiver gate는 열어
  capacity 실패와 섞이지 않게 한다. Source host가 종료된 뒤에도 결과를 조회할 수 있도록 operation evidence와
  admission-closed marker는 독립 process인 `EvidenceCollector`로 전송한다.
- **절차:** `SA-E2E-06.a`는 continuity preflight가 성공하도록 eligible target을 준비한 뒤 family별 source host의
  `Relocate`를 시작하고 `Relocating`과 admission-closed marker를 확인한 다음 public
  call barrier를 해제한다. `SA-E2E-06.b`는 같은 source에서 pending operation을 만든 뒤 해당 source Framework
  host의 `Shutdown`과 경쟁시킨다. Target 역할 server의 `Relocate`·`Shutdown` 결과와 source host 안의 control endpoint를
  종료 후 evidence로 사용하지 않는다.
- **기대 결과:** Admission seal 뒤의 신규 call과 shutdown이 먼저 선형화된 pending call은 runtime shutdown
  예외로 한 번 완료된다. 이미 transport admission을 완료한 call의 정상 완료는 바꾸지 않는다. 어느
  terminal도 뒤늦게 timeout으로 바뀌거나 `Draining` 뒤 새로 admission되지 않는다.
- **공통 evidence:** Family별 source 역할·process·host ID, effective termination intent, source
  `Draining`·admission-closed marker, terminal
  kind·count, transport attempt·commit과 target handler count를 `EvidenceCollector`에서 조회한다. Collector
  process ID가 종료한 source와 다르고 scenario 조회가 source 종료 뒤에도 성공하는지 함께 기록한다.

### SA-E2E-07 — cancellation winner와 Logical Multicast commit 경계

- **목적:** Cancellation을 제공하는 .NET·Java·Kotlin·Node.js에서 cancellation이 pending admission을 정리하고,
  Logical Multicast는 direct handoff·commit 전과 commit 후 의미를 구분하는지 확인한다.
- **Topology·사전 조건:** 일반 send는 receiver gate를 닫아 pending으로 만든다. Logical Multicast는 direct
  handoff를 시도하기 직전의 test barrier와 target별 receiver gate로 commit 전과 commit 후 실행을 각각 만든다.
  Test barrier는 executor 대기 queue가 아니며 검증 build의 scheduling 관측점에서만 call 진행을 제어한다. Java는
  `submit()`이 반환한 stage에서 `toCompletableFuture().cancel(false)`를 호출하고 Kotlin coroutine은 같은
  cancellation bridge를 사용한다.
- **절차:** 다음 하위 case를 실행한다.
  - `SA-E2E-07.a`: 일반 send pending 중 cancel한다.
  - `SA-E2E-07.b`: Logical Multicast는 direct handoff·commit 전 한 번, commit marker 후 한 번 cancel한다.
  - `SA-E2E-07.c`: Pending operation 하나에 timeout, cancellation과 `AdmissionCaller` shutdown을 같은 barrier에서
    해제한다. 세 signal의 실제 발생 순서를 기록하며 timeout 값을 늘려 winner를 고정하지 않는다.
  - `SA-E2E-07.d`: .NET은 pre-cancelled token, Node.js는 이미 aborted된 signal로 각각 유효 call을 시작한다.
    Java·Kotlin은 stage가 반환된 뒤 최초 `EAGAIN`으로 pending이 된 operation을 취소한다. JVM public call에는
    첫 admission attempt 전에 cancellation을 전달하는 인자가 없으므로 JVM에서 pre-cancel attempt 0을 요구하지
    않는다.
  - `SA-E2E-07.e`: .NET·Node.js의 같은 pre-cancelled 입력을 잘못된 argument 또는 handle과 함께 사용한다.
- **기대 결과:** 일반 send와 commit 전 multicast는 cancelled awaitable이며 late admission과 두 번째 completion이
  없다. Commit 전 snapshot pass와 target admission attempt는 0회다. Commit 후 cancel은 publish를 중단하지
  않고 snapshot pass 하나를 끝낸 뒤 반환 데이터 없이 정상 완료한다. Java·Kotlin의 commit 후
  `cancel(false)`는 `false`이며 shared stage가 정상으로 한 번 완료된다. .NET과 Node.js caller도 commit 뒤
  반환 데이터 없는 정상 완료를 관찰한다. Kotlin coroutine이
  commit 뒤 취소되면 caller의 `await`는 cancellation으로 끝나지만 shared JVM stage와 underlying runtime
  operation은 정상 terminal을 보존한다. 이를 coroutine 성공 결과로 기록하지 않는다. 세 terminal signal이
  경쟁하는 case도 실제 winner 하나만 terminal을 확정하고 나머지 signal은 결과를 바꾸지 않는다. 유효한
  .NET·Node.js만 pre-cancelled call의 transport attempt와 commit이 0인 cancelled awaitable을 요구한다.
  Argument·handle validation은 pre-cancellation보다 먼저 실행되므로 잘못된 call은 cancelled가 아니라
  exceptional completion이다.
- **공통 evidence:** Cancel timestamp, commit timestamp, terminal kind·count, snapshot pass count,
  target별 admission attempt와 detail을 남긴다. Java는 `cancel(false)` 반환값과 shared stage terminal을
  기록한다. Kotlin은 coroutine terminal과
  shared stage·runtime terminal을 별도 필드로 기록한다. Three-way race는 timeout·cancel·shutdown timestamp와
  winner를 모두 남긴다. C++만 cancellation과 three-way case를 feature map에
  `not applicable by contract`로 기록한다.

### SA-E2E-08 — RouteMesh node direct local·remote parity

- **목적:** Node direct가 local target과 remote target에서 같은 정상 완료·실패 계약을 사용하는지 확인한다.
- **Topology·사전 조건:** 같은 packet handler를 local `MeshTarget`과 remote `MeshTarget`에 등록한다. 두 route의
  receiver gate와 HWM을 같은 값으로 구성한다.
- **절차:** Local·remote에서 즉시 수락, gate pending 뒤 수락, deadline 만료를 같은 순서로 실행한다.
- **기대 결과:** Route 위치와 관계없이 즉시 admission과 delayed admission은 반환 데이터 없이 정상 완료하고,
  deadline 만료는 timeout 예외다. 원격 경로에만 application retry나 더 긴 timeout을 적용하지 않는다.
- **공통 evidence:** Route kind를 제외한 deadline·terminal·transport attempt·commit 필드를 나란히 비교하고
  sequence delivery를 target별로 남긴다.

### SA-E2E-09 — RouteMesh ChannelName deadline

- **목적:** ChannelName send가 process-local index가 선택한 MeshNode의 ROUTER send timeout과 완료 계약을 사용하는지
  확인한다.
- **Topology·사전 조건:** Channel server 두 개를 서로 다른 MeshNode에 두고 한 target만 positive weight로 선택되게
  한다. 선택된 target의 receiver gate를 닫는다.
- **절차:** §3.2로 pending을 만든 뒤 gate-open 수락과 gate-closed timeout을 각각 실행한다. 이 case에서는 다른
  positive-weight member를 추가하지 않아 재선택 후보가 없도록 한다.
- **기대 결과:** RouteMesh socket의 deadline만 적용되고 terminal은 한 번이다. Successful admission이 선택한
  MeshNode만 handler count가 1이고 다른 handler count는 0이다.
- **공통 evidence:** ChannelName, selected MeshNode ID, deadline source, terminal kind와 target별 handler count를
  기록한다.

### SA-E2E-10 — ClientServer ChannelName deadline

- **목적:** ClientServer send가 RouteMesh timeout이 아니라 해당 client DEALER의 send timeout을 사용하는지
  확인한다.
- **Topology·사전 조건:** 전용 ClientServer client와 server를 구성하고 server 방향 receiver gate를 닫는다.
  같은 process의 RouteMesh timeout은 다른 값으로 두어 잘못된 소유자를 드러낸다.
- **절차:** Pending 뒤 gate를 deadline 전에 열어 수락시키고, 별도 operation은 client deadline까지 닫아 둔다.
- **기대 결과:** 첫 call은 반환 데이터 없이 정상 완료하고, 둘째는 client DEALER deadline의 timeout 예외다.
  RouteMesh deadline이나 server retry를 사용하지 않는다.
- **공통 evidence:** ClientServer ChannelName, DEALER deadline, RouteMesh 비교값, gate 기록과 terminal kind를
  남긴다.

### SA-E2E-11 — Spot resolve generation과 route admission

- **목적:** Spot direct send가 location resolve 실패와 resolved owner route의 capacity 완료를 구분하는지 확인한다.
- **Topology·사전 조건:** 존재하지 않는 global Spot ID와 ready location을 가진 remote Spot을 준비한다.
  Ready Spot은 resolve와 outbound admission 사이에서 close·recreate하여 generation을 바꿀 수 있고 current
  owner의 receiver gate를 독립 제어할 수 있어야 한다.
- **절차:** Missing RID로 한 번 보낸다. Ready RID는 resolve barrier에서 정지한 뒤 close·recreate하고 barrier를
  해제한다. 별도 fresh operation으로 즉시 수락·pending 뒤 수락·deadline 만료를 실행한다.
- **기대 결과:** Missing Spot은 `TargetNotFound` Framework 예외이며 remote creation을 시작하지 않는다. Generation 교체 전
  operation은 새 incarnation으로 자동 retarget되지 않고 terminal 한 번으로 끝나며 잘못된 handler 실행은 0이다.
  Fresh operation은 route capacity에 따라 반환 데이터 없이 정상 완료하거나 timeout 예외로 끝나며 handler
  delivery는 성공 sequence에만 있다.
- **공통 evidence:** Global Spot ID, resolve에서 선택한 object·owner generation, current generation, owner route,
  admission terminal과 generation별 Spot handler sequence를 남긴다. Public call에는 handle·ref·owner를 넘기지 않는다.

### SA-E2E-12 — Actor resolve generation과 route admission

- **목적:** Actor direct send가 global `ActorId` resolve 뒤 generation 교체와 current owner route capacity를
  구분하는지 확인한다.
- **Topology·사전 조건:** Remote Actor를 준비하고 resolve와 admission 사이에서 current `ActorRef`로
  destroy한 뒤 같은 global ID·stable type으로 recreate해 ObjectGeneration을 바꿀 수 있어야 한다. Current
  owner의 receiver gate도 닫을 수 있어야 한다. 이 scenario는 owner relocation을 사용하지 않는다.
- **절차:** Global `ActorId` call을 resolve barrier에 멈춘 뒤 generation을 교체하고 해제한다. 이어서 같은 ID의
  fresh call로 gate-open·gate-closed case를 실행한다.
- **기대 결과:** 이전 operation은 current incarnation으로 자동 retarget되지 않고 terminal 한 번으로 끝나며
  generation별 handler 실행을 섞지 않는다. Fresh operation은 capacity에 따라 반환 데이터 없이 정상
  완료하거나 timeout 예외로 끝난다.
- **공통 evidence:** Global Actor ID, resolved·current object generation과 owner fence, terminal kind와
  generation별 handler count를 기록한다. Public call에는 `ActorRef`·handle·owner를 넘기지 않는다.

### SA-E2E-13 — Logical Multicast snapshot 단일 처리와 monitoring 부재

- **목적:** Logical Multicast가 executor direct handoff에 성공하면 target snapshot을 정확히 한 번 처리하고,
  각 snapshot member의 admission을 최대 한 번만 시도하며 partial admission을 전체 publish retry로 바꾸지
  않는지 확인한다.
- **Topology·사전 조건:** Remote MeshTarget 두 개와 local Spot subscription 하나를 기본 snapshot에 포함한다.
  Bounded executor의 `worker slot`과 target별 `ReceiverGate`·`LocalMailboxGate`를 독립 제어하고 commit evidence를
  활성화한다. Executor에는 대기 queue를 두지 않는다.
- **절차:** 다음 하위 case를 각각 새 public operation으로 실행한다.
  - `SA-E2E-13.a`: Target A와 local target은 수락 가능하게 두고 Target B는 HWM을 채운 뒤 gate를 닫는다.
  - `SA-E2E-13.b`: Local target을 포함하지 않고 remote target이 ready인 상태에서 publish를 시작한다.
    `PublishSnapshotBarrier`가 0보다 큰 snapshot을 기록하고 멈추면 모든 snapshot member pipe를 종료한 뒤
    barrier를 해제해 all-unreachable·admitted 0 결과를 만든다.
  - `SA-E2E-13.c`: Bounded executor의 `worker slot`을 모두 사용 중인 상태에서 publish 하나를 더 호출한다.
  - `SA-E2E-13.d`: Process-local index에는 유효한 ChannelName·topic을 등록하되 이에 일치하는 ready remote·local
    member는 0인 상태에서 publish한다.
  - `SA-E2E-13.e`: Remote target은 snapshot에 포함하지 않고 local Spot subscription만 포함한다.
    `LocalMailboxGate`로 local mailbox capacity drop을 만든다.
  Commit된 case는 marker 뒤 cancellation 또는 shutdown signal을 발생시키되 gate와 snapshot을 유지한다.
- **기대 결과:** Remote target별 source-local outbound transport queue의 capacity drop이 하나 이상인
  `SA-E2E-13.a`도 rollback이나 전체 retry 없이 반환 데이터 없이 정상 완료한다. Admitted와 dropped count는
  public 반환값이나 monitoring에 남기지 않는다. 모든 remote target이 unreachable인
  `SA-E2E-13.b`도 정상 완료하며 target count나 unreachable count를 public monitoring에 보존하지 않는다. Direct
  handoff에 사용할 `worker slot`이 없는
  `SA-E2E-13.c`는 executor capacity를 send timeout까지 기다린다. Capacity가 생기면 한 번 처리하고 정상
  완료하며, 생기지 않으면 snapshot pass·commit 없이 timeout 예외로 끝난다. Snapshot count가 모두 0인
  `SA-E2E-13.d`는 유효한 local index entry를 유지하며 target admission attempt 없이 한 번의 snapshot
  pass로 정상 완료한다. Local Spot만 capacity drop된 `SA-E2E-13.e`도 정상 완료하며 local dropped count를
  public monitoring에 만들지 않는다. Commit된 operation은 snapshot pass를 정확히 한 번 끝내며 각 snapshot member에는
  admission을 최대 한 번 시도한다. Commit 뒤 signal은 정상 완료를 cancellation 또는 runtime shutdown
  예외로 바꾸지 않는다.
- **공통 evidence:** Case마다 public invocation, executor direct handoff, transport attempt, commit count,
  snapshot pass count와 target별 admission attempt count를 검증 build 내부 evidence로 분리해 남긴다.
  MeshNode snapshot의 multicast field, publish target count가 있는 runtime event·message-flow event와
  `zlink.mesh_node.multicast.*` metric은 모두 부재함을 확인한다. `SA-E2E-13.b`는 snapshot member ID,
  snapshot 기록 시각, 각 pipe 종료 시각과 barrier 해제 시각을 남긴다. `SA-E2E-13.d`는 process-local
  index entry와 ready match 0을 함께 기록한다.

### SA-E2E-14 — Classic fanout subscriber 0

- **목적:** Classic fanout publisher의 local queue admission과 현재 subscriber 수를 섞지 않는지 확인한다.
- **Topology·사전 조건:** Publisher route는 ready지만 subscriber process는 시작하지 않는다. Publisher receiver
  gate는 열고 outbound queue는 비운다.
- **절차:** Fanout message 하나를 submit하고 publisher의 결과 데이터 없는 정상 완료를 확인한다. 이후 subscriber를
  시작해도 같은 message를 다시 publish하지 않는다.
- **기대 결과:** Subscriber 0이어도 local PUB queue가 수락하면 반환 데이터 없이 정상 완료한다. Late subscriber delivery와 replay는
  0이다.
- **공통 evidence:** Publisher terminal kind, subscriber snapshot 0, transport attempt와 commit 각각 1회,
  late subscriber handler count 0을 기록한다.

### SA-E2E-15 — Bound session·session Actor relay deadline과 replay 금지

- **목적:** Bound session과 session Actor relay target이 local에서 remote로 바뀌어도 Framework가 같은 family
  deadline을 적용하고 이미 수락한 message를 replay하지 않는지 확인한다.
- **Topology·사전 조건:** 같은 logical session을 local gateway에 bind한 case와 remote gateway로 이전한 case를
  준비한다. Remote 경로에는 `ReceiverGate`, local Spot·Actor·bound relay mailbox에는 `LocalMailboxGate`를
  사용한다.
- **절차:** 다음 하위 case를 실행한다.
  - `SA-E2E-15.a`: Bound session send의 local·remote 경로에서 pending 뒤 capacity signal 수락과 deadline 만료를
    각각 실행한다. Local relay 수락 직후 remote connection을 끊는 case도 실행한다.
  - `SA-E2E-15.b`: Session Actor relay의 local·remote 경로에서 즉시 정상 완료, capacity 대기 뒤 정상 완료,
    timeout 예외를 만들고 각 언어의 결과 없는 비동기 terminal을 확인한다.
- **기대 결과:** 두 family의 local·remote 경로는 같은 deadline source와 결과 없는 비동기 완료 계약을 사용한다.
  Local relay가 수락한 뒤 발생한 remote failure는 같은 submit을 되돌리거나 replay하지 않으며 session delivery
  count는 최대 1이다.
- **공통 evidence:** Family, bind owner·generation, route kind, deadline source, local relay admission, remote
  delivery와 replay count를 기록한다.

### SA-E2E-16 — Server STREAM send ordering

- **목적:** Framework server의 STREAM session send가 socket deadline을 지키면서 수락된 sequence 순서를
  유지하는지 확인한다.
- **Topology·사전 조건:** `StreamPeer`를 server STREAM session에 연결하고 peer 방향 receiver gate를 닫는다.
  STREAM socket HWM과 pending capacity는 §3.1 값이다.
- **절차:** Sequence를 가진 send를 제출해 pending을 만든 뒤 gate를 열고, 이어지는 sequence를 제출한다. 별도
  case는 gate를 deadline까지 닫는다.
- **기대 결과:** 수락된 send는 반환 데이터 없이 정상 완료하고, 닫힌 case는 timeout 예외다. Peer wire sequence는 수락 순서와 같고
  timeout operation은 wire에 나타나지 않는다.
- **공통 evidence:** Session ID, socket deadline, operation sequence·terminal, peer wire capture sequence를 남긴다.
  Connector send completion은 증거로 사용하지 않는다.

### SA-E2E-17 — Server STREAM reply token exactly-once

- **목적:** Request에서 받은 reply token을 유효한 첫 terminator invocation이 transport attempt 전에 원자적으로
  소비하며 admission 실패나 동시 호출 뒤에도 다시 사용하지 않는지 확인한다.
- **Topology·사전 조건:** `StreamPeer`가 서로 다른 request sequence를 보내고 server reply call의 receiver gate를
  닫을 수 있어야 한다. Send packet에는 reply token이 없음을 함께 검증한다.
- **절차:** 다음 하위 case를 실행한다.
  - `SA-E2E-17.a`: 첫 request는 정상 reply submit 뒤 같은 token을 다시 사용한다.
  - `SA-E2E-17.b`: 서로 다른 request token으로 pending capacity 대기 뒤 timeout, gate가 닫힌 timeout,
    cancellation 지원 언어의 cancelled awaitable을 각각 만든다. 각 terminal 뒤 같은 token을 다시
    사용하고 gate를 연다.
  - `SA-E2E-17.c`: 다른 runtime에서 만든 handle이나 이미 dispose한 handle로 call을 만들고 submit한다. 별도
    유효 call object에서는 terminator를 두 번 호출한다.
  - `SA-E2E-17.d`: 같은 token에서 reply call object 두 개를 만든다. 두 terminator를 같은 barrier에서 해제해
    atomic claim을 경쟁시킨다.
- **기대 결과:** 유효한 첫 terminator invocation은 transport attempt 전에 token을 claim하고 소비한다. 첫 정상
  reply만 반환 데이터 없이 정상 완료하며 duplicate와 token 없는 send packet reply는 invalid-state
  exceptional completion이다. Timeout과 cancellation도 token을 소비하며 이후 같은 token의 invocation은 exceptional
  completion이고 late wire reply는 0이다. 잘못된 handle은 token claim 전 local exceptional completion이며
  transport attempt와 commit이 0이다. Duplicate terminator의 첫 invocation 결과는 유지되고 두 번째 invocation만
  exceptional completion이다. 같은 token의 두 call object가 경쟁할 때도 winner 하나만 admission을 진행한다.
  Loser는 exceptional completion이며 transport attempt와 commit이 모두 0이다.
- **공통 evidence:** Request sequence, token consume count, public·invalid invocation count, exceptional type,
  call object ID별 transport attempt·commit·terminal count와 peer reply wire sequence를 기록한다. Concurrent
  case는 claim winner ID와 loser의 attempt·commit 0을 함께 남긴다.

### SA-E2E-18 — direct logical target fence와 select-one commit 전 재선택

- **목적:** Direct call은 Node의 `(MeshName, NodeRid)`, global Spot ID, global Actor ID와 session binding token을
  유지하고,
  ChannelName select-one은 admission되기 전까지 현재 eligible member를 다시 선택할 수 있음을 구분해
  확인한다. 두 경로 모두 commit 뒤
  같은 operation을 replay하지 않아야 한다.
- **Topology·사전 조건:** 다음 하위 case를 독립 topology로 실행한다.
  - `SA-E2E-18.a`: Node direct, Spot, Actor와 bound session별로 `ReceiverGate`를 닫아 pending operation을
    만든다. Node는 `(MeshName, NodeRid)`, Spot은 global Spot ID, Actor는 global Actor ID, bound session은
    binding token을 public identity로 고정한다. Spot·Actor의 resolved generation과 owner fence는 내부 admission
    evidence로만 기록하고 해당 current connection을 종료할 수 있어야 한다.
  - `SA-E2E-18.b`: RouteMesh select-one을 검증한다. 같은 ChannelName에 RouteMesh member A와 B를 준비한다.
    첫 attempt 시점에는 A만 eligible하고 A의
    receiver gate를 닫아 `EAGAIN`을 만든다. Commit 전에 B를 ready·eligible로 바꾸고 A를 eligible set에서
    제외할 수 있어야 한다.
  - `SA-E2E-18.c`: ClientServer select-one을 검증한다. 같은 ChannelName에 ClientServer server A와 B를
    준비한다. 첫 attempt 시점에는 A만 eligible하고 A의 receiver gate를 닫아 `EAGAIN`을 만든다. Commit 전에
    B를 ready·eligible로 바꾸고 A를 eligible set에서 제외할 수 있어야 한다.
- **절차:** `SA-E2E-18.a`는 pending marker 뒤 original route를 종료하고 peer lifecycle signal이 waiter를 깨운
  뒤 같은 logical identity로 재시도해 `RouteNotConnected`를 확인한다. 그 뒤 같은 identity의 route를
  새 physical connection으로 복구하고 application이 다른 operation
  ID로 한 번 submit한다. `SA-E2E-18.b`와 `SA-E2E-18.c`는 각각 A에서 최초 attempt가 실패한 뒤 B를 eligible로
  전환한다. Runtime이 관측한 route·capacity signal로 같은 operation을 한 번 재시도하게 하고, successful
  admission 뒤 B의 connection을 종료·복구해도 같은 operation을 다시 제출하지 않는지 확인한다.
- **기대 결과:** Direct logical target의 기존 operation은 `RouteNotConnected` Framework 예외로 한 번 완료되고 commit과 handler delivery는
  0이다. Connection 복구가 기존 operation을 자동 제출하지 않는다. 새 operation만 복구된 route에서 반환
  데이터 없이 정상
  완료된다. RouteMesh와 ClientServer의 각 select-one
  operation은 public invocation 1회 안에서 A에 대한 최초 attempt와 B에 대한 commit 전 retry를 수행한다.
  Successful admission이 B를 최종 target으로 확정하고 반환 데이터 없이 정상 완료한다. Commit은 1회이고
  B의 handler count는
  최대 1이다. 어느 하위 case도 commit된 payload를 route 복구나 member 변경 뒤 replay하지 않는다.
- **공통 evidence:** Direct case는 old·new operation ID, Node `(MeshName, NodeRid)`·global Spot ID·global Actor ID·session binding token,
  resolve에서 고정한 Spot·Actor generation·owner fence와 old·new physical connection generation을 남긴다. 두 select-one case는 family 이름과 attempt별 eligible
  snapshot·selected member·connection generation, successful admission target과 commit 시각을 각각 남긴다.
  모든 case는 public invocation·transport attempt·commit,
  terminal count와 target별 sequence count를 구분해 기록한다.

### SA-E2E-19 — terminal 뒤 route 복구와 재제출 금지

- **목적:** Timeout 또는 shutdown으로 이미 끝난 operation이 이후 route 복구로 다시 제출되지 않는지 확인한다.
- **Topology·사전 조건:** Receiver gate와 connection gate로 pending operation을 timeout 또는 runtime shutdown
  예외까지
  유지한다.
- **절차:** Terminal marker를 확인한 뒤 같은 target identity로 route를 복구하고 gate를 연다. 정상 operation은
  다른 ID로 한 번 제출한다.
- **기대 결과:** 완료된 operation의 late admission·handler delivery는 0이다. 새 operation만 한 번 처리된다.
- **공통 evidence:** Terminal 전후 connection generation, old operation의 transport attempt·commit·handler
  count, new operation sequence count 1을 남긴다.

### SA-E2E-20 — submit completion과 원격 실행 분리

- **목적:** Local outbound admission 완료를 원격 handler·subscriber 실행 완료로 잘못 기록하지 않는지 확인한다.
- **Topology·사전 조건:** Handler gate를 닫고 Channel, Spot, Actor, classic fanout subscriber와 server STREAM peer가
  payload를 받은 뒤 application 처리를 대기하게 한다. Bound session과 session Actor relay도 같은 handler gate를
  사용한다.
- **절차:** Family별 submit terminal을 먼저 기다리고 handler evidence가 아직 완료되지 않았음을 확인한다. 그 뒤
  handler gate를 열어 application completion을 확인한다.
- **기대 결과:** 비동기 terminal은 반환 데이터 없이 정상 완료하지만 handler completion은 gate-open 뒤에만
  기록된다. Public completion에 remote handler 반환값이나 실행 성공을 넣지 않는다.
- **공통 evidence:** Terminal timestamp가 handler release·completion보다 앞서는지, operation과 handler terminal
  count가 각각 1인지 기록한다.

## 6. 회귀 시나리오

### SA-REG-01 — 제거된 one-way result type 잔여 검사

- **목적:** 다섯 언어의 public declaration·guide·sample·consumer에 제거한 동기 `TrySubmit` 계열과 one-way
  status·result type이 남지 않는지 확인한다.
- **Topology·사전 조건:** 설치 package와 public API snapshot, 모든 server sample·E2E consumer source를 준비한다.
- **절차:** 언어별 exported declaration과 source allowlist를 검사하고 compile-negative fixture에서 제거된 호출이
  compile되지 않는지 확인한다.
- **기대 결과:** Public `TrySubmit`, `trySubmit`, `try_submit`, one-way status·result type과 동기 `void`
  wrapper가 0개다. Session Actor relay도 언어별 비동기 완료형을 사용하고 정상 완료 값은 없다.
- **공통 evidence:** Package version, API snapshot hash, 검색 결과 0과 compile-negative 결과를 남긴다.

### SA-REG-02 — 내부 non-blocking primitive 유지

- **목적:** Public API 단순화가 Core·binding·worker·runtime 내부의 bounded non-blocking primitive를 제거하지
  않았는지 확인한다.
- **Topology·사전 조건:** Owner별 internal allowlist와 기존 primitive contract test를 사용한다.
- **절차:** Allowlist 밖의 public 노출은 실패시키고, allowlist 안의 `DONT_WAIT`·worker queue·runtime 첫 시도
  primitive가 backpressure를 즉시 반환하는 회귀를 실행한다.
- **기대 결과:** Public 잔여는 0이고 필요한 internal primitive는 owner별 test를 통과한다.
- **공통 evidence:** Allowlist hash, owner·symbol 목록, primitive test command와 pass count를 남긴다.

### SA-REG-03 — Kotlin 결과 없는 완료 보존

- **목적:** Kotlin convenience wrapper가 Java runtime의 정상 완료와 실패를 `await(): Unit`으로 정확히
  투영하고 제거된 상태 payload를 다시 노출하지 않는지 확인한다.
- **Topology·사전 조건:** Kotlin compile fixture와 JVM Config 13 역할 server를 사용한다. Receiver gate로
  즉시 정상 완료, capacity 대기 뒤 정상 완료와 timeout 예외 case를 만든다.
- **절차:** `SA-REG-03.a`는 Kotlin wrapper의 `await()`가 `Unit`으로 정상 완료하는 두 success case와 timeout
  예외 case를 실행한다. Java call을 직접 반환하거나 결과 분기를 요구하는 extension이 compile
  surface에 없는지 확인한다.
  `SA-REG-03.b`는 Logical Multicast commit marker 뒤 coroutine을 취소하고 shared JVM stage가 끝날 때까지 runtime
  evidence만 별도로 수집한다.
- **기대 결과:** 두 success case는 `Unit`으로 정상 완료하고 timeout은 예외로 전달된다. Cancellation은
  pending admission cleanup과 연결된다.
  Commit 뒤 취소된 coroutine은 cancellation 상태를 유지하며 성공값을 반환하지 않는다. Shared JVM stage와
  underlying runtime operation은 취소 상태로 바뀌지 않고 반환 데이터 없이 한 번 정상 완료된다.
- **공통 evidence:** Kotlin signature snapshot, compile fixture, coroutine terminal, shared stage result와 JVM
  runtime terminal marker를 서로 다른 필드로 남긴다.

### SA-REG-04 — disposal resource cleanup

- **목적:** Socket disposal이 pending waiter, writable callback과 payload reservation을 한 번만 정리하는지
  확인한다.
- **Topology·사전 조건:** §3.2로 family별 pending operation을 만들고 cleanup counter baseline을 기록한다.
- **절차:** `SA-REG-04.a`는 socket 또는 host를 dispose하고 동시에 send-ready event를 발생시킨다. 같은 dispose를
  다시 호출한 뒤 process 종료까지 resource counter를 확인한다. `SA-REG-04.b`는 잘못된 handle과 duplicate
  terminator의 `SA-E2E-17.c`를 100회 반복하고 각 반복 뒤 resource counter를 확인한다.
- **기대 결과:** Pending operation은 terminal 한 번이고 waiter·reservation·callback count가 모두 0이 된다.
  Invalid invocation과 duplicate terminator도 waiter·reservation·callback을 만들지 않으며 두 번째 transport
  attempt나 commit이 없다. Double free, callback-after-dispose와 native assertion이 없어야 한다.
- **공통 evidence:** Dispose 횟수, terminal kind·count, resource counter baseline·final, process exit code와
  native stderr를 남긴다. Negative 반복 case는 public·invalid invocation count, transport attempt와 commit도
  함께 기록한다.

## 7. 언어별 feature map과 runner inventory

각 언어 feature map은 `SA-E2E-01~20`, `SA-REG-01~04`를 한 행씩 대응시킨다. 구현 전에는 `planned` 또는
구체적 gap으로 표시한다. Runner·assertion·evidence 경로가 없으면 `implemented`로 표시하지 않는다.

| lane | feature map | config runner | 필수 보충 검증 |
|---|---|---|---|
| `.NET` | `framework/languages/dotnet/e2e/SubmitAdmission/feature-map.ko.md` | `framework/languages/dotnet/e2e/SubmitAdmission/run_e2e.sh` | C# API snapshot, cancellation·disposal race |
| Java | `framework/languages/java/e2e/SubmitAdmission/feature-map.ko.md` | `framework/languages/java/e2e/SubmitAdmission/run_e2e.sh` | Java API snapshot, CompletionStage cancel(false)·terminal·cleanup |
| Kotlin | `framework/languages/java/e2e-kotlin/SubmitAdmission/feature-map.ko.md` | JVM runtime runner를 재사용하면 feature map에 해당 경로를 명시 | Coroutine completion·cancellation compile fixture |
| Node.js | `framework/languages/node/e2e/SubmitAdmission/feature-map.ko.md` | `framework/languages/node/e2e/SubmitAdmission/run_e2e.sh` | TypeScript API snapshot, AbortSignal·disposal race |
| C++ | `framework/languages/cpp/e2e/SubmitAdmission/feature-map.ko.md` | `framework/languages/cpp/e2e/SubmitAdmission/run_e2e.sh` | 설치 header compile, task terminal·cleanup |

개별 runner는 다음 selector를 지원한다.

```text
all
SA-E2E-01 ... SA-E2E-20
SA-REG-01 ... SA-REG-04
```

각 runner는 build, 전용 Redis, 역할 server, gate fixture, scenario client, evidence 수집과 process 정리를
담당한다. 선택한 gate fixture가 3초·64개 setup 상한 안에 pending을 만들지 못하면 setup 실패로 끝낸다. Native
abort, semantic assertion과 completion 중복은 retry하지 않는다. Local readiness·route settle·scenario
settle은 [E2E README §2.1](README.ko.md#21-로컬-e2e-대기-기준)의 기본값 안에서 통과해야 하며 timeout을
늘려 완료 처리하지 않는다.

## 8. 완료 조건

- 네 Framework runtime lane과 Java/Kotlin public compile fixture가 `SA-E2E-01~20`, `SA-REG-01~04`를 모두
  통과한다. Java와 Kotlin은 `cancel(false)`가 runtime cleanup에 연결되는 cancellation case를 실행한다. C++만
  cancellation 제외를 public 계약에 따른 `not applicable` evidence로 남긴다.
- Logical Multicast는 direct handoff 실패 시 snapshot pass와 commit이 0이고, direct handoff에 성공한
  operation은 snapshot을 정확히 한 번 처리한다. 각 snapshot member의 admission은 최대 한 번만 시도하며,
  commit 뒤에는 반환 데이터 없이 한 번만 정상 완료된다.
- 모든 일반 pending operation은 timeout·cancellation·shutdown·disposal 뒤 waiter, callback과 payload
  reservation을 남기지 않는다.
- Public API snapshot, exact interface, sample source와 실제 package가 같은 async-only signature를 사용한다.
- Local E2E는 기본 timeout으로 통과하고 timeout 증가, 반복 submit과 늦은 retry를 완료 증거로 사용하지 않는다.
- 종료 뒤 역할 server, client, gate fixture와 Redis process가 남지 않으며 native assertion이 없다.
