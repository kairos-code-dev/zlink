[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Interaction Model

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 상호작용 모델의 구분과 이름은 구현 전에 바뀔 수
> 있다.

## 1. 목적

`ZLink Framework` 공용 표면은 socket 이름보다 **상호작용 모델**을 먼저
드러내야 한다.
프레임워크 사용자는 보통 `ROUTER`, `DEALER`, `PUB`가 아니라 아래 중 하나를
원한다.

- 요청하고 응답받기
- 작업만 보내기
- 이벤트 발행하기
- 상태를 구독하기

## 2. 제안하는 공용 상호작용 모델

| 모델 | 설명 | 기본 내부 매핑 초안 | 1차 우선순위 |
|------|------|---------------------|--------------|
| `request-response` | 요청 하나에 응답 하나가 돌아온다 | `DEALER -> ROUTER`, 필요하면 routed `SPOT` request/reply | 높음 |
| `command` | 응답을 기다리지 않는 one-way 전송 | `DEALER -> ROUTER` 또는 routed send | 높음 |
| `publish-subscribe` | 발행자와 구독자가 느슨하게 연결된다 | `PUB/SUB` 또는 `SPOT` | 높음 |
| `worker-dispatch` | 여러 worker 중 하나가 처리한다 | `DEALER -> ROUTER` | 중간 |
| `scatter-gather` | 여러 대상에 요청을 보내고 결과를 모은다 | 여러 `request-response` 조합 | 낮음 |
| `stream` | 연속 메시지 교환 | 별도 설계 필요 | 낮음 |

## 3. 모델별 기본 의미

### 3.1 request-response

- 호출자는 응답을 기다린다.
- timeout, correlation, deadline이 중요하다.
- HTTP 호출이나 gRPC unary와 가장 비슷한 경험을 제공한다.
- 기본 토대는 `DEALER -> ROUTER`가 가장 자연스럽지만, 같은 모델을
  `SPOT`의 routed request/reply 위에 올려 설명해야 하는 경우도 있다.

### 3.2 command

- 호출자는 성공적으로 전송됐는지만 확인하거나, 그마저도 느슨하게 다룰 수 있다.
- 작업 위임, 후처리 트리거, 간단한 signal에 적합하다.

### 3.3 publish-subscribe

- 발행자는 수신자 목록을 직접 알지 않는다.
- 여러 소비자가 같은 이벤트를 동시에 처리할 수 있다.
- domain event와 state sync 양쪽에 쓸 수 있다.

### 3.4 worker-dispatch

- 의미상으로는 command 또는 request-response의 변형이지만, 사용자 기대가
  다르므로 별도 use case로 본다.
- 사용자는 "어느 worker가 받는가"보다 "worker group에 작업을 보낸다"를 먼저
  떠올린다.

### 3.5 scatter-gather

- 하나의 논리 요청이 여러 실제 요청으로 fan-out된다.
- 결과를 일부만 모을지 모두 기다릴지 정책이 필요하다.
- 단일 unary RPC의 단순 확장이 아니라 aggregate 모델에 가깝다.

## 4. 기본 원칙

- `router-router`는 고급 내부 모델로 남길 수 있지만, 1차 공용 API의 중심으로
  두지 않는다.
- `dealer-dealer`는 현재 목표 범위에 넣지 않는다.
- `SPOT`은 event 전파의 핵심 토대이지만, 필요할 때는 request/reply의 내부
  운반층으로도 쓸 수 있다. 다만 framework 공용 이름은 여전히 socket 이름보다
  상호작용 의미를 먼저 드러내야 한다.
- 같은 내부 topology를 쓰더라도, use case가 다르면 공용 이름도 다르게 둔다.
  예를 들어 `request-response`와 `worker-dispatch`는 둘 다
  `DEALER -> ROUTER`로 구현할 수 있지만, 같은 개념으로 설명하지 않는다.

## 5. use case와의 연결

| use case | 기본 모델 |
|----------|-----------|
| 일반 웹 백엔드 서비스 호출 | `request-response` |
| playhouse play -> api | `request-response` |
| worker dispatch | `worker-dispatch` 또는 `command` |
| domain event fanout | `publish-subscribe` |
| cache invalidation / config refresh | `publish-subscribe` |
| stage state sync | `publish-subscribe` |
| real-time notification fanout | `publish-subscribe` |
| scatter-gather query | `scatter-gather` |
| workflow orchestration | `request-response` + `publish-subscribe` 조합 |
