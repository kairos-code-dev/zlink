<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Use Case -- Cache Invalidation And Config Refresh](./05-cache-invalidation-and-config-refresh.ko.md) | [다음: Use Case -- Real Time Notification Fanout](./07-real-time-notification-fanout.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[use case 목록](./README.ko.md) | [Framework 문서 묶음](../../README.ko.md) | [검증](../usecase-validation.ko.md)

# Use Case -- Stage State Sync

## 1. 상황

게임 서버나 실시간 시스템에서는 여러 노드가 상태 일부를 서로 빠르게 공유해야
할 때가 있다. `playhouse`에서는 play 서버의 stage가 `SPOT` 기반으로 구성될
예정이므로, stage 간 상태 동기화는 중요한 기준 use case다.

예를 들면 아래와 같다.

- 인접 stage의 엔티티 상태 전파
- 룸 또는 채널 단위 상태 동기화
- presence, position, phase 변화 알림

## 2. 사용자가 기대하는 경험

- 특정 subject, topic, stage key를 기준으로 상태를 발행한다.
- 관심 있는 쪽만 구독한다.
- latency가 낮아야 한다.
- topology 변화를 매번 응용이 직접 관리하지 않아도 된다.

## 3. 필요한 능력

- publish-subscribe
- subject 또는 topic 기반 라우팅
- Discovery 기반 peer 구성
- 실시간 처리에 맞는 낮은 ceremony

## 4. 내부 매핑 초안

이 use case는 `SPOT`과 가장 자연스럽게 맞는다.
다만 `ZLink Framework`는 `SPOT`이라는 내부 구현 이름보다 아래 경험을 먼저
보여 주는 편이 낫다.

- `publish state`
- `subscribe state`
- `subject`
- `state handler`

## 5. 이 use case가 설계에 주는 요구

- `ZLink Framework`가 RPC 전용 계층으로 보이면 안 된다.
- event 모델이 실시간 상태 동기화에도 자연스럽게 확장되어야 한다.
- `playhouse`용 특수 기능처럼 닫지 말고, 일반적인 실시간 state sync 패턴으로
  설명할 수 있어야 한다.
