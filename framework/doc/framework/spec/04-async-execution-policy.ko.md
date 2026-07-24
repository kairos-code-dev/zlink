# 비동기 실행과 handler turn

[스펙 목차](README.ko.md) · [메시지 계약](03-message-model.ko.md) ·
[Framework API](05-framework-api.ko.md)

이 문서는 ZLink Framework 11.0.0의 submit, request completion, handler 직렬 실행, timeout,
cancellation과 timer 계약을 정의한다. 대상 독자는 언어별 비동기 API와 scheduler adapter를 구현하는
개발자다.

## 1. Operation terminator

### 1.1 Submit, Async와 Yield

Call object는 operation 종류에 맞는 terminator만 제공하며 같은 call에서 terminator를 두 번 실행할 수 없다.
Object create·get-or-create fluent call도 single-use다. 같은 option을 두 번 설정하면 `InvalidConfiguration`,
terminal submit을 두 번 실행하면 `AlreadySubmitted`로 끝난다. Terminal submit을 시작할 때 resolve, durable
request, reservation, factory와 Ready barrier 전체에 적용할 하나의 end-to-end deadline을 고정한다.

| terminator | 수락 뒤 완료 의미 | owner turn |
|---|---|---|
| one-way 비동기 terminal | local outbound admission이 성공하면 반환 데이터 없이 완료하고, 실패하면 예외로 완료한다. Remote handler 완료는 기다리지 않는다 | await하지 않으면 현재 turn을 기다리게 하지 않는다 |
| `Async` | request 또는 worker 결과가 terminal 상태가 될 때까지 기다린다 | 완료 continuation이 끝날 때까지 현재 owner turn을 유지한다 |
| `Yield` | 허용된 Spot 실행 문맥에서 operation을 제출한 뒤 shared Spot execution gate를 반납하고 terminal 결과를 기다린다 | 완료 continuation은 같은 gate를 다시 얻은 뒤 새 turn에서 재개한다 |

`Yield`는 `SpotWide` User Spot과 Instance Spot의 application callback에서만 사용할 수 있다. `SpotWide`
Member Actor는 Actor queue claim과 shared User Spot gate를 함께 얻어 handler를 실행한다. 이 Actor가
`Yield`하면 Actor queue claim은 유지하고 shared User Spot gate만 반납한다. 따라서 같은 Actor의 다음
record는 시작하지 않지만 다른 member Actor, Spot handler, timer와 lifecycle callback은 실행할 수 있다.
완료 continuation은 같은 User Spot gate를 다시 얻고 현재 Actor record를 끝낸 뒤에만 Actor queue claim을
해제한다.

`PerActor` User Spot, Entry Spot과 그 Actor, Node·Channel handler 및 owner callback 밖에서는 `Yield`를
허용하지 않는다. 언어의 공통 call type 때문에 해당 member를 정적으로 숨길 수 없으면 Framework는
operation을 제출하기 전에 현재 execution context를 검사한다. 허용되지 않은 호출은 outbound admission,
worker enqueue, queue 변경과 gate release를 수행하지 않고 `InvalidConfiguration`으로 완료한다.

`Yield` terminator는 Channel request, Spot request, Actor request, CPU worker·I/O worker와
Actor·Spot create·get-or-create call에만 제공한다. Actor join, send, publish, timer 등록, close와
destroy에는 제공하지 않는다. 여러 operation이 공통 request call type을 사용하는 언어에서는 Channel·Spot·Actor
request가 아닌 operation의 `Yield`도 submit 전에 `InvalidConfiguration`으로 거부한다.

`Yield` 뒤에는 같은 gate를 사용하는 다른 handler가 상태를 바꿀 수 있으므로 호출자는 대기 전에 읽은
mutable state를 그대로 신뢰하지 않는다. One-way call은 비동기 submit terminator 하나만 제공하고, 즉시 한 번만 시도하는
동기 `TrySubmit` 계열을 제공하지 않는다. 언어별 API는 `submit`, `async`, `yield`, `await`처럼 자연스러운
이름을 사용할 수 있지만 위 세 완료 의미를 섞지 않는다.

`SpotWide` callback이 `Async`로 같은 User Spot gate가 필요한 Actor 또는 Spot을 기다리면 target handler가
실행될 수 없다. Framework는 target resolve 뒤 outbound admission 전에 이 same-gate wait를
`InvalidConfiguration`으로 거부한다. Member Actor가 자기 Actor에 제출한 awaited request도 Actor queue
claim 때문에 실행될 수 없으므로 `Async`와 `Yield` 모두 같은 방식으로 거부한다. 같은 Actor에 보내는
one-way message는 현재 record 뒤 Actor FIFO에 추가할 수 있다. Framework는 이 경우 handler를 inline으로
호출하거나 재진입 실행으로 바꾸지 않는다.

Actor Join에는 awaited `Async` terminal이 없다. Handler 안에서 `Defer()`로 barrier만 등록하고 handler의
마지막 continuation이 정상 종료한 뒤 Join을 시작한다. Join callback을 inline 또는 재진입 방식으로
실행하지 않는다.

### 1.2 Worker offload

CPU 작업과 비동기 I/O 작업은 Framework가 소유한 bounded worker scheduler에 제출할 수 있다. CPU 작업은
동기 함수, I/O 작업은 해당 언어의 비동기 함수로 받으며 둘 다 cancellation 신호를 전달한다. Application은
scheduler thread, queue storage 또는 실행 executor를 선택하지 않는다.

Worker call은 timeout을 설정하고 언어별 일반 비동기 terminal 또는 허용된 Spot 실행 문맥의 `Yield`로 끝낸다.
Worker가 계산한 application 결과 type은 유지한다. Queue가 가득 차면
`WorkerQueueFull`, deadline을 넘으면 `WorkerTimedOut`, 작업이 실패하면 `WorkerFailed`로 완료한다. Timeout이나
cancellation 뒤 늦게 끝난 작업은 두 번째 terminal 결과를 만들지 않는다. Worker pool의 최소·최대 동시성,
idle timeout과 queue 상한은 root configuration에서 host start 전에만 바꿀 수 있다.

### 1.3 One-way submit

Send, publish, bound session send, session Actor relay와 명시적인 STREAM send·reply는 비동기 terminal
하나만 제공한다. 정상 완료 값은 없으며, operation family가 정의한 source-local admission boundary가
message를 수락했다는 뜻이다. Remote handler 실행, subscriber 수신, remote Spot queue 수락 또는 application
callback 완료는 기다리지 않는다. 즉시 수락할 수 있으면 이미 완료된 awaitable을 반환한다.

Global Spot·Actor send도 같은 async-only terminal을 사용한다. Cache된 `Ready` route가 있어도 별도 동기
terminal을 제공하지 않는다. Caller가 기다리는 범위는 current Ready authority resolve부터 source local
outbound admission까지다. Remote target은 local transport queue, local target은 해당 mailbox 또는 relay
queue, classic fanout과 STREAM은 해당 socket queue가 admission boundary다.

Queue capacity가 일시적으로 부족하면 Framework는 해당 operation family의 유한한 send timeout까지
send-ready 또는 local capacity signal을 기다린다. `Backpressured`는 terminal result나 즉시 발생하는
application exception이 아니다. Capacity가 먼저 확보되면 message를 정확히 한 번 제출하고 정상 완료한다.
Timeout, cancellation 또는 runtime shutdown이 먼저 확정되면 late admission 없이 해당 예외로 한 번
완료한다. Pending waiter를 위한 내부 bounded storage가 필요하더라도 그 포화 상태를 public result로
노출하지 않는다. Waiter storage 안에 등록된 operation은 같은 send timeout과 cancellation 계약을
적용한다. Bounded waiter capacity까지 모두 사용 중이면 Framework는 새 payload를 보관하지 않고
`DeadlineExceeded`로 즉시 완료한다. 이 hard overload boundary에서도 `Backpressured` status를 공개하거나
나중에 message를 제출하지 않는다.

다음 오류는 반환 status가 아니라 Framework exceptional completion으로 전달한다.

| 실패 | 오류 분류 |
|---|---|
| Actor authority 없음 | `ActorRouteNotFound` |
| Spot authority 없음 | `SpotRouteNotFound` |
| Mesh나 선택 가능한 Server 없음 | 기존 `MeshNotFound` 또는 operation별 target-not-found kind |
| 확인한 target으로 사용할 route가 없음 | `RouteNotConnected` |
| admission deadline 만료 | `DeadlineExceeded` |
| runtime이 새 admission을 받지 않음 | `RuntimeShutdown`(`36`) |
| 같은 call의 terminal을 두 번 실행 | `AlreadySubmitted` |

잘못된 argument·state와 이미 사용한 reply token도 exceptional completion이다. Timeout, cancellation,
route 오류 또는 shutdown 뒤에는 operation을 자동으로 다시 제출하지 않는다. Application이 예외를 받아
재시도하면 이전 제출 여부를 증명할 수 없는 family에서는 중복 전달이 발생할 수 있다.

여기서 STREAM은 server package가 handler context에 노출하는 session call을 뜻한다. Request handler가
typed 값을 반환해 만드는 reply, request·join, worker와 lifecycle operation의 application 결과는 제거하지
않는다. 별도 Stream Connector package도 one-way 정상 완료 값은 만들지 않지만, 정확한 call surface는
해당 package spec이 정의한다.

Pending admission은 Node direct RID, global Spot·Actor ID와 session binding token처럼 호출자가 지정한
논리 target identity를 유지한다. 물리 peer lifecycle generation은 public commitment가 아니다. Send-ready 또는
peer lifecycle signal 뒤 재시도할 때는 그 identity의 current authority route만 사용하며 다른 RID·global ID나
binding token으로 전환하지 않는다. Target authority가 없으면 operation별 not-found exception, 재시도
시점에 route가 없으면 `RouteNotConnected`로 한 번 완료한다. Route는 유지되지만 capacity가 deadline까지
회복되지 않으면 `DeadlineExceeded`다.

RouteMesh·ClientServer select-one ChannelName의 첫 capacity rejection은 target commitment가 아니다. Framework
service runtime은 성공한 admission 전까지 같은 ChannelName의 현재 eligible member를 다시 선택할 수 있고,
transport queue가 수락한
시점에 target이 확정된다. 수락된 뒤나 terminal 결과가 확정된 뒤에는 어떤 family도 같은
operation을 다시 제출하지 않는다.

Logical Multicast는 operation을 시작할 때 target snapshot을 고정하고 각 target을 한 번씩 시도한다.
Operation 자체를 local executor에 제출할 수 없으면 send timeout까지 capacity를 기다리며 timeout,
cancellation 또는 shutdown 중 먼저 확정된 예외로 완료한다. Bounded worker와 source-local capacity를
확보해 transaction이 시작되면 public terminal은 반환 데이터 없이 정상 완료하고 target별 제출은 내부에서
계속한다. Transaction이 시작된 뒤에는 일부 target이
이미 message를 수락했을 수 있으므로 개별 target 실패가 전체 publish를 rollback하지 않으며 이미 수락한
target을 취소하거나 전체 publish를 자동 재시도하지 않는다.

Remote target의 source-local transport admission과 local Spot queue admission 결과는 public 반환값이나
publish 전용 monitoring 값으로 만들지 않는다. Target snapshot이 0개여도 정상 완료한다. Transaction 시작
뒤 개별 target 실패도 publish 전체의 exceptional completion으로 바꾸지 않는다. 정상 완료는 target별 제출 완료, 모든 remote
Spot queue 수락이나 subscriber handler 실행을 보장하지 않는다.

Classic fanout은 subscriber가 없어도 publisher의 local queue가 event를 수락하면 정상 완료한다. Subscriber
수와 수신 완료를 public result로 만들지 않는다.

### 1.4 Admission deadline

One-way admission deadline은 operation이 실제로 사용하는 outbound socket 또는 MeshNode가 소유한다.

| Operation family | deadline owner | 기본 규칙 |
|---|---|---|
| RouteMesh node·channel, Spot, Actor | 선택한 MeshNode ROUTER send timeout | global object route resolve 시간을 포함하며 설정이 없으면 1초 |
| ClientServer | client DEALER send timeout | 설정이 없으면 1초 |
| Logical Multicast | 선택한 MeshNode ROUTER의 target별 send timeout | commit된 publish transaction의 각 remote target에 적용한다 |
| classic fanout | publisher socket send timeout | 설정이 없으면 1초 |
| bound session·session Actor relay | Framework socket send timeout | local·remote Actor route가 바뀌어도 같은 deadline을 사용한다 |
| STREAM send·reply | 해당 STREAM socket send timeout | reply에 caller request timeout을 사용하지 않는다 |

Framework public send timeout은 millisecond로 올림한 값이 `1..INT_MAX` 범위인 유한한 duration이어야 한다.
양수인 sub-millisecond 값은 1ms로 올린다. `0`, 음수, 무한대와 상한 초과는 늦어도 host startup에서
거부하며 유효한 기본값으로 바꾸지 않는다. 값이 지정되지 않으면 해당 family의 1초 기본값을 선택한다.
기존 public root fallback이 있으면 같은 의미로 적용하지만, 다른 언어에 같은 root option을 새로 추가해야
한다는 뜻은 아니다. Runtime setter가 있는 경우 잘못된 값은 setter 호출에서 즉시 거부한다.

Bound session과 session Actor relay는 local relay가 수락한 뒤 발생한 remote 실패를 같은 submit의 실패로
되돌리거나 자동 replay하지 않는다. STREAM reply는 request sequence와 one-shot reply token을 call을
만들 때 보존한다. 유효한 첫 terminator invocation이 transport admission 시도 전에 token을 원자적으로
claim하고 소비한다. 그 terminator가 `DeadlineExceeded`, cancellation 또는 runtime shutdown 예외로 완료되어도 token은
다시 사용할 수 없다. 같은 token에서 만든 두 call이 경쟁하면 claim에 성공한 하나만 transport
admission을 시작하고 나머지는 transport 시도 없이 exceptional completion으로 끝난다. Caller request timeout은
reply wire에 전달되지 않으므로 STREAM reply의 admission deadline으로 사용하지 않는다. 늦게 수락된 reply가
client correlation에서 일치하지 않더라도 transport admission 결과를 request 결과로 바꾸지 않는다.

## 2. Request completion

Request는 reply, remote 오류, timeout, cancellation 또는 shutdown 가운데 먼저 확정된 결과로 한 번
완료된다. timeout과 cancellation은 호출자의 대기를 끝내지만 원격 handler가 이미 시작한 업무를
rollback하지 않는다. 늦게 도착한 reply는 application handler에 다시 전달하지 않고 correlation state를
정리한다.

Global object request timeout은 current Ready authority resolve, outbound admission, handler와 reply 전체를
포함한다. Source는 앞 단계에서 사용한 시간을 뺀 잔여 시간만 다음 단계에 전달한다. Remote target의
미수락을 증명하는 receipt가 없으므로 timeout이나 연결 실패 뒤 다른 owner에게 request를 자동 재제출하지
않는다.

같은 handler turn에서 보낸 request를 기다릴 수 있다. reply completion과 send-ready 같은 infrastructure
작업은 application turn과 분리되어 진행되므로 해당 Spot이나 Actor의 다음 application message를 실행하지
않고도 현재 turn을 재개할 수 있다. 다만 `SpotWide` callback이 같은 User Spot gate가 필요한 target을
`Async`로 기다리는 경우와 Actor가 자기 Actor의 awaited request를 기다리는 경우는 §1.1의 교착 방지 규칙으로
submit 전에 거부한다.

Channel request의 target이 다른 RouteMesh 또는 ClientServer Channel이어도 이 규칙은 같다. Framework는
ChannelName으로 선택한 송신 경로의 completion을 원래 Spot activation과 generation에 연결한다. `Async`는
원래 Spot turn을 유지한 채 pending operation의 completion으로 계속 실행하고, `Yield`는 turn을 반환한 뒤
completion이 확정되면 원래 Spot queue에 실행 재개 record 하나를 넣는다. Reply payload를 새 Spot packet으로
dispatch하지 않는다.

Reply, timeout, cancellation과 Spot shutdown이 경쟁하면 먼저 확정된 terminal 결과 하나만 사용한다. Spot이
종료되거나 같은 RID로 새 generation이 만들어지면 이전 activation의 늦은 reply를 새 Spot에 전달하지 않는다.
Target 연결 종료나 timeout 뒤 다른 RouteMesh member, ClientServer server 또는 송신 경로로 자동 재전송하지
않는다.

## 3. Handler turn과 claim

Node handler, ChannelName handler, 각 Spot과 각 Actor는 자기 application queue를 순서대로 처리한다. `Async`로
기다리는 handler는 완료 continuation이 끝날 때까지 현재 claim과 gate를 유지한다. `SpotWide` Member Actor의
Actor claim과 User Spot gate는 서로 다른 직렬성 경계다. `Yield`는 User Spot gate만 반납하며 Actor claim은
유지하므로 같은 Actor의 FIFO와 non-overlap을 바꾸지 않는다. Spot handler·timer·lifecycle 또는 Instance
Spot의 `Yield`는 해당 Spot gate를 반납하고 completion을 같은 gate에 다시 제출한다. Gate를 다시 얻지 못한
application continuation을 completion thread에서 inline으로 실행하면 안 된다.

`PerActor` User Spot에서는 Actor별 queue, Spot direct·lifecycle lane과 timer별 lane을 구분한다. 같은 Actor와
같은 timer의 callback은 각각 순서대로 하나씩 실행하지만 서로 다른 Actor lane, Spot lane과 서로 다른 timer
lane은 동시에 실행할 수 있다. Entry Spot Actor도 Actor별 claim만 사용한다. 이 문맥의 `Async`는 같은
Actor의 다음 record만 기다리게 하며 다른 Actor의 진행을 막지 않는다.

각 언어 service runtime은 application domain과 infrastructure domain을 독립적으로 진행한다. Payload decoding,
user callback과 exception mapping은 application turn에서 처리한다. Completion, send-ready, peer lifecycle,
relocation control과 shutdown barrier는 infrastructure task에서 처리한다. Application handler가 대기 중이어도
infrastructure task를 진행할 수 있어야 한다.

Object placement와 activation도 infrastructure task에서 처리한다. Location Store reservation이 확정한 owner만
factory를 실행한다. AuthorityOwnerGeneration과 owner lease는 Store와 runtime fencing에만 사용한다.
ObjectGeneration은 public ref와 exact-incarnation mutation·session bind에서도 사용한다. Instance cold
activation은 durable inbox first record를 확정하고 recovery root·cursor를 포함한 `Ready`를 commit한다. Owner
lease에서 계산한 admission deadline을 적용해 first record를 local queue head로 복원한 뒤 Framework activation
barrier를 연다.

Handler가 예외를 반환하면 send handler는 오류 observer와 metric에 기록한다. Request handler는 같은
request의 framework 오류 reply를 생성한다. 오류 observer의 실패는 원래 dispatch 결과를 바꾸지 않는다.

### 3.1 Actor Join의 deferred terminal

Actor membership Join의 `Defer()`는 async call naming 규칙의 예외가 아니라 비동기 operation을 시작하지 않는
handler-scoped registration이다. 모든 언어에서 결과 없는 동기 terminal이며 awaitable이나 coroutine을
반환하지 않는다. Handler terminal 뒤 infrastructure scheduler가 barrier를 활성화하고 Join을 실행한다.

`Defer()`와 `Yield`는 서로 다른 기능이다. `Yield`는 현재 `SpotWide` gate를 일시적으로 반납하고 같은 handler
continuation이 gate를 다시 얻도록 한다. `Defer()`는 gate와 Actor claim을 유지하며 handler continuation을
재개할 Join 결과도 만들지 않는다. Handler가 Yield 전이나 Yield continuation에서 Join을 등록할 수 있지만
barrier 활성화·폐기는 최종 handler terminal에서 한 번만 결정한다. Join completion은 원래 handler가 아니라
이동 대상 Actor queue의 lifecycle callback으로 전달한다.

One-way terminal의 single-use 규칙과 Join call의 single-use 규칙은 같은 `AlreadySubmitted` 오류를 사용하지만
완료 경계는 다르다. One-way terminal은 bounded outbound admission을 기다리고, `Defer()`는 local registration
검증까지만 수행한다. `Defer()` hard failure는 I/O나 queue mutation 전에 동기적으로 끝나며 target lookup,
capacity, relocation policy와 callback failure는 handler terminal 뒤 Actor completion으로 전달한다.

## 4. Cancellation과 shutdown

Cancellation은 협력적 요청이다. 이미 완료된 결과를 cancellation으로 바꾸지 않으며, 이미 수락한 one-way
메시지의 전달을 취소하지 않는다. 언어별 표면은 `.NET` `CancellationToken`, Java
`CompletionStage.toCompletableFuture().cancel(false)`, Kotlin coroutine cancellation, Node.js `AbortSignal`을
사용한다. Java Framework가 반환한 stage의 `toCompletableFuture()`는 원본 pending admission의
cancellation과 cleanup에 연결된다. C++ one-way submit은 별도 public cancellation 입력을 제공하지
않는다. C++ task를 사용하지 않거나 Java stage를 단순히 보관하지 않는 것만으로 operation이
취소됐다고 보장하지 않는다.

Call은 argument, handle과 one-shot state를 먼저 검증한다. `.NET`의 pre-cancelled `CancellationToken`과
Node.js의 이미 abort된 `AbortSignal`은 유효한 call의 runtime admission을 시작하지 않고 해당 언어의
cancelled awaitable로 완료한다. Java와 Kotlin의 submit에는 cancellation 입력이 없다. 유효한 일반 JVM call은
첫 non-blocking admission 시도를 마친 뒤 stage를 caller에게 반환하므로, caller가 stage를 받은 뒤 실행하는
Java `cancel(false)`나 그 stage를 기다리는 Kotlin coroutine cancellation은 첫 시도를 취소할 수 없다. Operation이
pending 상태이면 이 cancellation이 이후 admission과 경쟁하고 send-ready waiter, queue reservation과 payload
reservation을 정리한다. 따라서 JVM 경로는 pre-cancellation에 따른 transport attempt 0을 보장하지 않는다.

Cancellation은 exceptional completion이다. Admission을 시작한 뒤 cancellation, timeout, shutdown과
수락이 경쟁하면 원자 terminal state를 먼저 확정한 하나만 call을 완료한다. 취소된 pending admission은 나중에
수락되면 안 된다. Logical Multicast cancellation은 아래의 direct handoff와 commit 경계를 따른다.

Logical Multicast는 executor direct handoff와 publish transaction 시작이 원자적으로 확정되기 전에만 cancellation이
operation 시작을 막을 수 있다. Publish transaction이 시작된 뒤의 cancellation은 commit된 snapshot
operation을 중단하지 않으며 target별 결과를 반환하거나 publish 전용 monitoring 값으로 만들지 않는다.
.NET `ValueTask`와 Node.js `Promise`는 commit 뒤 cancellation 신호로 완료를 바꾸지 않는다.
Java stage의 `cancel(false)`와 Kotlin의
연결된 stage cancellation은 `false`를 반환하고 underlying operation을 취소하지 않는다. Kotlin에서는 이미
취소된 caller coroutine이
cancellation 상태를 유지하지만 공유 `CompletionStage`와 runtime operation evidence는 최종
normal completion과 monitoring event를 기록한다. 이는 operation cancellation이 아니다. Drain·shutdown도
시작된 transaction의 완료를 기다리며, host drain deadline을 넘긴 경우에만 전체 runtime의 bounded force
stop 규칙을 따른다.

MeshNode가 `Retiring`으로 전환되면 새 ChannelName 선택과 Logical Multicast target에서 제외된다. Relocation permit을
얻지 못한 unit의 application claim은 계속 진행하고, permit을 얻은 queue turn 경계에서만 해당 unit을 seal한다.
`Draining` 뒤에는 이미 수락한 application record, request completion, Actor relocation과 STREAM barrier만 shutdown
deadline까지 진행한다. Deadline 뒤에는 남은 claim을 revoke하고 대기 중인 operation을 shutdown 결과로 완료한다.

Draining MeshNode는 새 object placement 후보에서도 제외된다. Pending activation은 drain deadline과 Framework
activation deadline 가운데 먼저 도달한 경계에서 request를 한 번 terminal 완료하고 one-way payload를 drop 처리한다.
Cancellation, timeout, shutdown과 activation barrier 개방이 경쟁해도 pending operation과 payload reservation을
한 번만 정리한다.

## 5. Spot timer

Spot timer는 네트워크 record와 같은 Spot application turn에서 callback을 실행한다. 각 언어 service runtime은
platform timer의 만료를 Spot queue record로 바꾸며, backend와 관계없이 아래 의미를 유지한다.

같은 timer key를 다시 등록하면 generation이 증가한다. queue에 이미 추가된 이전 generation의 record는
callback을 실행하지 않는다. cancel은 해당 generation 이후 callback의 시작을 막는다. 이미 시작한 callback은
강제로 중단하지 않는다. 반복 timer가 handler 실행보다 빠르게 만료되어도 같은 key의 callback을 동시에
실행하지 않으며, 중복 만료를 한 번의 pending record로 합칠 수 있다.

Spot timer는 service runtime이 current owner lease와 admission deadline을 확인한 뒤에만 admission할 수
있다. Lease 갱신이 멈추어 monotonic deadline을 넘으면 Framework process가 일시 정지된 상태였더라도 재개 후
새 tick을 queue에 넣거나 callback을 시작하지 않는다. 이전 object·owner authority의 pending tick도
실행하지 않는다.

고빈도 timer도 관리형 언어에서 native callback 경계를 매 tick마다 왕복하지 않는다. Platform timer가
Framework scheduler에 wakeup 신호를 보내면 scheduler가 만료 record를 batch로 처리한다.

## 6. 언어별 표현

공통 계약은 특정 async type 이름을 강제하지 않는다. 완료 순서, cancellation과 오류 의미는 이 문서가
소유하며, 각 언어의 정확한 반환 type과 오류 표현은 다음 exact interface가 소유한다.

| 언어 | exact interface owner |
|---|---|
| `.NET` | [exact interface 목차](server/languages/dotnet/interfaces/README.ko.md) |
| Java | [Channel messaging](server/languages/java/interfaces/channel-messaging.ko.md) |
| Kotlin | [Channel messaging](server/languages/kotlin/interfaces/channel-messaging.ko.md) |
| Node.js | [인터페이스 목차](server/languages/node/interfaces/README.ko.md) |
| C++ | [framework 인터페이스](server/languages/cpp/interfaces/README.ko.md) |

각 exact interface는 terminator별 return type, cancellation 인자, callback 또는 coroutine 표현을 고정한다.
언어 표준 표현이 달라도 같은 operation의 완료 시점, ordering과 오류 분류는 달라지지 않는다.
