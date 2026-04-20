[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [use cases](./use-cases/README.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [검증](./usecase-validation.ko.md) | [.NET](./dotnet/README.ko.md)

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

| 모델 | 설명 | 1차 우선순위 |
|------|------|--------------|
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
- node 간 직접 경로가 꼭 필요할 때만 `ROUTER <-> ROUTER`를 별도 내부 경로로
  둔다.
- 다만 같은 SPOT mesh 안의 `spot-to-spot` request/reply는
  `ROUTER <-> ROUTER` routed 경로로 설명하는 편이 맞다.
- 다만 최신 SPOT topology 초안에서는 high-level public surface에서
  `targetRid + spotRid` routed 호출을 기본으로 두지 않는다. current SPOT
  channel 안의 상호작용과 다른 channel 호출은 분리해서 설명하는 편이 더
  자연스럽다.

### 3.2 command

- 호출자는 성공적으로 전송됐는지만 확인하거나, 그마저도 느슨하게 다룰 수 있다.
- 작업 위임, 후처리 트리거, 간단한 signal에 적합하다.
- 현재 framework 초안의 기본 channel send 토대는
  `DEALER(client) -> ROUTER(server)`다.
- 다른 channel에 접근할 때는 그 channel에 붙은 `DEALER(client)`를 통해 보내는
  구조를 기본으로 본다.
- node 간 직접 send가 꼭 필요할 때만 `ROUTER <-> ROUTER`를 별도 내부 경로로
  둔다.
- 다만 같은 SPOT mesh 안의 `spot-to-spot` send는
  `ROUTER <-> ROUTER` routed 경로로 설명하는 편이 맞다.
- SPOT 쪽은 routed 호출보다 attach된 channel client를 통한
  `SendChannelAsync(...)` 같은 표면이 먼저 보이는 편이 더 자연스럽다.

### 3.3 publish-subscribe

- 발행자는 수신자 목록을 직접 알지 않는다.
- 여러 소비자가 같은 이벤트를 동시에 처리할 수 있다.
- domain event와 state sync 양쪽에 쓸 수 있다.

### 3.4 stream

- 연결 수명과 수신 이벤트가 중요하다.
- 일반 request handler와 같은 모양으로 억지로 맞추기보다, stream 전용
  connection 또는 packet handler 모델이 필요하다.
- packet framing 규약을 framework가 얼마나 감출지는 별도 설계가 필요하다.

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
| connection/session gateway | `stream` |
| scatter-gather query | `scatter-gather` |
| workflow orchestration | `request-response` + `publish-subscribe` 조합 |
