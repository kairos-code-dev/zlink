# zlink Feature Roadmap

> zlink는 단순 소켓 라이브러리가 아니라 **개발 편의성을 위한 통합 메시징 스택**을 지향한다.
> 특히 서비스 디스커버리 + gateway 기능을 통해 **서비스 내부 통신을 빠르고 직관적으로 구성**하는 것을 목표로 한다.
> 외부 트래픽의 엣지 게이트웨이 역할은 필요 시 기존 LB/API Gateway와 병행 가능하도록 설계한다.

## 구현 완료 기능

| 기능 | 설명 | 가이드 |
|------|------|:------:|
| Routing ID 통합 | zlink_routing_id_t 표준 타입, own 16B UUID / peer 4B uint32 | [guide/08](../guide/08-routing-id.md) |
| 모니터링 강화 | routing_id 기반 이벤트 식별, polling 방식 모니터 API | [guide/06](../guide/06-monitoring.md) |
| 서비스 디스커버리 | Registry 클러스터 기반, 클라이언트 사이드 로드밸런싱 | [guide/07-1](../guide/07-1-discovery.md) |
| Gateway | Discovery 기반 위치투명 요청/응답 | [guide/07-2](../guide/07-2-gateway.md) |
| SPOT 토픽 PUB/SUB | 토픽 기반 발행/구독, 패턴 구독, 위치 투명성 | [guide/07-3](../guide/07-3-spot.md) |
| SPOT 콜백 핸들러 | spot_sub 콜백 기반 메시지 수신 (v0.9.0) | [guide/07-3](../guide/07-3-spot.md) |

## 향후 계획

| 기능 | 설명 | 상세 |
|------|------|:----:|
| PUB/SUB PGM/EPGM | 멀티캐스트 전송(고성능 fanout) 지원 | [internals/design-decisions](../internals/design-decisions.md) |
| Discovery 타입 분리 | Registry/Discovery에 service_type 도입, Gateway/SPOT 분리 운영 | [type-segmentation](type-segmentation.md) |

## 의존성

```
Routing ID 통합
 ├──> 모니터링 강화
 ├──> 서비스 디스커버리
 └──> SPOT 토픽 PUB/SUB

서비스 디스커버리
 ├──> Gateway
 └──> SPOT 토픽 PUB/SUB (자동 연결)
```

## 테스트 작성 규칙

- 기능별로 `tests/<feature>/` 폴더를 만들고 그 아래에 `test_*.cpp`를 둔다.
  - 예: `tests/spot/test_spot_pubsub_basic.cpp`
