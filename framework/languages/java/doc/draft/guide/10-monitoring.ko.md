<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Registry](./09-registry.ko.md) | [다음: 인터페이스 카탈로그](./11-interface-catalog.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java Monitoring Guide

> 이 문서는 **구현 전 초안**이다.
> 자세한 계약은 [spring-boot-monitoring](../spring-boot-monitoring.ko.md)이 소유한다.

handler 호출만으로는 운영을 다 볼 수 없다. socket connect/disconnect, registry
status/topology 변화, spot peer/subject 변화, timer handler 실패 같은 **runtime
변화**도 framework 표면에서 받아야 한다. monitoring이 이를 source별로 통일된
방식으로 노출한다.

## 1. source 별 표면

하부 Java zlink 표면이 source마다 모양이 달라, framework는 source별로 표면을
다르게 둔다.

| source | 방식 |
|--------|------|
| socket | raw monitor 기반 event (connect/disconnect/handshake 등) |
| registry | 주기적 snapshot diff 기반 event 합성 |
| spot | 주기적 snapshot diff 기반 + timer 실패는 즉시 |
| discovery | 별도 runtime event 없음 -> Registry snapshot/query로 조회([09](./09-registry.ko.md)) |

공통 규칙: event kind는 `enum`, payload는 `record`, 응용은
`ZLinkRuntimeEventHandler<TEvent>`를 구현해 수신한다.

## 2. Source 등록

`ZLinkMonitoringCustomizer`는 **source 등록만** 한다. 실제 source(socket/registry/
spot)는 같은 앱에 framework 또는 registry 등록으로 이미 올라와 있어야 한다.

```java
@Bean
ZLinkMonitoringCustomizer monitoring() {
    return options -> {
        options.addSocketEvents("profile.server", SocketEvent.CONNECTION_READY);
        options.addRegistryEvents("registry", Duration.ofSeconds(1));
        options.addSpotEvents("stage-node", Duration.ofSeconds(1));
    };
}
```

- socket source 이름은 `channel + capability`(예: `profile.server`,
  `profile.client`), registry는 infrastructure 이름(예: `registry`), spot은 spot
  node 등록 이름(예: `stage-node`)이다.
- registry/spot polling 주기는 **항상 명시**해야 한다(숨은 기본 주기 없음 — 운영
  코드가 polling 비용을 설정에서 바로 읽도록).
- 존재하지 않는 source 이름을 등록하면 startup validation 오류다.

## 3. Typed handler 작성

`ZLinkRuntimeEventHandler<TEvent>`를 구현하면 framework가 DI로 만들어 호출한다.

### registry

```java
@Component
public final class RegistryMonitor
    implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
    @Override
    public void handle(ZLinkRegistryEvent event) {
        switch (event.event()) {
            case STATUS_CHANGED:
                // event.status() 관찰
                break;
            case TOPOLOGY_CHANGED:
                // event.topology() 관찰
                break;
            default:
                break;
        }
    }
}
```

registry event는 `STATUS_CHANGED`, `TOPOLOGY_CHANGED`, `SERVICE_SUMMARY_CHANGED`
**3종 고정**이다. framework가 주기적으로 registry snapshot을 읽고 직전 값과 비교해
변경 event를 만든다.

### socket

```java
@Component
public final class ProfileSocketMonitor
    implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
    @Override
    public void handle(ZLinkSocketEvent event) {
        // event.event()와 event.diagnostic()으로 상태와 진단 정보를 확인한다.
    }
}
```

socket event kind는 `CONNECTED`, `CONNECTION_READY`, `DISCONNECTED`,
`HANDSHAKE_FAILED`, `PEER_ADMISSION_CHANGED`, `CLOSED`, `INTERNAL`이다.

### spot

spot event는 `STATUS_CHANGED`, `PEERS_CHANGED`, `SUBJECTS_CHANGED` 고정이다.

> **timer 실패는 polling 주기를 기다리지 않는다.** status/peer/subject 변화는
> `addSpotEvents(...)`의 interval로 snapshot diff하지만, timer handler 실패는
> 발생 시점에 즉시 발행된다. timer 정책은 [06-spot §4](./06-spot.ko.md) 참고.

Spring application event bridge는 선택 기능이다. public 기준은 typed handler다.

## 4. 자주 막히는 곳

- **이벤트가 안 온다** -> `ZLinkMonitoringCustomizer`는 source 등록만 한다. 해당
  source가 framework/registry 등록으로 실제로 떠 있는지 확인한다.
- **discovery 상태를 받고 싶다** -> discovery는 runtime event가 아니다. Registry
  snapshot/query로 조회한다([09-registry](./09-registry.ko.md)).

## 5. 더 보기

- topology 스냅샷 조회: [09-registry](./09-registry.ko.md)
- timer 정책: [06-spot](./06-spot.ko.md)
- 정식 계약: [spring-boot-monitoring](../spring-boot-monitoring.ko.md)
