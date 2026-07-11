<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: C++ Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md) | [다음: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](../../../../cpp/README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Registry](cpp-registry.ko.md)

# Spec -- ZLink Framework C++ Monitoring

> 이 문서는 C++ monitoring 공개 표면을 고정한다. handler 등록, source 등록 검증,
> 직접 publish와 channel, registry, Spot runtime의 자동 event 발행이 공개 계약이다.

## 인터페이스 경계

monitoring public contract는 `contracts/eventing/*`와 필요한 기능별 event model이
소유한다. 사용자는 logical source name, event kind, typed payload, handler 등록 표면만
본다. source 등록 API는 source 이름과 filter 설정을 검증하고 monitoring runtime에 보관한다.
channel, registry와 Spot runtime은 등록된 source의 상태 변화를 typed payload로 만들어
monitoring runtime에 자동 발행해야 한다.

C++ event 발행 표면은 `runtime_event_publisher_t`다. 사용자는
`app.monitoring().publisher().publish(event)`로
typed runtime event를 직접 올릴 수 있고, 등록된 typed handler와 trace hook은 monitoring
runtime이 같은 순서로 호출한다. publisher는 monitoring state를 공유하지만 handler map이나
trace hook 저장 구조를 공개하지 않는다.

monitoring event는 내부 구현 상태를 그대로 공개하지 않는다. payload는 운영자가 이해할 수
있는 안정적인 field만 담고, native handle이나 private runtime pointer를 포함하지 않는다.

health public contract는 `contracts/eventing/health.hpp`가 소유한다. 사용자는
`app.health()`에서 zlink runtime, channel, registry, STREAM endpoint, hosted service check를
등록하고 `report()`로 전체 health, readiness, liveness를 읽는다. check 저장 구조와 집계
규칙은 `src/runtime/diagnostics/health.cpp`에 둔다.
HTTP를 사용하는 app은 `options.http().map_health(...)`,
`map_readiness(...)`, `map_liveness(...)`로 같은 report를 JSON endpoint로 노출할 수 있다.
이 endpoint는 health 집계를 새로 계산하지 않고 `app.health()` 표면을 읽는다.

metrics public contract는 `metrics_builder_t`와 `metric_event_payload_t`가 소유한다. 사용자는
`app.metrics().add_runtime_metrics()`로 runtime metric event 표면을 켜고,
`record_runtime_metric(...)`으로 typed metric payload를 올린다. metric event는 monitoring
state를 공유하므로 `app.monitoring().on<metric_event_payload_t>(...)` handler와 trace hook을
같은 순서로 통과한다. exporter, label schema, 외부 telemetry backend는 core가 직접 정하지 않고
extension에서 연결한다.

## 1. 방향

- event kind는 enum으로 둔다.
- 실제 callback payload는 struct로 둔다.
- socket, discovery, registry, spot source 이름은 public builder에서 등록한다.
- 등록된 source의 event payload는 monitoring runtime의 handler와 trace hook을 통과한다.
- application이 `monitoring().publisher().publish(...)`로 직접 올린 event도 같은 handler와
  trace hook 경로를 지난다.
- framework runtime은 channel, registry와 Spot 상태 변화를 등록된 source event로 자동
  발행한다.

## 2. 등록 예시

```cpp
auto &monitoring = app.monitoring();
monitoring.add_socket_events("profile.server");
monitoring.add_socket_events(
  "profile.client",
  {socket_event_kind_t::connection_ready});
monitoring.add_discovery_events("profile.client.discovery");
monitoring.add_registry_events("registry", std::chrono::seconds(1));
monitoring.add_spot_events("stage-node", std::chrono::seconds(1));
monitoring.add_spot_timer_events("spot-timer");
```

socket event 등록은 source 이름을 기준으로 한다. event 목록을 넘기지 않으면 해당 source의 모든
socket event를 받는다. event 목록을 넘기면 지정한 kind만 handler와 trace hook으로 전달한다.
source 이름은 비어 있으면 안 되고, 같은 source를 두 번 등록하면 설정 오류로 처리한다. 이 규칙은
source별 event filter를 한 곳에서 해석하게 해서 handler가 불필요한 event를 직접 걸러 내는 부담을
줄이기 위한 것이다.

## 3. Handler 예시

```cpp
app.monitoring().on<socket_event_payload_t>(
    [](const socket_event_payload_t &event) {
    });
```

source 이름은 logical name을 쓰는 편이 자연스럽다. 모든 monitoring source 이름은 비어 있으면
안 되고, 같은 종류 안에서 같은 이름을 두 번 등록할 수 없다. 등록된 source 이름과 kind filter는
monitoring runtime이 event 전달 여부를 판단할 때 사용한다. `monitoring().publisher().publish(...)`
직접 호출 경로는 source 검증을 거치지 않는다.

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`
- spot timer: `spot-timer`

registry와 spot source 등록은 polling interval을 받는다. interval은 0보다 커야 한다. 0 이하
interval은 실제 polling source를 구성할 수 없는 설정이므로 builder가 설정 오류로 처리한다.

timer handler 예외 payload 모델은 exception 객체 자체가 아니라 timer 이름, handler 타입 이름,
delivery index, scheduled index, exception type, message 같은 직렬화 가능한 요약 정보를 담는다.
framework runtime은 timer handler 예외를 이 payload로 자동 발행한다.

## 4. Publisher 예시

```cpp
auto publisher = app.monitoring().publisher();
publisher.publish(actor_event_payload_t{
  runtime_event_base_t{"game.actor"},
  actor_event_kind_t::bound,
  "player",
  "alice",
  "session-1",
  {}
});
```

publisher는 event timestamp를 publish 시점으로 보정한다. 사용자가 만든 payload가
handler로 전달되기 전에 trace hook이 먼저 호출된다. 이 순서는 `.NET` monitoring event
publisher와 같은 의미로, 운영자가 전체 event stream을 먼저 볼 수 있게 하기 위한 것이다.

## 5. Health 예시

```cpp
auto report = app.health()
  .add_zlink_runtime_check()
  .add_channel_check("profile.server")
  .add_registry_check("registry")
  .add_stream_endpoint_check("game.stream")
  .add_hosted_service_check("worker")
  .report();

if (!report.ready()) {
  return 1;
}
```

readiness는 외부 요청을 받을 준비가 되었는지를 나타내고, liveness는 process를 계속 유지할
수 있는지를 나타낸다. channel, registry, STREAM endpoint는 readiness에 반영하고, hosted
service와 zlink runtime은 readiness와 liveness에 함께 반영한다.

HTTP endpoint를 함께 열면 아래처럼 health route를 매핑한다.

```cpp
app.add_zlink_framework([](auto &options) {
  options.http()
    .listen("http://0.0.0.0:8080")
    .map_health("/health")
    .map_readiness("/ready")
    .map_liveness("/live");
});
```

## 6. Metrics 예시

```cpp
app.monitoring().on<metric_event_payload_t>([](const auto &event) {
  // exporter extension can consume event.name, event.value, and event.tags.
});

app.metrics()
  .add_runtime_metrics()
  .record_runtime_metric(
    "active_http_requests",
    3,
    {{"surface", "http"}});
```

## 7. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/registry/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. C++는 이 기능의
**레퍼런스 구현**이다. 공통 의미는 [공통 스펙 — 메시지 흐름 추적](../../message-flow-tracing.ko.md)이
소유하고, 이 절은 C++ 표면만 적는다. dispatch 제어가 아니라 관측이며, observer 실패가 처리/응답을
깨지 않는다.

### 7.1 표면

| 공통 개념 | C++ 타입 / 멤버 |
|-----------|-----------------|
| 로그 모드 | `message_flow_log_mode_t` { `off`, `errors_only`(기본), `key_transitions`, `verbose`, `diagnostic` } |
| outcome | `message_flow_outcome_t` { `received`, `dispatched`, `replied`, `dropped`, `sent`, `reply_received`, `error` } |
| event | `message_flow_event_t`: `outcome`, `surface`, `message_kind`, `packet_name`, `channel_name`, `topic`, `correlation_id`, `source_rid`, `spot_rid`, `actor_id`, `message_size`, `error_reason`, `error_action`, `exception` |
| observer | `message_flow_observer_t::on_message_flow(...)` / `dispatch_options_t::set_message_flow_observer(shared_ptr \| std::function)` |
| 진단 옵션(read-only) | `dispatch_options().diagnostics.message_flow()` / `effective_message_flow()` / `sample_rate()` / `include_message_sizes()` / `log_file()` / `label()` |
| 런타임 토글 | `app_t::set_message_flow_mode(mode)` / `message_flow_mode()` |

게이팅(공통 규칙): `dropped`·에러는 `errors_only` 이상, 성공 전이는 `key_transitions` 이상에서
발화한다. `sample_rate<1`은 성공 전이만 thinning하고 `dropped`·에러는 항상 통과한다.

### 7.2 설정 (builder 전용)

진단 필드는 read-only이며 `configure_dispatch()` fluent 체인으로만 설정한다.

```cpp
app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &options) {
    options.configure_dispatch ()
      .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
      .trace_log_file ("logs/flow-api.log")   // 지정=전용 파일, 미지정=app.logging() 통합
      .trace_label ("api")                    // 구조화 필드 label=
      .include_message_sizes (true);          // verbose에서 size=
});
```

- `trace_log_file` 지정 시 트레이싱/에러는 전용 파일로만, 미지정 + `app.logging()` sink 있으면
  통합, 둘 다 없으면 `std::clog` 폴백. `use_file`/`use_rotating_file`은 부모 디렉토리를 자동
  생성한다. 출력은 `logger_t::log_with_fields`로 구조화 필드 + `label=`를 함께 낸다.
- observer setter는 `shared_ptr<message_flow_observer_t>`와 `std::function` 두 변형을 받는다(한쪽
  설정 시 다른 쪽은 비워진다). OTel/콜렉터 어댑터는 앱 레이어 책임이다(공통 스펙 §6).
- 트레이서는 dispatch 옵션을 복사하지 않는 참조 기반이고 호출부가 lazy(게이트 통과 후 이벤트
  생성)라, `off`일 때 옵션 복사·문자열 할당이 0이다. 게이트는 공유 atomic load 1회다.
  dispatch 실패는 별도 observer 표면을 두지 않고 `outcome=error` 메시지 흐름 이벤트로 남긴다.

### 7.3 런타임 토글

`app.set_message_flow_mode(...)`는 공유 atomic live cell을 바꿔 모든 surface에 재시작 없이 즉시
반영한다. `message_flow(...)`는 seed(기본값)이고, live cell이 설치되면 정적 모드를 override한다.

```cpp
app.set_message_flow_mode (zlink::framework::message_flow_log_mode_t::key_transitions);  // off→on
```

### 7.4 스트림 correlation_id 와이어

C++ 스트림은 correlation_id를 헤더 1급 필드로 올린다(채널과 일관). connector `header_codec`와
framework `stream_runtime`이 바이트 동일하게 인코딩하며(`has_correlation_id = 0x08`, 메타데이터
블록 뒤 `u8 len+bytes`), 보내는 클라이언트가 생성하고 서버(session)는 echo만 한다. 와이어
레이아웃 전체는 공통 스펙 §9를 따른다.

### 7.5 샘플

Bingo 3노드(Api/Play/Session)는 각자 `configure_dispatch().message_flow(key_transitions)
.trace_log_file(flow_log_path(role)).trace_label(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR`로 디렉토리 override, `run_sample.sh`가 export). 한 요청을 `corr=`로 grep하면
노드 간 `sent`→`received`→`replied`→`reply_received`가 이어진다.

## 8. 런타임 메트릭 (runtime metrics)

공통 의미는 [공통 스펙 — 런타임 메트릭](../../runtime-metrics.ko.md)이 소유한다. 이 절은 C++ 표면만
적는다.

> **설계 원칙(같은 개념 → 같은 메커니즘).** C++에는 이미 §6의 metric event 표면
> (`metric_event_payload_t` + `app.monitoring().on<metric_event_payload_t>(...)`)이 있다. framework
> 카탈로그 계기는 **새 reader를 만들지 않고 이 기존 표면으로 방출**한다. 앱은 §6과 같은 handler로
> 받아 자기 백엔드(OTel C++ SDK 등)로 브리지한다. per-계기 API는 노출하지 않는다.

### 8.1 표면

| 공통 개념 | C++ |
|-----------|-----|
| 계기 방출 | framework가 공통 §4 카탈로그를 확장된 `metric_event_payload_t` 이벤트로 방출(이름·라벨 키는 카탈로그와 바이트 동일) |
| 수신 | 기존 `app.monitoring().on<metric_event_payload_t>(handler)`(§6) — 새 표면 없음 |
| OTel 브리지 | 앱이 handler에서 OTel C++ SDK로 매핑(framework는 OTel을 모른다, 공통 §6) |

- §6의 `record_runtime_metric(...)`은 **앱 자신의 커스텀 metric**용이고 framework 카탈로그와는 별개
  경로다 — 앱 커스텀 계기에 framework 소유 라벨(`surface` 등)을 붙이지 않는다(공통 §5).
- `updown`은 이벤트 시점 값, `observable`은 주기 방출 시점 값(공통 §7.1). 구독 handler가 없으면 방출
  자체가 접힌다(무한 적재 없음, 공통 §7.3).

```cpp
enum class metric_instrument_kind_t { counter, updown, observable, histogram };
enum class metric_temporality_t { delta, current, sample };

struct metric_event_payload_t {
    std::string name;
    double value;
    std::string unit;
    metric_instrument_kind_t instrument_kind;
    metric_temporality_t temporality;
    std::map<std::string, std::string> tags;
};
```

counter/updown update는 `delta`, observable gauge는 `current`, histogram record는 `sample`이다. 앱의
OTel bridge는 이름별 카탈로그를 다시 하드코딩하지 않고 이 필드로 instrument와 기록 방식을 정한다.

## 9. 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../flow-correlation.ko.md)가 소유한다. §7(메시지
흐름 추적)의 additive 확장이다.

### 9.1 표면

| 공통 개념 | C++ |
|-----------|-----|
| 생성 게이트 | 기존 `configure_dispatch().message_flow(...)` 설정을 그대로 사용한다. 별도 flow id 설정은 없다. |
| event 필드(추가) | `message_flow_event_t.flow_id`(`std::optional<std::string>`, 36B lowercase UUIDv7), `message_flow_event_t.flow_origin`(`enum class flow_origin_t { inbound, timer, application, lifecycle }`, §4.2), `dispatch_error_event_t`에도 `flow_id` 동일 |
| 스트림 와이어 | 첫 byte `format_marker=0xF2`, `has_flow_id=0x10`, correlation_id 뒤 UUIDv7 36B + origin u8(공통 §3.2) |

- connector `header_codec`와 framework `stream_runtime`이 바이트 동일하게 인코딩한다. marker가 없는
  구형 frame decoder를 병행하지 않으며 mismatch는 protocol error다.
- gateway tracer 기본 배선으로 stream/actor gateway 무로그 함정 제거(공통 §7), 게이팅 불변.

## 10. Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../graceful-drain-handoff.ko.md)가 소유한다.
lifecycle 제어 표면(관측 아님)의 C++ 투영이다.

### 10.1 표면

```cpp
enum class flow_origin_t { inbound, timer, application, lifecycle };
enum class spot_drain_policy_t { drain_natural, release_and_recreate };
enum class drain_force_reason_t {
    deadline_exceeded,
    draining_state_publish_failed,
    owner_cleanup_failed,
    teardown_failed
};
struct drained_t {};
struct force_stopped_t { drain_force_reason_t reason; };
using drain_result_t = std::variant<drained_t, force_stopped_t>;

// app_t public members; the parameterless overload uses 30 seconds.
task_t<drain_result_t> drain(std::chrono::milliseconds deadline);
task_t<drain_result_t> drain();
task_t<drain_result_t> await_drained();
bool is_ready() const;
```

| 공통 개념 | C++ |
|-----------|-----|
| drain 제어 | `task<drain_result_t> app.drain(deadline)` / `app.drain()`(30초) / `task<drain_result_t> app.await_drained()` / `bool app.is_ready()` |
| drain 결과 | `std::variant<drained_t, force_stopped_t>`이며 `force_stopped_t.reason`은 `drain_force_reason_t { deadline_exceeded, draining_state_publish_failed, owner_cleanup_failed, teardown_failed }`다. |
| SPOT drain 정책 | spot mesh 등록의 `.use_drain_policy(spot_drain_policy_t::{drain_natural /*기본*/, release_and_recreate})` |
| 상태 관측 | 기존 `app.monitoring().on<drain_event_t>(...)` 재사용. `drain_event_t.state` { `serving`/`draining`/`drained`/`force_stopping` } |
| readiness | Draining 진입 시 기존 `app.health().report().ready()`가 false — 새 표면 없음(§5 `map_readiness`가 그대로 반영) |
| connector 종료 사유 | `stream_disconnect_event_t.close_reason`은 `stream_close_reason_t { client_close, idle_timeout, heartbeat_timeout, server_drain, protocol_error, transport_error }`를 제공 |

- C++는 DI/host lifecycle 앰비언트가 없으므로 애플리케이션이 signal을 소유하고 종료 실행 문맥에서
  `app.drain()`을 명시 호출한다. signal handler 안에서 직접 비동기 작업을 수행하지 않으며 framework가
  signal handler를 설치하지 않는다. draining 마커·owner lease 유지·`Takeover` 순서(공통 §3)는
  framework 내부가 소유한다.
- **drain 이벤트는 source 등록이 필요 없다.** 노드 생애 수 회의 저빈도 lifecycle 이벤트라 polling·
  filter 개념이 없고, `on<drain_event_t>(...)` handler 존재만으로 수신한다(공통 §9, 조용한 무관측
  없음).

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: C++ Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md) | [다음: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
