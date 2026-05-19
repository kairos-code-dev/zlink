<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](./cpp-framework-policy.ko.md) | [다음: Draft -- ZLink Framework C++ Registry](./cpp-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework C++ Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 socket, discovery, registry, spot
> runtime event를 어떤 표면으로 올릴지 정리한다.

## 1. 방향

- event kind는 enum으로 둔다.
- 실제 callback payload는 struct로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.

## 2. 등록 예시

```cpp
monitoring_options_t monitoring;
monitoring.add_socket_events("profile.server", socket_event_t::all);
monitoring.add_discovery_events("profile.client.discovery");
monitoring.add_registry_events("registry", std::chrono::seconds(1));
monitoring.add_spot_events("stage-node", std::chrono::seconds(1));
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
