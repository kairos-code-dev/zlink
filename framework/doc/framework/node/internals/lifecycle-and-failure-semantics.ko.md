<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Node.js DI Capability Exposure Policy](di-capability-exposure-policy.ko.md) | [다음: ZLink Framework Node.js Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

[표면 매핑 정책](dotnet-to-node-surface-mapping.ko.md)

[Node.js 묶음](../README.ko.md) | [Behavior Matrix](behavior-matrix.ko.md) | [Monitoring](../spec/nestjs-monitoring.ko.md) | [Registry](../spec/nestjs-registry.ko.md)

# ZLink Framework Node.js Lifecycle And Failure Semantics

> 이 문서는 [표면 매핑 정책](dotnet-to-node-surface-mapping.ko.md)을 따른다.
> 호스트 표면은 NestJS 의 `DynamicModule` + lifecycle hook, 언어 표면은 TypeScript
> 다. 기준은 `framework/languages/node` 의 코드이며, dotnet 동작은 parity 비교용
> 선택 참고다.

## 1. 목적

framework 구현은 public API 의 모양만 맞춰서는 끝나지 않는다. host 의
lifecycle[^lifecycle], startup validation[^startup-validation], reconnect,
shutdown 순서 같은 동작 약속도 문서로 단단히 닫혀 있어야 한다. 그래야 회귀
테스트와 실제 운영 코드가 같은 결과를 기대할 수 있다. 이 문서는 그 약속을 한
자리에 모은다.

NestJS 표면에서 lifecycle 의 두 경계는 다음과 같이 본다.

- **시동(startup)**: NestJS provider lifecycle hook `onModuleInit()`.
  모든 provider 가 DI 에서 resolvable 해진 뒤에 runtime 시동(bind / connect /
  discovery 시작)을 건다.
- **종료(shutdown)**: NestJS `onModuleDestroy(signal?)` 또는
  `onModuleDestroy()`. graceful close(linger, drain)를 수행한다.

dotnet 의 `IHostedService.StartAsync` / `StopAsync` 가 정확히 이 두 hook 으로
매핑된다. 한 가지 짚을 점은, **설정 검증은 시동 hook 이 아니라 module 등록
시점**(`ZLinkModule.forRoot(...)` / `forRootFactory(...)`)에 먼저 일어난다는
것이다(§3 참조). dotnet 도 `AddZLinkFramework(...)` 등록 호출 안에서 검증을
끝낸다.

## 2. Startup 순서

기본 startup 순서는 다음과 같이 본다.

1. registration surface[^registration-surface] 파싱
2. 중복된 이름, 잘못된 역할[^capability] 조합, 누락된 endpoint 같은 설정의 검증
3. `Context` 와 framework runtime 생성
4. embedded 구성이라면 Registry[^registry] bind
5. channel runtime, spot node, stream node 시작
6. monitoring source attach
7. application host ready

이 가운데 1~2 단계는 `ZLinkModule.forRoot(...)` / `forRootFactory(...)` 가
`DynamicModule` 을 만들어 내는 **등록 시점**에 끝난다. 3~7 단계가 NestJS
lifecycle hook(`onModuleInit()`) 안에서 일어나는 실제 시동이다.

`onModuleInit()` 안에서의 세부 순서는 dotnet runtime state 생성
순서를 그대로 따른다.

1. backend channel adapter 로 `Context` 생성
2. inbound(server) channel 초기화 → publisher channel → client channel →
   route(mesh) channel 초기화
3. spot node 초기화
4. stream node 초기화

순서 자체보다 더 중요한 약속들은 아래와 같다.

- 설정 검증에서 걸리면, bind / connect 단계에 들어가기 전에 그 자리에서 바로
  예외를 던진다. NestJS 에서는 이 예외가 `forRoot(...)` / `forRootFactory(...)`
  호출 시점에 던져지므로, application 이 부팅을 시작하기도 전에 멈춘다.
- embedded registry 가 있는 구성에서는 Registry 가 먼저 bind 되어야 한다.
  그래야 그 위에서 돌아갈 discovery 기반 channel 과 SPOT[^spot] mesh 가
  정상적으로 시작될 수 있다. 구현상으로는 framework runtime 시동 hook 이 자기
  state 를 만들기 **전에** embedded registry runtime 을 먼저 시동한다.
- monitoring 은 감시 대상 source 가 만들어진 뒤에 attach 한다. monitoring
  hook 은 framework / registry runtime 을 (idempotent 하게) 먼저 시동시켜
  source 가 존재함을 보장한 다음, polling source preflight 를 거쳐 마지막에
  socket monitor 를 붙인다.
- DI 로 주입되는 channel / fanout / route outbound client 는 transport 를 직접
  소유하지 않는다. framework runtime host 가 channel runtime state 를 만들고,
  client provider 는 그 host-owned transport 를 참조한다. 그래서 startup 전 호출은
  runtime-not-started 오류가 되고, startup 이후 호출은 같은 provider 인스턴스로
  host 가 관리하는 socket bundle 을 사용한다.

> NestJS 매핑 노트: 위 약속을 만족시키려면 시동이 **runtime 별로 분리된 hook**
> 으로 구성되어야 한다. dotnet 은 `ZLinkRegistryHostedService`,
> `ZLinkFrameworkHostedService`, `ZLinkMonitoringHostedService` 세 개의
> hosted service 가 같은 순서로 등록되어 같은 순서로 `StartAsync` 된다. node
> 는 이 세 단계를 register → framework → monitoring 순으로 시동되는
> lifecycle 참여자로 구성한다. 단, framework / registry runtime 의 시동은
> **idempotent**(이미 시작됐으면 무시) 해야 한다. monitoring hook 이 같은
> runtime 을 다시 시동시키더라도 두 번 시작되지 않아야 하기 때문이다.

## 3. Fail-Fast 규칙

다음 상황은 모두 host startup 자체를 실패로 본다. 즉 fail-fast[^fail-fast] 다.

- 잘못된 registration 조합
- bind 가 필수인 endpoint 의 누락
- startup 단계의 bind 실패
- startup 단계에서 반드시 만들어져야 하는 runtime 객체의 생성 실패
- monitoring source 이름의 mismatch

앞의 두 항목(잘못된 registration 조합, 필수 endpoint 누락)은 등록 시점 검증에서
걸리므로 `ZLinkModule.forRoot(...)` / `forRootFactory(...)` 호출 자체가 reject /
throw 한다. 나머지(bind 실패, runtime 객체 생성 실패, monitoring source
mismatch)는 lifecycle hook(`onModuleInit()`) 안에서 던져지고, NestJS
가 application 부팅을 중단시킨다.

startup 단계에서 runtime state 를 만들다가 어느 한 컴포넌트라도 생성에
실패하면, 그때까지 만든 state 를 **그 자리에서 정리(dispose)한 뒤 예외를 다시
던진다.** 반쯤 열린 socket 이나 매달린 context 를 남기지 않는다. 마찬가지로
monitoring 시동이 도중에 실패하면, 이미 붙인 monitor 를 정리하고, 이번 시동이
시작시킨 framework / registry runtime 을 되돌려 stop 한 뒤 예외를 다시 던진다.

반대로 다음 상황은 startup 실패로 다루지 않고, runtime event 와 reconnect 정책
쪽으로 넘긴다.

- 이미 시작된 뒤에 일어나는 discovery provider down
- 이미 연결되어 있던 peer 가 일시적으로 disconnect 되는 경우
- polling 기반 source 의 일시적 query 실패

## 4. Shutdown 순서

기본 shutdown 순서는 다음과 같이 본다.

1. monitoring source detach
2. channel runtime, spot node, stream node stop
3. embedded Registry stop
4. `Context` dispose

NestJS 에서는 이 순서가 `onModuleDestroy(signal?)` / `onModuleDestroy()`
hook 의 실행 순서로 나타난다. monitoring hook 이 먼저 polling 을 멈추고 monitor
를 (등록의 역순으로) 떼어 낸 다음, framework runtime hook 이 자기 state 를
graceful 하게 내린다. embedded registry stop 은 framework runtime state 가 내려간
**뒤에** 일어난다.

framework runtime 을 내리는 세부 순서는 `ZLinkFrameworkRuntimeHost.stop()` 기준이다.

1. stop token 을 cancel 하고 listener task 들이 끝날 때까지 drain
2. `streamRuntime` dispose
3. `spotNodeRuntime` dispose
4. `channelRuntime` dispose
5. 마지막으로 runtime state / `Context` dispose

이 순서를 따르는 이유는 두 가지다.

- 첫째, runtime 이 내려가는 동안 monitoring 이 새로운
  synthetic event[^synthetic-event] 를 계속 만들어 내지 않도록 하기 위해서다.
  그래서 monitoring 을 가장 먼저 떼어 낸다.
- 둘째, service runtime 을 먼저 내린 뒤에 Registry 를 정리해야, 다른 노드들이
  topology[^topology] 변화를 정상적인 절차로 읽어 갈 수 있기 때문이다. 그래서
  embedded Registry stop 을 framework state dispose 뒤로 둔다.

> NestJS 매핑 노트: dotnet 의 embedded registry hosted service 는, framework
> runtime 이 함께 있는 구성에서는 자기 `StopAsync` 를 **no-op** 으로 두고,
> 실제 registry stop 을 framework runtime 의 stop 경로가 책임진다(framework
> state 가 내려간 뒤 registry 를 stop). node 에서도 embedded registry 가
> framework 와 함께 구동될 때는 registry 종료를 framework 종료 경로에 위임해서,
> "framework 먼저, registry 나중" 순서가 깨지지 않게 한다. registry 단독 구성
> 일 때만 registry 종료 hook 이 직접 stop 한다.

## 5. Request / Send / Publish 실패 의미

| 동작 | 실패 의미 |
| ---- | --------- |
| `request(...).submit(...)` | route-not-ready, reply timeout, serialization 실패, runtime stop 을 모두 예외(reject)로 본다 |
| `send(...).submit(...)` | route-not-ready, send timeout, serialization 실패, runtime stop 을 예외(reject)로 본다 |
| `publish(...).submit(...)` | route-not-ready, send timeout, serialization 실패, runtime stop 을 예외(reject)로 본다 |

TypeScript 표면에서 이들 `submit(...)` 은 모두 `Promise` 를 반환하는 async
submit 이다. 위 실패 의미는 그 `Promise` 의 reject 로 나타난다.

`send(...).submit(...)` 과 `publish(...).submit(...)` 은 원격 peer 의 handler
처리가 끝나기를 기다리지 않는다. framework 가 메시지를 transport 에 넘길 수
있게 될 때까지만 기다리는 비동기 submit 이다. 일시적인
backpressure[^backpressure] 는 `false` 반환값으로 노출하지 않는다. 대신
nonblocking send, pending queue, ready notification 조합으로 내부에서 처리한다.

이 대기는 thread 를 블로킹하는 대기가 아니다. caller 가 `await` 하면 application
흐름은 submit 이 끝날 때까지 기다린다. 다만 runtime 은 Node 의 event loop 나
worker 를 backpressure 대기용으로 잡아 두지 않는다. pending queue 의 동작
약속은 두 가지다.

- 크기는 high water mark[^high-water-mark] 와 timeout 정책으로 제한해야 한다.
- runtime stop 이나 cancellation(`AbortSignal`)이 들어오면, 대기 중이던 submit
  을 깨워서 완료 또는 실패로 정리해 줘야 한다.

`request(...).submit(...)` 은 두 단계로 나눠서 본다.

- request packet 의 submit 자체는 `send(...).submit(...)` 과 같은 전송 경로를
  타고 submitter timeout 정책을 따른다.
- 그 뒤의 reply 대기는 `timeout(...)` 으로 정한 request timeout 정책을 따른다.

## 6. Reconnect 와 Monitoring 의미

- discovery 기반 역할은 provider 집합이 바뀔 때마다 runtime 이 알아서
  따라간다.
- 반면 manual 역할의 경우, framework 가 자동 reconnect 정책을 안에
  숨겨서 끼워 넣지 않는다. reconnect 가 필요하면 명시적인 `connect(...)`
  호출이나 상위의 retry policy 가 책임진다.
- socket 단의 이벤트는 하부 monitor event 를 그대로 감싼다.
- registry 와 spot 쪽은 다르게 다룬다. polling 과 snapshot diff 를 기반으로
  framework 가 다시 만든 synthetic event 로 올린다.
- discovery 상태 자체는 별도의 event 로 노출하지 않는다. 대신 registry 의
  snapshot 이나 query 로 확인하게 한다.

## 7. Stream Session Error 의미

- `onConnected(...)` 는 `ConnectionReady` 시점을 기준으로 호출한다.
- `onError(...)` 는 session 과 매칭되는 transport 오류만 받는다.
- handshake 실패, bind / accept / close 실패는 session 콜백으로 올리지 않고
  monitoring 쪽에만 남긴다.
- transport error 가 발생한 뒤 연결이 실제로 끊어진 것이 확인되면, 이어서
  `onDisconnected(...)` 가 한 번 더 호출될 수 있다.

## 8. Session Actor Route 변경 의미

session 이 actor 에 attach 되면 framework 는 actor id, actor type, session rid,
session owner SpotNode, session binding token 을 ActorGateway binding 으로 연결한다.
이후 session -> actor relay 는 logical actor handle 을 core ActorGateway 로 내려보내고,
packet 마다 application resolver 를 호출하지 않는다.

actor 위치가 바뀌면 core ActorGateway 의 current location 이 relay 대상이 된다.
framework 는 concrete route 주소를 session 상태로 갱신하지 않는다. session binding token 은
disconnect cleanup 과 stale session 방어에 계속 쓰며, 이전 session 의 늦은 close 가 새
binding 을 지우지 못하도록 조건부 unbind 에 사용한다.

## 9. Spot Lifecycle 의미

- `onInitialize(...)` 는 spot 의 실행 문맥에서 단 한 번만 호출된다.
- `configure()` 는 `onInitialize(...)` 보다 먼저 한 번 호출된다.
  `context.addPacket<THandler>()`, `context.addSubscribe<THandler>()`,
  같은 spot-local 등록은 이 단계에서 수행한다. actor packet handler 는
  `configure()` 에서 등록하지 않고 NestJS decorator discovery 로 등록한다.
- `onActorJoin(...)`, `onJoinedActor(...)`, `onLeaveActor(...)` 는 별도 handler
  등록이 아니라 Spot / Entry Spot 멤버 callback 으로 선언한다.
- `onClosing(...)` 는 `ZLinkSpotManager.close(...)` 로 SPOT 을 정상적으로
  종료할 때, spot 의 실행 문맥에서 호출된다. host shutdown 이나 process 종료
  시점에 반드시 호출되는 destructor 같은 의미는 아니다.
- framework 는 spot 마다 별도의 scope[^per-spot-scope] 를 만들고, 등록해 둔
  handler 타입은 그 scope 안에서 resolve 한다.
- `context.addPacket<THandler>()`, `context.addSubscribe<THandler>()`,
  `context.addTimer<THandler>()` 는 service locator 가 아니라 "이 타입을 이 spot
  scope 에서 사용해 달라" 는 등록 의미로 본다.
- spot 이 제거되면, 그에 묶여 있던 scope 도 함께 정리된다.

## 10. Host 중지 중 호출 의미

- host stopping 이 시작되면, 새로 들어오는 inbound dispatch 는 받지 않는 편을
  기본으로 본다.
- 이미 시작되어 돌고 있던 handler 는 cancellation 신호(`AbortSignal`)를
  전달받고, 빠르게 종료할 기회를 가진다.
- graceful timeout[^graceful-timeout] 을 넘긴 작업은 host 의 shutdown 정책에
  따라 중간에 끊어질 수 있다. NestJS 에서는 `app.enableShutdownHooks()` 로
  활성화된 종료 신호(`onModuleDestroy()`)가 이 경계를 정한다.
- shutdown 도중에 새로 던지는 outbound request 의 성공은 보장하지 않는다.

## 11. 회귀 테스트

lifecycle 과 failure semantics 항목은 다음을 모두 테스트로 못 박아 둔다.

- 시작 순서
- shutdown 정리 순서
- request / send 실패 의미
- stream transport error 의 범위
- attached actor remote address update 의 stale generation 방어

만약 구현이 오류를 더 늦게 드러내는 방향으로 바뀐다면, 이 문서와 테스트를 함께
갱신한다.

> 아래 표는 dotnet 회귀 테스트를 node 표면으로 옮긴 대응이다. 테스트 이름은
> node 테스트 러너 관례에 맞춰 옮기되, **확인 기준(의미)은 그대로 유지**한다.
> dotnet 테스트가 기능의 최종 기준이다.

| 테스트 케이스(node) | 확인 기준 |
| ------------- | --------- |
| `Host_Starts_And_Stops_FrameworkRuntimeContext` | host 의 시작·종료(`onModuleInit` / `onModuleDestroy`)에 맞춰 framework runtime context 가 생성되고 정리된다. |
| `Host_Starts_EmbeddedRegistry_Before_FrameworkRuntime` | embedded Registry 와 framework runtime 사이의 시작 순서(registry 먼저)가 유지된다. |
| `runtime task runner observes detached task exceptions without unhandled rejection` | detached runtime task 예외가 unhandled rejection 으로 새지 않고 runtime error sink 로 보고된다. |
| `framework runtime state aborts listener tasks before disposing backend context` | shutdown 시 listener task 가 stop signal 을 먼저 보고 종료한 뒤 backend context 가 정리된다. |
| `pending submit fails when send timeout expires` | pending submit 은 submitter timeout 정책에 따라 reject 되고, event loop 를 묶지 않는다. |
| `RequestTimeoutRemovesPendingRequest` | stream connector 의 request timeout 이 끝나면 pending request 가 정리된다. |
| `StreamRawSession_OnError_Reports_TransportError_For_RemoteDisconnect` | remote disconnect 는 stream session 의 transport error 콜백(`onError`)으로 보고된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^reconnect]: reconnect 는 끊어진 연결을 다시 맺으려고 시도하는 동작을 가리킨다. 자동 재시도 정책과 명시적 호출 두 가지 모양이 있다.
[^lifecycle]: lifecycle 은 컴포넌트가 시작·동작·종료되는 전체 수명 주기와, 그 단계마다 일어나는 일을 가리킨다. NestJS 에서는 `onModuleInit` / `onModuleDestroy` / `onModuleDestroy` 같은 lifecycle hook 으로 그 단계가 드러난다.
[^startup-validation]: startup validation 은 host 가 본격적으로 동작하기 전에 설정과 등록 정보를 검사해서, 잘못된 구성이라면 그 자리에서 막아 내는 단계다. node 에서는 `ZLinkModule.forRoot(...)` / `forRootFactory(...)` 등록 시점에 수행된다.
[^registration-surface]: registration surface 는 `ZLinkModule.forRoot(...)` 의 options 객체를 통해 framework 에 쌓이는 설정의 집합을 가리킨다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, client, publisher, subscriber)를 가리킨다.
[^registry]: Registry 는 어느 노드가 어떤 역할을 어디서 제공하는지를 모아 두고, 다른 노드가 그 정보를 조회할 수 있게 해 주는 컴포넌트다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다.
[^fail-fast]: fail-fast 는 잘못된 설정이나 상태를 발견하면 즉시 예외를 던지고 실행을 멈춰서, 더 큰 문제로 번지는 것을 막는 전략이다.
[^synthetic-event]: synthetic event 는 backend 가 직접 발생시키는 이벤트가 아니라, framework 가 snapshot 의 차이나 polling 결과를 보고 합성해서 만들어 내는 이벤트를 가리킨다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있고, 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^high-water-mark]: high water mark 는 pending queue 같은 버퍼에 쌓아 둘 수 있는 항목 수의 상한선이다. 이 한계를 넘으면 흐름 제어가 발동한다.
[^per-spot-scope]: per-spot scope 는 SPOT 인스턴스마다 별도로 만들어지는 DI scope 다. 그 scope 안에서 spot 전용 handler 들이 resolve 된다.
[^graceful-timeout]: graceful timeout 은 정상 종료를 시도할 때 작업이 자발적으로 마무리될 때까지 기다려 주는 최대 시간이다. 이 시간을 넘기면 강제로 중단할 수 있다.
</content>
</invoke>

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Node.js DI Capability Exposure Policy](di-capability-exposure-policy.ko.md) | [다음: ZLink Framework Node.js Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:bottom:end -->
