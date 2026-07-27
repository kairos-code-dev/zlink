<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot 서비스](config-2-spot-service.ko.md) | [다음: 등록·codec](config-4-registration-codec.ko.md)
<!-- framework-adapter-nav:end -->

# Config 3 — Pub/Sub 이벤트 배포

publisher 하나가 이벤트를 내보내고 여러 subscriber가 받는 배포다. classic fanout 전달과
packet name 기반 handler dispatch가 public API 계약대로 동작하는지 확인한다.

## 1. 목적과 범위

- 다룬다: location store 기반 publisher 자동 발견, fanout 전달, packet name 기반 handler 선택,
  late subscriber 합류, subscriber 재연결, publisher 재시작, lease·store 장애 복구, subscriber 느린
  handler, manual endpoint 회귀, publisher별 liveness와 publish negative path.
- 여기서 다루지 않는 것: channel request/send(Config 1), spot publish(Config 2).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| publisher | 1 (`pub-a`) | `AddFanoutChannel("events").EnablePublisher()`와 고정 또는 할당 Publisher RID로 PUB listener를 열고 실제 bound endpoint를 fanout publisher descriptor로 게시한다. 서로 다른 packet name의 typed event를 발행한다. `/evidence`·`/health`. |
| subscriber | 3 (`sub-1`, `sub-2`, `sub-3`) | endpoint 없는 automatic subscriber를 등록하고 같은 ChannelName의 live publisher descriptor를 연결한다. Packet name별 typed handler를 보유하며 받은 event, public fanout runtime snapshot, publisher changed와 location changed variant를 evidence로 기록한다. |
| consumer | 시나리오별 | publisher의 application endpoint를 호출해 publish를 트리거하고 subscriber evidence를 조회한다. Transport endpoint를 application 설정이나 client 입력으로 전달하지 않는다. |

handler 동작(공유): subscriber는 서로 다른 packet name으로 등록한 typed event와 value를 evidence에
기록한다. handler가 없는 packet name으로 오는 publish는 subscriber
dispatch에서 drop되고 observer marker가 남는다.

## 3. 실행 모델

`run_e2e.sh`가 실행별 Redis key prefix를 만들고 publisher와 subscriber를 순서대로 시작한다. late subscriber 시나리오는 subscriber
하나의 시작 시점을 늦춘다. client 시나리오가 publish를 트리거하고 각 subscriber의 evidence를
조회해 확인한다. Publisher와 automatic subscriber host는 공식 Redis extension을 명시적으로 등록한다.
Manual endpoint 회귀는 별도 process 집합과 prefix를 사용하며 manual subscriber에는 store를 등록하지
않는다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — fanout과 dispatch

#### PS-A1 fanout basic delivery

우선순위: `P0`

**검증 질문:** 세 subscriber의 구독 준비가 끝난 뒤 측정 구간에서 같은 연속 sequence를 같은 순서로
받는가.

- 절차: 세 subscriber 각각의 fanout runtime snapshot에서 ready connection을 확인한 뒤 측정용 고유
  sequence를 발행한다.
- readiness는 `connect` 성공, desired set이나 내부 active target 수가 아니라 publisher 전용 SUB socket의
  native-ready와 같은 socket의 첫 valid application record 또는 beacon을 모두 반영한 `Ready=true`
  snapshot과 publisher changed event로 판정한다.
- 검증: 측정 구간에서 모든 subscriber가 공유하는 하나의 연속 sequence가 존재하고 세 subscriber 모두
  그 sequence를 같은 순서로 수신한다. 언어별 bounded submit call의 terminal completion 자체를 remote
  수신 evidence로 쓰지 않고 subscriber handler evidence로 판정한다.
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
  ([Framework API §11](../spec/05-framework-api.ko.md#11-classic-fanout)). Logical Multicast의
  ChannelName·topic subscription과 혼합하지 않는다.

#### PS-A3 late subscriber

우선순위: `P0`

**검증 질문:** 발행이 시작된 뒤 새 subscriber가 연결되면, 구독 준비가 끝난 뒤 발행한 event부터 받고
그 전에 발행한 event는 replay하지 않는가.

- 절차: publisher가 고유 sequence의 `before-ready` event를 발행한다 → 새 subscriber를 시작한다 →
  subscriber의 public fanout runtime event가 `ready` event entry를 제공할 때까지 기다린다 → 고유 sequence의
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
  A의 transport 연결만 끊는다 → public fanout runtime event의 `disconnected` event entry를 확인한 뒤
  `while-disconnected` event를 한 번
  발행한다 → B 수신을 확인한다 → network fault를 해제하고 A의
  `reconnecting` 다음 `ready` event entry를 기다린다 → application의 handler 등록이나
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
  public fanout runtime event의 새 descriptor identity와 `ready` event entry를 기다린 뒤 고유 sequence의
  event를 한 번 발행한다.
- 검증: 복구 뒤 고유 sequence를 가진 새 event가 기존 subscriber handler에 도달하고 payload가 일치한다. subscriber application이
  구독을 다시 등록하거나 reconnect loop를 만들지 않아야 한다. subscriber process는 전 구간 유지된다.
- 세부 동작: transport 재연결 뒤 기존 subscription 설정으로 publisher restart 복구(자동 replay 아님).

### Track D — automatic discovery와 location lifecycle

#### PS-D1 publisher descriptor와 자동 연결

우선순위: `P0`

**검증 질문:** subscriber가 endpoint 입력 없이 같은 ChannelName의 publisher descriptor를 발견해 실제
advertised endpoint에 연결하는가.

- 절차: publisher를 port `0`으로 시작하고 Redis row와 owner lease를 읽는다. 이후 endpoint를 받지 않은
  subscriber를 시작한다.
- 검증: row kind, key, canonical JSON과 actual bound port가 정식 fixture와 같은 규칙을 따르고,
  subscriber fanout runtime snapshot에서 actual endpoint의 `ConnectionIntent=true`, `Ready=true` entry를
  확인한 뒤 고유 event가 handler에 정확히 한 번 도달한다. Application 설정과
  client 입력에는 publisher transport endpoint가 없다.

#### PS-D2 ChannelName과 descriptor 종류 격리

우선순위: `P0`

**검증 질문:** automatic subscriber가 같은 ChannelName의 fanout publisher descriptor만 연결하는가.

- 절차: `events` publisher와 다른 `audit` publisher를 시작하고, 같은 prefix에 MeshNode와 ClientServer
  descriptor도 둔다. 유효한 lease를 가지지만 host `Relocate`로 `Relocating`이거나 `Shutdown`으로 `Draining` 중인 별도
  `events` publisher descriptor도 넣는다.
  Subscriber descriptor는 fanout location 계약에 존재하지 않으므로 만들지 않는다.
- 검증: public fanout runtime snapshot의 `ConnectionIntentCount`와 `Publishers`에는 live `events` publisher만
  `ConnectionIntent=true`로 나타난다. `Draining` 중인 `events` publisher는 `excluded_draining` state와
  `ConnectionIntent=false`로 나타나며, `audit` publisher, MeshNode와 ClientServer descriptor endpoint는
  `events` snapshot의 publisher identity로 들어오지 않는다. `zlink.runtime.fanout.publisher_changed` event도
  같은 event entry를 제공한다. 제외한 endpoint에서 발행한 event는 business handler에 도달하지 않고 live
  `events` publisher의 event만 handler에 도달한다.

#### PS-D3 publisher 추가·정상 제거 수렴

우선순위: `P1`

**검증 질문:** 같은 ChannelName의 publisher 수가 바뀌면 subscriber connection set이 polling만으로
수렴하는가.

- 절차: `pub-a` 연결 뒤 `pub-b`를 추가하고 두 publisher의 event를 확인한다. `pub-a`를 정상 종료해
  descriptor를 제거한다.
- 검증: public fanout runtime snapshot의 `ConnectionIntentCount=2`, `ReadyConnectionCount=2`와 두 publisher
  identity를 확인한다. `pub-a` 제거 뒤 `zlink.runtime.fanout.publisher_changed` publisher changed variant는
  `pub-a` identity의
  `disconnected` event entry를 제공하고, 다음 snapshot에는 `pub-b`만 connection intent와 ready 상태로
  남는다. `pub-b` event는 계속 수신하며 subscriber process와 handler 등록은 유지한다.

#### PS-D4 owner lease 만료와 재등록

우선순위: `P0`

**검증 질문:** crash한 publisher의 lease가 만료되면 stale endpoint가 제거되고 같은 Publisher RID의 새
generation이 게시될 때 새 endpoint로 연결하는가.

- 절차: publisher를 강제 종료하고 Redis `PTTL`로 기존 owner lease 만료를 확인한다. 같은 Publisher RID와
  port `0`으로 publisher를 다시 시작한다. Subscriber가 더 큰 lifecycle generation과 최신 descriptor
  revision을 적용한 뒤, controlled store fixture로 낮은 generation과 같은 generation의 낮은 revision
  snapshot을 차례로 다시 노출한다.
- 검증: 고정 sleep 대신 lease evidence를 사용한다. Public fanout runtime event는 이전 identity의
  `disconnected`, 새 identity의 `reconnecting`과 `ready` event entry를 순서대로 제공한다. 최신 snapshot은
  더 큰 lifecycle generation의 actual endpoint만 `ConnectionIntent=true`, `Ready=true`로 제공한다. 이후 낮은
  generation 또는 revision을 읽으면 event에 `excluded_stale` event entry가 나타나지만 최신 snapshot과
  connection intent는 이전 값으로 되돌아가지 않는다. 새 event만 수신하며 crash 구간 event를 replay하지
  않는다.

#### PS-D5 store 장애 fail-static과 복구

우선순위: `P1`

**검증 질문:** Redis 장애가 기존 fanout 연결을 즉시 끊지 않고 복구 뒤 최신 descriptor로 수렴하는가.

- 절차: 연결과 첫 event를 확인한 뒤 subscriber의 Redis 접근을 차단하고 기존 publisher event를 발행한다.
  장애 중 새 descriptor를 connection set에 반영하지 않는지 확인한다. Store를 복구해 최신 generation과
  revision의 snapshot을 적용한 뒤 낮은 generation과 같은 generation의 낮은 revision snapshot을 차례로
  노출한다.
- 검증: 장애 중 public fanout runtime snapshot의 기존 publisher entry와 ready state가 유지되고 event를 계속
  받는다. 신규 publisher changed event는 없으며 `zlink.runtime.location.store_changed` location changed
  variant가 `degraded` Location snapshot을 제공한다. 복구 시 같은 variant가 `ready` Location snapshot을
  제공한 뒤 최신 descriptor identity의 `reconnecting`과 `ready` publisher changed event로 한 번 수렴한다.
  이후 stale descriptor에는 `excluded_stale` event entry를 제공하되 snapshot의 current connection intent를
  이전 endpoint로 되돌리지 않고 최신 publisher의 event만 handler에 전달한다.

#### PS-D6 port 0 publisher 재시작

우선순위: `P1`

**검증 질문:** publisher의 actual port가 바뀌어도 application이 endpoint를 다시 전달하지 않고 기존
subscriber가 새 descriptor를 따라가는가.

- 절차: port `0` publisher를 정상 종료한 뒤 같은 Publisher RID로 다시 시작한다.
- 검증: 새 lifecycle generation과 actual advertised endpoint가 이전 값과 구분되고, subscriber public fanout
  runtime event가 새 endpoint의 `reconnecting`과 `ready` event entry를 제공한 뒤 event를 받는다. Wildcard
  host나 port `0`이 Redis row에 남지 않는다.

#### PS-D7 fanout observer bounded lifecycle과 manual mutation 격리

우선순위: `P1`

**검증 질문:** 느리거나 취소된 observer가 automatic fanout 연결·message dispatch·다른
observer를 막지 않고, bounded queue의 sequence gap을 snapshot으로 복원하며 manual endpoint 변경이
automatic snapshot과 event를 변경하지 않는가.

- 절차: 같은 automatic subscriber에 capacity `1`인 느린 observer와 정상 observer를 동시에 연다.
  Publisher 추가·host `Relocate`에 따른 `Relocating` 제외·재등록을 반복해 observer queue capacity보다 많은
  전이를 만들고, 그 동안
  business event를 발행한다. 느린 observer에서 sequence gap을 관찰하면 같은 ChannelName의 public
  snapshot을 다시 읽어 최신 publisher identity·connection intent·native ready 상태를 복원한다. 그 뒤
  느린 observer만 언어별 public cancellation/close 표면으로 종료한다. 별도 manual fanout
  ChannelName의 endpoint connection handle에 endpoint를 추가하고 제거한다.
- 검증: bounded observer overflow는 message dispatch를 대기시키지 않고 coalescing 또는 sequence gap을
  남긴다. Gap 뒤 snapshot은 정상 observer의 최신 sequence와 상태에 수렴하며 publisher event는
  handler에 계속 도달한다. 취소된 observer에는 새 event가 전달되지 않지만 정상 observer와
  automatic connection은 유지된다. Manual endpoint 추가·제거 전후 automatic snapshot의 publisher
  identity, connection intent·ready count와 event sequence가 그 변경 때문에 바뀌지 않는다.

### Track E — mode와 startup 회귀

#### PS-E1 manual endpoint 비회귀

우선순위: `P0`

**검증 질문:** manual subscriber가 location store 없이 명시한 endpoint만 연결하는가.

- 절차: Framework PUB endpoint와 manual subscriber를 별도 process 집합으로 시작한다. Subscriber에는
  location store를 등록하지 않고 endpoint를 한 번 명시한다.
- 검증: event 전달, late join과 non-replay가 기존 계약대로 동작하고 Redis key를 만들거나 다른 publisher를
  발견하지 않는다.

#### PS-E2 automatic mode와 Publisher identity startup 검증

우선순위: `P0`

**검증 질문:** automatic subscriber의 필수 store 누락, subscriber mode 혼합과 Publisher RID 설정 누락·충돌을
connect loop 전에 구성 오류로 거부하고, store 없는 manual 조합은 계속 동작하는가.

- 절차: 서로 분리한 host 설정으로 endpoint 없는 subscriber의 store 누락, 같은 subscriber registration의
  automatic·manual endpoint mode 동시 설정, location store를 등록한 publisher의 고정 Publisher RID·자동
  할당 둘 다 누락, 그리고 한 publisher의 고정 Publisher RID와 자동 할당 동시 설정을 각각
  시작한다. 별도 process 집합에서는 store 없는 framework publisher와 manual subscriber를 고정
  endpoint로 시작한다.
- 검증: 네 invalid host는 background reconnect와 socket bind 전에 non-zero로 종료하고 각 원인을 typed
  configuration error로 기록한다. Manual 조합은 descriptor를 만들지 않고 event를 전달한다. 어느 negative도
  timeout으로 판정하지 않는다.

### Track F — publisher liveness

#### PS-F1 automatic·manual first-receive readiness

우선순위: `P0`

**검증 질문:** automatic descriptor와 manual endpoint가 모두 connect 반환만으로 ready가 되지 않고, publisher
전용 SUB socket에서 첫 valid application record 또는 liveness beacon을 받은 뒤 ready가 되는가.

- 절차: automatic publisher·subscriber와 별도 manual publisher·subscriber를 시작한다. 각 subscriber에서
  connection intent가 생긴 직후와 publisher의 5초 periodic beacon 이후 snapshot을 순서대로 읽는다.
- 검증: 첫 valid receive 전에는 `ConnectionIntent=true`, `Ready=false`이고, exact beacon을 받은 뒤에만
  `Ready=true`다. Beacon은 typed handler evidence, message-flow publish event와
  `zlink.fanout.received` application count를 만들지 않는다.
- 세부 동작: first-valid-receive barrier와 infrastructure-only beacon.

#### PS-F2 publisher별 timeout 격리

우선순위: `P0`

**검증 질문:** publisher 하나의 단방향 packet blackhole이 같은 ChannelName의 다른 publisher를 not-ready로
바꾸지 않는가.

- 절차: 같은 ChannelName의 publisher `pub-a`, `pub-b`를 automatic subscriber 하나가 발견하게 한다. 각
  descriptor에 대해 전용 SUB socket이 ready인 것을 확인한 뒤 fault proxy가 `pub-b`의 publisher→subscriber
  방향 packet만 차단한다. `pub-a`는 정상 application event 또는 beacon을 계속 보낸다.
- 검증: 15초 inbound deadline 안에 `pub-b` entry만 `disconnected`와 `reconnecting`으로 바뀌고 `pub-a`는
  ready를 유지한다. Host는 `Error`가 되지 않으며 `pub-a` event는 계속 handler에 전달된다. Proxy를 복구한 뒤
  `pub-b`는 새 전용 socket에서 첫 valid receive 전에는 ready로 복원되지 않는다.
- 세부 동작: publisher별 socket attribution, timeout과 reconnect 격리.

#### PS-F3 reserved topic와 malformed beacon

우선순위: `P0`

**검증 질문:** exact reserved topic만 application publish에서 거부하고 malformed internal record는
application에 전달하지 않으며, 같은 prefix의 다른 topic은 정상 topic으로 처리하는가.

- 절차: public fanout publish에 exact topic byte `01 5A 4C 46 31`을 전달한다. 이어서 같은 byte prefix 뒤에
  추가 byte가 있는 topic으로 정상 typed event를 발행한다. Protocol-negative publisher fixture는 exact reserved
  topic에 잘못된 payload와 extra frame을 각각 전송한다.
- 검증: public exact-topic 호출은 transport 전에 언어별 호출 인자 오류로 실패한다. Prefix가 더 긴 topic의
  event는 정상 handler에 한 번 도달한다. Malformed reserved record는 handler·application metric·receive
  activity를 만들지 않고 해당 publisher만 즉시 not-ready로 바꾼다. Golden fixture는 topic frame
  `01 5A 4C 46 31`과 payload frame `5A 46 01 01` 두 개만 valid beacon으로 승인한다.
- 세부 동작: exact-only reservation, fail-closed beacon decode와 public topic 비회귀.

#### PS-F4 orderly disconnect와 cleanup

우선순위: `P1`

**검증 질문:** publisher 정상 종료가 15초 deadline을 기다리지 않고 반영되며 reconnect·beacon resource가
subscriber 종료 뒤 남지 않는가.

- 절차: ready publisher를 정상 종료하고 public fanout event·snapshot으로 상태를 관찰한다. Subscriber host도
  종료한 뒤 process와 resource evidence를 확인한다.
- 검증: raw disconnect를 관측한 즉시 publisher entry가 ready에서 제외되며 15초 대기로 판정하지 않는다.
  Subscriber terminal 뒤 publisher별 receive loop, reconnect timer와 beacon deadline callback이 남지 않는다.
- 세부 동작: orderly disconnect 즉시 반영과 terminal cleanup.

#### PS-F5 topic filter와 periodic beacon 독립성

우선순위: `P0`

**검증 질문:** Subscriber가 구독하지 않는 topic의 application traffic이 계속되어도 periodic beacon을 받아
정상 publisher를 timeout으로 잘못 제외하지 않는가.

- 절차: Publisher가 topic `events.a` application record를 1초마다 발행하고 subscriber는 `events.b`만
  구독한다. Publisher별 SUB socket에는 application filter와 별도로 exact reserved beacon topic subscription이
  설정되어 있어야 한다. 15초 inbound deadline보다 긴 30초 동안 runtime snapshot과 handler evidence를
  관찰한다.
- 검증: `events.a`는 application handler에 전달되지 않지만 5초 periodic beacon은 계속 receive activity를
  갱신한다. Publisher entry는 ready를 유지하고 reconnect나 timeout event를 만들지 않는다. Beacon은
  application handler·publish/receive count를 만들지 않는다.
- 세부 동작: application traffic과 독립된 beacon scheduler와 reserved subscription.

### Track C — negatives

#### PS-C1 publish 미등록 message name

우선순위: `P0`

**검증 질문:** handler 없는 message name으로 발행하면 subscriber에서 drop evidence가 기록되고 다른
정상 전달은 계속 성공하는가.

- 절차: subscriber에 handler가 없는 **packet name**으로 발행한다. Classic fanout handler namespace는
  packet name으로 구분한다
  ([Framework API §11](../spec/05-framework-api.ko.md#11-classic-fanout)).
- 검증: 해당 publish는 subscriber dispatch에서 drop되고, **subscriber** observer evidence에 reason
  `no_handler`/action `drop` marker가 남는다. Publisher의 bounded submit terminal completion은 local
  transport admission만 나타내므로 publisher 측에는 dispatch marker가 없다. 다른 정상 message 전달은
  영향이 없어야 한다.
- 세부 동작: publish negative path(message name 기준) + subscriber 관측.

## 5. 완료 기준

- Track A~F의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- Automatic subscriber의 물리 연결 evidence는 public fanout runtime snapshot과 event만 사용한다. Raw socket
  monitor, private runtime hook과 manual endpoint connection handle로 automatic state를 읽거나 변경하지 않는다.
- `connect` 성공, desired set과 내부 active target 목록은 readiness evidence로 사용하지 않는다. `Ready=true`와
  publisher changed의 `ready`는 전용 SUB socket native-ready와 첫 valid receive를 모두 반영해야 한다.
  `disconnected`는 raw disconnect, malformed reserved record 또는 15초 inbound timeout을 구분해 반영한다.
- 실패 시 publisher/subscriber 로그와 evidence로 원인 레이어를 분리한다.
