[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [use cases](./use-cases/README.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [.NET](./dotnet/README.ko.md)

# Draft -- ZLink Framework Use Case Validation

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, use case 관점에서 설계 방향을 점검하는 문서다.

## 1. 목적

이 문서는 케이스별 요구가 현재 초안으로 설명되는지 확인한다.
여기서의 "만족"은 구현 완료를 뜻하지 않는다. 현재 draft 문서 묶음이 그
use case를 설명할 틀을 갖추었는지를 본다.

## 2. 검증 기준

아래 항목을 기준으로 본다.

- 맞는 상호작용 모델이 있는가
- message model이 필요한 metadata를 담을 수 있는가
- channel name과 연결 방식이 설명되는가
- 프레임워크 사용자가 익숙한 API로 올릴 수 있는가

## 3. use case별 점검

| use case | 필요한 모델 | 현재 초안 상태 | 판정 | 메모 |
|----------|-------------|----------------|------|------|
| [Service To Service RPC](./use-cases/01-service-to-service-rpc.ko.md) | request-response | `interaction-model`, `message-model`, `framework-api`, `channel-topology`에 설명 있음 | 만족 | 1차 핵심 범위 |
| [Playhouse Play To API](./use-cases/02-playhouse-play-to-api.ko.md) | request-response | channel별 client, Discovery, channel grouping 설명 있음 | 만족 | 대표 기준 use case |
| [Worker Dispatch](./use-cases/03-worker-dispatch.ko.md) | worker-dispatch, command | 모델은 있으나 retry, in-flight failure, 처리 보장 논의가 부족함 | 부분 만족 | 후속 설계 필요 |
| [Domain Event Fanout](./use-cases/04-domain-event-fanout.ko.md) | publish-subscribe | event 모델과 framework handler 방향이 설명됨 | 만족 | 일반 메시징 시스템 핵심 use case |
| [Cache Invalidation And Config Refresh](./use-cases/05-cache-invalidation-and-config-refresh.ko.md) | publish-subscribe | 운영성 이벤트 용도까지 무리 없이 설명 가능 | 만족 | 일반 메시징 시스템 핵심 use case |
| [Stage State Sync](./use-cases/06-stage-state-sync.ko.md) | publish-subscribe | `SPOT` 기반 설명과 subject/topic 방향이 연결됨 | 만족 | `playhouse`와 직접 연결됨 |
| [Real Time Notification Fanout](./use-cases/07-real-time-notification-fanout.ko.md) | publish-subscribe | fan-out 모델로 설명 가능하지만 target grouping과 subscriber 정책이 덜 정해짐 | 부분 만족 | front 계층 use case 보강 필요 |
| [Scatter Gather Query](./use-cases/08-scatter-gather-query.ko.md) | scatter-gather | interaction-model에 항목을 추가했지만 aggregate result와 timeout 정책이 부족함 | 부분 만족 | 고급 request 조합 모델 필요 |
| [Workflow Orchestration](./use-cases/09-workflow-orchestration.ko.md) | request-response + publish-subscribe | header metadata 방향은 있으나 보상 처리와 장기 상관관계 모델은 미정 | 부분 만족 | metadata와 tracing 설계 필요 |

## 4. 현재 초안의 결론

현재 문서 묶음은 아래 방향을 설명하는 데 충분하다.

- 일반적인 서비스 간 요청/응답
- 일반적인 이벤트 fan-out
- 운영성 메시지 전파
- `playhouse`의 play -> api 요청
- stage/state sync
- 실시간 알림 fan-out의 기본 방향

반면 아래는 아직 부족하다.

- worker dispatch의 재시도 정책
- worker 장애 시 in-flight task 의미
- 실시간 알림의 target grouping 규칙
- scatter-gather의 aggregate 결과 모델
- workflow orchestration의 장기 상관관계와 보상 처리 모델
- stream을 공용 모델로 노출할지 여부

## 5. 문서 유지 규칙

- 새 use case를 추가하면 이 문서 표에도 같은 줄을 추가한다.
- 어떤 케이스가 `부분 만족`이면, 부족한 항목을 공통 문서에 반영한 뒤 다시
  판정을 갱신한다.
