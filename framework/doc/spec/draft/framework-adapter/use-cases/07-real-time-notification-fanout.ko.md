<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft Use Case -- Stage State Sync](06-stage-state-sync.ko.md) | [다음: Draft Use Case -- Scatter Gather Query](08-scatter-gather-query.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[use case 목록](./README.ko.md) | [Framework 초안 묶음](../README.ko.md) | [검증](../usecase-validation.ko.md)

# Draft Use Case -- Real Time Notification Fanout

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 실시간 알림 전파 use case를 정리하기 위한 초안이다.

## 1. 상황

일반적인 서비스 이벤트와 달리, 어떤 알림은 여러 front 노드나 session actor dispatch 계층으로
아주 빨리 퍼져야 한다. 수신자는 내부 worker가 아니라 현재 연결을 붙잡고 있는
front 계층일 수 있다.

예를 들면 아래와 같다.

- 특정 유저에게 즉시 알림 전송
- 길드 공지나 매치 시작 알림
- 운영자 강제 메시지 브로드캐스트
- 친구 접속 상태 변경 통지

## 2. 사용자가 기대하는 경험

- 알림 발행자는 현재 어느 front 노드가 사용자를 들고 있는지 직접 알 필요가 없다.
- front 계층은 관심 있는 알림만 빠르게 수신한다.
- 같은 알림을 여러 노드가 받아도 되는지, 특정 그룹만 받아야 하는지를 표현할 수
  있어야 한다.

## 3. 필요한 능력

- 낮은 지연의 one-way fan-out
- topic, user group, channel 같은 대상 표현
- 여러 front 인스턴스에 대한 전파
- 필요하면 일부 대상만 고르는 routing 힌트

## 4. 다른 event use case와의 차이

이 use case는 [Domain Event Fanout](./04-domain-event-fanout.ko.md)과 비슷해
보이지만, 관심사가 다르다.

- domain event는 느슨한 내부 소비자 모델이 중심이다.
- real-time notification은 front 계층 전파와 지연 요구가 더 중요하다.

즉 둘 다 `publish-subscribe`로 설명할 수 있지만, 같은 운영 요구로 보면 안 된다.

## 5. 내부 매핑 초안

기본 매핑은 `PUB/SUB` 또는 `SPOT` 계열이 자연스럽다.
다만 공용 표면은 아래 경험이 먼저 보여야 한다.

- `publish notification`
- `subscribe notification`
- `channel`, `group`, `target key`

## 6. 이 use case가 설계에 주는 요구

- event 모델이 내부 시스템 이벤트와 사용자 대면 실시간 알림 둘 다 설명할 수
  있어야 한다.
- 단순 broadcast만이 아니라 target grouping을 함께 고려해야 한다.
- backpressure나 느린 subscriber 문제를 운영 관점에서 어떻게 볼지 나중에
  별도 설계가 필요하다.
