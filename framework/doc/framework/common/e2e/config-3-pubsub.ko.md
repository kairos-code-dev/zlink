<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot 서비스](config-2-spot-service.ko.md) | [다음: 등록·codec](config-4-registration-codec.ko.md)
<!-- framework-adapter-nav:end -->

# Config 3 — Pub/Sub 이벤트 배포

이벤트를 뿌리는 형상이다. publisher 하나가 이벤트를 내보내고 여러 subscriber가 받는 배포를 한
번 띄워 두고, fanout(여럿에게 퍼지는지)과 topic 필터가 실제 사용자처럼 도는지 본다.

## 1. 목적과 범위

- 다룬다: fanout 전달, topic 필터(application-level, publish context.Topic 기반), late subscriber 합류, subscriber 느린 handler, publish negative path.
- 여기서 다루지 않는 것: channel request/send(Config 1), spot publish(Config 2).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록하고, fanout 연결에 필요한 peer location row는 framework lifecycle이 자동 갱신한다. |
| publisher | 1 (`pub-a`) | publish channel server. `EventPublish(topic, value)` 발행. peer location row 자동 등록. `/evidence`·`/health`. |
| subscriber | 3 (`sub-1`, `sub-2`, `sub-3`) | subscribe handler 보유. 받은 이벤트를 evidence로 기록. handler가 publish context.Topic으로 관심 topic만 처리. |
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

**한마디로:** warm-up 뒤 측정 구간에서 세 subscriber가 같은 연속 sequence를 같은 순서로 받는가(fanout 증명 — 전량 무손실이 아니라 공통 sequence 도달로 본다).

- 절차: warm-up publish를 반복해 각 subscriber가 처음 수신할 때까지 기다린다(별도 "subscribe 완료" event는 없으므로 warm-up 수신을 구독 준비 barrier로 쓴다). 그 뒤 측정 구간에서 이벤트를 발행한다.
- 검증: 측정 구간에서 모든 subscriber가 공유하는 하나의 연속 sequence가 존재한다(세 subscriber 모두 그 sequence를 순서대로 수신). `Publish(...).Async()`는 remote 수신을 보장하지 않으므로 "전량 N개 무손실"이 아니라 공통 sequence 도달로 fanout을 증명한다(기존 fanout E2E와 같은 oracle).
- 세부 동작: warm-up barrier 후 공통 sequence fanout.

#### PS-A2 topic filter

우선순위: `P0`

**한마디로:** 여러 topic으로 뿌려도, 각 subscriber가 자기 관심 topic만 골라서 처리하고 나머지는 흘려보내는가.

- 절차: 여러 topic으로 발행한다. channel fanout은 subscriber transport 단계의 topic 필터를 노출하지 않으므로([20 §3.3](../spec/20-spot-messaging.ko.md)) subscriber는 전량 수신하되 handler가 publish context의 topic을 보고 관심 topic만 처리한다.
- 검증: 각 subscriber handler가 publish context.Topic으로 자신의 관심 topic만 evidence에 기록한다. 비관심 topic은 기록되지 않는다.
- 세부 동작: publish context.Topic 기반 application-level 필터링. **이것이 channel fanout의 계약이다** — topic이 subscriber set을 고르는 것은 SPOT subscribe 표면이며, 두 표면의 구분은 [20 §3.3](../spec/20-spot-messaging.ko.md)이 소유한다. fanout에 transport topic 필터를 추가하려면 공개 계약 확장 절차를 먼저 거친다.

#### PS-A3 late subscriber

우선순위: `P0`

**한마디로:** 발행이 시작된 뒤 새로 구독하면, 구독 이후 발행분만 받고 그 전 것은 못 받는가(replay 없음).

- 절차: 발행이 시작된 뒤 새 subscriber를 띄워 구독한다.
- 검증: late subscriber는 구독 이후 발행분을 받는다. 구독 이전 발행분은 전달되지 않는다(replay 계약 없음). 이 late-subscriber 규칙은 public 계약이 아니라 관측된 기대(observed expectation)이며, 본 config가 그 동작을 evidence로 고정한다.
- 세부 동작: 동적 구독 합류.

#### PS-A4 subscriber 재연결·재구독

우선순위: `P1`

**한마디로:** subscriber가 끊겼다 다시 붙어 재구독하면, 그때부터의 발행분을 다시 받고(끊긴 동안 것은 replay 없이 못 받고) 다른 subscriber엔 영향이 없는가.

- 절차: subscriber 하나를 연결 해제했다가 재접속해 다시 구독한다. 그 사이에도 발행은 계속된다.
- 검증: 재구독 후 그 시점 이후 발행분을 다시 받는다. 끊긴 동안 발행된 것은 전달되지 않는다(공개 replay 계약이 없는 관측된 동작으로 고정 — PS-A3과 동일 성격). 끊김·재구독 중에도 다른 subscriber의 수신은 영향받지 않는다. (재구독은 fanout builder에 별도 reconnect/unsubscribe API가 아니라 runtime/process lifecycle로 유도한다.)
- 세부 동작: 동적 재구독(끊김 구간 비replay 관측 + subscriber 격리).

### Track B — subscriber 동작

#### PS-B1 subscriber 느린 handler

우선순위: `P1`

**한마디로:** 한 subscriber가 처리에 굼떠도, 다른 빠른 subscriber는 막힘 없이 계속 받는가(subscriber 간 격리).

- 절차: 한 subscriber의 handler를 느리게 만들고 발행을 지속한다.
- 검증: 느린 subscriber가 처리 지연 중에도 다른(빠른) subscriber는 계속 정상 수신한다(subscriber 간 격리)만 검증한다.
- 세부 동작: fast subscriber 격리. (느린 subscriber의 catch-up 완전성·drop/backpressure 정책은 public 계약으로 정의되지 않아 단언하지 않는다.)

#### PS-B2 publisher 재시작

우선순위: `P1`

**한마디로:** publisher가 죽었다 다시 떠도, 복구 후 발행이 기존 subscriber에게 다시 도달하는가(다운 구간 발행분 유실은 계약대로).

- 절차: 발행 중 publisher 프로세스를 종료했다가 재기동한다(harness restart 전제). subscriber는 그대로 둔다.
- 검증: publisher 재기동 후 새 발행이 다시 subscriber에 도달한다(subscriber 재구독이 필요한지 여부는 transport 동작에 따르며, 관측 결과로 고정한다). 다운 구간 발행 시도분의 유실 여부도 공개 replay 계약이 없으므로 관측으로 고정한다(replay를 보장하지 않음). subscriber 측은 재기동에도 죽지 않는다. publisher 재기동은 runtime/process lifecycle(start/stop)로 유도한다.
- 세부 동작: publisher 재기동 복구(다운 구간 비replay 관측).

### Track C — negatives

#### PS-C1 publish 미등록 message name

우선순위: `P0`

**한마디로:** handler 없는 message name으로 발행하면 subscriber 쪽에서 drop되고 그 흔적이 observer에 남되, 다른 정상 전달은 멀쩡한가.

- 절차: subscriber에 handler가 없는 **packet name**으로 발행한다(channel fanout의 publish dispatch는 topic이 아니라 packet name으로 handler를 찾는다 — [20 §3.3](../spec/20-spot-messaging.ko.md)).
- 검증: 해당 publish는 subscriber dispatch에서 drop되고, **subscriber** observer evidence에 reason `handlerMissing`/action `drop` marker가 남는다. publisher의 `Publish(...).Async()`는 transport submit만 하므로 publisher 측엔 dispatch marker가 없다. 다른 정상 message 전달은 영향 없음.
- 세부 동작: publish negative path(message name 기준) + subscriber 관측.

## 5. 완료 기준

- Track A~C의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 실패 시 publisher/subscriber 로그와 evidence로 원인 레이어를 분리한다.
