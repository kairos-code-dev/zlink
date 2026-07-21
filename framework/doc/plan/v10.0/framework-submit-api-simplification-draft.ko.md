# Framework one-way submit API 단순화 초안

## 0. 문서 상태와 목적

이 문서는 RouteMesh 10.0.0 공개 전 검토한 설계 결정과 초기 감사를 보존하는 계획 문서이며, 정식 공개 계약
자체가 아니다. 확정한 one-way submit 계약은 `framework/doc/framework/spec/`의 공통 spec과 언어별 exact
interface가 소유하고, 구현 진행 상태와 검증 증거는 execution ledger만 소유한다. 이 문서는 Framework one-way
send·publish call의 동기 `TrySubmit` 계열을 제거하고 비동기 submit 하나로 통일한 이유와 구현 gate를 설명한다.

Core와 bindings의 non-blocking submit primitive는 이 문서의 제거 대상이 아니다. Framework runtime은 bounded
admission을 구현하기 위해 내부에서 이 primitive를 계속 사용할 수 있다.

## 1. 결정

Framework public messaging call에서는 동기 `TrySubmit`, `trySubmit`, `try_submit`을 제거한다. One-way send,
publish와 reply는 언어별 비동기 submit 하나만 제공한다.

- .NET: `SubmitAsync()`
- Java: `submit()`이 반환하는 `CompletionStage`
- Kotlin: Java `submit()`의 coroutine projection
- Node.js: `submit()`이 반환하는 `Promise`
- C++: `submit()`이 반환하는 `task_t`

비동기 submit은 바로 수락되지 않으면 operation family가 소유한 bounded admission deadline까지 기다린다.
Cancellation을 public 인자로 제공하는 언어는 cancellation과 admission 중 먼저 확정된 결과를 한 번만
완료한다. 완료 결과는 local outbound admission만 나타내며 원격 handler 실행이나 one-way application callback
완료를 기다리지 않는다. 즉시 수락할 수 있으면 별도 Framework scheduler hop 없이 이미 완료되었거나
resolved된 awaitable을 반환한다.

## 2. 적용 범위

제거 대상은 Framework가 public call object 또는 public wrapper·extension으로 제공하는 다음 messaging
operation이다. Wrapper가 내부 async admission 결과를 기다리지 않고 버리는 fire-and-forget 표면도 같은 감사
대상이다.

- RouteMesh node·channel·ClientServer one-way send
- Spot과 Actor direct send
- Logical Multicast와 classic fanout publish
- STREAM session send·reply와 bound session send
- session Actor relay
- Entry Spot·Domain Spot·Instance Spot outbound send와 publish

Request handler의 typed 반환 reply, request·join call, worker submit과 builder lifecycle submit은 제거 대상이
아니다. Worker pool의 내부 `TrySubmit`, Core C API, bindings의 `DONT_WAIT` flag와 runtime queue helper는
public Framework messaging call이 아니므로 유지한다. Request는 원래 terminal reply를 기다리는 비동기
call이므로 이 변경으로 완료 의미를 바꾸지 않는다.

STREAM 범위는 framework server package가 handler context에 노출하는 bound session과 session send·reply
call이다. 별도 stream connector package의 send builder는 현재 public `TrySubmit` 제거 범위가 아니므로 이
초안에서 반환 type을 새로 만들지 않는다. Connector의 `void submit`을 비동기 결과로 바꿀 필요가 있으면
stream connector 공통 계약과 언어별 exact interface가 소유하는 별도 설계로 검토한다.

### 2.1 결정 당시 공개 인터페이스 감사

설계를 확정할 때 다섯 언어의 source와 exact interface를 함께 확인한 결과는 다음과 같다. `혼재`는 같은 언어 안에서도
operation family에 따라 동기·비동기와 결과 반환 여부가 다르다는 뜻이다. 정식 spec과 source가 다른 경우에는
구현 완료로 보지 않는다.

| 언어 | 결정 당시 RouteMesh·Spot | 결정 당시 Actor·session·STREAM | 결정 당시 publish | 확인된 문제 |
|---|---|---|---|---|
| .NET | `TrySubmit`과 `SubmitAsync` | `void Submit` 중심 | 공용 call을 재사용 | Actor·session·STREAM 결과가 호출자에게 전달되지 않는다 |
| Java | `trySubmit`과 `CompletionStage`가 혼재 | `void submit` 중심 | classic fanout 전용 표면이 source에 없다 | Kotlin wrapper가 일부 결과를 `Unit`으로 버린다 |
| Kotlin | Java member와 coroutine wrapper가 혼재 | 일부 wrapper가 `Unit` 반환 | Java call을 투영 | member와 같은 이름의 extension은 우선 선택되지 않을 수 있다 |
| Node.js | `trySubmit`과 `Promise` | `void submit` 중심 | 공용 call을 재사용 | MeshNode·Actor의 pending admission과 signal 경합이 구현되지 않았다 |
| C++ | 동기 `submit` 또는 `void submit` | `void submit` 중심 | Logical Multicast만 별도 result | ready task가 실제 비동기 admission을 보장하지 않는다 |

ClientServer는 RouteMesh·Spot과 같은 `SubmitResult` 의미이므로 generic send call을 재사용한다. Classic
fanout은 일부 언어에서 Logical Multicast call을 재사용하지만 결과 의미가 다르므로 전용 fanout call로
분리한다. 사용자가 transport 종류를 선택하거나 내부 socket을 다루게 하지는 않는다.

다음 표는 결정 당시 source와 정식 exact interface의 terminator를 operation family별로 펼친 결과다. 각 칸은
`source / exact interface` 순서다. `Try+async`는 동기 1회 시도와 비동기 결과 반환이 함께 있다는 뜻이고,
`void`는 local admission 결과가 호출자에게 전달되지 않는다는 뜻이다.

| Operation family | .NET | Java | Kotlin wrapper | Node.js | C++ |
|---|---|---|---|---|---|
| RouteMesh node·channel | `Try+async(result)` / 동일 | `void` / `Try+stage(result)` | `Unit` / call 반환 | `Try+Promise(result)` / 동일 | `void` / `Try+sync(result)` |
| ClientServer | RouteMesh call 혼재 / generic async call | generic `void` / `Try+stage(result)` | `Unit` / call 반환 | `Try+Promise(result)` / 동일 | timeout builder `void` / `Try+sync(result)` |
| Spot direct | `Try+async(result)` / 동일 | generic `void` / `Try+stage(result)` | Java call / call 반환 | `Try+Promise(result)` / 동일 | `void` / `Try+sync(result)` |
| Actor direct | `void` / `Try+async(result)` | `void` / `Try+stage(result)` | Java call / call 반환 | `Try+Promise(result)` / 동일 | `void` / `Try+sync(result)` |
| Logical Multicast | `Try+async(detail)` / 동일 | `Try+stage(detail)` / 동일 | Java call / call 반환 | `Try+Promise(detail)` / 동일 | `Try+sync+ready-task(detail)` / `Try+sync(detail)` |
| Classic fanout | logical call 재사용 / 전용 `Try+async(result)` | logical call 재사용 / 전용 `Try+stage(result)` | 결과를 버리는 `Unit` / 전용 call 반환 | logical call 재사용 / 전용 `Try+Promise(result)` | generic call 재사용 / 전용 `Try+sync(result)` |
| Bound session | `void` / `Try+async(result)` | `void` / `Try+stage(result)` | Java call / call 반환 | `void` / `void` | timeout builder `void` / `Try+sync(result)` |
| Session Actor relay | `ValueTask<void>` / `ValueTask<SubmitResult>` | `Stage<Void>` / `Stage<SubmitResult>` | Java stage를 await / `SubmitResult`를 await | `Promise<void>` / `Promise<SubmitResult>` | `void` submit / `task<submit_result>` |
| STREAM session send | `void` / `Try+async(result)` | `void` / 목표 선언 누락 | `Unit` / Java call | `void` / `void` | `void` / `void` |
| STREAM reply | `void` / `Try+async(result)` | `void` / 목표 선언 누락 | Java call / Java call | `void` / `void` | `void` / `void` |

정식 exact interface에는 일부 Java·Kotlin·Node.js·.NET 선언이 source보다 앞서 있거나 서로 다른 fanout call을
규정한 부분이 있다. 이 차이는 현재 기능으로 계산하지 않고 `90-implementation-gap.ko.md`에 기록한다.

결정 당시 공개 call 이름과 source terminator 반환형은 다음과 같다. 괄호 안은 정식 exact interface가 source와
다를 때의 목표 선언이다.

| Operation family | .NET | Java | Kotlin wrapper | Node.js | C++ |
|---|---|---|---|---|---|
| RouteMesh·ClientServer·Spot | `IZLinkSendCall`: sync·`ValueTask<Result>` | `ZLinkSendCall`: `void` (`Stage<Result>`) | `ZLinkClient.send`, `ZLinkRouteClient.send`: `Unit` (call) | `ZLinkSendCall`: sync·`Promise<Result>` | `route_send_call_t` 또는 `send_call_t`: `void` (sync result) |
| Actor | `IZLinkActorSendCall`: `void` (`ValueTask<Result>`) | `ZLinkActorSendCall`: `void` (`Stage<Result>`) | Java call을 사용 | `ZLinkActorSendCall`: sync·`Promise<Result>` | `actor_send_call_t`: `void` (sync result) |
| Logical Multicast | `IZLinkPublishCall`: sync·`ValueTask<Detail>` | `ZLinkPublishCall`: sync·`Stage<Detail>` | Java call을 사용 | `ZLinkPublishCall`: sync·`Promise<Detail>` | `publish_call_t`: sync result·ready task |
| Classic fanout | source `IZLinkPublishCall` (exact `IZLinkFanoutPublishCall`) | source `ZLinkPublishCall` (exact `ZLinkFanoutPublishCall`) | `publishToTopic`: `Unit` (전용 call) | source `ZLinkPublishCall` (exact `ZLinkFanoutPublishCall`) | source `send_call_t` (exact `fanout_publish_call_t`) |
| Bound session | `IZLinkBoundSessionSendCall`: `void` | `ZLinkBoundSessionSendCall`: `void` | Java call을 사용 | `ZLinkBoundSessionSendCall`: `void` | `bound_session_send_call_t`: `void` |
| Session Actor relay | `IZLinkSessionActor.RelayAsync`: `ValueTask<void>` | `ZLinkSessionActor.relay`: `Stage<void>` | Java call을 사용 | `ZLinkSessionActor.relay`: `Promise<void>` | `session_actor_t::relay(const message_t &)`: `task_t<submit_result_t>` |
| STREAM send | `IZLinkSessionSendCall`: `void` | `ZLinkSessionSendCall`: `void` | Java server call을 사용 | `ZLinkSessionSendCall`: `void` | `stream_send_call_t`: `void` |
| STREAM reply | `IZLinkSessionReplyCall`: `void` | `ZLinkSessionReplyCall`: `void` | Java call을 사용 | `ZLinkSessionReplyCall`: `void` | `stream_write_call_t`: `void` |

목표 public call과 terminator는 다음과 같이 고정한다. Metadata와 compress 같은 앞단 builder method는 각
family의 기존 계약을 유지한다. One-way call의 호출별 timeout은 제거하고 실제 outbound socket 또는
MeshNode의 send timeout을 사용한다. Terminator는 표의 한 가지 형태만 제공한다.

| Operation family | .NET | Java | Kotlin | Node.js | C++ |
|---|---|---|---|---|---|
| RouteMesh·ClientServer·Spot | `IZLinkSendCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkSendCall.submit(): Stage<SubmitResult>` | `send(...)`은 `ZLinkSendCall`; `submit().await()` | `ZLinkSendCall.submit(signal): Promise<SubmitResult>` | `route_send_call_t`·`send_call_t.submit(): task<submit_result>` |
| Actor | `IZLinkActorSendCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkActorSendCall.submit(): Stage<SubmitResult>` | Java call의 `submit().await()` | `ZLinkActorSendCall.submit(signal): Promise<SubmitResult>` | `actor_send_call_t.submit(): task<submit_result>` |
| Logical Multicast | `IZLinkPublishCall.SubmitAsync(token): ValueTask<PublishResult>` | `ZLinkPublishCall.submit(): Stage<PublishResult>` | Java call의 `submit().await()` | `ZLinkPublishCall.submit(signal): Promise<PublishResult>` | `publish_call_t.submit(): task<publish_result>` |
| Classic fanout | `IZLinkFanoutPublishCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkFanoutPublishCall.submit(): Stage<SubmitResult>` | Java 전용 call의 `submit().await()` | `ZLinkFanoutPublishCall.submit(signal): Promise<SubmitResult>` | `fanout_publish_call_t.submit(): task<submit_result>` |
| Bound session | `IZLinkBoundSessionSendCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkBoundSessionSendCall.submit(): Stage<SubmitResult>` | Java call의 `submit().await()` | `ZLinkBoundSessionSendCall.submit(signal): Promise<SubmitResult>` | `bound_session_send_call_t.submit(): task<submit_result>` |
| Session Actor relay | `IZLinkSessionActor.RelayAsync(message, token): ValueTask<SubmitResult>` | `ZLinkSessionActor.relay(dispatch, payload): Stage<SubmitResult>` | Java call의 `relay(...).await()` | `ZLinkSessionActor.relay(payload, signal): Promise<SubmitResult>` | `session_actor_t::relay(const message_t &): task_t<submit_result_t>` |
| STREAM send | `IZLinkSessionSendCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkSessionSendCall.submit(): Stage<SubmitResult>` | Java server call의 `submit().await()` | `ZLinkSessionSendCall.submit(signal): Promise<SubmitResult>` | `stream_send_call_t.submit(): task<submit_result>` |
| STREAM reply | `IZLinkSessionReplyCall.SubmitAsync(token): ValueTask<SubmitResult>` | `ZLinkSessionReplyCall.submit(): Stage<SubmitResult>` | Java call의 `submit().await()` | `ZLinkSessionReplyCall.submit(signal): Promise<SubmitResult>` | `stream_write_call_t.submit(): task<submit_result>` |

Kotlin wrapper는 call object를 반환하며 자동으로 terminator를 호출하지 않는다. Java와 C++에는 새 cancellation
인자를 추가하지 않는다. Java는 반환된 `CompletionStage`의 `cancel(false)`를 지원하고 C++은 public
cancellation을 제공하지 않는다. Classic fanout은 topic이 없는 typed event 전용 call을 사용하고 Logical
Multicast detail type을 재사용하지 않는다.

### 2.2 목표 결과와 오류 구분

유효한 one-way operation의 admission 결과는 모든 언어에서 같은 여섯 상태를 사용한다.

| 상태 | 의미 |
|---|---|
| `Submitted` | operation family가 정의한 admission boundary가 operation을 수락했다 |
| `Backpressured` | Family admission capacity가 operation을 수락하지 못했다. 일반 send는 최초 시도 뒤 waiter 공간도 없고, Logical Multicast는 direct handoff가 불가능하거나 remote capacity drop이 발생한 경우다 |
| `TimedOut` | family가 소유한 admission deadline까지 수락되지 않았다 |
| `TargetNotFound` | 선택할 수 있는 논리 target이 없다 |
| `RouteNotConnected` | target은 확인했지만 사용할 수 있는 route가 없다 |
| `Shutdown` | drain·shutdown 때문에 새 admission을 받을 수 없다 |

일반 send family의 admission boundary는 local outbound queue 수락이다. Logical Multicast의 admission boundary는
Core가 고정한 target snapshot의 처리를 완료한 시점이다. `Submitted`는 원격 handler 실행, subscriber 수신 또는
reply 소비를 뜻하지 않는다. 잘못된 인자·handle·상태,
이미 사용한 reply token과 중복 submit은 local programming error이므로 result status로 바꾸지 않고 각 언어의
exceptional completion 규칙을 따른다. Timeout이나 cancellation 뒤에는 runtime이 operation을 자동으로 다시
제출하지 않는다.

Logical Multicast는 Core가 만든 snapshot 하나에서 각 target을 한 번만 submit하고, 이미 성공한 target을
rollback하거나 전체 publish를 다시 실행하지 않는다. Remote target별 snapshot·admitted·dropped·unreachable과
local target의 snapshot·admitted·dropped detail을 그대로 보존한다. Remote target에서 capacity drop이 하나라도
발생하면 Core와 같이 `Backpressured`를 반환한다. Local Spot mailbox의 drop은 detail에만 기록하며 top-level
status를 바꾸지 않는다. Snapshot target이 모두 0이면 `TargetNotFound`다. Snapshot에 remote target이
있지만 모두 unreachable이고 capacity drop이 없어 admitted가 0이면 `Submitted`다. 이 operation은 Core가
target별 send timeout 뒤의
`EAGAIN`도 remote capacity drop인 `Backpressured`로 반환하므로 `TimedOut`으로 다시 분류하지 않는다.

Classic fanout은 subscriber가 0이어도 publisher local queue가 수락하면 `Submitted`이며 Logical Multicast의
detail 결과로 바꾸지 않는다.

Family별 status와 local 오류 경계는 다음과 같다. 모든 family는 runtime이 drain·shutdown 상태면 `Shutdown`을
사용한다.

| Operation family | `TargetNotFound` | `RouteNotConnected` | Capacity와 deadline | Exceptional completion |
|---|---|---|---|---|
| RouteMesh node | target RID가 snapshot에 없다 | 알려진 target pipe가 ready가 아니다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 잘못된 RID·metadata·handle |
| RouteMesh·ClientServer Channel | ChannelName 송신 경로나 선택 가능한 target snapshot이 없다 | 선택한 target의 pipe가 ready가 아니다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 빈·잘못된 ChannelName 또는 metadata |
| Spot·Actor | address resolve가 실패하거나 generation이 stale이다. Stale target도 `TargetNotFound`다 | 확인된 owner route가 ready가 아니다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 형식이 잘못됐거나 다른 runtime의 handle |
| Logical Multicast | snapshot target이 모두 0이다 | 별도 status로 바꾸지 않고 remote unreachable detail에 기록한다 | remote target의 capacity 실패만 `Backpressured`; local Spot drop은 detail에만 기록하고 `TimedOut`은 사용하지 않는다 | 잘못된 ChannelName·topic·metadata·handle |
| Classic fanout | 등록된 local publisher가 없다 | 사용하지 않는다. Subscriber 0도 local queue가 수락하면 `Submitted`다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 빈·잘못된 ChannelName, event 또는 metadata |
| Bound session·session Actor relay | session binding 또는 target Actor가 없다 | 확인된 relay route가 ready가 아니다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 형식이 잘못됐거나 다른 runtime의 session handle |
| STREAM send·reply | session lookup이 실패했다 | 알려진 session transport가 disconnected 상태다 | pending full은 `Backpressured`, deadline은 `TimedOut` | 잘못된 payload·metadata·handle, reply 불가 packet, 중복 reply token |

### 2.3 Admission deadline owner

Logical Multicast를 제외한 `SubmitAsync` 계열은 즉시 수락되면 바로 완료한다. Queue가 일시적으로 가득 차면
writable wakeup을 기다린다. 유효한 call은 pending 공간이 이미 가득 찬 경우에도 해당 family의 remote
transport, local mailbox 또는 local relay queue admission을 먼저 non-blocking 방식으로 한 번 시도한다.
그 시도가 capacity 부족이고 waiter를 기록할 공간도 없을 때
`Backpressured`로 완료한다. Deadline까지 writable admission이 성공하지 않으면 `TimedOut`으로 완료한다.

Logical Multicast는 partial 성공 뒤 전체 operation을 재시도할 수 없으므로 writable waiter나 executor 대기
queue를 사용하지 않는다. Framework는 bounded I/O executor의 worker slot을 즉시 direct handoff한 경우에만
Core blocking publish를 정확히 한 번 호출한다. Worker slot을 바로 얻지 못하면 Core를
시작하거나 commit하지 않고 즉시 `Backpressured`를 반환한다. Core가 시작된 뒤에는 snapshot의 각 remote
target에 MeshNode send timeout을 적용하고 local Spot mailbox는 즉시 판단한다. Remote target이 여러 개면 전체
call 시간은 target별 timeout의 합까지 늘어날 수 있다.

Direct pending operation은 Node direct RID, Spot·Actor handle의 owner와 generation, session binding token으로
표현된 논리 target identity를 유지한다. 물리 peer lifecycle generation은 public commitment가 아니다.
Send-ready 또는 peer lifecycle signal 뒤의 재시도는 같은 identity의 현재 route만 사용하고, 다른
RID·owner·Spot generation·Actor generation·binding token으로 전환하지 않는다. 재시도 시점에 해당
route가 없으면 `RouteNotConnected`로 한 번 완료한다.

RouteMesh와 ClientServer의 select-one ChannelName send는 첫 `EAGAIN`에서 특정 target에 고정되지 않는다.
Framework는 admission deadline 안에서 현재 eligible member를 다시 선택할 수 있다. Core admission이 성공한
시점에 target이 확정되며, 그 뒤에는 연결 상태가 바뀌어도 operation을 다른 member에 제출하거나 replay하지
않는다. 이 규칙은 admission 전의 member 선택과 admission 뒤의 중복 전송 방지를 구분한다.

| Family | Admission deadline owner | 기본값과 추가 규칙 |
|---|---|---|
| RouteMesh node, Spot, Actor | target MeshNode ROUTER send timeout | Node RID 또는 Spot·Actor owner·handle generation을 바꾸지 않는다 |
| RouteMesh Channel | source MeshNode ROUTER send timeout | admission 성공 전까지 eligible member를 다시 선택할 수 있다 |
| ClientServer Channel | client DEALER send timeout | admission 성공 전까지 eligible server를 다시 선택할 수 있다 |
| Logical Multicast | 선택한 MeshNode ROUTER의 target별 send timeout | Core blocking publish를 한 번만 실행하고 `Backpressured`와 partial detail을 유지한다 |
| Classic fanout | publisher socket send timeout | 명시하지 않으면 framework socket send timeout 1초 |
| Bound session | framework socket send timeout | session target generation을 고정하고 disconnect에서 `RouteNotConnected`로 완료한다 |
| Session Actor relay | framework socket send timeout | session과 Actor target generation을 고정하며 local mailbox와 remote relay가 같은 admission 결과를 사용한다 |
| STREAM session send | 해당 STREAM socket send timeout | session target generation과 ordering을 유지하고 disconnect에서 `RouteNotConnected`로 완료한다 |
| STREAM reply | 해당 STREAM socket send timeout | session target generation을 고정하며 caller request timeout을 reply deadline에 사용하지 않는다 |

기본값은 framework가 내부에서 소유하는 1초다. Socket·MeshNode owner builder가 send timeout을 이미 공개하면 그 값을
사용한다. .NET root의 `DefaultSocketSendTimeout`처럼 기존 공개 fallback이 있으면 그 값도 유지하지만, 다른
언어에 같은 root option을 새로 추가하지 않는다. 공개 설정이 없는 family는 내부 기본값 1초를 사용한다.
Framework public send timeout은 millisecond로 올림한 값이 `1..INT_MAX` 범위인 유한 duration만 허용한다.
Millisecond 정수를 받는 언어도 같은 범위를 사용한다. `0`을 즉시 drop option으로 사용하거나 `-1`을 무한
대기로 사용하는 설정, `INT_MAX + 1`, 양의 infinity와 음의 infinity는 startup validation에서 거부한다.
Node.js는 `NaN`과 정수가 아닌 millisecond 값도 거부한다. Core와 bindings의 low-level timeout 값은 이 검증
대상이 아니다. 기존 runtime이 설정값을 socket 또는 MeshNode에 적용하지 않거나 `-1`을 1초로 조용히 바꾸는
동작은 구현 gap이며 목표 계약을 바꾸는 근거가 아니다.

설정하지 않은 `null`, `undefined`와 `nullopt`는 무한 대기가 아니라 해당 family의 기본값 1초를 선택한다.
Builder에서 설정한 잘못된 값은 늦어도 startup에서 거부하고, runtime setter가 있는 경우에는 setter 호출에서
즉시 거부한다. 양수인 sub-millisecond duration은 1ms로 올림하며 잘못된 값을 기본값으로 조용히 바꾸지 않는다.

Bound session은 local relay가 먼저 수락한 뒤 발생한 remote failure를 같은 submit의 실패로 되돌리지 않으며
자동 replay도 하지 않는다. STREAM reply는 argument와 handle을 검증한 다음, 유효한 첫 terminator invocation이
transport attempt 전에 request sequence와 one-shot reply token을 원자적으로 claim하고 소비한다. 이후 결과가
`Backpressured`, `TimedOut` 또는 cancellation이어도 token은 소비된 상태를 유지한다. 같은 token에서 만든 두
call object의 terminator가 경쟁하면 claim winner 하나만 admission을 진행하고 loser는 transport attempt와
commit 없이 exceptional completion으로 끝난다. 늦게 도착한 reply가 client correlation에서 일치하지 않을 수
있다는 사실을 transport admission timeout과 섞지 않는다.

### 2.4 Cancellation 계약

| 언어 | 공개 표현 | 목표 계약 |
|---|---|---|
| .NET | `CancellationToken` | cancelled `ValueTask`; submit status를 새로 만들지 않는다 |
| Node.js | `AbortSignal` | `AbortError`로 reject; submit status를 새로 만들지 않는다 |
| Java | `CompletionStage.toCompletableFuture().cancel(false)` | 첫 admission attempt 뒤 pending cancellation을 runtime cleanup에 연결한다 |
| Kotlin | coroutine cancellation | coroutine이 취소되면 pending admission을 취소하고 late admission을 만들지 않는다 |
| C++ | 별도 token 없음 | `task_t` 반환; 첫 버전에는 추측한 token을 추가하지 않는다 |

Admission, timeout, cancellation과 shutdown이 경쟁하면 runtime의 원자 terminal state winner 하나만 결과를
확정한다. Java는 `submit()`이 반환한 `CompletionStage`의 `toCompletableFuture().cancel(false)`를 public
cancellation 표면으로 지원한다. Kotlin coroutine cancellation도 같은 `cancel(false)` bridge를 사용한다. 두
경로는 future 상태 변경으로만 끝내지 않고 JVM runtime의 pending admission 취소와 cleanup까지 연결한다.
C++은 public cancellation 입력이 없으므로 task를 버렸다는 이유만으로 operation이 취소됐다고 보장하지 않는다.

Call은 먼저 argument·handle·one-shot state를 검증한다. 잘못된 입력은 pre-cancelled token·signal보다 먼저
exceptional completion으로 끝나며 transport attempt는 0이다. 입력이 유효한 상태에서 이미 cancellation이
요청된 .NET·Node.js call은 runtime admission을 시작하지 않고 cancelled awaitable로 끝난다. Java는 stage가
`submit()`에서 반환된 뒤, Kotlin은 Java stage를 await하는 동안 cancellation을 pending cleanup에 연결한다.
JVM public call에는 첫 admission attempt 전에 cancellation을 전달하는 인자가 없으므로 JVM에서 pre-cancel
attempt 0을 보장하지 않는다. Pre-cancellation이 없고 runtime이 이미 shutdown이면 `Shutdown`을 반환한다.
Admission을 시작한 뒤의
cancellation·shutdown·timeout 경쟁은 위의 원자 winner 규칙을 사용한다.

Logical Multicast는 direct handoff와 commit 전에만 cancellation과 shutdown이 operation 시작을 막을 수 있다.
Core blocking publish가 시작되는 순간 snapshot operation은 commit된 것으로 본다. 그 뒤 Java·Kotlin의
`cancel(false)`는 `false`를 반환하고 다른 언어의 cancellation도
이미 성공한 target을 취소하거나 남은 target submit을 중단하지 않으며, underlying operation은 Core가 반환한
최종 `PublishResult`로 완료한다. .NET과 Node.js caller는 이 최종 결과를 받는다. Java caller의 stage도 최종
결과를 유지한다. Kotlin caller coroutine이 이미 취소됐다면 cancellation 상태를 유지하지만 공유 stage와
runtime operation evidence는 최종 결과를 보존한다. Drain·shutdown도 시작된 Core call의 결과를 기다리고,
host drain deadline을 넘긴 경우에만 전체 runtime의 bounded force stop 규칙을 따른다. 따라서 commit 뒤 target
admission은 late admission이 아니라 이미 수락된 한 publish operation의 일부다.

### 2.5 결정 당시 binding capability 감사

| 언어 | 결정 당시 public primitive | 구현 전에 확인한 gate |
|---|---|---|
| .NET | non-blocking submit, send-ready callback, MeshNode send-ready record | callback 수명과 pending cleanup을 socket 수명 하나로 관리하는 contract test |
| Java | Socket·STREAM send-ready handler, MeshNode ready handler와 `SEND_READY` record kind | MeshNode send-ready의 typed destination payload projection을 binding에 추가한다. CompletionStage·Kotlin cancellation도 pending cleanup에 연결한다 |
| Node.js | non-blocking submit, socket send-ready handler, MeshNode send-ready record, 동기 Logical Multicast publish | host dispatcher에서 MeshNode send-ready를 소비한다. Logical Multicast를 event loop 밖에서 한 번만 실행하고 부분 결과를 보존할 비동기 binding publish가 필요한지 검증한다 |
| C++ | `dontwait`, send-ready handler와 `task_t` | handler 수명, pending cleanup과 실제 비동기 task completion을 검증한다 |

Callback 해제를 위한 public binding API가 없으면 framework가 socket 수명 동안 handler를 한 번만 등록하고
socket disposal이 handler와 pending operation을 함께 끝내는 구조를 우선 사용한다. 이 구조로 terminal 1회와
resource cleanup을 증명할 수 없을 때만 binding 보강 lane을 연다. Private API, reflection과 raw frame 우회는
사용하지 않는다.

### 2.6 감사 근거

결정 당시 인터페이스와 구현은 다음 source에서 확인했다. 정식 exact interface와 source가 다른 항목도 이 목록의
비교 결과에 포함한다.

- .NET: `Contracts/Channels/Calls.cs`, `Contracts/Actors/IZLinkActorClient.cs`,
  `Contracts/Streams/BoundSessionContracts.cs`, `Contracts/Streams/IZLinkSession.cs`,
  `Runtime/Messaging/ZLinkAsyncSubmitter.cs`
- Java·Kotlin: `channels/ZLinkSendCall.java`, `channels/ZLinkPublishCall.java`,
  `actors/ZLinkActorSendCall.java`, `streams/ZLinkSessionSendCall.java`,
  `streams/ZLinkSessionReplyCall.java`, `ZLinkFrameworkExtensions.kt`, `ZLinkCoroutineTurnAwait.kt`
- Node.js: `contracts/Channels/Calls.ts`, `contracts/Actors/ZLinkActorClient.ts`,
  `contracts/Streams/IZLinkSession.ts`, `runtime/messaging/index.ts`, `runtime/host/index.ts`
- C++: `contracts/channels/call.hpp`, `contracts/actors/actor.hpp`,
  `runtime/channels/channel_outbound_exchange.cpp`, `runtime/streams/stream_runtime.cpp`
- Binding capability: 각 binding의 public MeshNode·socket·poller contract와 framework binding adapter
- 정식 계약 비교: `framework/doc/framework/spec/04-async-execution-policy.ko.md`,
  `framework/doc/framework/spec/05-framework-api.ko.md`, 각 언어의 server exact interface 문서

## 3. 선택 이유

동기 호출은 target과 local queue가 이미 확정된 경우에만 의미가 분명하다. Location resolve, owner claim,
automatic discovery 또는 reconnect가 필요한 call에서는 즉시 한 번 시도할 target 자체가 아직 없을 수 있다.
Cache 유무에 따라 같은 public method의 의미가 달라지면 호출자가 runtime 내부 상태를 알아야 한다.

`SubmitAsync()` 하나로 통일하면 즉시 수락과 bounded wait를 같은 계약으로 처리할 수 있다. `ValueTask`, 이미
완료된 `CompletionStage`·`Promise` 또는 준비된 C++ task를 사용하므로 일반 send의 즉시 수락 경로가 별도
public method를 요구하지 않는다. Logical Multicast는 중복 전송을 막기 위해 Core blocking publish를 bounded
I/O executor에서 한 번 실행하므로 이 fast path 요구에서 제외한다.

## 4. Best-effort 전송

첫 계약에서는 `timeout = 0`, `BestEffort()` 또는 `DropIfBusy` 같은 public option으로 `TrySubmit`과 같은 기능을
다른 이름으로 다시 제공하지 않는다. 즉시 drop이 필요한 실제 application 시나리오와 필요한 관측 값을 먼저 수집한 뒤,
call마다 transport 세부를 설정하지 않는 별도 admission policy로 설계한다.

Runtime 내부 monitoring과 telemetry가 즉시 drop을 요구하면 internal non-blocking primitive를 사용할 수 있다.
이 내부 선택을 일반 application public contract로 노출하지 않는다.

## 5. Result와 reply 결정

§2.2의 여섯 admission status는 local outbound admission 결과다. Cancellation은 status가 아니며 지원 언어의
cancelled awaitable로 표현한다. Invalid argument·state·handle과 중복 terminator는 exceptional completion이다.
이 구분은 모든 언어에서 같고 언어별 예외 type과 cancellation 표기만 다르다.

Logical Multicast는 Core publish가 시작되기 전 cancellation만 cancelled awaitable로 완료한다. Commit 뒤
cancellation은 §2.4 규칙에 따라 최종 `PublishResult`를 바꾸지 않는다.

Logical Multicast는 partial admission에서도 기존 `PublishResult`의 remote snapshot·admitted·dropped·unreachable과
local snapshot·admitted·dropped detail을 손실하지 않는다. Classic fanout의 subscriber 0과 local publisher
admission 결과를 Logical Multicast detail로 바꾸지 않는다.

STREAM request call의 reply wait는 이 변경 대상이 아니다. 반면 request handler가 명시적으로 만드는 outbound
STREAM reply call은 변경 대상이다. 유효한 첫 terminator invocation은 transport attempt 전에 request sequence와
reply token을 원자적으로 claim하고 소비한다. Local admission이 `Backpressured`·`TimedOut` 또는 cancellation로
끝나도 token은 다시 사용할 수 없다. 같은 token에서 만든 두 call object의 terminator가 경쟁하면 winner 하나만
admission을 진행하고 loser는 transport attempt와 commit 없이 invalid-state exceptional completion으로 끝난다.
Handler의 typed return-value reply는 이 call 표면과 별개이므로 변경하지 않는다.

## 6. 언어별 목표 형태

```csharp
public interface IZLinkSendCall : IZLinkMetadataCall<IZLinkSendCall>
{
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall : IZLinkMetadataCall<IZLinkPublishCall>
{
    ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkFanoutPublishCall
{
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}
```

```java
public interface ZLinkSendCall {
    ZLinkSendCall metadata(String key, String value);
    CompletionStage<ZLinkSubmitResult> submit();
}

public interface ZLinkPublishCall {
    ZLinkPublishCall metadata(String key, String value);
    CompletionStage<ZLinkPublishResult> submit();
}

public interface ZLinkFanoutPublishCall {
    CompletionStage<ZLinkSubmitResult> submit();
}

public interface ZLinkSessionReplyCall {
    CompletionStage<ZLinkSubmitResult> submit();
}
```

```ts
export interface ZLinkSendCall {
    metadata(key: string, value: string): this;
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkPublishCall {
    metadata(key: string, value: string): this;
    submit(signal?: AbortSignal): Promise<ZLinkPublishResult>;
}

export interface ZLinkFanoutPublishCall {
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkSessionReplyCall {
    submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}
```

```cpp
class send_call_t {
public:
    task_t<submit_result_t> submit();
};

class publish_call_t {
public:
    task_t<publish_result_t> submit();
};

class fanout_publish_call_t {
public:
    task_t<submit_result_t> submit();
};

class stream_write_call_t {
public:
    task_t<submit_result_t> submit();
};

class session_actor_t {
public:
    // Relay는 call object를 만들지 않고 admission 결과를 직접 반환한다.
    task_t<submit_result_t> relay(const message_t &message);
};
```

Actor, Spot, bound session, session Actor relay와 fanout 전용 call은 같은 async-only 원칙을 사용한다. Logical
Multicast만 상세 `PublishResult`를 반환하고 나머지 family는 `SubmitResult`를 반환한다. Session Actor relay는
metadata builder나 call object를 거치지 않는다. 위 코드는 방향을 보여주는 예시이며, 언어별 정확한 전체
signature는 구현 전에 각 server package exact interface에 기록한다.

Kotlin convenience wrapper는 내부에서 `submit()`을 호출한 뒤 결과를 `Unit`으로 버리지 않는다. Wrapper는 call
object를 반환하고 coroutine에서는 `call.submit().await()`로 admission 결과를 받는다. Java member와 같은
이름·인자의 suspend extension은 추가하지 않는다.

## 7. 검증 요구

- Logical Multicast 이외의 즉시 수락 가능한 call은 별도 scheduler hop이나 내부 application queue 추가 없이
  완료된다.
- Family별 backpressure가 해소되면 해당 admission deadline 안에 수락되고 local admission 결과를 반환한다.
  Pending 공간이 가득 찬 유효한 call도 non-blocking transport 또는 local mailbox admission을 먼저 한 번
  시도하며, 그 결과가 `EAGAIN` 또는 mailbox-full일 때 `Backpressured`로 완료한다.
- Logical Multicast 이외에는 timeout이 먼저 발생하면 `TimedOut`으로, 지원 언어의 cancellation이 먼저
  발생하면 cancelled awaitable로 한 번만 완료되고 late admission은 0이다. Logical Multicast는 Core call 시작
  전 cancellation만 operation을 취소한다.
- 비동기 submit 완료가 원격 handler 완료로 기록되지 않는다.
- 향후 Location resolve·claim이 필요한 Instance Spot도 새 submit type을 만들지 않고 같은 공통 send call을
  재사용한다. Instance Spot runtime E2E는 이 문서의 선행 gate가 아니라 `S11-10D`가 소유한다.
- 다섯 언어의 public exported declaration, API snapshot, guide, sample과 consumer fixture에 제거된 `TrySubmit` 계열이
  남지 않는다. Core·binding·worker·runtime internal helper는 owner별 allowlist로 구분한다.
- Core·bindings의 non-blocking primitive와 runtime 내부 queue helper는 primitive 회귀와 함께 유지된다.
- Generic local·remote send, Spot, Actor, Logical Multicast partial detail, classic fanout, bound session, STREAM
  send·reply와 session Actor relay를 deterministic HWM·receiver gate 또는 local mailbox gate로 각각 검증한다.
  Timeout을 늘려 통과시키지 않는다.
- STREAM reply는 send packet에서 reply 거부, transport attempt 전 token claim, request reply exactly-once,
  같은 token의 두 call object가 경쟁할 때 loser의 exceptional completion, admission 실패 뒤 late reply 0을
  검증한다.

### 7.1 공통 scenario ID

| ID | 검증 내용 |
|---|---|
| SA-E2E-01 | Logical Multicast 이외의 즉시 수락은 scheduler hop 없이 `Submitted`로 완료한다 |
| SA-E2E-02 | Logical Multicast 이외의 call은 일시적 backpressure 뒤 writable wakeup으로 deadline 전에 수락한다 |
| SA-E2E-03 | pending admission 공간이 가득 찬 유효한 call은 실제 transport·mailbox·relay admission의 최초 시도에서 capacity 부족을 확인한 뒤 `Backpressured`다 |
| SA-E2E-04 | Logical Multicast 이외의 call은 deadline까지 수락되지 않으면 `TimedOut`이고 late admission은 0이다 |
| SA-E2E-05 | target 부재와 route 미연결을 서로 다른 status로 반환한다 |
| SA-E2E-06 | shutdown·drain admission 거부는 terminal `Shutdown` 한 번이다 |
| SA-E2E-07 | .NET·Java·Kotlin·Node cancellation winner는 late admission과 두 번째 completion을 만들지 않으며 Logical Multicast는 Core call 시작 전과 후를 구분한다 |
| SA-E2E-08 | RouteMesh node direct의 local·remote admission이 같은 결과 계약을 사용한다 |
| SA-E2E-09 | RouteMesh ChannelName send가 선택한 MeshNode deadline과 결과 계약을 사용한다 |
| SA-E2E-10 | ClientServer ChannelName send가 client DEALER deadline과 결과 계약을 사용한다 |
| SA-E2E-11 | Spot direct send가 missing·stale target의 `TargetNotFound`와 resolved route admission을 구분한다 |
| SA-E2E-12 | Actor direct send가 stale target과 route admission을 구분한다 |
| SA-E2E-13 | Logical Multicast는 direct handoff 뒤 Core blocking publish를 한 번만 실행한다. Remote capacity drop은 `Backpressured`, snapshot 0은 `TargetNotFound`, all-unreachable·local-only drop은 `Submitted`와 detail을 반환한다 |
| SA-E2E-14 | Classic fanout subscriber 0은 local queue 수락 시 `Submitted`다 |
| SA-E2E-15 | Bound session과 session Actor relay는 local·remote route가 바뀌어도 같은 deadline을 사용하고 replay하지 않는다 |
| SA-E2E-16 | STREAM send가 socket deadline과 ordering을 함께 지킨다 |
| SA-E2E-17 | STREAM reply token은 첫 유효 terminator가 원자적으로 소비하며 실패·경쟁 뒤 다시 사용되지 않는다 |
| SA-E2E-18 | Direct call은 Node RID, Spot·Actor owner와 handle generation, session binding token을 유지하고 route 부재 재시도에서 `RouteNotConnected`로 끝낸다. Select-one ChannelName send는 첫 `EAGAIN` 뒤 admission 성공 전까지 eligible member를 다시 선택할 수 있지만, 성공 뒤에는 다른 member로 replay하지 않는다 |
| SA-E2E-19 | timeout·shutdown 뒤 route가 복구되어도 완료된 operation을 다시 제출하지 않는다 |
| SA-E2E-20 | submit 완료가 원격 handler·subscriber 실행 완료로 기록되지 않는다 |
| SA-REG-01 | public declaration·sample·consumer에는 제거된 `TrySubmit` 계열이 없다 |
| SA-REG-02 | Core·binding·worker·runtime internal non-blocking primitive는 allowlist와 회귀로 유지된다 |
| SA-REG-03 | Kotlin wrapper는 admission 결과를 `Unit`으로 버리지 않는다 |
| SA-REG-04 | socket disposal이 pending waiter·callback·payload reservation을 한 번만 정리한다 |

## 8. 실행 순서

S2-SA-01에서 family×언어 public interface·result·timeout·cancellation과 binding capability 감사를 먼저 닫는다.
그 뒤 정식 공통 async 계약과 다섯 언어 exact interface를 갱신하고, 네 runtime lane에서 public call, 구현,
sample, E2E와 API snapshot을 함께 변경한다. Java와 Kotlin exact interface는 JVM runtime lane이 같이 소유한다.

Binding source가 바뀌지 않으면 binding version이나 Framework 참조 version을 올리지 않고 기존 내부 package로
회귀만 수행한다. `S7-SA-DN/CPP/JVM/NODE`는 각 binding의 public writable wakeup·timeout·cancel·cleanup
capability를 먼저 검증한다. Public binding primitive 보강이 실제로 필요한 언어만 binding patch와 내부 package를
만들고 검증 뒤 version을 올린다. 외부 package registry에는 배포하지 않는다.

진행 상태와 증거는
[`RouteMesh 10.0.0 실행 진행표`](./route-mesh-10.0.0-execution-ledger.ko.md)의 담당 행만 소유한다.
