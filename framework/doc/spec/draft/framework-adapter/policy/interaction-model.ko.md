<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Overview](overview.ko.md) | [다음: ZLink Framework Message Model](message-model.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [use cases](../use-cases/README.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [검증](../usecase-validation.ko.md) | [.NET](../bindings/dotnet/README.ko.md) | [Java](../bindings/java/README.ko.md) | [Node.js](../bindings/node/README.ko.md) | [Python](../bindings/python/README.ko.md) | [C++](../bindings/cpp/README.ko.md)

# Draft -- ZLink Framework Interaction Model

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 상호작용 모델의 구분과 이름은 구현 전에 바뀔 수
> 있다.

## 1. 목적

`ZLink Framework` 공용 표면은 socket 이름보다 **상호작용 모델**을 먼저
드러내야 한다.
프레임워크 사용자는 보통 `ROUTER`, `SPOT`, `PUB`, `STREAM` 같은 내부 이름보다
아래 중 하나를
원한다.

- 요청하고 응답받기
- 작업만 보내기
- 이벤트 발행하기
- 상태를 구독하기
- stream session 또는 packet을 처리하기

## 2. 제안하는 공용 상호작용 모델

| 모델 | 설명 | 현재 비중 |
|------|------|-----------|
| `request-response` | 요청 하나에 응답 하나가 돌아온다 | 높음 |
| `command` | 응답을 기다리지 않는 one-way 전송 | 높음 |
| `publish-subscribe` | 발행자와 구독자가 느슨하게 연결된다 | 높음 |
| `stream` | 연결 수명 위에서 packet 또는 session 단위로 처리한다 | 높음 |
| `worker-dispatch` | 여러 worker 중 하나가 처리한다 | 중간 |
| `scatter-gather` | 여러 대상에 요청을 보내고 결과를 모은다 | 낮음 |

각 모델이 어떤 내부 transport에 매핑되는지는
[channel-topology.ko.md](./channel-topology.ko.md)의 section 3을 참고한다.

## 3. 모델별 기본 의미

### 3.1 request-response

- 호출자는 응답을 기다린다.
- timeout, correlation, deadline이 중요하다.
- HTTP 호출이나 gRPC unary와 가장 비슷한 경험을 제공한다.
- 현재 framework 초안의 기본 channel 요청 토대는
  `DEALER(client) -> ROUTER(server)`다.
- 일반 handler dispatch는 local `ROUTER(server)`가 받은 request를 기준으로
  설명한다.
- outbound `DEALER(client)`가 받은 메시지는 기본적으로 reply 매칭 대상으로 보고,
  일반 handler dispatch 경로에 넣지 않는다.
- `SPOT` 쪽 public request도 기본 application 표면에서는 resolver가 target을 숨기는
  형태를 우선한다. resolved route를 받는 transport helper가 필요하면 runtime/internal
  표면으로 둔다.

### 3.2 command

- 호출자는 성공적으로 전송됐는지만 확인하거나, 그마저도 느슨하게 다룰 수 있다.
- 작업 위임, 후처리 트리거, 간단한 signal에 적합하다.
- 현재 framework 초안의 기본 channel send 토대는
  `DEALER(client) -> ROUTER(server)`다.
- 다른 channel에 접근할 때는 그 channel에 붙은 `DEALER(client)`를 통해 보내는
  구조를 기본으로 본다.
- command를 받은 쪽의 handler dispatch도 local `ROUTER(server)` 기준으로
  설명한다.
- `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.
- 다만 같은 SPOT mesh 안의 `spot-to-spot` send는
  `ROUTER <-> ROUTER` routed 경로로 설명하는 편이 맞다.
- SPOT 쪽은 routed 호출보다 attach된 channel client를 통한
  `SendChannel(...).Submit(...)` 같은 표면이 먼저 보이는 편이 더 자연스럽다.
- caller가 `targetRid`와 `spotRid`를 이미 알고 있더라도, 기본 application public
  surface에서는 direct target send를 먼저 보여 주지 않는다. 위치값은 resolver 구현체와
  runtime transport helper 안에 가둔다.
- command send는 기본 async submit을 뜻한다. framework는 blocking send를 task로
  감싸지 않고 nonblocking send와 ready notification을 이용해서 backpressure를
  내부에서 처리한다.
- send backpressure 대기 한계는 call builder가 아니라 channel 또는 socket의
  `SendTimeout` 옵션을 따른다. request의 `WithTimeout(...)`은 reply 대기 시간만
  정한다.
- spot/actor join은 caller가 `string spotName`을 받아서 들고 다닐 수 있다.
  `RoutingId` 변환은 framework 내부 spot route resolver가 푼다. application
  표면에는 transport 위치값을 노출하지 않는다.

### 3.3 publish-subscribe

- 발행자는 수신자 목록을 직접 알지 않는다.
- 여러 소비자가 같은 이벤트를 동시에 처리할 수 있다.
- domain event와 state sync 양쪽에 쓸 수 있다.
- 일반 channel의 event publish와, current SPOT channel 안의 publish는 같은
  상호작용 모델이지만 표면은 나눌 수 있다.
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish할 때는,
  별도 spot publisher client 표면을 두는 편이 더 자연스럽다.

### 3.4 stream

- 연결 수명과 수신 이벤트가 중요하다.
- 일반 request handler와 같은 모양으로 억지로 맞추기보다, stream 전용
  session 모델이 필요하다.
- stream callback은 별도 recv context보다, write와 peer metadata를 함께 가진
  stream 객체를 인자로 받는 편이 더 자연스럽다.
- framework 표면은 Header 기반 packet path만 먼저 지원한다. raw byte dispatch는
  MVP 범위에 넣지 않는다.
- session error는 application handler 예외가 아니라, monitor에서 관찰 가능한
  transport 오류를 session 단위로 다시 올리는 축으로 제한하는 편이 맞다.
- 이 session error는 raw monitor event를 그대로 노출하기보다, error kind enum과
  native detail을 함께 가진 구조화된 값으로 다시 올리는 편이 맞다.
- packet framing 규약을 framework가 얼마나 감출지는 별도 설계가 필요하다.
- session callback은 transport callback 안에서 직접 실행하지 않는다. framework는
  수신 이벤트를 비동기 실행 단위로 넘긴 뒤 application callback을 호출한다.
- 같은 session 안에서는 packet callback과 lifecycle callback이 직렬로 실행된다.
  이 직렬성은 session 단위 계약이며, 서로 다른 session의 전역 순서를 의미하지
  않는다.

### 3.4.1 stream-attached actor

stream session 위에 actor/session 모델을 얹을 수 있다. 이 경우 session은 연결과
packet ingress를 맡고, actor는 계정 또는 플레이어 같은 논리 객체를 표현한다.
actor 자체의 라이프사이클(Entry Spot 머무름, session bind, user Spot join /
leave, destroy)과 application 로직 분담은 [actor-model.ko.md](./actor-model.ko.md)
가 별도로 정의한다. 본 절은 그 actor 모델이 상호작용 모델 중 stream session
경로와 어떻게 결합되는지만 다룬다.

actor가 아직 `Spot`에 attach되지 않았다면 actor dispatch는 일반 actor session
dispatch로 처리할 수 있다. 하지만 actor가 특정 `Spot`에 attach된 뒤에는 actor의
packet dispatch가 해당 `Spot` 실행 문맥에서 실행되어야 한다. attach 이후 actor
코드는 room, stage, zone 같은 domain 상태를 읽고 쓸 수 있으므로, stream session
callback 문맥에서 직접 실행하면 같은 `Spot` 상태에 대한 직렬 실행 계약이 깨진다.

actor join으로 현재 `Spot`이 바뀌는 경우에는 join 완료 뒤 들어오는 actor dispatch가
새 `Spot` 실행 문맥으로 들어가야 한다. framework는 actor session state 갱신과
이후 packet dispatch 선택 사이의 경합을 숨겨야 한다.

actor 코드는 framework outbound client를 직접 고르지 않는다. actor는 runtime이
주입한 actor context만 사용하고, context가 현재 actor 상태에 맞는 channel request
경로를 선택한다. actor가 아직 `Spot`에 join되지 않았으면 context의 channel
request는 일반 framework channel client 경로로 나간다. actor가 `Spot`에 join된
뒤에는 같은 호출이 현재 `Spot`에 attach된 channel client 경로로 나간다. 이 규칙은
사용자가 join 전후에 `IZLinkClient`와 `IZLinkSpotClient` 중 무엇을 써야 하는지
판단하지 않게 하려는 것이다.

actor 또는 `Spot` callback 안에서 task 기반 request를 `await`하면 현재 callback은
응답 또는 timeout 전까지 끝나지 않는다. thread를 점유한다는 뜻은 아니지만, 같은
`Spot`의 다음 dispatch, join, timer, subscription 처리는 현재 callback task가 끝난
뒤에 실행된다. 명시 timeout이 없으면 framework default timeout을 사용한다.

### 3.5 worker-dispatch

- 의미상으로는 command 또는 request-response의 변형이지만, 사용자 기대가
  다르므로 별도 use case로 본다.
- 사용자는 "어느 worker가 받는가"보다 "worker group에 작업을 보낸다"를 먼저
  떠올린다.

### 3.6 scatter-gather

- 하나의 논리 요청이 여러 실제 요청으로 fan-out된다.
- 결과를 일부만 모을지 모두 기다릴지 정책이 필요하다.
- 단일 unary RPC의 단순 확장이 아니라 aggregate 모델에 가깝다.

## 4. 기본 원칙

- framework가 직접 통합할 transport 축은 네 가지로 한정한다. 구체적인 축
  정의는 [overview.ko.md](./overview.ko.md)의 section 2를, 각 모델과의 매핑은
  [channel-topology.ko.md](./channel-topology.ko.md)의 section 3을 참고한다.
- 서버 간 `send/request`는 프레임워크 사용자에게 HTTP handler 매핑과 비슷한
  방식으로 보여야 한다.
- 이 경로에서 wire header는 공용 handler 시그니처에 직접 노출하지 않는다.
  handler는 typed body와 context만 받는 편을 기본으로 본다.
- header metadata가 필요하면 framework context에서 조회하게 한다.
- 같은 이유로 channel messaging에서 일반 message dispatch는 local `ROUTER`
  ingress를 기준으로 설명하는 편이 맞다. outbound `DEALER` 수신은 우선 reply
  correlation 경로로 처리한다.
- `dealer-dealer`는 현재 목표 범위에 넣지 않는다.
- `SPOT`은 event 전파의 핵심 토대이지만, 필요할 때는 request/reply의 내부
  운반층으로도 쓸 수 있다. 다만 framework 공용 이름은 여전히 socket 이름보다
  상호작용 의미를 먼저 드러내야 한다.
- `SpotNode` peer topology와 channel 단위 호출은 서로 다른 의미다.
- 현재 방향에서는 `SpotNode.router` 경로를 공개 high-level direct API로 그대로
  드러내기보다, current channel publish와 cross-channel send/request를 분리해서
  설명하는 편이 더 명확하다.
- 같은 내부 topology를 쓰더라도, use case가 다르면 공용 이름도 다르게 둔다.
  예를 들어 `request-response`와 `worker-dispatch`는 둘 다 어떤 routed transport
  위에 올릴 수 있어도, 같은 개념으로 설명하지 않는다.

## 5. use case와의 연결

| use case | 기본 모델 |
|----------|-----------|
| 일반 웹 백엔드 서비스 호출 | `request-response` |
| playhouse play -> api | `request-response` |
| room/stage/zone 안의 channel 호출 | `request-response` 또는 `command` |
| worker dispatch | `worker-dispatch` 또는 `command` |
| domain event fanout | `publish-subscribe` |
| cache invalidation / config refresh | `publish-subscribe` |
| stage state sync | `publish-subscribe` |
| real-time notification fanout | `publish-subscribe` |
| session actor dispatch | `stream` + actor create/dispatch + session proxy |
| scatter-gather query | `scatter-gather` |
| workflow orchestration | `request-response` + `publish-subscribe` 조합 |
