<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md)
<!-- framework-adapter-nav:end -->

# Java Lifecycle And Failure Semantics

Spring host에서 lifecycle의 두 경계는 다음과 같다.

- **시동(startup)**: framework runtime을 구동하는 `SmartLifecycle.start()`.
  모든 bean이 준비된 뒤 bind/connect/discovery를 시작한다.
- **종료(shutdown)**: `SmartLifecycle.stop()`. graceful close(linger/drain)를
  수행한다.

`ApplicationRunner`는 구동 driver로 쓰지 않고 one-shot readiness 신호용으로만
예약한다. 설정 검증은 시동 hook이 아니라 등록 시점(`@EnableZLinkFramework`
auto-config / `ZLinkFrameworkConfigurer` 적용 시점, `.NET`
`AddZLinkFramework(...)` 등록 호출 안 검증 대응)에 먼저 끝낸다.

## 1. Startup 순서

1. `ZLinkFrameworkConfigurer`와 registry/monitoring configurer 수집
2. builder validation(등록 시점)
3. **embedded registry runtime 먼저 시동**
4. Java binding context 생성
5. channel, spot mesh, stream node runtime 시작
6. discovery attach와 manual connection 적용
7. monitoring source validation과 attach
8. Spring host ready

embedded registry와 framework runtime이 같은 application에 있으면 registry runtime이
**먼저** 시동된다(`.NET` `ZLinkFrameworkRuntime.StartAsync`가 자기 state를 만들기
전에 `_registryRuntime.StartAsync`를 먼저 호출하는 것 대응). framework discovery
client가 registry endpoint를 연결하기 전에 endpoint가 열려 있어야 하기 때문이다.
시동 순서는 registry → framework → monitoring이며, framework/registry runtime 시동은
idempotent해야 한다.

## 2. Shutdown 순서

호스트 종료는 `SmartLifecycle.stop()` 실행 순서로 나타난다. monitoring을 가장 먼저
떼어 내고, framework runtime state를 `.NET`의
`ZLinkFrameworkRuntimeState.DisposeAsync` dispose 순서 그대로 내린다. embedded
registry stop은 framework state가 내려간 **뒤에** 일어난다.

1. monitoring polling과 monitor attach 해제
2. framework runtime state dispose (세부 순서는 아래)
3. embedded registry stop
4. (framework state dispose의 마지막 단계로) Java binding context close

framework runtime state를 내리는 세부 순서(`DisposeAsync`):

1. stop token cancel 후 listener task drain
2. SpotNode dispose
3. route(mesh) channel dispose
4. spot discovery dispose
5. stream node dispose
6. client → publisher → subscriber → server channel bundle dispose
7. 마지막으로 Java binding context dispose

embedded registry stop이 framework state dispose 뒤에 오는 이유는, service runtime을
먼저 내린 뒤 registry를 정리해야 다른 노드들이 topology 변화를 정상적인 절차로 읽어
갈 수 있기 때문이다. `.NET`에서는 embedded registry hosted service가 framework와
함께 있을 때 자기 `StopAsync`를 no-op으로 두고, 실제 registry stop을 framework
runtime의 stop 경로가 책임진다(framework state가 내려간 뒤 registry stop). Java도
같은 위임 구조로 "framework 먼저, registry 나중" 순서를 지킨다.

shutdown은 새 dispatch를 받지 않고, 이미 queue에 들어간 work item은 timeout과
cancellation 정책에 따라 정리한다.

## 3. Startup 오류

구성만 보고 알 수 있는 문제는 startup validation 오류다.

- 중복 channel, handler, actor factory, Spot factory
- endpoint 누락
- discovery/manual connection 혼용
- stream node session type 중복
- actor factory without SpotNode
- monitoring source 이름 불일치

## 4. Runtime event 또는 호출 실패

운영 중 외부 상태 때문에 발생하는 문제는 startup 오류가 아니다.

- 이미 연결된 peer의 disconnect
- discovery provider down
- registry query 일시 실패
- stale bound session push 실패
- request timeout
- timer handler exception
- user callback exception

## 5. Submit 의미

`send`와 `publish`는 remote handler 완료를 기다리지 않는다. transport에 submit할 수
있게 될 때까지 비동기 대기한다. `request`는 submit timeout과 reply timeout을 분리한다.

## 6. STREAM session 의미

`onConnectedAsync()`는 connection ready 이후 호출한다. `onErrorAsync(...)`는 session과
매칭되는 transport error만 받는다. handshake 실패나 bind 실패는 monitoring event로
남긴다. 같은 session의 dispatch callback은 직렬로 실행한다.
