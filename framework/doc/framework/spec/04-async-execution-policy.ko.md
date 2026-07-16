# 비동기 실행과 handler turn

[스펙 목차](README.ko.md) · [메시지 계약](03-message-model.ko.md) ·
[Framework API](05-framework-api.ko.md)

이 문서는 ZLink Framework 10.0.0의 submit, request completion, handler 직렬 실행, timeout,
cancellation과 timer 계약을 정의한다. 대상 독자는 언어별 비동기 API와 scheduler adapter를 구현하는
개발자다.

## 1. One-way submit

Send와 publish는 입력 검증과 Framework가 소유한 bounded queue의 수락 결과를 반환한다. 원격 handler의
실행 완료를 나타내는 awaitable 결과는 제공하지 않는다. 수락할 수 없으면 backpressure, 대상 없음,
shutdown 또는 잘못된 입력을 구분해 반환한다.

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

Node handler, ChannelName handler, 각 Spot과 각 Actor는 자기 application queue를 순서대로 처리한다. 하나의
handler가 반환하거나 허용된 await에서 재개되어 끝날 때까지 같은 owner의 다음 application record를
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
| C와 C++ | Core C API timer가 만료 record를 Spot queue에 넣는다 |

같은 timer key를 다시 등록하면 generation이 증가한다. queue에 이미 들어간 이전 generation의 record는
callback을 실행하지 않는다. cancel은 해당 generation 이후 callback의 시작을 막는다. 이미 시작한 callback은
강제로 중단하지 않는다. 반복 timer가 handler 실행보다 빠르게 만료되어도 같은 key의 callback을 동시에
실행하지 않으며, 중복 만료를 한 번의 pending record로 합칠 수 있다.

고빈도 timer도 관리형 언어에서 native callback 경계를 매 tick마다 왕복하지 않는다. Platform timer가
Framework scheduler를 깨우고 만료 record를 batch로 처리한다.

## 6. 언어별 표현

공통 계약은 특정 async type 이름을 강제하지 않는다. .NET의 정확한 반환 type과 cancellation 인자는
[.NET handler 인터페이스](server/languages/dotnet/02-handler-interfaces.ko.md)와
[.NET RouteMesh 인터페이스](server/languages/dotnet/05-route-mesh.ko.md)가 소유한다. 다른 언어는 구현
단계에서 같은 완료·ordering·오류 의미를 해당 언어의 표준 비동기 표현으로 고정한다.
