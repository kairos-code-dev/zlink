<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot Channel Messaging](spring-boot-channel-messaging.ko.md) | [다음: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../../../../java/README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Registry](spring-boot-registry.ko.md)

# ZLink Framework Spring Boot Monitoring

## 1. 방향

운영 이벤트는 일반 request/send handler와 다르다. 이 문서는 아래를 기본으로 본다.

- event kind는 enum으로 둔다.
- 실제 callback payload는 record로 둔다.
- socket은 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.
- stream session lifecycle로 매핑 가능한 transport 오류만 `ZLinkStreamError`로
  session callback에 올린다.
- timer handler 실패는 spot runtime event로도 볼 수 있어야 한다.

## 2. 등록 예시

Java monitoring 구성은 monitoring source만 등록한다. 실제 socket, registry, spot source는 같은 application 안에 이미
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

    @Override
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

## 7. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/registry/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미는
[공통 스펙 — 메시지 흐름 추적](../../message-flow-tracing.ko.md)이 소유하고, 이 절은
Java 표면만 적는다. dispatch 제어가 아니라 관측이며, observer 실패가 처리나 응답을 깨지 않는다.

### 7.1 표면

| 공통 개념 | Java 타입 / 멤버 |
|-----------|------------------|
| 로그 모드 | `ZLinkMessageFlowLogMode` { `OFF`, `ERRORS_ONLY`(기본), `KEY_TRANSITIONS`, `VERBOSE`, `DIAGNOSTIC` } |
| outcome | `ZLinkMessageFlowOutcome` { `RECEIVED`, `DISPATCHED`, `REPLIED`, `DROPPED`, `SENT`, `REPLY_RECEIVED`, `ERROR` } |
| event | `ZLinkMessageFlowEvent`(record): `outcome()`, `surface()`, `messageKind()`, `packetName()`, `channelName()`, `topic()`, `correlationId()`, `sourceRid()`, `spotRid()`, `actorId()`, `messageSize()`, `errorReason()`, `errorAction()`, `exception()` |
| observer | `ZLinkMessageFlowObserver.onMessageFlow(ZLinkMessageFlowEvent)` → `CompletionStage<Void>` |
| 진단 옵션(read-only) | `configureDispatch().diagnostics()` → `ZLinkDiagnosticsOptions` { `messageFlow()`, `effectiveMessageFlow()`, `sampleRate()`, `includeMessageSizes()`, `logFile()`, `label()` } |
| 런타임 토글 | `ZLinkMessageFlowControl.setMessageFlowMode(...)` / `messageFlowMode()` (Spring `ZLinkFrameworkLifecycle` 빈이 구현·위임) |

게이팅(공통 규칙): `DROPPED`·에러는 `ERRORS_ONLY` 이상, 성공 전이는 `KEY_TRANSITIONS` 이상에서
발화한다. `sampleRate<1`은 성공 전이만 thinning하고 `DROPPED`·에러는 항상 통과한다.

### 7.2 설정 (builder 전용)

framework options 등록(`ZLinkFrameworkConfigurer`)에서 `configureDispatch()` 체인으로만 설정한다.
진단 필드는 read-only다.

```java
@Bean
ZLinkFrameworkConfigurer dispatchTracing() {
    return options -> options.configureDispatch()
        .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
        .traceLogFile("logs/flow-api.log")   // 지정=전용 파일(앱 로그와 분리)
        .traceLabel("api")                   // 구조화 필드 label=
        .includeMessageSizes(true);          // VERBOSE에서 size=
}
```

- `traceLogFile` 지정 시 트레이싱/에러는 전용 파일로만, 미지정 + 앱 로거 sink 있으면 통합, 둘 다
  없으면 표준 에러스트림 폴백. 출력은 key=value 구조화(JUL/파일) + `label=`로 콜렉터 ingest 가능.
- observer는 `setMessageFlowObserver(MyObserver.class)` 또는 인스턴스/람다로 등록한다(단일 메서드
  인터페이스라 람다 호환). 콜렉터/OTel 어댑터는 앱 레이어 책임이다(공통 스펙 §6).
- `OFF`일 때는 이벤트를 생성조차 하지 않아(호출부 가드 + lazy) 운영 성능에 영향이 없다.

### 7.3 런타임 토글

`ZLinkMessageFlowControl`은 Spring `ZLinkFrameworkLifecycle` 빈이 구현하므로 주입받아 재시작 없이
모드를 바꾼다. 공유 live cell을 모든 surface가 읽어 즉시 반영된다.

```java
flowControl.setMessageFlowMode(ZLinkMessageFlowLogMode.KEY_TRANSITIONS);  // off→on
```

### 7.4 샘플

Java/Kotlin Bingo 3노드는 각자 `messageFlow(KEY_TRANSITIONS)` +
`traceLogFile(.../flow-<role>.log)` + `traceLabel(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR` override). Kotlin은 같은 Java 런타임을 공유하며 `configureDispatch { }` DSL과
`onMessageFlow { }` 람다 옵저버(에르고노믹스)를 추가로 제공한다.

## 8. 런타임 메트릭 (runtime metrics)

공통 의미는 [공통 스펙 — 런타임 메트릭](../../runtime-metrics.ko.md)이 소유한다. 이 절은 Java 표면만
적는다.

> **설계 원칙(깊은 모듈): 공통 케이스는 무설정.** Spring Boot 앱에 Micrometer `MeterRegistry` 빈이
> 있으면 framework가 자동으로 바인딩해 카탈로그 계기를 방출한다. 앱은 계기를 하나도 선언하지 않는다.

### 8.1 표면

| 공통 개념 | Java |
|-----------|------|
| 계기 이름 접두 | `zlink.` (Micrometer meter name, 공통 §4.0 바이트 동일) |
| 계기 방출 | 앰비언트 `MeterRegistry`에 `Counter`/`Gauge`/`Timer`(histogram) 등록 |
| 앱 연결(공통 케이스) | Spring Boot Actuator + Micrometer registry 자동 구성 — 별도 zlink 설정 없음 |
| 커스텀 조정(선택) | `ZLinkMetricsCustomizer`(registry·공통 태그 조정) |

- 공통 §3 `updown`=Micrometer `Gauge`(등록 시 상태 참조), `observable`=`Gauge` 콜백, histogram=`Timer`
  또는 `DistributionSummary`.
- registry가 없으면(예: 테스트) 계기는 no-op registry로 접혀 성능 영향이 없다(공통 §7.2).
- 대시보드·exporter(Prometheus/OTLP)는 앱 몫이다(공통 §6).

## 9. 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../flow-correlation.ko.md)가 소유한다. §7(메시지
흐름 추적)의 additive 확장이며 새 최상위 표면을 만들지 않는다.

### 9.1 표면

| 공통 개념 | Java |
|-----------|------|
| flow id 모드 | `ZLinkFlowIdMode` { `NONE`, `MONOTONIC`(기본), `GLOBAL_UNIQUE` } |
| 설정 | `configureDispatch().flowId(ZLinkFlowIdMode.GLOBAL_UNIQUE)` |
| event 필드(추가) | `ZLinkMessageFlowEvent.flowId()` (§7.x record accessor 추가), 오류 이벤트에도 동일 |

```java
ZLinkFrameworkConfigurer dispatchTracing() {
    return configurer -> configurer.configureDispatch()
        .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
        .flowId(ZLinkFlowIdMode.GLOBAL_UNIQUE);   // 기본은 MONOTONIC
}
```

- 생성은 모드 게이트, 전파는 무조건(공통 §2.2). stream/actor gateway 로거 자동 배선(공통 §7),
  게이팅 불변. Kotlin은 `configureDispatch { flowId(...) }` DSL로 노출한다.

## 10. Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../graceful-drain-handoff.ko.md)가 소유한다.
lifecycle 제어 표면(관측 아님)의 Java 투영이다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 Spring `SmartLifecycle`로 graceful
> shutdown에 자동 참여해 drain한다. 앱은 코드를 쓰지 않는다.

### 10.1 표면

| 공통 개념 | Java |
|-----------|------|
| 자동 drain(기본) | framework `SmartLifecycle` 빈이 shutdown에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `useDrainPolicy(ZLinkSpotDrainPolicy.{DRAIN_NATURAL(기본)/DEADLINE/RELEASE_AND_RECREATE})` |
| 명시 제어(선택) | `ZLinkDrainControl` { `drainAsync(Duration deadline)` → `CompletionStage<Void>`, `awaitDrained()` → `CompletionStage<Void>`, `boolean isReady()` } (빈) |
| readiness probe | `ZLinkDrainControl.isReady()` 또는 Actuator readiness group 연동 |
| 상태 관측 | 기존 `ZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.state()` { `SERVING`/`DRAINING`/`DRAINED`/`FORCE_STOPPING` } |

```java
options.addSpotMesh("orders")
    .useDrainPolicy(ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE);
```

- drain 상태 관측은 monitoring의 `ZLinkRuntimeEventHandler<T>`를 그대로 쓴다(같은 개념 → 같은
  메커니즘). Kotlin은 `suspend fun awaitDrained()` extension과 `onDrain { }` 람다를 추가로 제공한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot Channel Messaging](spring-boot-channel-messaging.ko.md) | [다음: ZLink Framework Spring Boot Registry](spring-boot-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
