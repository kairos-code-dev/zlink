<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](../internals/cpp-framework-policy.ko.md) | [다음: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Registry](cpp-registry.ko.md)

# Spec -- ZLink Framework C++ Monitoring

> 이 문서는 **구현 완료된 설계 계약**이다.
> `C++` runtime에서 socket, discovery, registry, spot
> runtime event를 어떤 표면으로 올릴지 정리한다.

## 인터페이스 경계

monitoring public contract는 `contracts/eventing/*`와 필요한 기능별 event model이
소유한다. 사용자는 logical source name, event kind, typed payload, handler 등록 표면만
본다. socket monitor binding, registry snapshot diff cache, spot snapshot provider,
timer failure event factory, telemetry backend는 `src/runtime/diagnostics/*`와 각 기능별
runtime에 둔다.

`.NET`의 `IZLinkRuntimeEventPublisher`에 대응하는 C++ 표면은
`runtime_event_publisher_t`다. 사용자는 `app.monitoring().publisher().publish(event)`로
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
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.
- SPOT timer handler failure는 snapshot diff를 기다리지 않는 point-in-time event로
  올린다.

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
안 되고, 같은 종류 안에서 같은 이름을 두 번 등록할 수 없다. framework runtime이 올리는 event
(`add_socket_events` 등)는 등록된 source만 handler로 전달된다. (`monitoring().publisher().publish(...)`
직접 호출 경로는 source 검증을 거치지 않는다.)

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`
- spot timer: `spot-timer`

registry와 spot snapshot monitoring은 주기적으로 상태를 읽어 event로 바꾸므로 interval이 0보다
커야 한다. 0 이하 interval은 polling이 실제로 일어나지 않는 설정이므로 builder가 설정 오류로
처리한다.

timer handler 예외 event는 interval 설정을 기다리지 않고 즉시 전달한다. payload에는
exception 객체 자체가 아니라 timer 이름, handler 타입 이름, delivery index,
scheduled index, exception type, message 같은 직렬화 가능한 요약 정보를 넣는다.

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

readiness는 외부 요청을 받을 준비가 되었는지를 나타내고, liveness는 process를 계속 살려 둘
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
**레퍼런스 구현**이다. 공통 의미는 [공통 스펙 — 메시지 흐름 추적](../../common/spec/message-flow-tracing.ko.md)이
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

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](../internals/cpp-framework-policy.ko.md) | [다음: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
