# Service dispatch

이 문서는 ZLink Framework 11.0 service runtime의 message admission, handler turn, request correlation,
timeout, cancellation과 close progress를 정의한다. Ready record, claim, receive batch와 reply token은 각 언어
runtime 내부에 두며 application public API에 노출하지 않는다.

## 1. 책임 경계

Dispatch runtime은 transport event를 application callback으로 직접 전달하지 않는다. Inbound message를
검증하고 logical owner의 bounded queue에 수락한 뒤 그 owner의 handler turn에서 실행한다. Node direct,
Channel, Spot, Actor와 STREAM session은 같은 공통 규칙을 사용하되 각각 독립된 handler namespace와 owner
queue를 가진다.

Application이 관찰하는 operation은 다음 세 종류다.

| Operation | 완료 의미 |
|---|---|
| One-way submit | Source runtime의 outbound admission 결과가 확정됨. Remote handler 완료는 기다리지 않음 |
| Request | Reply, typed remote failure, timeout, cancellation 또는 shutdown 가운데 terminal 결과 하나가 확정됨 |
| Lifecycle operation | 등록·join·close·termination처럼 해당 operation이 정한 durable 또는 local commit point가 확정됨 |

Wire envelope, operation registry, retry cursor, claim과 reply route는 runtime이 소유한다. Application handler는
endpoint, transport frame, source pipe와 correlation token을 조립하지 않는다.

Channel handler context는 logical ChannelName만 제공한다. Node direct handler context는 caller가 선택한
MeshName과 source node RID를 제공한다. Spot·Actor·Channel handler에 physical MeshName을 공통 속성으로
노출하지 않는다.

## 2. Application과 infrastructure progress

각 logical owner는 application work와 infrastructure work를 독립적으로 진행한다.

| 구분 | 포함하는 work |
|---|---|
| Application | Typed payload decode와 handler, Spot timer, Actor membership callback, STREAM session callback |
| Infrastructure | Request completion, send-ready, peer lifecycle, owner claim, activation, transfer, binding과 termination barrier |

Node handler, Channel handler, 각 Spot과 각 Actor의 application turn은 owner별로 한 번에 하나만 실행한다.
서로 다른 owner는 scheduler가 병렬로 실행할 수 있다. Application handler가 async operation을 기다리는
동안에도 같은 owner의 request completion, send-ready, transfer와 shutdown progress가 진행되어야 한다.

Runtime은 queue가 empty에서 non-empty로 바뀌는 순간을 잃지 않고 owner를 ready index에 등록한다. Worker가
bounded quantum을 처리하고 work가 남으면 다시 ready 상태로 만든다. Callback 해제는 이미 시작한 callback이
끝난 뒤 완료되며 callback 안에서 같은 callback의 해제 또는 owner의 destructive close를 동기적으로 기다리면
deadlock 오류로 끝난다.

## 3. One-way submit

One-way call은 비동기 submit terminator 하나만 제공한다. 즉시 한 번만 시도하는 동기 `TrySubmit` 계열은
제공하지 않는다. 첫 non-blocking admission이 바로 성공하면 별도 Framework queue에 operation을 넣지 않고
이미 완료된 awaitable을 반환할 수 있다.

첫 admission이 일시적인 capacity 부족으로 끝나면 bounded waiter 공간을 확보하고 해당 operation family의
admission deadline까지 send-ready 또는 local capacity signal을 기다린다. Waiter 공간도 가득 차면 기다림을
시작하지 않고 `Backpressured`로 완료한다. Pending 공간이 가득 차 있어도 첫 admission이 즉시 성공하면
`Submitted`다.

One-way operation의 닫힌 결과는 다음과 같다.

| 결과 | 의미 |
|---|---|
| `Submitted` | Operation family가 정의한 source-side outbound admission boundary가 message를 수락함 |
| `Backpressured` | Bounded waiter 공간까지 가득 차 admission 대기를 시작하지 못함 |
| `TimedOut` | Admission deadline까지 source-side boundary가 수락하지 못함 |
| `TargetNotFound` | 선택할 logical target이 없음 |
| `RouteNotConnected` | Logical target은 확인했지만 사용할 route가 없음 |
| `Shutdown` | Admission seal 또는 runtime 종료로 신규 admission을 받을 수 없음 |

Cancellation은 submit 결과 값이 아니다. 언어의 cancellation 표면으로 awaitable을 취소한다. 이미 수락한
one-way message의 전달을 취소하지 않으며 timeout, cancellation 또는 shutdown 뒤 같은 operation을 다른
target에 자동 재제출하지 않는다.

Logical Multicast는 partial admission 뒤 전체 transaction을 다시 시도할 수 없으므로 일반 send-ready waiter를
사용하지 않는다. Bounded I/O executor slot을 direct handoff로 확보하지 못하면 transaction을 시작하지 않고
`Backpressured`로 끝낸다. Transaction이 시작된 뒤에는 snapshot target별 제출을 끝까지 집계하며 cancellation과
shutdown으로 이미 수락한 target을 취소하거나 나머지 target을 중단하지 않는다.

## 4. Request와 terminal completion

Request 하나는 operation identity 하나와 terminal completion 하나를 가진다. Reply, remote failure, timeout,
cancellation, disconnect, owner transfer와 shutdown이 경쟁해도 원자적으로 먼저 확정된 결과 하나만 caller에게
전달한다. 늦게 도착한 reply는 application handler에 다시 전달하지 않고 correlation resource를 정리한다.

Timeout과 cancellation은 caller의 대기를 끝내지만 remote handler가 이미 시작한 업무를 rollback하지 않는다.
Target queue가 request를 수락했는지 증명할 수 없는 timeout·disconnect에는 다른 member, owner 또는 MeshNode로
자동 재제출하지 않는다. Safe retry는 operation별 계약이 pre-admission 미수락을 증명할 때만 같은 logical
identity 안에서 제한적으로 허용한다.

Request handler의 성공 reply와 Framework error reply는 원본 correlation을 사용한다. Application은 reply
route를 저장하거나 source endpoint를 지정하지 않는다. Reply operation은 한 번만 사용할 수 있으며 첫 유효한
terminal invocation이 ownership을 원자적으로 획득한다. Admission 실패나 cancellation 뒤에도 같은 reply를
다시 사용할 수 없다.

Public reply operation을 소비한 뒤의 bounded transport admission은 runtime이 끝까지 소유한다. Application에
같은 reply operation을 반환하거나 재시도를 요구하지 않는다. Reply admission, timeout, disconnect와 shutdown이
경쟁하면 runtime이 원본 operation ID로 terminal transport result 하나를 source에 전달한다. 따라서 public
operation은 한 번만 사용하면서도 reply admission 실패가 source request를 미완료 상태로 남기지 않는다.

## 5. Handler turn과 continuation

Request·join과 worker call은 현재 owner turn을 유지하거나 명시적으로 반납하는 두 대기 의미를 제공할 수
있다.

| 대기 의미 | Owner turn |
|---|---|
| `Async` | Terminal completion의 continuation이 끝날 때까지 현재 owner의 다음 application record를 실행하지 않음 |
| `Yield` | Operation을 제출한 뒤 current turn을 반납하고, completion을 owner queue의 새 turn으로 재개함 |

`Yield` 뒤에는 같은 owner의 다른 handler가 mutable state를 변경할 수 있다. Caller는 대기 전에 읽은 state를
그대로 신뢰하지 않는다. 어느 의미를 사용해도 한 owner에서 application turn 두 개를 동시에 실행하지 않는다.

Handler가 예외를 반환하면 one-way handler는 runtime error observer와 metric에 기록한다. Request handler는
reply route가 유효하면 같은 operation의 Framework error reply로 완료한다. Observer callback 실패는 원래
dispatch 결과를 바꾸지 않는다.

## 6. Message와 callback ownership

Complete application message가 admission 단위다. Multipart 또는 packet의 일부만 queue나 handler에 전달하지
않는다. Metadata는 validation이 끝난 immutable snapshot이며 duplicate key, malformed length, invalid UTF-8와
configured byte limit을 admission 전에 거부한다.

Caller가 submit한 message와 metadata는 caller가 계속 소유한다. Runtime이 async operation에서 더 오래
사용해야 하면 call이 반환되기 전에 안전한 reference나 copy를 확보한다. Handler argument와 callback view는
callback scope 안에서만 유효하며 callback 밖에서 사용할 때는 해당 언어의 public copy·retain 의미를 사용한다.

Runtime 내부 batch는 complete message boundary, record와 payload의 같은 수명을 보존한다. Buffer capacity가
첫 complete message보다 작으면 partial result를 만들지 않는다. Application public API는 batch capacity,
part offset과 receive poll 순서를 설정하지 않는다.

## 7. Backpressure와 limit

Queue는 message count와 byte count를 함께 제한하며 먼저 도달한 한도에서 새 admission을 기다리게 하거나
거부한다. Queue와 waiter limit은 모두 유한하다. Runtime은 unbounded queue를 기본값으로 사용하거나 capacity
부족을 숨기기 위해 timeout을 임의로 늘리지 않는다.

One-way admission deadline은 실제 outbound boundary를 소유한 socket 또는 source MeshNode가 정한다. RouteMesh,
Spot과 Actor의 기본 send timeout은 1초다. Instance Spot address resolve와 target selection 시간도 source-side
deadline에 포함하지만 target factory와 handler 실행 시간은 포함하지 않는다. Request timeout은 resolve,
activation, handler와 reply까지 전체 operation에 적용한다.

Logical Multicast는 remote target별 send timeout을 사용한다. Local Spot queue는 즉시 수락 여부를 판단하며
capacity가 없으면 해당 target의 drop으로 기록한다. Classic fanout의 `Submitted`는 publisher local queue
수락을 뜻하며 subscriber 수신을 보장하지 않는다.

## 8. 동시성, 종료와 오류 우선순위

Send, request와 immutable snapshot query는 여러 thread에서 호출할 수 있다. Lifecycle mutation은 해당 owner의
create·close·transfer와 직렬화한다. 같은 handle의 destructive close와 다른 호출이 경쟁하면 runtime이
entry fence를 획득한 순서로 결과를 확정하며 terminal cleanup 뒤 새 callback을 시작하지 않는다.

Host admission seal 전 수락한 application work와 infrastructure completion은 deadline까지 진행한다. Seal 뒤
새 request는 termination intent에 따라 moving 또는 shutdown 결과로 끝나며 one-way operation은 shutdown
drop으로 관측한다. Deadline 뒤에는 남은 waiter와 operation을 terminal shutdown 결과로 한 번 완료하고
runtime-owned resource를 정리한다.

오류 판단 순서는 다음과 같다.

1. Argument, handle ownership과 one-shot 사용 여부를 검증한다.
2. Runtime과 owner lifecycle state를 검증한다.
3. Logical target과 generation을 확인한다.
4. Route readiness를 확인한다.
5. Queue와 transport admission을 시도한다.

잘못된 argument, stale handle과 중복 terminator는 submit result가 아니라 해당 언어의 local exception 또는
exceptional completion이다. Draining·shutdown과 target·capacity 오류가 함께 있어도 lifecycle seal을 먼저
적용해 신규 application admission을 열지 않는다.

## 9. 검증 요구

- 한 owner의 application turn이 동시에 두 개 실행되지 않는다.
- Application callback이 대기 중이어도 infrastructure completion과 shutdown progress가 진행된다.
- One-way call이 첫 non-blocking admission을 한 번 수행하고 capacity가 부족할 때만 bounded waiter를 사용한다.
- Request reply, timeout, cancellation, transfer와 shutdown 경쟁에서 terminal completion이 하나만 발생한다.
- Complete message와 immutable metadata가 partial payload 없이 전달된다.
- Callback 뒤 stale view와 terminal reply ownership을 다시 사용하지 못한다.
- Target queue 수락 여부가 불명확한 request와 one-way operation을 다른 owner로 자동 재제출하지 않는다.
- Admission seal 뒤 새 application callback과 terminal 뒤 callback이 시작되지 않는다.
- Callback, timer, waiter, poller와 socket close 경쟁 뒤 runtime-owned resource가 남지 않는다.
