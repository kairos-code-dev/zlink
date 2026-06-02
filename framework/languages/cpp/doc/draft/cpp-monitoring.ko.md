<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md) | [다음: Draft -- ZLink Framework C++ Registry](./cpp-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework C++ Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 socket, discovery, registry, spot
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

## 1. 방향

- event kind는 enum으로 둔다.
- 실제 callback payload는 struct로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.
- SPOT timer handler failure는 snapshot diff를 기다리지 않는 point-in-time event로
  올린다.

## 2. 등록 예시

```cpp
monitoring_options_t monitoring;
monitoring.add_socket_events("profile.server", socket_event_t::all);
monitoring.add_discovery_events("profile.client.discovery");
monitoring.add_registry_events("registry", std::chrono::seconds(1));
monitoring.add_spot_events("stage-node", std::chrono::seconds(1));
monitoring.add_spot_timer_events("spot-timer");
```

## 3. Handler 예시

```cpp
class profile_socket_monitor_t final
    : public runtime_event_handler_t<socket_event_payload_t> {
public:
    void handle(const socket_event_payload_t &event) override {
    }
};
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`
- spot timer: `spot-timer`

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
