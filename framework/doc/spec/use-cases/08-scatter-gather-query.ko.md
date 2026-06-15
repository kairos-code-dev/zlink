<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Use Case -- Real Time Notification Fanout](./07-real-time-notification-fanout.ko.md) | [다음: Use Case -- Workflow Orchestration](./09-workflow-orchestration.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[use case 목록](./README.ko.md) | [Framework 문서 묶음](../../README.ko.md) | [검증](../usecase-validation.ko.md)

# Use Case -- Scatter Gather Query

## 1. 상황

어떤 조회는 서비스 하나에 request-response를 한 번 보내는 것으로 끝나지 않는다.
여러 shard, 여러 provider, 여러 stage에 동시에 질의하고, 그 결과를 모아서 하나의
응답으로 만들고 싶을 수 있다.

예를 들면 아래와 같다.

- 여러 shard에 플레이어 상태 질의
- 여러 검색 provider 결과 병합
- 여러 zone 또는 stage의 상태 요약 수집

## 2. 사용자가 기대하는 경험

- 호출자는 병렬 fan-out과 결과 수집을 직접 socket 수준에서 짜고 싶어하지 않는다.
- 전체 timeout과 부분 실패를 한 번에 다루고 싶다.
- 일부 결과만 와도 응답할지, 모두 기다릴지 정책을 정하고 싶다.

## 3. 필요한 능력

- 다수 대상 fan-out request
- deadline 또는 aggregate timeout
- partial success 표현
- 결과 병합 정책

## 4. 내부 매핑 초안

기본 토대는 여러 `request-response` 호출의 조합이다.
하지만 공용 표면은 단순 unary RPC와 구분되는 것이 좋다.

예를 들면 아래 같은 개념이 필요할 수 있다.

- `query many`
- `fanout request`
- `aggregate result`

## 5. 현재 스펙과의 관계

현재 문서 묶음은 unary `request-response`를 중심으로 설명한다.
따라서 이 use case는 그 위에 더 높은 수준의 조합 계층이 필요함을 보여 준다.

## 6. 이 use case가 설계에 주는 요구

- 단일 request-response만으로는 부족한 운영 시나리오가 있다는 점을 드러낸다.
- timeout과 에러 모델이 "한 요청/한 응답"에만 묶이지 않아야 한다.
- 프레임워크 adapter가 1차 MVP에서 이 기능을 직접 제공할지, 응용 helper로
  둘지는 따로 결정해야 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: Use Case -- Real Time Notification Fanout](./07-real-time-notification-fanout.ko.md) | [다음: Use Case -- Workflow Orchestration](./09-workflow-orchestration.ko.md)
<!-- framework-adapter-nav:bottom:end -->
