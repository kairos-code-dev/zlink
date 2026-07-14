<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: ZLink Framework Overview](01-overview.ko.md) | [다음: ZLink Framework Message Model](03-message-model.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../README.ko.md) | [개요](01-overview.ko.md) | [메시지 모델](03-message-model.ko.md) | [channel topology](10-channel-topology.ko.md) | [framework API](05-framework-api.ko.md) | [공통 sample](../sample/README.ko.md) | [공통 E2E](../e2e/README.ko.md) | [.NET](../../dotnet/README.ko.md) | [Java](../../java/README.ko.md) | [Node.js](../../node/README.ko.md) | [C++](../../cpp/README.ko.md)

# ZLink Framework Interaction Model

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

## 2. 공용 상호작용 모델

| 모델 | 설명 | 현재 비중 |
|------|------|-----------|
| `request-response` | 요청 하나에 응답 하나가 돌아온다 | 높음 |
| `command` | 응답을 기다리지 않는 one-way 전송 | 높음 |
| `publish-subscribe` | 발행자와 구독자가 느슨하게 연결된다 | 높음 |
| `stream` | 연결 수명 위에서 packet 또는 session 단위로 처리한다 | 높음 |

각 모델이 어떤 내부 transport에 매핑되는지는
[10-channel-topology.ko.md](10-channel-topology.ko.md)의 section 3을 참고한다.

## 3. 모델별 기본 의미

### 3.1 request-response

- 호출자는 응답을 기다린다.
- timeout, correlation, deadline이 중요하다.
- HTTP 호출이나 gRPC unary와 가장 비슷한 경험을 제공한다.
- framework의 기본 channel 요청 토대는
  `DEALER(client) -> ROUTER(server)`다.
- 일반 handler dispatch는 local `ROUTER(server)`가 받은 request를 기준으로
  설명한다.
- outbound `DEALER(client)`가 받은 메시지는 기본적으로 reply 매칭 대상으로 보고,
  일반 handler dispatch 경로에 넣지 않는다.
- `SPOT` 쪽 public request는 resolver가 만든 `SpotHandle`을 받는다.
  위치값을 낱개(`targetRid + spotRid`)로 받는 표면은 두지 않는다
  ([spot 주소 메시징](24-spot-address-messaging.ko.md)).

### 3.2 command

- 호출자는 성공적으로 전송됐는지만 확인하거나, 그마저도 느슨하게 다룰 수 있다.
- 작업 위임, 후처리 트리거, 간단한 signal에 적합하다.
- framework의 기본 channel send 토대는
  `DEALER(client) -> ROUTER(server)`다.
- 다른 channel에 접근할 때는 그 channel에 붙은 `DEALER(client)`를 통해 보내는
  구조를 기본으로 본다.
- command를 받은 쪽의 handler dispatch도 local `ROUTER(server)` 기준으로
  설명한다.
- `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.
- 다만 같은 SPOT mesh 안의 `spot-to-spot` send는
  `ROUTER <-> ROUTER` routed 경로로 설명한다.
- RouteMesh의 모든 구성원은 endpoint 유무와 관계없이 `ROUTER` 역할을 사용한다.
  endpoint가 없는 구성원은 다른 `ROUTER`를 항상 dial하고, 양쪽 모두 endpoint가
  있으면 pairwise initiator 규칙으로 한쪽만 dial한다. RouteMesh에 `DEALER` 역할을
  섞는 구성은 유효하지 않다.
- SPOT 쪽은 routed 호출보다 route bridge channel socket을 통한
  `SendToChannel(...).Submit(...)` 같은 표면이 먼저 보이는 편이 더 자연스럽다.
- caller가 위치값을 이미 알고 있더라도, 기본 application public API 표면에서는
  낱개 위치값을 받는 direct target send를 보여 주지 않는다. 위치는 항상
  resolve 로 얻은 주소 값 하나로만 다룬다.
- command send는 기본 one-way submit을 뜻한다. framework는 blocking send를 task로
  감싸지 않고 nonblocking send와 ready notification을 이용해서 backpressure를
  내부에서 처리한다.
- send backpressure 대기 한계는 application 호출부가 아니라 framework 기본값 또는 socket의
  `SendTimeout` 옵션을 따른다. request의 `Timeout(...)`은 reply 대기 시간만 정한다.
- spot/actor join은 caller가 `RoutingId spotRid`를 받아서 들고 다닐 수 있다.
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
  transport 오류를 session 단위로 다시 올리는 축으로 제한한다.
- 이 session error는 raw monitor event를 그대로 노출하지 않고, error kind enum과
  native detail을 함께 가진 구조화된 값으로 다시 올린다.
- application handler에는 packet framing을 노출하지 않는다. framing은 connector와
  transport adapter 내부에서 처리하며, low-level encoded payload extension만 명시적으로
  frame bytes를 다룰 수 있다.
- session callback은 transport callback 안에서 직접 실행하지 않는다. framework는
  수신 이벤트를 비동기 실행 단위로 넘긴 뒤 application callback을 호출한다.
- 같은 session 안에서는 packet callback과 lifecycle callback이 직렬로 실행된다.
  이 직렬성은 session 단위 계약이며, 서로 다른 session의 전역 순서를 의미하지
  않는다.
- stream socket은 같은 session의 frame 도착 순서를 보존한다. framework는 그 순서를
  session별 직렬 callback 실행으로 이어 주면 된다. actor처럼 별도 public mailbox
  개념을 session 표면에 노출하지 않는다.

### 3.4.1 stream-attached actor

stream session 위에 actor/session 모델을 얹을 수 있다. 이 경우 session은 연결과
packet ingress를 맡고, actor는 계정 또는 플레이어 같은 논리 객체를 표현한다.
actor 자체의 라이프사이클(Entry Spot 머무름, session bind, user Spot join /
leave, destroy)과 application 로직 분담은 [22-actor-model.ko.md](22-actor-model.ko.md)
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
뒤에는 같은 호출이 현재 `Spot`에 route bridge channel socket 경로로 나간다. 이 규칙은
사용자가 join 전후에 일반 channel client 역할과 Spot outbound 역할 중 무엇을
골라야 하는지 판단하지 않게 하려는 것이다.

actor 또는 `Spot` callback 안에서 request를 **`async`로** 기다리면 현재 callback은 응답 또는
timeout 전까지 끝나지 않는다. thread를 점유한다는 뜻은 아니지만, **같은 `Spot`의 다음 dispatch,
join, timer, subscription 처리는 현재 callback이 끝난 뒤에 실행된다.** 즉 handler는 하나의 turn이며
spot 상태를 lock 없이 다룰 수 있다.

**`yield`로 기다리면 실행 줄을 반납한다.** 그 대기 중에 같은 `Spot`의 다른 callback이 실행되고,
완료된 continuation은 큐에 다시 들어가 순서대로 재개된다. spot 공유 흐름과 무관한 대기(외부 API,
DB 조회 등)에만 제한적으로 쓴다. 세 terminator의 정확한 계약은
[04 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) §1.1이 소유한다.
명시 timeout이 없으면 framework default timeout을 사용한다.

이 직렬화 규칙은 user Spot 과 Entry Spot 의 lifecycle, route, subscription callback 에
적용된다. Entry Spot actor packet 은 대상 actor 의 mailbox 에서 처리하므로, 서로 다른
actor 의 packet 이 Entry Spot 하나의 실행 줄에 묶이지 않는다. Entry Spot timer 정합성은
actor packet mailbox 계약과 분리해서 다룬다. room, stage, match 같은 권위 상태를 바꾸는
주기 작업은 그 상태를 소유하는 user Spot timer 로 등록해야 한다.

## 4. 기본 원칙

- framework가 직접 통합할 transport 축은 네 가지로 한정한다. 구체적인 축
  정의는 [01-overview.ko.md](01-overview.ko.md)의 section 2를, 각 모델과의 매핑은
  [10-channel-topology.ko.md](10-channel-topology.ko.md)의 section 3을 참고한다.
- 서버 간 `send/request`는 프레임워크 사용자에게 HTTP handler 매핑과 비슷한
  방식으로 보여야 한다.
- 이 경로에서 wire header는 공용 handler 시그니처에 직접 노출하지 않는다.
  handler는 typed body와 context만 받는 편을 기본으로 본다.
- header metadata가 필요하면 framework context에서 조회하게 한다.
- 같은 이유로 channel messaging에서 일반 message dispatch는 local `ROUTER`
  ingress를 기준으로 설명하는 편이 맞다. outbound `DEALER` 수신은 우선 reply
  correlation 경로로 처리한다.
- `dealer-dealer`는 **channel 등록 표면**에 노출하지 않는다. location 계층에는
  `DealerMesh` auto-connect 종류가 존재하지만 channel 등록 API가 그 값을 받지 않으므로,
  application이 dealer mesh channel을 만들 수는 없다. RouteMesh도 `ROUTER` 구성원끼리
  연결하며, endpoint가 없는 구성원을 `DEALER`로 바꾸지 않는다.
- `SPOT`은 event 전파의 핵심 토대이지만, 필요할 때는 request/reply의 내부
  운반층으로도 쓸 수 있다. 다만 framework 공용 이름은 여전히 socket 이름보다
  상호작용 의미를 먼저 드러내야 한다.
- `SpotNode` peer topology와 channel 단위 호출은 서로 다른 의미다.
- 현재 방향에서는 `SpotNode.router` 경로를 공개 high-level direct API로 그대로
  드러내지 않고, current channel publish와 cross-channel send/request를 분리해서
  설명한다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: ZLink Framework Overview](01-overview.ko.md) | [다음: ZLink Framework Message Model](03-message-model.ko.md)
<!-- framework-adapter-nav:bottom:end -->
