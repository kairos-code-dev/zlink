# 비동기 실행과 handler turn

[스펙 목차](README.ko.md) · [메시지 계약](03-message-model.ko.md) ·
[Framework API](05-framework-api.ko.md)

이 문서는 ZLink Framework 10.0.0의 submit, request completion, handler 직렬 실행, timeout,
cancellation과 timer 계약을 정의한다. 대상 독자는 언어별 비동기 API와 scheduler adapter를 구현하는
개발자다.

## 1. Operation terminator

### 1.1 Submit, Async와 Yield

Call object는 operation 종류에 맞는 terminator만 제공하며 같은 call에서 terminator를 두 번 실행할 수 없다.

| terminator | 수락 뒤 완료 의미 | owner turn |
|---|---|---|
| `Submit` | one-way operation을 수락하면 즉시 반환한다. remote handler 완료는 기다리지 않는다 | 현재 turn을 기다리게 하지 않는다 |
| `Async` | request, join 또는 worker 결과가 terminal 상태가 될 때까지 기다린다 | 완료 continuation이 끝날 때까지 현재 owner turn을 유지한다 |
| `Yield` | operation을 제출한 뒤 현재 owner turn을 반납하고 terminal 결과를 기다린다 | 같은 owner의 다음 application record를 실행할 수 있으며, 완료 continuation은 owner queue에 다시 들어가 새 turn에서 재개한다 |

`Yield` 뒤에는 같은 owner의 다른 handler가 상태를 바꿀 수 있으므로 호출자는 대기 전에 읽은 mutable state를
그대로 신뢰하지 않는다. 언어별 API는 `submit`, `async`, `yield`, `await`처럼 자연스러운 이름을 사용할 수
있지만 위 세 완료 의미를 섞지 않는다.

### 1.2 Worker offload

CPU 작업과 비동기 I/O 작업은 Framework가 소유한 bounded worker scheduler에 제출할 수 있다. CPU 작업은
동기 함수, I/O 작업은 해당 언어의 비동기 함수로 받으며 둘 다 cancellation 신호를 전달한다. Application은
scheduler thread, queue storage 또는 실행 executor를 선택하지 않는다.

Worker call은 timeout을 설정하고 `Submit`, `Async` 또는 `Yield`로 끝낸다. Queue가 가득 차면
`WorkerQueueFull`, deadline을 넘으면 `WorkerTimedOut`, 작업이 실패하면 `WorkerFailed`로 완료한다. Timeout이나
cancellation 뒤 늦게 끝난 작업은 두 번째 terminal 결과를 만들지 않는다. Worker pool의 최소·최대 동시성,
idle timeout과 queue 상한은 root configuration에서 host start 전에만 바꿀 수 있다.

### 1.3 One-way submit

Send와 publish는 Framework가 소유한 bounded queue의 수락 결과를 반환한다. 원격 handler의 실행 완료를
나타내는 awaitable 결과는 제공하지 않는다. 유효한 operation을 수락할 수 없으면 backpressure, 대상 없음
또는 shutdown을 구분해 반환한다. 잘못된 handle·argument·state는 submit 결과가 아니라 언어별 local call
오류로 처리한다.

`NoDrop`은 언어별 Framework API가 Core `ZLINK_MESH_PUBLISH_OPT_NODROP`의 0 또는 1을 boolean으로
표현한 이름이다. 두 표기는 같은 정책이며 별도 전달 모드를 뜻하지 않는다.

Logical Multicast의 기본 `NoDrop = true`는 local matching queue와 모든 remote target에 대한 수락을 하나의
operation으로 처리한다. blocking submit은 MeshNode send timeout까지 backpressure 해소를 기다린다.
non-blocking submit은 기다리지 않고 backpressure 결과를 반환한다. `NoDrop = false`는 수락할 수 없는
대상을 제외하고 나머지 대상에 전달할 수 있다.

## 2. Request completion

Request는 reply, remote 오류, timeout, cancellation 또는 shutdown 가운데 먼저 확정된 결과로 한 번
완료된다. timeout과 cancellation은 호출자의 대기를 끝내지만 원격 handler가 이미 시작한 업무를
rollback하지 않는다. 늦게 도착한 reply는 application handler에 다시 전달하지 않고 correlation state를
정리한다.

같은 handler turn에서 보낸 request를 기다릴 수 있다. reply completion과 send-ready 같은 infrastructure
작업은 application turn과 분리되어 진행되므로 해당 Spot이나 Actor의 다음 application message를 실행하지
않고도 현재 turn을 재개할 수 있다.

## 3. Handler turn과 claim

Node handler, ChannelName handler, 각 Spot과 각 Actor는 자기 application queue를 순서대로 처리한다. `Async`로
기다리는 handler는 완료 continuation이 끝날 때까지 같은 owner의 다음 application record를 실행하지 않는다.
`Yield`로 기다리면 현재 turn을 반납하므로 같은 owner의 다음 record를 실행할 수 있고, 완료 continuation은
owner queue에 들어가 새 turn으로 재개한다. 어느 경우에도 한 owner의 application turn 두 개를 동시에
실행하지 않는다. 서로 다른 owner의 handler는 scheduler가 병렬로 실행할 수 있다.

Core ready callback은 payload를 실행하지 않고 처리할 domain이 준비되었음을 알린다. Framework scheduler는
application domain과 infrastructure domain을 별도 claim으로 가져온다. payload decoding, user callback과
exception mapping은 application claim에서 처리한다. completion, send-ready, peer lifecycle, transfer control과
shutdown barrier는 infrastructure claim에서 처리한다.

Handler가 예외를 반환하면 send handler는 오류 observer와 metric에 기록한다. Request handler는 같은
request의 framework 오류 reply를 생성한다. 오류 observer의 실패는 원래 dispatch 결과를 바꾸지 않는다.

## 4. Cancellation과 shutdown

Cancellation은 협력적 요청이다. 이미 완료된 결과를 cancellation으로 바꾸지 않으며, 이미 수락한 one-way
메시지의 전달을 취소하지 않는다. 언어별 표면은 `CancellationToken`, coroutine cancellation,
`AbortSignal`과 같이 해당 언어의 표준 표현을 사용한다.

MeshNode가 drain을 시작하면 새 ChannelName 선택과 Logical Multicast target에서 제외된다. 이미 수락한
application record, request completion, Actor transfer와 STREAM barrier는 shutdown deadline까지 진행한다.
deadline 뒤에는 남은 claim을 revoke하고 대기 중인 operation을 shutdown 결과로 완료한다.

## 5. Spot timer

Spot timer는 네트워크 record와 같은 Spot application turn에서 callback을 실행한다. timer backend는 언어
runtime에 맞게 선택하지만 관찰 가능한 의미는 같다.

| Framework | Timer backend |
|---|---|
| .NET, Java/Kotlin, Node.js | 각 platform timer가 만료를 알리고 Spot queue에 timer record를 넣는다 |
| C++ | Core C API timer가 만료 record를 Spot queue에 넣는다 |

같은 timer key를 다시 등록하면 generation이 증가한다. queue에 이미 들어간 이전 generation의 record는
callback을 실행하지 않는다. cancel은 해당 generation 이후 callback의 시작을 막는다. 이미 시작한 callback은
강제로 중단하지 않는다. 반복 timer가 handler 실행보다 빠르게 만료되어도 같은 key의 callback을 동시에
실행하지 않으며, 중복 만료를 한 번의 pending record로 합칠 수 있다.

고빈도 timer도 관리형 언어에서 native callback 경계를 매 tick마다 왕복하지 않는다. Platform timer가
Framework scheduler에 wakeup 신호를 보내면 scheduler가 만료 record를 batch로 처리한다.

## 6. 언어별 표현

공통 계약은 특정 async type 이름을 강제하지 않는다. 완료 순서, cancellation과 오류 의미는 이 문서가
소유하며, 각 언어의 정확한 반환 type과 오류 표현은 다음 exact interface가 소유한다.

| 언어 | exact interface owner |
|---|---|
| `.NET` | [handler 인터페이스](server/languages/dotnet/02-handler-interfaces.ko.md), [RouteMesh 인터페이스](server/languages/dotnet/05-route-mesh.ko.md) |
| Java | [handler 인터페이스](server/languages/java/02-handler-interfaces.ko.md) |
| Kotlin | [handler 인터페이스](server/languages/kotlin/02-handler-interfaces.ko.md) |
| Node.js | [handler 인터페이스](server/languages/node/02-handler-interfaces.ko.md) |
| C++ | [framework 인터페이스](server/languages/cpp/02-framework-interfaces.ko.md) |

각 exact interface는 terminator별 return type, cancellation 인자, callback 또는 coroutine 표현을 고정한다.
언어 표준 표현이 달라도 같은 operation의 완료 시점, ordering과 오류 분류는 달라지지 않는다.
