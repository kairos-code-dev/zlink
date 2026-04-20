[스펙 목차](../../../README.ko.md)

[Framework 초안 묶음](../README.ko.md) | [개요](../overview.ko.md) | [검증](../usecase-validation.ko.md)

# Draft -- ZLink Framework Use Cases

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 여기 적힌 use case 분류와 우선순위는 구현 전에
> 바뀔 수 있다.

## 1. 목적

`ZLink Framework` 초안은 socket 조합보다 use case를 먼저 기준으로 잡는다.
프레임워크 사용자는 보통 `DEALER -> ROUTER`를 원해서 이 계층을 쓰는 것이
아니라, "서비스 호출", "이벤트 발행", "작업 위임", "상태 동기화"를 쉽게 하고
싶어서 이 계층을 쓴다.

그래서 use case는 하나의 큰 문서에 섞어 적지 않고, **case 별 문서**로 나눈다.
새 요구가 들어오면 기존 문서에 문단을 덧붙이기보다, 새 case 문서를 만들고
필요하면 validation 문서를 갱신한다.

## 2. 문서 목록

| 문서 | 설명 |
|------|------|
| [01-service-to-service-rpc.ko.md](./01-service-to-service-rpc.ko.md) | 일반적인 웹 백엔드 서비스 간 요청/응답 |
| [02-playhouse-play-to-api.ko.md](./02-playhouse-play-to-api.ko.md) | `playhouse`에서 play 서버가 api 서버로 outgame 요청 |
| [03-worker-dispatch.ko.md](./03-worker-dispatch.ko.md) | 여러 worker 중 하나에게 작업을 분배 |
| [04-domain-event-fanout.ko.md](./04-domain-event-fanout.ko.md) | 서비스가 이벤트를 발행하고 여러 소비자가 구독 |
| [05-cache-invalidation-and-config-refresh.ko.md](./05-cache-invalidation-and-config-refresh.ko.md) | 캐시 무효화와 설정 갱신 전파 |
| [06-stage-state-sync.ko.md](./06-stage-state-sync.ko.md) | 게임 stage 또는 실시간 상태 동기화 |
| [07-real-time-notification-fanout.ko.md](./07-real-time-notification-fanout.ko.md) | 실시간 알림을 여러 front 또는 gateway 노드로 전파 |
| [08-scatter-gather-query.ko.md](./08-scatter-gather-query.ko.md) | 여러 shard 또는 provider에 질의하고 결과를 모음 |
| [09-workflow-orchestration.ko.md](./09-workflow-orchestration.ko.md) | 여러 서비스 단계를 거치는 workflow 또는 saga 성격의 처리 |

## 3. 관리 규칙

- 케이스 문서는 "누가", "왜", "무엇을 기대하는가"를 먼저 적는다.
- topology 이름은 설명에 필요할 때만 뒤쪽에 적는다.
- 한 use case가 공통 설계에 요구하는 능력은 `interaction-model`,
  `message-model`, `channel-topology`, `framework-api` 문서에서 받아준다.
- 어떤 케이스가 현재 초안으로 충분히 설명되지 않으면
  `usecase-validation.ko.md`에서 `부분 만족` 또는 `미해결`로 남긴다.
- 비슷해 보이는 케이스라도 운영 요구가 다르면 문서를 분리한다.
  예를 들어 일반 `domain event fanout`과 `real-time notification fanout`은
  둘 다 fan-out이지만, 대상 선택과 지연 요구가 다르므로 따로 적는 편이 낫다.
