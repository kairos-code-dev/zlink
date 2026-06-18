<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Use Case -- Scatter Gather Query](08-scatter-gather-query.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[use case 목록](README.ko.md) | [Framework 문서 묶음](../../../README.ko.md) | [검증](../usecase-validation.ko.md)

# Use Case -- Workflow Orchestration

## 1. 상황

어떤 비즈니스 작업은 서비스 하나에 요청을 보내고 끝나지 않는다.
여러 서비스가 순서대로 참여하고, 일부 단계가 실패하면 보상 처리나 상태 정리가
필요할 수 있다.

예를 들면 아래와 같다.

- 결제 승인 -> 인벤토리 차감 -> 주문 확정
- 매치 생성 -> 세션 할당 -> 알림 발행
- 계정 생성 -> 기본 데이터 초기화 -> 환영 메시지 발행

## 2. 사용자가 기대하는 경험

- 각 단계는 익숙한 request-response 또는 event handler로 구현하고 싶다.
- workflow 전체의 상관관계 정보는 공통으로 전달되길 원한다.
- 단계 실패를 추적하고 보상 처리 여부를 판단할 수 있어야 한다.

## 3. 필요한 능력

- correlation, causation, trace 성격의 metadata
- request-response와 event를 섞어 쓰는 모델
- 명확한 에러 전파
- 선택적 보상 처리 훅

## 4. 내부 매핑 초안

이 use case는 새로운 socket topology가 필요한 문제라기보다,
기존 `request-response`와 `publish-subscribe`를 함께 쓰는 조합 문제에 가깝다.

즉 `ZLink Framework`가 해야 할 일은 아래 쪽일 가능성이 크다.

- workflow에 필요한 metadata를 공통 context로 전달
- 단계별 handler 모델을 일관되게 제공
- tracing과 timeout 같은 횡단 관심사를 함께 설명

## 5. 이 use case가 설계에 주는 요구

- header model이 충분히 풍부해야 한다.
- request-response와 event 모델이 서로 단절되면 안 된다.
- 단일 요청 처리보다 긴 생명주기의 상관관계를 어떻게 표현할지 나중에 더
  정해야 한다.

## 6. 현재 위치

이 use case는 중요한 운영 시나리오지만, 현재 스펙에서는 아직 공통 metadata
요구를 드러내는 참고 케이스에 가깝다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Use Case -- Scatter Gather Query](08-scatter-gather-query.ko.md)
<!-- framework-adapter-nav:bottom:end -->
