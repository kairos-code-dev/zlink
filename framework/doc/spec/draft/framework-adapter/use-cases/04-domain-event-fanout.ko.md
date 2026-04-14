[스펙 목차](../../../README.ko.md)

# Draft Use Case -- Domain Event Fanout

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 이벤트 전파 use case를 정리하기 위한 초안이다.

## 1. 상황

일반적인 메시징 시스템에서는 서비스 호출 외에도 이벤트 전파가 자주 필요하다.
한 서비스가 이벤트를 발행하고, 여러 서비스가 각자 필요에 따라 구독하는 형태다.

예를 들면 아래와 같다.

- `user.created`
- `order.paid`
- `match.started`
- `guild.updated`

## 2. 사용자가 기대하는 경험

- 발행자는 특정 소비자를 직접 알 필요가 없다.
- 소비자는 관심 있는 이벤트만 구독한다.
- 이벤트 body는 typed object로 다루고 싶다.
- 프레임워크에서는 익숙한 event handler 방식으로 받고 싶다.

## 3. 필요한 능력

- publish-subscribe
- topic 또는 pattern 기반 구독
- codec 교체
- metadata 조회
- 다수 소비자 fan-out

## 4. 내부 매핑 초안

이 use case는 raw `PUB/SUB` 또는 `SPOT` 기반으로 설명할 수 있다.
어느 쪽을 쓰더라도 공용 모델은 아래처럼 단순해야 한다.

- `publish`
- `subscribe`
- `event handler`
- `event name`

## 5. 이 use case가 설계에 주는 요구

- request-response와 별도의 상호작용 모델이 필요하다.
- one-way 이벤트와 응답이 있는 호출을 같은 API로 억지로 묶지 않는 편이 좋다.
- NestJS의 `@EventPattern`처럼 event 전용 표면이 있는 편이 이해하기 쉽다.
