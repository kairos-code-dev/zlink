<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot 서비스](config-2-spot-service.ko.md) | [다음: 등록·codec](config-4-registration-codec.ko.md)
<!-- framework-adapter-nav:end -->

# Config 3 — Pub/Sub 이벤트 배포

이벤트를 뿌리는 형상이다. publisher 하나가 이벤트를 내보내고 여러 subscriber가 받는 배포를 한
번 띄워 두고, fanout(여럿에게 퍼지는지)과 topic 필터가 실제 사용자처럼 도는지 본다.

## 1. 목적과 범위

- 다룬다: fanout 전달, topic 필터(application-level, publish context.Topic 기반), late subscriber 합류,
  subscriber 재연결, publisher 재시작, subscriber 느린 handler, publish negative path.
- 여기서 다루지 않는 것: channel request/send(Config 1), spot publish(Config 2).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록하고, fanout 연결에 필요한 peer location row는 framework lifecycle이 자동 갱신한다. |
| publisher | 1 (`pub-a`) | publish channel server. `EventPublish(topic, value)` 발행. peer location row 자동 등록. `/evidence`·`/health`. |
| subscriber | 3 (`sub-1`, `sub-2`, `sub-3`) | subscribe handler 보유. 받은 이벤트와 public socket lifecycle의 `ConnectionReady`·`Disconnected`를 evidence로 기록. handler가 publish context.Topic으로 관심 topic만 처리. |
| consumer | 시나리오별 | publish를 트리거하거나 직접 subscribe하는 client. |

handler 동작(공유): subscriber는 `EventNotify`를 받아 publish context.Topic과 value를 evidence에
쌓되, 자기 관심 topic만 기록한다. handler가 없는 message name으로 오는 publish는 subscriber
dispatch에서 drop되고 observer marker가 남는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → publisher → subscriber 순으로 띄운다. late
subscriber 시나리오는 subscriber 하나를 일부러 늦게 띄운다. client 시나리오가 publish를
트리거하고 각 subscriber의 evidence를 조회해 확인한다. 실행이 끝나면 전용 prefix의 key를
정리하거나 disposable Redis instance를 버린다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — fanout과 필터

#### PS-A1 fanout basic delivery

우선순위: `P0`

**한마디로:** 세 subscriber의 구독 준비가 끝난 뒤 측정 구간에서 같은 연속 sequence를 같은 순서로
받는가.

- 절차: 세 subscriber 각각의 `ConnectionReady`를 기다린 뒤 측정용 고유 sequence를 발행한다.
- 검증: 측정 구간에서 모든 subscriber가 공유하는 하나의 연속 sequence가 존재하고 세 subscriber 모두
  그 sequence를 같은 순서로 수신한다. `Publish(...).Async()` 완료 자체를 remote 수신 evidence로 쓰지
  않고 subscriber handler evidence로 판정한다.
- 세부 동작: 구독 readiness 뒤 공통 sequence fanout.

#### PS-A2 topic filter

우선순위: `P0`

**한마디로:** 여러 topic으로 뿌려도, 각 subscriber가 자기 관심 topic만 골라서 처리하고 나머지는 흘려보내는가.

- 절차: 여러 topic으로 발행한다. channel fanout은 subscriber transport 단계의 topic 필터를 노출하지 않으므로([20 §3.3](../../spec/server/20-spot-messaging.ko.md)) subscriber는 전량 수신하되 handler가 publish context의 topic을 보고 관심 topic만 처리한다.
- 검증: 각 subscriber handler가 publish context.Topic으로 자신의 관심 topic만 evidence에 기록한다. 비관심 topic은 기록되지 않는다.
- 세부 동작: publish context.Topic 기반 application-level 필터링. **이것이 channel fanout의 계약이다** — topic이 subscriber set을 고르는 것은 SPOT subscribe 표면이며, 두 표면의 구분은 [20 §3.3](../../spec/server/20-spot-messaging.ko.md)이 소유한다. fanout에 transport topic 필터를 추가하려면 공개 계약 확장 절차를 먼저 거친다.

#### PS-A3 late subscriber

우선순위: `P0`

**한마디로:** 발행이 시작된 뒤 새 subscriber가 연결되면, 구독 준비가 끝난 뒤 발행한 event부터 받고
그 전에 발행한 event는 replay하지 않는가.

- 절차: publisher가 고유 sequence의 `before-ready` event를 발행한다 → 새 subscriber를 시작한다 →
  subscriber의 `ConnectionReady`를 기다린다 → 고유 sequence의
  `after-ready` event를 한 번 발행한다.
- 검증: late subscriber는 `after-ready` event를 받고 payload가 일치한다. `before-ready` event는
  subscriber evidence에 없으며 준비 완료 뒤에도 replay되지 않는다. publish 완료는 subscriber 수신
  acknowledgement로 사용하지 않는다.
- 세부 동작: 현재 연결과 구독 준비가 끝난 subscriber에게만 전달하는 fanout의 동적 구독 합류.

#### PS-A4 subscriber 재연결·기존 subscription 재적용

우선순위: `P1`

**한마디로:** subscriber transport 연결이 끊겼다가 복구되어도 application이 handler를 다시 등록하지
않고 기존 subscription으로 새 event를 받으며, 끊긴 동안 발행된 event는 replay하지 않는가.

- 절차: subscriber A·B가 같은 topic을 받고 있음을 확인한다 → runner의 process-external network fault로
  A의 transport 연결만 끊는다 → 연결 단절 evidence를 확인한 뒤 `while-disconnected` event를 한 번
  발행한다 → B 수신을 확인한다 → network fault를 해제하고 A의
  `ConnectionReady`를 기다린다 → application의 handler 등록이나
  재구독 API 호출 없이 `after-reconnect` event를 한 번 발행한다.
- 검증: B는 두 event를 모두 받는다. A는 `after-reconnect` event를 받고
  `while-disconnected` event를 받지 않으며 복구 뒤에도 replay evidence가 없다. A application은 handler를
  다시 등록하거나 reconnect loop를 만들지 않는다.
- 세부 동작: 기존 subscription을 유지한 transport 재연결과 subscriber 격리.

> subscriber 하나의 transport만 끊을 수 있는 network fault harness가 없으면 PS-A4는 `blocked`로
> 기록한다. subscriber process 재시작으로 바꾸면 application startup이 handler를 다시 등록하므로 이
> 시나리오를 검증한 것이 아니다.

### Track B — subscriber 동작

#### PS-B1 subscriber 느린 handler

우선순위: `P1`

**한마디로:** 한 subscriber가 처리에 굼떠도, 다른 빠른 subscriber는 막힘 없이 계속 받는가(subscriber 간 격리).

- 절차: 한 subscriber의 handler를 느리게 만들고 발행을 지속한다.
- 검증: 느린 subscriber가 처리 지연 중에도 다른(빠른) subscriber는 계속 정상 수신한다(subscriber 간 격리)만 검증한다.
- 세부 동작: fast subscriber 격리. (느린 subscriber의 catch-up 완전성·drop/backpressure 정책은 public 계약으로 정의되지 않아 단언하지 않는다.)

#### PS-B2 publisher 재시작

우선순위: `P1`

**한마디로:** publisher가 같은 rid·endpoint로 재시작해도 subscriber application이 구독을 다시
등록하지 않은 채 복구 후의 새 발행을 받는가.

- 절차: 발행 중 publisher에 정상 종료를 요청하고 terminal `Drained`와 old peer row 제거를 확인한 뒤
  같은 rid·endpoint로 재시작한다. subscriber 프로세스와 등록한 topic은 그대로 유지하며 application
  코드에서 재구독 API를 호출하지 않는다. subscriber가 새 publisher에 대해 기록한
  `ConnectionReady`를 기다린 뒤 고유 sequence의 event를 한 번 발행한다.
- 검증: 복구 뒤 고유 sequence를 가진 새 event가 기존 subscriber handler에 도달하고 payload가 일치한다. subscriber application이
  구독을 다시 등록하거나 reconnect loop를 만들지 않아야 한다. subscriber process는 전 구간 유지된다.
- 세부 동작: transport 재연결 뒤 기존 subscription 설정으로 publisher restart 복구(자동 replay 아님).

### Track C — negatives

#### PS-C1 publish 미등록 message name

우선순위: `P0`

**한마디로:** handler 없는 message name으로 발행하면 subscriber 쪽에서 drop되고 그 흔적이 observer에 남되, 다른 정상 전달은 멀쩡한가.

- 절차: subscriber에 handler가 없는 **packet name**으로 발행한다(channel fanout의 publish dispatch는 topic이 아니라 packet name으로 handler를 찾는다 — [20 §3.3](../../spec/server/20-spot-messaging.ko.md)).
- 검증: 해당 publish는 subscriber dispatch에서 drop되고, **subscriber** observer evidence에 reason `handlerMissing`/action `drop` marker가 남는다. publisher의 `Publish(...).Async()`는 transport submit만 하므로 publisher 측엔 dispatch marker가 없다. 다른 정상 message 전달은 영향 없음.
- 세부 동작: publish negative path(message name 기준) + subscriber 관측.

## 5. 완료 기준

- Track A~C의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 실패 시 publisher/subscriber 로그와 evidence로 원인 레이어를 분리한다.
