<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Java STREAM Open Items](stream-open-items.ko.md) | [다음: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework Spring Boot Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 socket, discovery, registry, spot
> runtime event를 어떤 표면으로 올릴지 정리한다.

## 1. 방향

운영 이벤트는 일반 request/send handler와 다르다. 이 초안은 아래를 기본으로 본다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.

## 2. 등록 예시

```java
@Configuration
public class MonitoringConfig {
    @Bean
    ZLinkMonitoringCustomizer zlinkMonitoringCustomizer() {
        return options -> {
            options.addSocketEvents(
                "profile.server",
                SocketEvent.CONNECTION_READY | SocketEvent.DISCONNECTED);
            options.addDiscoveryEvents(
                "profile.client.discovery",
                ServiceMonitorEventMask.ALL);
            options.addRegistryEvents("registry", Duration.ofSeconds(1));
            options.addSpotEvents("stage-node", Duration.ofSeconds(1));
        };
    }
}
```

## 3. Handler 예시

```java
@Component
public final class ProfileSocketMonitor
    implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {

    @Override
    public CompletionStage<Void> handleAsync(ZLinkSocketEvent event) {
        return CompletableFuture.completedFuture(null);
    }
}

@Component
public final class RegistryMonitor
    implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {

    @Override
    public CompletionStage<Void> handleAsync(ZLinkRegistryEvent event) {
        return CompletableFuture.completedFuture(null);
    }
}
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`
