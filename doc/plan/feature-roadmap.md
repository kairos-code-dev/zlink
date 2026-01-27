# zlink Feature Roadmap

> zlink는 단순 소켓 라이브러리가 아니라 **개발 편의성을 위한 통합 메시징 스택**을 지향한다.
> 특히 서비스 디스커버리 + gateway 기능을 통해 **서비스 내부 통신을 빠르고 직관적으로 구성**하는 것을 목표로 한다.
> 외부 트래픽의 엣지 게이트웨이 역할은 필요 시 기존 LB/API Gateway와 병행 가능하도록 설계한다.

## 기반 기능 (Foundation)

| 우선순위 | 기능 | 설명 | 스펙 문서 |
|:--------:|------|------|:----------:|
| 0 | Routing ID 통합 | zmq_routing_id_t 표준 타입, 자동 생성 값 uint32 통일 | [00](00-routing-id-unification.md) |

## 핵심 기능 (Core Features)

| 우선순위 | 기능 | 설명 | 스펙 문서 |
|:--------:|------|------|:----------:|
| 1 | 모니터링 강화 | routing_id 기반 이벤트 식별, 사용 편의성 개선 | [01](01-enhanced-monitoring.md) |
| 2 | Thread-safe 소켓 | 여러 스레드에서 하나의 소켓 안전하게 사용 | [02](02-thread-safe-socket.md) |
| 3 | Request/Reply API | ROUTER 소켓에서 요청-응답 패턴 쉽게 처리 | [03](03-request-reply-api.md) |
| 4 | 서비스 디스커버리 | Registry 클러스터 기반, 클라이언트 사이드 로드밸런싱 | [04](04-service-discovery.md) |
| 5 | SPOT 토픽 PUB/SUB | 토픽 기반 발행/구독, 패턴 구독, 위치 투명성 | [05](05-spot-topic-pubsub.md) |

## 추천 기능 (Recommended Features)

| 우선순위 | 기능 | 설명 | 스펙 문서 |
|:--------:|------|------|:----------:|
| 6 | 메시지 우선순위 | 큐 레벨 QoS (priority 필드 활성화) | - |
| 7 | 메시지 TTL | 오래된 메시지 자동 폐기 | - |
| 8 | 메트릭스 API | 송수신 메시지 수/바이트, 큐 상태, 드롭 수 | - |

## 의존성 (Dependencies)

```
[0] Routing ID 통합
 ├──> [1] 모니터링 강화
 ├──> [3] Request/Reply API
 ├──> [4] 서비스 디스커버리
 └──> [5] SPOT 토픽 PUB/SUB

[2] Thread-safe 소켓
 └──> [3] Request/Reply API
```

**[0] Routing ID 통합**은 다른 기능의 선행 작업이며, routing_id를 사용하는 [1]/[3]/[4]/[5]에 직접적인 영향을 미침.
