<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot Channel Messaging](spring-boot-channel-messaging.ko.md) | [다음: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Registry](spring-boot-registry.ko.md)

# ZLink Framework Spring Boot Monitoring

## 1. 방향

운영 이벤트는 일반 request/send handler와 다르다. 이 문서는 아래를 기본으로 본다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record로 둔다.
- socket는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.
- stream session lifecycle로 매핑 가능한 transport 오류만 `ZLinkStreamError`로
  session callback에 올린다.
- timer handler 실패는 spot runtime event로도 볼 수 있어야 한다.

## 2. 등록 예시

`.NET`의 `AddZLinkMonitoring(...)`에 대응하는 Java 표면은 monitoring source만
등록한다. 실제 socket, registry, spot source는 같은 application 안에 이미
framework나 registry 등록으로 만들어져 있어야 한다.

```java
@Configuration
public class MonitoringConfig {
    @Bean
    ZLinkMonitoringOptionsCustomizer zlinkMonitoringOptionsCustomizer() {
        return options -> {
            options.addSocketEvents(
                "profile.server",
                ZLinkSocketEventKind.CONNECTION_READY,
                ZLinkSocketEventKind.DISCONNECTED);
            options.addRegistryEvents("registry", Duration.ofSeconds(1));
            options.addSpotEvents("stage-node", Duration.ofSeconds(1));
        };
    }
}
```

Spring Boot starter는 configurer가 있으면 monitoring hosted lifecycle을 등록한다.
configurer가 없으면 monitoring runner를 만들지 않는다.

등록 가능한 source는 아래로 제한한다.

| Source | 등록 메서드 | source name 기준 |
|--------|-------------|------------------|
| socket | `addSocketEvents(...)` | channel 역할 logical name |
| registry | `addRegistryEvents(...)` | registry event source label |
| spot | `addSpotEvents(...)` | SpotNode name |

socket과 spot source 이름은 startup 시점에 실제 runtime source와 대조한다. 이름이
맞지 않으면 startup validation 오류다. registry source 이름은 embedded registry에서
발생한 event의 label로 쓰이며, registry monitoring을 쓰려면 embedded registry가 같은
application 안에 있어야 한다. registry와 spot polling interval은 `Duration.ZERO`보다
커야 한다.

## 3. Handler 예시

```java
@Component
public final class ProfileSocketMonitor
    implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {

    @Override
    public void handle(ZLinkSocketEvent event) {
    }
}

@Component
public final class RegistryMonitor
    implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {

    @Override
    public void handle(ZLinkRegistryEvent event) {
    }
}
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- registry: `registry`, `ops-registry`
- spot: `stage-node`

Spring adapter는 typed event handler를 호출한다. public 계약은
`ZLinkRuntimeEventHandler<T>`가 기준이다.

## 4. Event 모델

event payload는 source별 record로 둔다. application handler는 native event mask나
registry snapshot 형식을 직접 해석하지 않는다.

```java
public interface ZLinkRuntimeEvent {
    String sourceName();
    Instant timestamp();
}

public interface ZLinkRuntimeEventHandler<T extends ZLinkRuntimeEvent> {
    void handle(T event);
}
```

handler 실패는 monitoring runner를 중단하지 않는다. 실패 event를 내부 logger와
diagnostic counter에 기록하고 다음 event dispatch를 계속한다.

## 5. Lifecycle

monitoring lifecycle은 source runtime이 준비된 뒤 attach된다.

1. framework와 registry option validation
2. framework와 registry runtime start
3. monitoring source validation
4. socket monitor attach
5. registry/spot polling task start

shutdown은 polling task와 monitor attach를 먼저 해제한 뒤 framework와 registry
runtime을 멈춘다. 일시적인 registry query 실패나 spot snapshot 실패는 startup 실패가
아니라 runtime event 또는 diagnostic failure로 다룬다.

## 6. 검증 기준

- socket/SpotNode monitoring source 이름이 runtime source name과 맞지 않으면
  startup validation 오류다. registry source는 embedded registry 존재 여부로 매핑되어
  event label로 쓰인다.
- registry/spot polling interval이 `0` 이하이면 startup validation 오류다.
- socket native event는 typed event로 변환된다.
- registry/spot snapshot diff는 typed event로 변환된다(발행 spot event는 status/peers/subjects).
- `ZLinkRuntimeEventHandler<T>` 실패는 monitoring runner를 중단하지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot Channel Messaging](spring-boot-channel-messaging.ko.md) | [다음: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
