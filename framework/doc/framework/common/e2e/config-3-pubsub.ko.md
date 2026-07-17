<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot 서비스](config-2-spot-service.ko.md) | [다음: 등록·codec](config-4-registration-codec.ko.md)
<!-- framework-adapter-nav:end -->

# Config 3 — Pub/Sub 이벤트 배포

publisher 하나가 이벤트를 내보내고 여러 subscriber가 받는 배포다. classic fanout 전달과
packet name 기반 handler dispatch가 public API 계약대로 동작하는지 확인한다.

## 1. 목적과 범위

- 다룬다: fanout 전달, packet name 기반 handler 선택, late subscriber 합류,
  subscriber 재연결, publisher 재시작, subscriber 느린 handler, publish negative path.
- 여기서 다루지 않는 것: channel request/send(Config 1), spot publish(Config 2).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| publisher | 1 (`pub-a`) | `AddFanoutChannel("events").EnablePublisher(endpoint)`로 PUB endpoint를 열고 서로 다른 packet name의 typed event를 발행한다. `/evidence`·`/health`. |
| subscriber | 3 (`sub-1`, `sub-2`, `sub-3`) | packet name별 typed handler를 보유한다. 받은 이벤트와 public socket lifecycle의 `ConnectionReady`·`Disconnected`를 evidence로 기록한다. |
| consumer | 시나리오별 | publisher의 application endpoint를 호출해 publish를 트리거한다. subscriber는 `ConnectSubscriber(publisherEndpoint)`로 PUB endpoint를 명시한다. |

handler 동작(공유): subscriber는 서로 다른 packet name으로 등록한 typed event와 value를 evidence에
기록한다. handler가 없는 packet name으로 오는 publish는 subscriber
dispatch에서 drop되고 observer marker가 남는다.

## 3. 실행 모델

`run_e2e.sh`가 publisher와 subscriber를 순서대로 시작한다. late subscriber 시나리오는 subscriber
하나의 시작 시점을 늦춘다. client 시나리오가 publish를 트리거하고 각 subscriber의 evidence를
조회해 확인한다. classic fanout만 사용하는 이 config는 location store를 등록하지 않는다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — fanout과 dispatch

#### PS-A1 fanout basic delivery

우선순위: `P0`

**검증 질문:** 세 subscriber의 구독 준비가 끝난 뒤 측정 구간에서 같은 연속 sequence를 같은 순서로
받는가.

- 절차: 세 subscriber 각각의 `ConnectionReady`를 기다린 뒤 측정용 고유 sequence를 발행한다.
- 검증: 측정 구간에서 모든 subscriber가 공유하는 하나의 연속 sequence가 존재하고 세 subscriber 모두
  그 sequence를 같은 순서로 수신한다. `Publish(...).Async()` 완료 자체를 remote 수신 evidence로 쓰지
  않고 subscriber handler evidence로 판정한다.
- 세부 동작: 구독 readiness 뒤 공통 sequence fanout.

#### PS-A2 packet name 기반 handler dispatch

우선순위: `P0`

**검증 질문:** 서로 다른 packet name으로 발행하면 각 event가 그 이름에 등록된 typed handler에서만
처리되는가.

- 절차: 같은 fanout channel에 `InventoryChangedNotify`와 `PriceChangedNotify`를 서로 다른 packet name으로
  연속 발행한다. Subscriber는 두 typed handler를 각각 등록한다.
- 검증: 각 event는 자기 packet name의 handler evidence에 정확히 한 번 기록되고 다른 handler에는
  기록되지 않는다. Packet name을 topic이나 payload field로 다시 분류하지 않는다.
- 세부 동작: classic fanout handler namespace는 packet name으로 구분하며 transport topic filter를 공개
  API로 제공하지 않는다
  ([Framework API §11](../../spec/05-framework-api.ko.md#11-classic-fanout)). Logical Multicast의
  ChannelName·topic subscription과 혼합하지 않는다.

#### PS-A3 late subscriber

우선순위: `P0`

**검증 질문:** 발행이 시작된 뒤 새 subscriber가 연결되면, 구독 준비가 끝난 뒤 발행한 event부터 받고
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

**검증 질문:** subscriber transport 연결이 끊겼다가 복구되어도 application이 handler를 다시 등록하지
않고 기존 subscription으로 새 event를 받으며, 끊긴 동안 발행된 event는 replay하지 않는가.

- 절차: subscriber A·B가 같은 packet name의 event를 받고 있음을 확인한다 → runner의 process-external network fault로
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

**검증 질문:** 한 subscriber의 처리가 지연되어도 다른 subscriber의 수신이 계속되는가.

- 절차: 한 subscriber의 handler를 느리게 만들고 발행을 지속한다.
- 검증: 느린 subscriber가 처리 지연 중에도 다른(빠른) subscriber는 계속 정상 수신한다(subscriber 간 격리)만 검증한다.
- 세부 동작: fast subscriber 격리. (느린 subscriber의 catch-up 완전성·drop/backpressure 정책은 public 계약으로 정의되지 않아 단언하지 않는다.)

#### PS-B2 publisher 재시작

우선순위: `P1`

**검증 질문:** publisher가 같은 endpoint로 재시작해도 subscriber application이 handler를 다시
등록하지 않은 채 복구 후의 새 발행을 받는가.

- 절차: 발행 중 publisher를 정상 종료하고 endpoint가 닫힌 것을 확인한 뒤 같은 endpoint로
  재시작한다. subscriber 프로세스와 등록한 handler는 그대로 유지하며 application
  코드에서 재구독 API를 호출하지 않는다. subscriber가 새 publisher에 대해 기록한
  `ConnectionReady`를 기다린 뒤 고유 sequence의 event를 한 번 발행한다.
- 검증: 복구 뒤 고유 sequence를 가진 새 event가 기존 subscriber handler에 도달하고 payload가 일치한다. subscriber application이
  구독을 다시 등록하거나 reconnect loop를 만들지 않아야 한다. subscriber process는 전 구간 유지된다.
- 세부 동작: transport 재연결 뒤 기존 subscription 설정으로 publisher restart 복구(자동 replay 아님).

### Track C — negatives

#### PS-C1 publish 미등록 message name

우선순위: `P0`

**검증 질문:** handler 없는 message name으로 발행하면 subscriber에서 drop evidence가 기록되고 다른
정상 전달은 계속 성공하는가.

- 절차: subscriber에 handler가 없는 **packet name**으로 발행한다. Classic fanout handler namespace는
  packet name으로 구분한다
  ([Framework API §11](../../spec/05-framework-api.ko.md#11-classic-fanout)).
- 검증: 해당 publish는 subscriber dispatch에서 drop되고, **subscriber** observer evidence에 reason `no_handler`/action `drop` marker가 남는다. publisher의 `Publish(...).Async()`는 transport submit만 하므로 publisher 측엔 dispatch marker가 없다. 다른 정상 message 전달은 영향 없음.
- 세부 동작: publish negative path(message name 기준) + subscriber 관측.

## 5. 완료 기준

- Track A~C의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 실패 시 publisher/subscriber 로그와 evidence로 원인 레이어를 분리한다.
