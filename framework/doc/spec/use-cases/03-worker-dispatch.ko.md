<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Use Case -- Playhouse Play To API](./02-playhouse-play-to-api.ko.md) | [다음: Use Case -- Domain Event Fanout](./04-domain-event-fanout.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[use case 목록](./README.ko.md) | [Framework 문서 묶음](../../README.ko.md) | [검증](../usecase-validation.ko.md)

# Use Case -- Worker Dispatch

## 1. 상황

일반적인 메시징 시스템에서는 요청/응답만 필요한 것이 아니다. 어떤 작업은 여러
worker 중 하나가 처리하면 충분하고, 호출자는 결과를 기다릴 수도 있고 기다리지
않을 수도 있다.

예를 들면 아래와 같다.

- 이미지 변환 작업 큐
- 로그 집계 작업
- 알림 발송 작업
- 배치 계산 요청

## 2. 사용자가 기대하는 경험

사용자는 `DEALER -> ROUTER`를 직접 드러낸 worker pool보다, 아래와 같은 의미를
원한다.

- 작업을 발행한다.
- 여러 worker 중 하나가 처리한다.
- 필요하면 결과를 기다린다.
- 필요 없으면 one-way command로 보낸다.

즉 이 use case는 topology보다 **작업 분배 의미**가 더 중요하다.

## 3. 필요한 능력

- worker group 단위 라우팅
- 여러 worker에 대한 load balancing
- optional reply
- backpressure surface
- timeout 또는 취소

## 4. 내부 매핑 초안

이 use case는 내부적으로 `DEALER -> ROUTER`로 구현할 수 있다.
하지만 공용 API 이름을 `dealer`나 `router`로 노출하는 것은 바람직하지 않다.

더 자연스러운 공용 개념은 아래와 같다.

- `dispatch`
- `command`
- `worker group`
- `task handler`

## 5. 미해결 점

이 use case는 다른 케이스보다 아직 덜 정해져 있다.
특히 아래는 구현 전에 더 논의가 필요하다.

- 재시도 책임을 `ZLink Framework`가 질지 응용이 질지
- worker 장애 시 in-flight task를 어떻게 다룰지
- 정확히 한 번 처리 같은 강한 보장을 목표로 할지

따라서 현재 스펙에서는 이 케이스를 **중요하지만 1차 MVP 고정 범위는 아님**으로
본다.
