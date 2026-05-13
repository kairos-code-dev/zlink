<!-- framework-adapter-nav:start -->
[문서 목록](README.ko.md) | [다음: ZLink Framework](policy/README.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Adapter 문서 목록

> 이 문서 묶음은 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, framework adapter의 정책, use case, 언어별 상세 설계를
> 한곳에서 찾아가기 위한 목록이다.

## 1. 읽는 순서

처음 읽을 때는 아래 순서를 따른다.

1. [정책 문서 목록](./policy/README.ko.md)
2. [Use case 문서 목록](./use-cases/README.ko.md)
3. [Use case 검증](./usecase-validation.ko.md)
4. [언어별 바인딩 상세](#4-언어별-바인딩-상세)

각 문서 위쪽에는 `문서 목록`, `이전`, `다음` 링크가 있다. 긴 문서를 읽다가도
언제든 이 목록으로 돌아오거나 다음 문서로 이동할 수 있다.

## 2. 공통 정책 문서

| 문서 | 다루는 범위 |
|------|-------------|
| [정책 문서 목록](./policy/README.ko.md) | 공통 정책 문서의 전체 목록과 읽는 순서 |
| [개요](./policy/overview.ko.md) | Framework Adapter의 목적과 우선 범위 |
| [상호작용 모델](./policy/interaction-model.ko.md) | request-response, command, publish-subscribe 같은 사용자 모델 |
| [메시지 모델](./policy/message-model.ko.md) | header/body 구조와 metadata 정책 |
| [Channel topology](./policy/channel-topology.ko.md) | channel grouping, discovery, 수동 연결, 내부 transport 매핑 |
| [Framework API](./policy/framework-api.ko.md) | 언어별 framework API의 공통 방향 |
| [Actor 모델](./policy/actor-model.ko.md) | actor 위치, session binding, Entry Spot, user Spot, dispatch 기준 |
| [Session Actor Dispatch 사용성](./policy/session-gateway-usability.ko.md) | session과 actor를 연결하는 helper와 routing 정책 |
| [Session Gateway 보관본](./policy/session-gateway.ko.md) | 이전 초안의 제거 이력과 배경 |

## 3. Use Case 문서

| 문서 | 다루는 범위 |
|------|-------------|
| [Use case 목록](./use-cases/README.ko.md) | use case 문서의 전체 목록과 관리 규칙 |
| [Service To Service RPC](./use-cases/01-service-to-service-rpc.ko.md) | 서비스 간 요청/응답 |
| [Playhouse Play To API](./use-cases/02-playhouse-play-to-api.ko.md) | play 서버에서 api 서버로 보내는 outgame 요청 |
| [Worker Dispatch](./use-cases/03-worker-dispatch.ko.md) | 여러 worker 중 하나에게 작업 분배 |
| [Domain Event Fanout](./use-cases/04-domain-event-fanout.ko.md) | domain event 발행과 구독 |
| [Cache Invalidation And Config Refresh](./use-cases/05-cache-invalidation-and-config-refresh.ko.md) | 캐시 무효화와 설정 갱신 전파 |
| [Stage State Sync](./use-cases/06-stage-state-sync.ko.md) | game stage 또는 실시간 상태 동기화 |
| [Real Time Notification Fanout](./use-cases/07-real-time-notification-fanout.ko.md) | 실시간 알림 fan-out |
| [Scatter Gather Query](./use-cases/08-scatter-gather-query.ko.md) | 여러 대상 질의와 결과 집계 |
| [Workflow Orchestration](./use-cases/09-workflow-orchestration.ko.md) | 여러 서비스 단계를 거치는 workflow |
| [Use case 검증](./usecase-validation.ko.md) | 현재 초안이 use case를 얼마나 설명하는지 점검 |

## 4. 언어별 바인딩 상세

| 언어 | 문서 목록 |
|------|-----------|
| `.NET` | [bindings/dotnet/README.ko.md](./bindings/dotnet/README.ko.md) |
| `Java` | [bindings/java/README.ko.md](./bindings/java/README.ko.md) |
| `Node.js` | [bindings/node/README.ko.md](./bindings/node/README.ko.md) |
| `Python` | [bindings/python/README.ko.md](./bindings/python/README.ko.md) |
| `C++` | [bindings/cpp/README.ko.md](./bindings/cpp/README.ko.md) |
| `Go` | [bindings/go/README.ko.md](./bindings/go/README.ko.md) |
| `Rust` | [bindings/rust/README.ko.md](./bindings/rust/README.ko.md) |

## 5. 유지 규칙

- 새 문서를 추가하면 이 목록과 해당 디렉토리 `README.ko.md`를 함께 갱신한다.
- 공통 정책을 바꿀 때는 언어별 문서에서 같은 의미를 다시 정의하지 않고 링크로
  연결한다.
- 언어별 문서는 공통 의미를 해당 언어의 시그니처와 샘플로 구체화한다.
