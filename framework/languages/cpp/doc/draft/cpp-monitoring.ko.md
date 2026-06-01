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

monitoring event는 내부 구현 상태를 그대로 공개하지 않는다. payload는 운영자가 이해할 수
있는 안정적인 field만 담고, native handle이나 private runtime pointer를 포함하지 않는다.

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
