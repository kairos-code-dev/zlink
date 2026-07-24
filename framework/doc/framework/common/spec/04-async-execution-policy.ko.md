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
| one-way submit | local outbound admission 결과가 확정되면 비동기 결과를 완료한다. remote handler 완료는 기다리지 않는다 | await하지 않으면 현재 turn을 기다리게 하지 않는다 |
| `Async` | request, join 또는 worker 결과가 terminal 상태가 될 때까지 기다린다 | 완료 continuation이 끝날 때까지 현재 [owner](01-glossary.ko.md#owner) turn을 유지한다 |
| `Yield` | operation을 제출한 뒤 shared Spot turn을 반납하고 terminal 결과를 기다린다 | `SpotWide` User Spot 또는 Instance Spot의 다음 application record를 실행할 수 있으며, 완료 continuation은 같은 Spot queue에 들어가 새 turn에서 재개한다 |

`Yield`는 `SpotWide` User Spot과 Instance Spot에서만 사용할 수 있다. `SpotWide` User Spot에서는 Spot·member
Actor·timer·lifecycle handler가 같은 공통 execution gate를 사용하므로 handler 종류와 관계없이 허용한다.
Entry Spot, `PerActor` User Spot, Entry Spot Actor, Node·Channel handler와 owner turn 밖의 client에서는
사용할 수 없다. 지원하지 않는 문맥의 호출은 operation을 제출하거나 turn을 반납하지 않고
`InvalidConfiguration`으로 완료한다.

`Yield` 뒤에는 같은 Spot의 다른 handler가 상태를 바꿀 수 있으므로 호출자는 대기 전에 읽은 mutable state를
그대로 신뢰하지 않는다. One-way call은 비동기 submit terminator 하나만 제공하고, 즉시 한 번만 시도하는
동기 `TrySubmit` 계열을 제공하지 않는다. 언어별 API는 `submit`, `async`, `yield`, `await`처럼 자연스러운
이름을 사용할 수 있지만 위 세 완료 의미를 섞지 않는다.

`Yield` terminator는 Channel request, Spot request, Actor request, CPU worker와 I/O worker call에만
제공한다. Actor join, Actor·Spot create와 get-or-create, send, publish, timer 등록, close와 destroy에는
제공하지 않는다. 이 operation은 각 call이 정한 submit 또는 `Async` 완료 의미만 사용한다.

### 1.2 Worker offload

CPU 작업과 비동기 I/O 작업은 Framework가 소유한 bounded worker scheduler에 제출할 수 있다. CPU 작업은
동기 함수, I/O 작업은 해당 언어의 비동기 함수로 받으며 둘 다 cancellation 신호를 전달한다. Application은
scheduler thread, queue storage 또는 실행 executor를 선택하지 않는다.

Worker call은 timeout을 설정하고 `Submit` 또는 `Async`로 끝낸다. `SpotWide` User Spot과 Instance Spot의
실행 문맥에서는 `Yield`로도 끝낼 수 있다. Queue가 가득 차면
`WorkerQueueFull`, [deadline](01-glossary.ko.md#deadline)을 넘으면 `WorkerTimedOut`, 작업이 실패하면 `WorkerFailed`로 완료한다. Timeout이나
cancellation 뒤 늦게 끝난 작업은 두 번째 terminal 결과를 만들지 않는다. Worker pool의 최소·최대 동시성,
idle timeout과 queue 상한은 root configuration에서 host start 전에만 바꿀 수 있다.

### 1.3 One-way submit

Send, publish, bound session send, session Actor relay와 명시적인 STREAM send·reply는 비동기 submit 하나로 local outbound
admission 결과를 반환한다. 원격 handler 실행, subscriber 수신 또는 application callback 완료는 기다리지
않는다. 즉시 수락할 수 있으면 Framework scheduler나 별도 work queue에 추가하지 않고 해당 언어의
이미 완료되었거나 resolved된 awaitable을 반환한다. Promise continuation 같은 언어 runtime의 표준
microtask는 Framework scheduler hop으로 계산하지 않는다. Queue 용량이 일시적으로 부족하면
해당 operation family의 유한한 admission deadline까지 send-ready 또는 local capacity signal을 기다린다.
Pending admission을 보관하는 bounded 공간까지 가득 차면 기다림을 시작하지 않고 `Backpressured`로
완료한다.

Global Spot·Actor send도 같은 async-only terminator를 사용한다. Cache된 `Ready` route가 있는 경우에도 별도
동기 terminator를 제공하지 않는다. Caller가 관찰하는 submit 대기는 current [Ready](01-glossary.ko.md#ready) authority resolve부터 source
local outbound admission까지만 포함한다. 그 뒤 remote handler 실행은 submit 완료 조건이 아니다.

유효한 call은 pending 공간을 확인하기 전에 해당 family가 실제로 사용하는 admission primitive를
non-blocking 방식으로 정확히 한 번 호출한다. Remote 경로는 transport submit을 사용하고, local 경로는
mailbox 또는 relay queue admission을 사용한다. 이 첫 시도가 즉시 성공하면 pending 공간이 차 있어도
`Submitted`다. 첫 시도가 transport 또는 local capacity 부족으로 끝났을 때만 waiter 공간을 확인하며,
공간이 없으면 첫 시도 뒤 `Backpressured`로 완료한다. Local 경로가 즉시 수락할 수 있는데 pending 공간만
가득 찼다는 이유로 `Backpressured`를 반환하면 안 된다.

여기서 STREAM은 server package가 handler context에 노출하는 session call을 뜻한다. 별도 stream connector
package의 send builder는 해당 package 계약을 따른다. Request handler가 typed 값을 반환해 만드는 reply,
request·join call, worker submit과 lifecycle submit도 이 one-way call 계약의 대상이 아니다.

유효한 one-way operation은 다음 여섯 상태 중 하나로 완료한다.

| 상태 | 의미 |
|---|---|
| `Submitted` | operation family가 정의한 outbound admission boundary가 operation을 수락했다 |
| `Backpressured` | bounded pending admission 공간까지 가득 차서 기다림을 시작하지 못했다 |
| `TimedOut` | admission deadline까지 queue가 operation을 수락하지 못했다 |
| `TargetNotFound` | 선택할 수 있는 논리 target이 없다 |
| `RouteNotConnected` | target은 확인했지만 사용할 수 있는 route가 없다 |
| `Shutdown` | drain 또는 shutdown 때문에 새 admission을 받을 수 없다 |

잘못된 handle·argument·state, 이미 사용한 reply token과 같은 call의 중복 terminator 실행은 submit status가
아니다. 각 언어의 local exception 또는 exceptional completion으로 처리한다. Timeout, cancellation 또는
[shutdown](01-glossary.ko.md#shutdown) 뒤에는 operation을 자동으로 다시 제출하지 않는다.

Direct pending admission은 Node direct RID, global [Spot](01-glossary.ko.md#spot)·Actor ID와 session binding token처럼 호출자가 지정한
논리 target identity를 유지한다. 물리 peer lifecycle generation은 public commitment가 아니다. Send-ready 또는
peer lifecycle signal 뒤 재시도할 때는 그 identity의 current [authority](01-glossary.ko.md#authority) route만 사용하며 다른 RID·global ID나
[binding token](01-glossary.ko.md#binding-token)으로 전환하지 않는다. Target authority가 없으면 `TargetNotFound`, 재시도 시점에 route가 없으면
`RouteNotConnected`로 한 번 완료한다. Route는 유지되지만 capacity가 deadline까지 회복되지
않은 경우만 `TimedOut`이다.

RouteMesh·ClientServer select-one ChannelName의 첫 capacity rejection은 target commitment가 아니다. Framework
service runtime은 성공한 admission 전까지 같은 [ChannelName](01-glossary.ko.md#channelname)의 현재 eligible member를 다시 선택할 수 있고,
transport queue가 수락한
시점에 target이 확정된다. 수락된 뒤나 terminal 결과가 확정된 뒤에는 어떤 family도 같은
operation을 다시 제출하지 않는다.

Logical Multicast는 partial admission 뒤 전체 publish를 재시도할 수 없으므로 일반 send-ready waiter를
사용하지 않는다. Framework는 대기 queue가 없는 bounded I/O executor direct handoff를 사용한다.
즉시 사용할 worker slot이 없으면 publish transaction을 시작하지 않고 `Backpressured`로 완료한다. Handoff에
성공하면 service runtime이 snapshot의 remote target마다 MeshNode send timeout까지 한 번 제출하고 local Spot
queue는 즉시 수락 여부를 판단한다. Transaction이 시작되면 publish는 commit된 것이다. 이후 cancellation
또는 shutdown으로 이미 수락한 target을 취소하거나 남은 target 제출을 중단하지 않는다.

Executor direct handoff가 즉시 성공하지 못하면 `Backpressured`로 완료한다. Publish transaction이 시작된
뒤 remote target 하나 이상이 용량 때문에 수락되지 않아도 결과는 `Backpressured`이고, 이미 수락한 target은
유지한다. Target별 send timeout 뒤의 capacity rejection도 `TimedOut`으로 다시 분류하지 않는다. [Snapshot](01-glossary.ko.md#snapshot) target이
없으면 `TargetNotFound`를 사용한다. 그 밖의 연결 불가 target은 별도 top-level status로 바꾸지 않고 remote
unreachable detail에 기록한다. Local Spot queue drop도 top-level status를 바꾸지 않는다. 그 밖의 결과는
`Submitted`와 detail로 완료한다. [Logical Multicast](01-glossary.ko.md#logical-multicast)의 `Submitted`는 remote capacity drop 없이 snapshot
처리를 완료했다는 뜻이므로, remote target이 모두 연결 불가여서 admitted count가 0이어도 사용한다.
Publish 결과는 remote와 local 각각의 snapshot, admitted, dropped와 remote
unreachable detail을 보존한다. Remote target이 여러 개면 publish transaction의 전체 시간은 target별 send
timeout의 합까지 늘어날 수 있다.

Classic fanout은 subscriber가 없어도 publisher의 local queue가 event를 수락하면 `Submitted`다. Logical
Multicast의 target별 detail 결과를 [classic fanout](01-glossary.ko.md#classic-fanout) 결과에 추가하지 않는다.

### 1.4 Admission deadline

One-way admission deadline은 operation이 실제로 사용하는 outbound socket 또는 [MeshNode](01-glossary.ko.md#meshnode)가 소유한다.

| Operation family | deadline owner | 기본 규칙 |
|---|---|---|
| [RouteMesh](01-glossary.ko.md#routemesh) node·channel, Spot, Actor | 선택한 MeshNode ROUTER send timeout | global object route resolve 시간을 포함하며 설정이 없으면 1초 |
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
되돌리거나 자동 replay하지 않는다. STREAM reply는 request sequence와 one-shot [reply token](01-glossary.ko.md#reply-token)을 call을
만들 때 보존한다. 유효한 첫 terminator invocation이 transport admission 시도 전에 token을 원자적으로
claim하고 소비한다. 그 terminator가 `Backpressured`, `TimedOut` 또는 cancellation으로 완료되어도 token은
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
않고도 현재 turn을 재개할 수 있다.

Channel request의 target이 다른 RouteMesh 또는 ClientServer Channel이어도 이 규칙은 같다. Framework는
ChannelName으로 선택한 송신 경로의 completion을 원래 Spot activation과 generation에 연결한다. `Async`는
원래 turn을 유지한 채 pending operation의 completion으로 계속 실행한다. `SpotWide` User Spot과 Instance
Spot에서 사용한 `Yield`는 shared Spot turn을 반환한 뒤 completion이 확정되면 원래 Spot queue에 실행 재개
record 하나를 넣는다. Reply payload를 새 Spot packet으로 dispatch하지 않는다.

Reply, timeout, cancellation과 Spot shutdown이 경쟁하면 먼저 확정된 terminal 결과 하나만 사용한다. Spot이
종료되거나 같은 Spot ID로 새 generation이 만들어지면 이전 activation의 늦은 reply를 새 Spot에 전달하지 않는다.
Target 연결 종료나 timeout 뒤 다른 RouteMesh member, ClientServer server 또는 송신 경로로 자동 재전송하지
않는다.

## 3. Handler turn과 claim

Node handler, ChannelName handler, 각 Spot과 각 Actor는 자신에게 적용되는 execution gate의 순서에 따라
application record를 처리한다. `Async`로 기다리는 handler는 완료 continuation이 끝날 때까지 같은 gate의
다음 application record를 실행하지 않는다. `SpotWide` User Spot과 Instance Spot에서 `Yield`로 기다리면
shared Spot turn을 반납하므로 같은 Spot의 다음 record를 실행할 수 있고, 완료 continuation은 같은 Spot
queue에 들어가 새 turn으로 재개한다. Entry Spot Actor와 `PerActor` User Spot의 Actor는 Actor별 gate를
사용하며 `Yield`를 제공하지 않는다. 어느 경우에도 같은 execution gate의 application turn 두 개를 동시에
실행하지 않는다.

`SpotWide` User Spot의 member Actor가 `Yield`하면 User Spot execution gate만 반환한다. 현재 Actor queue
head를 실행할 권한인 Actor queue claim은 continuation이 끝날 때까지 유지한다. 따라서 다른 Actor·Spot
handler·timer는 실행할 수 있지만 같은 Actor queue의 다음 job은 먼저 실행할 수 없다. Continuation은 User
Spot gate를 다시 얻어 현재 job을 끝낸 뒤 Actor queue claim을 해제한다. 같은 Actor 자신에게 보낸 request도
재진입 호출로 바꾸거나 inline으로 실행하지 않는다.

각 언어 service runtime은 application domain과 infrastructure domain을 독립적으로 진행한다. Payload decoding,
user callback과 exception mapping은 application turn에서 처리한다. Completion, send-ready, peer lifecycle,
relocation control과 shutdown barrier는 infrastructure task에서 처리한다. Application handler가 대기 중이어도
infrastructure task를 진행할 수 있어야 한다.

Object placement와 activation도 infrastructure task에서 처리한다. Location Store reservation이 확정한 owner만
[factory](01-glossary.ko.md#factory)를 실행한다. AuthorityOwnerGeneration과 owner lease는 Store와 runtime fencing에만 사용한다.
ObjectGeneration은 public ref와 exact-incarnation mutation·session bind에서도 사용한다. Instance cold
activation은 durable inbox first record를 확정하고 recovery root·cursor를 포함한 `Ready`를 commit한다. Owner
lease에서 계산한 admission deadline을 적용해 first record를 local queue head로 복원한 뒤 Framework activation
barrier를 연다.

Handler가 예외를 반환하면 send handler는 오류 observer와 metric에 기록한다. Request handler는 같은
request의 framework 오류 reply를 생성한다. 오류 observer의 실패는 원래 dispatch 결과를 바꾸지 않는다.

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

Cancellation을 submit status로 추가하지 않는다. Admission을 시작한 뒤 cancellation, timeout, shutdown과
수락이 경쟁하면 원자 terminal state를 먼저 확정한 하나만 결과를 완료한다. 취소된 pending admission은 나중에
수락되면 안 된다. Logical Multicast cancellation은 아래의 direct handoff와 commit 경계를 따른다.

Logical Multicast는 executor direct handoff와 publish transaction 시작이 원자적으로 확정되기 전에만 cancellation이
operation 시작을 막을 수 있다. Publish transaction이 시작된 뒤의 cancellation은 commit된 snapshot
operation을 중단하지 않으며,
underlying operation은 service runtime이 집계한 최종 publish 결과로 완료한다. .NET `ValueTask`와 Node.js `Promise`는
commit 뒤 cancellation 신호로 결과를 바꾸지 않는다. Java stage의 `cancel(false)`와 Kotlin의
연결된 stage cancellation은 `false`를 반환하고 underlying operation을 취소하지 않는다. Kotlin에서는 이미
취소된 caller coroutine이
cancellation 상태를 유지하지만 공유 `CompletionStage`와 runtime operation evidence는 최종
`ZLinkPublishResult`로 완료한다. 이는 operation cancellation이 아니다. Drain·shutdown도 시작된 transaction의
결과를 기다리며, host drain deadline을 넘긴 경우에만 전체 runtime의 bounded force stop 규칙을 따른다.

MeshNode가 `Retiring`으로 전환되면 새 ChannelName 선택과 Logical Multicast target에서 제외된다. Relocation permit을
얻지 못한 unit의 application claim은 계속 진행하고, permit을 얻은 queue turn 경계에서만 해당 unit을 seal한다.
`Draining` 뒤에는 이미 수락한 application record, request completion, Actor relocation과 STREAM barrier만 shutdown
deadline까지 진행한다. Deadline 뒤에는 남은 claim을 revoke하고 대기 중인 operation을 shutdown 결과로 완료한다.

Draining MeshNode는 새 object placement 후보에서도 제외된다. Pending activation은 [drain deadline](01-glossary.ko.md#drain-deadline)과 Framework
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

Spot timer는 service runtime이 current [owner lease](01-glossary.ko.md#owner-lease)와 admission deadline을 확인한 뒤에만 admission할 수
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
