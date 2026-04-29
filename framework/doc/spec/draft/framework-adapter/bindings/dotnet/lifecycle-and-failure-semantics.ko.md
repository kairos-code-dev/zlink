[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Monitoring](./aspnet-core-monitoring.ko.md) | [Registry](./aspnet-core-registry.ko.md)

# Draft -- ZLink Framework .NET Lifecycle And Failure Semantics

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework의 startup, shutdown, reconnect, failure
> 의미를 구현 기준으로 정리한다.

## 1. 목적

framework 구현은 public API 모양만 맞는다고 끝나지 않는다. host lifecycle, startup
validation, reconnect, shutdown 순서가 문서로 닫혀 있어야 회귀 테스트와 운영 코드가
같은 결과를 기대할 수 있다.

## 2. Startup 순서

기본 startup 순서는 아래처럼 본다.

1. registration surface 파싱
2. duplicate name, invalid capability 조합, 누락된 endpoint 같은 설정 검증
3. `Context`와 framework runtime 생성
4. embedded 구성이라면 Registry bind
5. channel runtime, spot node, stream node 시작
6. monitoring source attach
7. application host ready

중요한 점은 아래와 같다.

- 설정 검증 실패는 bind/connect 전에 바로 예외를 던진다.
- embedded registry가 있는 경우 Registry가 먼저 bind되어야 discovery 기반 channel과
  SPOT mesh가 정상 시작될 수 있다.
- monitoring은 source가 생긴 뒤 attach한다.

## 3. Fail-Fast 규칙

아래 항목은 host startup을 실패로 본다.

- invalid registration 조합
- bind 필수 endpoint 누락
- startup 단계의 bind 실패
- startup 단계에서 반드시 만들어야 하는 runtime 객체 생성 실패
- monitoring source 이름 mismatch

반면 아래는 runtime event와 reconnect 정책으로 넘긴다.

- 이미 시작된 뒤의 discovery provider down
- 이미 연결된 peer의 일시 disconnect
- polling source의 일시 query 실패

## 4. Shutdown 순서

기본 shutdown 순서는 아래처럼 본다.

1. monitoring source detach
2. channel runtime, spot node, stream node stop
3. embedded Registry stop
4. `Context` dispose

이 순서를 쓰는 이유는 runtime이 내려가는 동안 monitoring이 새 synthetic event를
계속 만들지 않게 하고, service runtime이 먼저 내려간 뒤 Registry가 정리되게 해서
다른 노드가 topology 변화를 읽을 수 있게 하기 위해서다.

## 5. Request / Send / Publish 실패 의미

| 동작 | 실패 의미 |
|------|-----------|
| `Request(...).ExecAsync(...)` | route-not-ready, timeout, serialization 실패, runtime stop을 예외로 본다 |
| `Send(...).Exec()` | 기본 blocking submit 실패를 예외로 본다 |
| `Publish(...).Exec()` | 기본 blocking submit 실패를 예외로 본다 |
| `WithDontWait()` 또는 non-blocking submit | temporary backpressure만 `false`로 돌려주고, 그 밖의 실패는 예외로 본다 |

## 6. Reconnect 와 Monitoring 의미

- discovery 기반 capability는 provider 집합 변화를 runtime이 따라간다.
- manual capability는 framework가 자동 reconnect policy를 숨겨서 넣지 않는다.
  reconnect가 필요하면 explicit `Connect(...)` 또는 상위 retry policy가 맡는다.
- socket은 하부 monitor event를 직접 감싼다.
- registry/spot는 polling + snapshot diff 기반 synthetic event로 다시 만든다.
- discovery 상태는 별도 event가 아니라 registry snapshot/query로 조회한다.

## 7. Stream Session Error 의미

- `OnConnectedAsync(...)`는 `ConnectionReady` 기준으로 올린다.
- `OnErrorAsync(...)`는 session-correlatable transport 오류만 받는다.
- handshake 실패와 bind/accept/close 실패는 session callback으로 올리지 않고
  monitoring에만 남긴다.
- transport error 뒤 연결 종료가 확인되면 `OnDisconnectedAsync(...)`가 이어질 수
  있다.

## 8. Spot Lifecycle 의미

- `OnInitializeAsync(...)`는 spot 실행 문맥에서 한 번만 호출된다.
- `Configure()`는 `OnInitializeAsync(...)`보다 먼저 한 번 호출되며,
  `Context.AddPacket<THandler>()`, `Context.AddSubscribe<THandler>()`,
  `Context.AddActorJoin<THandler, TActor, TRequest, TReply>()`는 이 단계에서만
  허용된다.
- `OnClosingAsync(...)`는 `IZLinkSpotManager.RemoveAsync(...)`로 SPOT을 정상
  제거할 때 spot 실행 문맥에서 호출된다. host shutdown이나 process 종료에서
  반드시 호출되는 destructor 의미는 아니다.
- framework는 per-spot scope를 만들고, 등록한 handler 타입을 그 scope에서 resolve한다.
- `Context.AddPacket<THandler>()`, `Context.AddSubscribe<THandler>()`, `Context.AddTimer<THandler>()`는
  service locator가 아니라 "이 타입을 spot scope에서 써 달라"는 등록 의미다.
- spot 제거 뒤에는 해당 scope도 함께 정리된다.

## 9. Host 중지 중 호출 의미

- host stopping이 시작되면 새 inbound dispatch는 받지 않는 편을 기본으로 본다.
- 이미 시작된 handler는 cancellation을 전달받고 빠르게 종료할 기회를 가진다.
- graceful timeout을 넘긴 작업은 host shutdown 정책에 따라 중단될 수 있다.
- shutdown 중 새 outbound request 성공을 보장하지 않는다.
