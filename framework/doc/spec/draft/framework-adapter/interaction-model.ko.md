[스펙 목차](../../README.ko.md)

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
[service-topology.ko.md](./service-topology.ko.md)의 section 3을 참고한다.

## 3. 모델별 기본 의미

### 3.1 request-response

- 호출자는 응답을 기다린다.
- timeout, correlation, deadline이 중요하다.
- HTTP 호출이나 gRPC unary와 가장 비슷한 경험을 제공한다.
- 현재 framework 초안의 기본 서버 간 request/reply 토대는
  `ROUTER <-> ROUTER`다.
- 같은 모델을 `SPOT`의 routed request/reply 위에 올려 설명해야 하는 경우도
  있다.
- 다만 `router rid` direct 전송과 `spot` 전송은 주소 체계가 다르다.
  `spot`으로 보낼 때는 `targetRid`와 `spotRid`를 함께 알아야 하므로,
  framework 공용 표면은 별도 함수 이름보다 `RequestTo(...)` 오버로드로 구분하는
  편이 더 자연스럽다.

### 3.2 command

- 호출자는 성공적으로 전송됐는지만 확인하거나, 그마저도 느슨하게 다룰 수 있다.
- 작업 위임, 후처리 트리거, 간단한 signal에 적합하다.
- 현재 framework 초안의 기본 서버 간 send 토대는 `ROUTER <-> ROUTER`다.
- 이 모델도 `SendTo(...)` 오버로드만으로 `router rid` direct 전송과 `spot`
  대상 전송을 함께 설명할 수 있어야 한다.

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
  [service-topology.ko.md](./service-topology.ko.md)의 section 3을 참고한다.
- 서버 간 `send/request`는 프레임워크 사용자에게 HTTP handler 매핑과 비슷한
  방식으로 보여야 한다.
- 이 경로에서 wire header는 공용 handler 시그니처에 직접 노출하지 않는다.
  handler는 typed body와 context만 받는 편을 기본으로 본다.
- header metadata가 필요하면 framework context에서 조회하게 한다.
- `dealer-dealer`는 현재 목표 범위에 넣지 않는다.
- `SPOT`은 event 전파의 핵심 토대이지만, 필요할 때는 request/reply의 내부
  운반층으로도 쓸 수 있다. 다만 framework 공용 이름은 여전히 socket 이름보다
  상호작용 의미를 먼저 드러내야 한다.
- `server -> spot`, `spot -> server`, `spot -> spot`은 모두 가능해야 한다.
  다만 `spot` 주소는 `router rid`와 다르므로, `SendTo(...)` / `RequestTo(...)`
  오버로드에서 `targetRid, spotRid`를 함께 받는 방식으로 구분하는 편이 더
  명확하다.
- 같은 내부 topology를 쓰더라도, use case가 다르면 공용 이름도 다르게 둔다.
  예를 들어 `request-response`와 `worker-dispatch`는 둘 다 어떤 routed transport
  위에 올릴 수 있어도, 같은 개념으로 설명하지 않는다.

## 5. use case와의 연결

| use case | 기본 모델 |
|----------|-----------|
| 일반 웹 백엔드 서비스 호출 | `request-response` |
| playhouse play -> api | `request-response` |
| room/stage/zone 대상 direct call | `request-response` 또는 `command` |
| worker dispatch | `worker-dispatch` 또는 `command` |
| domain event fanout | `publish-subscribe` |
| cache invalidation / config refresh | `publish-subscribe` |
| stage state sync | `publish-subscribe` |
| real-time notification fanout | `publish-subscribe` |
| connection/session gateway | `stream` |
| scatter-gather query | `scatter-gather` |
| workflow orchestration | `request-response` + `publish-subscribe` 조합 |
