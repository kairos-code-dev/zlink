<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Registry](08-registry.ko.md) | [다음: Feature Map](10-feature-map.ko.md)
<!-- framework-adapter-nav:end -->

# Java Monitoring Guide

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
| discovery | 별도 runtime event 없음 -> Registry snapshot/query로 조회([09](08-registry.ko.md)) |

공통 규칙: event kind는 `enum`, payload는 `record`, 응용은
`ZLinkRuntimeEventHandler<TEvent>`를 구현해 수신한다.

## 2. Source 등록

`ZLinkMonitoringOptionsCustomizer`는 **source 등록만** 한다. 실제 source(socket/registry/
spot)는 같은 앱에 framework 또는 registry 등록으로 이미 올라와 있어야 한다.

```java
@Bean
ZLinkMonitoringOptionsCustomizer monitoring() {
    return options -> {
        options.addSocketEvents("profile.server", ZLinkSocketEventKind.CONNECTION_READY);
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
    @Override
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

발행되는 snapshot spot event는 `STATUS_CHANGED`, `PEERS_CHANGED`, `SUBJECTS_CHANGED` 3종이다.
`ZLinkSpotEventKind` enum에는 `TIMER_HANDLER_FAILED`, `TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION`
도 정의되어 있으나 현재 monitoring runtime은 이 timer event를 발행하지 않는다.

> **poll 모델.** registry/spot interval 중 최소값으로 단일 poller를 만들어, 매 tick 모든
> source를 snapshot diff한다. per-source 별도 interval로 도는 것은 아니다.

public 기준은 typed handler(`ZLinkRuntimeEventHandler<TEvent>`)다.

## 4. 자주 막히는 곳

- **이벤트가 안 온다** -> `ZLinkMonitoringOptionsCustomizer`는 source 등록만 한다. 해당
  source가 framework/registry 등록으로 실제로 떠 있는지 확인한다.
- **discovery 상태를 받고 싶다** -> discovery는 runtime event가 아니다. Registry
  snapshot/query로 조회한다([09-registry](08-registry.ko.md)).
- **등록되지 않은 메시지를 알고 싶다** -> `configureDispatch()` 에
  `ZLinkMessageFlowObserver` 를 등록하고 `outcome() == ERROR` event 를 본다. request 실패는 error reply 로 돌아가고,
  send/publish/subscription/actor send 실패는 drop 되지만 로그, counter, message-flow event 로 남는다.
  observer 는 관측용이므로 callback 이 실패해도 원래 dispatch 결과를 바꾸지 않는다.

## 5. 메시지 흐름 추적 — 메시지 생애주기 관찰

monitoring 이 socket/registry/spot **상태 변화**를 본다면, 메시지 흐름 추적은 한 메시지가
**도착했나 / 핸들러로 갔나 / 응답이 나갔나**를 dispatch 길목에서 표준 기능으로 찍는다. `corr=`로
grep 하면 한 요청의 생애주기가 노드 간으로 이어진다. dispatch 제어가 아니라 관측이다.

framework options 등록(`ZLinkFrameworkConfigurer`)에서 `configureDispatch()` 체인으로만 켠다.

```java
@Bean
ZLinkFrameworkConfigurer dispatchTracing() {
    return options -> options.configureDispatch()
        // OFF → ERRORS_ONLY(기본) → KEY_TRANSITIONS → VERBOSE → DIAGNOSTIC
        .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
        .traceLogFile("logs/flow-api.log")   // 지정=전용 파일, 미지정=앱 로거 통합, 둘 다 없으면 stderr
        .traceLabel("api");                  // 구조화 필드 label=
}
```

- 모드 게이팅: `DROPPED`·에러는 `ERRORS_ONLY` 이상, 성공 전이는 `KEY_TRANSITIONS` 이상. `OFF` 면
  이벤트 생성 자체가 없어 제로코스트다.
- 운영 중 켜고 끄기: `ZLinkMessageFlowControl`(Spring `ZLinkFrameworkLifecycle` 빈)을 주입받아
  `setMessageFlowMode(...)`(재시작 불필요).
- 콜렉터/OTel 연동: `setMessageFlowObserver(...)`로 구조화 이벤트를 받는다(앱 레이어). framework 는
  `correlationId` + 구조화 필드 + observer 훅까지만 제공하고 OTel 에 의존하지 않는다.
- 정식 계약: [spring-boot-monitoring §7](../../spec/server/languages/java/01-system-structure.ko.md), 공통 의미:
  [공통 스펙 메시지 흐름 추적](../../spec/server/52-message-flow-tracing.ko.md).

## 6. 더 보기

- topology 스냅샷 조회: [09-registry](08-registry.ko.md)
- timer 정책: [06-spot](05-spot.ko.md)
- 정식 계약: [spring-boot-monitoring](../../spec/server/languages/java/01-system-structure.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Registry](08-registry.ko.md) | [다음: Feature Map](10-feature-map.ko.md)
<!-- framework-adapter-nav:bottom:end -->
