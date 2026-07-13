<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework NestJS Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ZLink Framework NestJS Registry](nestjs-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../node/README.ko.md)

[Node.js 묶음](../../../../node/README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel](nestjs-channel-messaging.ko.md) | [SPOT](nestjs-spot.ko.md) | [STREAM](nestjs-stream.ko.md) | [Registry](nestjs-registry.ko.md)

# ZLink Framework NestJS Monitoring

> 이 문서는 Node.js `ZLink Framework`(NestJS)의 monitoring **스펙**이다. 기준은
> `framework/languages/node` 코드이며, 표면은 TypeScript / NestJS 모양이다.
>
> **현재 구현 상태:** monitoring 등록 표면(`monitoring: {...}` 옵션, `ZLinkModule`
> 통합), `ZLinkRuntimeEventHandler<TEvent>` provider discovery, `ZLinkMonitoringModule`
> public module path, timer handler failure 의 즉시 event 발행이 구현돼 있다.
>
> 공통 monitoring 의미는 공통 spec을 따르며, 이 문서는 Node.js의 public 타입과
> NestJS 등록 계약을 정의한다.

## 1. 목표

이 절은 monitoring 표면이 어떤 사건을 담아야 하는지, 그리고 왜 그렇게 정했는지를
정리한다.

운영 관점에서는 handler 호출만 관측해서는 부족하다. 다음과 같은 runtime 변화도
framework 표면에서 함께 받을 수 있어야 한다.

- socket connect / disconnect / handshake[^handshake] 실패
- discovery[^discovery] provider up / down / changed
- registry status / topology[^topology] 변화
- spot node[^spot-node] peer / subject 변화

문제는 하부 Node 바인딩(`@zlink-systems/zlink`) 표면이 source 마다 모양이 다르다는
점이다.

- socket: `SocketMonitor`
- discovery: runtime event 로 노출하지 않는다. 운영 조회는 registry snapshot/query 로 처리한다.
- registry: snapshot/query 만 제공한다.
- spot: status/peer/subject snapshot 만 제공한다.
- timer handler failure: 하부 snapshot 이 아니라 framework timer loop 안에서
  직접 관찰한다.

그래서 framework 는 source 마다 표면을 달리 둔다.

- socket 은 raw monitor[^raw-monitor] 기반 event 로 올린다.
- registry / spot 은 snapshot diff[^snapshot-diff] 기반 event 로 올린다.
- timer handler failure 는 발생 시점에 point-in-time event 로 올린다.
- discovery 자체는 별도 runtime event 로 만들지 않는다. registry 의 topology /
  service / member snapshot 을 조회해서 현재 provider 상태를 확인한다.

## 2. 기본 방향

이 절은 monitoring 표면이 따르는 규칙을 정리한다.

이 문서는 다음 규칙을 기본으로 둔다.

- event kind 는 `enum` 으로 둔다.
- 실제 callback payload 는 불변 `interface` 로 둔다.
- socket 은 하부 monitor 를 그대로 감싼다.
- registry / spot 은 polling[^polling] 과 snapshot diff 로 event 를 합성한다.
- timer handler failure 는 polling interval 을 기다리지 않고 즉시 발행한다.
- discovery 상태는 registry snapshot / query 결과로 조회한다.
- application 은 `ZLinkRuntimeEventHandler<TEvent>` 를 구현해서 이벤트를 받는다.

enum 하나만으로는 충분하지 않다. 운영 코드에서는 event 종류뿐 아니라 source 이름,
endpoint, routing id, snapshot 본문도 함께 필요하기 때문이다.

Node.js monitoring은 `ZLinkMonitoringOptions`, `ZLinkRuntimeEvent`와
`ZLinkRuntimeEventHandler<TEvent>`를 사용한다. handler는
`handle(event): Promise<void>`로 완료를 알리고, payload는 불변 `interface`, event
kind는 `enum`으로 정의한다.
| `TimeSpan interval` | `intervalMs: number` |
| `RoutingId` | `RoutingId`(branded `string`) |
| `DateTimeOffset Timestamp` | `timestamp: Date` |

## 3. 등록 모델

framework 등록은 module options 의 `monitoring` 키로 둔다. `.NET` 의
`AddZLinkMonitoring(monitor => ...)` 람다는 선언적 options 객체와
`ZLinkMonitoringModule.forRoot()` 로 옮긴다. `ZLinkModule` 은 runtime event
publisher token 도 함께 내보내므로, handler discovery 대신 직접 등록해야 하는
애플리케이션은 `ZLINK_RUNTIME_EVENT_PUBLISHER` 를 주입해 사용할 수 있다.

```ts
@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .useInMemoryLocationStores()
        .addClientServerChannel('profile')
          .enableServer('tcp://0.0.0.0:7101')
          .enableClient()
        .addSpotMesh('stage-node')
          .enablePubSub('tcp://0.0.0.0:9000')
        .options({
          monitoring: {
            socket: [
              {
                sourceName: 'profile.server',
                events: [
                  ZLinkSocketEventKind.ConnectionReady,
                  ZLinkSocketEventKind.Disconnected,
                ],
              },
            ],
            registry: [{ sourceName: 'registry', intervalMs: 1000 }],
            spot: [{ sourceName: 'stage-node', intervalMs: 1000 }],
          },
        })
        .build()
    ),
    ZLinkMonitoringModule.forRoot(),
  ],
  providers: [
    ProfileServerSocketMonitor,
    RegistryMonitor,
    StageNodeMonitor,
  ],
})
export class AppModule {}
```

`monitoring` 키는 source 등록만 맡는다. 즉 실제 socket, registry, spot source 는
같은 애플리케이션의 `.addClientServerChannel(...)`, `.addFanoutChannel(...)`,
`.addRouteMesh(...)`, `.addSpotMesh(...)`와 location store 등록으로 이미 구성되어 있어야 한다.

> `.NET` 의 `AddZLinkMonitoring(...)` 은 `AddZLinkFramework(...)` 와 분리된 두 번째
> 등록 호출이다. NestJS 에서는 source 등록은 동일 `ZLinkModule.forRoot(...)` options
> 안의 `monitoring` 키로 두고, monitoring publisher 노출은 `ZLinkMonitoringModule.forRoot()`
> 로 분리한다. 의미는 동일하다 — source 등록은 framework 등록과 같은 application 에
> 있어야 한다.

여기서 한 가지 짚어 둘 점이 있다. 일반 channel 역할[^capability] 와 SPOT
mesh 는 각자 자신의 discovery source 를 가진다. 즉 registry endpoint 집합을
공급하는 곳이 둘로 나뉜다.

- 일반 channel: framework 등록 루트의 `discovery: { registries: [...] }` 가 공급한다.
- SPOT node: framework 등록 루트의 `discovery: { registries: [...] }` 가 공급한다.

source 이름은 다음 규칙으로 잡는 편이 자연스럽다.

- socket
  - `channel + capability` 형태
  - 예: `profile.server`, `profile.client`
- discovery
  - framework 는 별도의 monitoring source 이름을 두지 않는다.
  - 현재 provider 상태는 registry query(`topology(...)`, `serviceSummary(...)`,
    `memberPeers(...)`) 로 조회한다. ([nestjs-registry](nestjs-registry.ko.md))
  - application logging 쪽에서 discovery 활동을 별도 식별자로 남기고 싶다면,
    `profile.client.discovery`, `game.stage.discovery` 같은 이름을 application
    logging convention 으로 둘 수는 있다. 이 이름은 framework monitoring source 의
    등록 이름이 아니다.
- registry
  - infrastructure source 이름
  - 예: `registry`
- spot
  - spot node 등록 이름
  - 예: `stage-node`

> monitoring 에는 `monitoring.discovery: [...]` 등록 키가 없다. discovery 는 runtime
> event 가 아니라 registry query 로만 관측한다
> (§5.4, §7).

## 4. 인터페이스

이 절은 monitoring 표면에서 사용자가 직접 마주하는 타입을 정리한다. `.NET`
`Contracts/Eventing/Contracts.cs` 를 코드로 확인해 옮겼다.

### 4.1 등록 / 핸들러 인터페이스

```ts
export interface ZLinkMonitoringOptions {
  /**
   * socket source 등록. events 를 비우면 그 source 가 지원하는
   * 모든 logical event 를 받는다.
   */
  socket?: ZLinkSocketMonitoringRegistration[];

  /** registry snapshot diff polling source 등록. */
  registry?: ZLinkPollingMonitoringRegistration[];

  /** spot snapshot diff polling source 등록. */
  spot?: ZLinkPollingMonitoringRegistration[];
}

export interface ZLinkSocketMonitoringRegistration {
  sourceName: string;
  /** 생략하면 해당 source 의 모든 logical event 를 받는다. */
  events?: ZLinkSocketEventKind[];
}

export interface ZLinkPollingMonitoringRegistration {
  sourceName: string;
  /** 0 보다 커야 한다. registry/spot polling 주기는 항상 명시한다(§7). */
  intervalMs: number;
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}
```

> `.NET` 의 `AddSocketEvents(sourceName, params events[])` /
> `AddSpotEvents(sourceName, interval)` 메서드는 위 `socket` / `spot` 배열 키에 대응한다.
> 내부적으로 .NET 은 source 이름이 비어 있거나 중복이면 `ZLinkConfigurationException`
> 을 던진다. node 는 동일하게 startup validation 에서 거부한다(빈 이름, interval ≤ 0,
> 중복 source 이름).

socket, registry, spot 은 각각 framework 가 소유한 event kind enum 과 payload 를
가진다. backend 의 raw monitor enum 이나 status 값이 필요하면, event 안의 optional
diagnostic detail 로만 노출한다([backend-dependency-policy §6](../../../../node/internals/backend-dependency-policy.ko.md)).

이 "optional diagnostic" 도 framework 가 소유한 타입으로 다시 감싼다. 즉 Node 바인딩의
raw monitor 타입을 framework 의 public surface 에 직접 노출하지 않는다.

`socket[].events` 를 생략하면, 그 source 에서 지원하는 모든 logical event 를 받는다는
뜻으로 해석한다.

### 4.2 socket event 타입

```ts
export enum ZLinkSocketEventKind {
  Connected = 0,
  ConnectionReady = 1,
  Disconnected = 2,
  HandshakeFailed = 3,
  PeerAdmissionChanged = 4,
  Closed = 5,
  Internal = 6,
}

/** backend raw monitor event(optional diagnostic detail 전용). */
export enum ZLinkSocketNativeEventType {
  Connected = 0x0001,
  ConnectDelayed = 0x0002,
  ConnectRetried = 0x0004,
  Listening = 0x0008,
  BindFailed = 0x0010,
  Accepted = 0x0020,
  AcceptFailed = 0x0040,
  Closed = 0x0080,
  CloseFailed = 0x0100,
  Disconnected = 0x0200,
  MonitorStopped = 0x0400,
  HandshakeFailedNoDetail = 0x0800,
  ConnectionReady = 0x1000,
  HandshakeFailedProtocol = 0x2000,
  HandshakeFailedAuth = 0x4000,
  PeerAdmissionChanged = 0x8000,
}

export interface ZLinkSocketDiagnostic {
  readonly nativeEvent: ZLinkSocketNativeEventType;
  readonly nativeValue: number; // .NET uint
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSocketEventKind;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
  readonly diagnostic?: ZLinkSocketDiagnostic;
}
```

### 4.3 registry event 타입

```ts
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2,
}

  readonly status?: ZLinkRegistryStatus;
  readonly topology?: readonly ZLinkRegistryTopologyEntry[];
  readonly serviceSummary?: readonly ZLinkRegistryServiceSummaryEntry[];
}
```

`ZLinkRegistryStatus`, `ZLinkRegistryTopologyEntry`,
`ZLinkRegistryServiceSummaryEntry` snapshot DTO 의 정식 필드는
[nestjs-registry](nestjs-registry.ko.md) 가 소유한다(`.NET`
`Contracts/Registry/Models.cs` 대응). 핵심 필드는 다음과 같다.

- `ZLinkRegistryStatus`: `registryId`, `bindEndpoint`, `state`(`ZLinkRegistryState`),
  `topologyEntryCount`, `peerRegistryCount`, `connectedPeerRegistryCount`,
  `listSeq`, `lastError`, `lastChangedMs`.
- `ZLinkRegistryTopologyEntry`: `autoConnectType`, `routingId?`, `serviceKind`,
  `serviceRole`, `channelName`, `endpoint`, `source`, `state`, `desiredCount`,
  `readyCount`, `errorCode`, `lastReportedMs`, `spotKind`.
- `ZLinkRegistryServiceSummaryEntry`: `autoConnectType`, `serviceRole`,
  `channelName`, `totalCount`, `connectingCount`, `readyCount`, `errorCount`,
  `stoppedCount`, `lastReportedMs`.

### 4.4 spot event 타입

```ts
export enum ZLinkSpotEventKind {
  StatusChanged = 0,
  PeersChanged = 1,
  SubjectsChanged = 2,
  TimerHandlerFailed = 3,
  TimerStoppedAfterUnhandledException = 4,
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotRid: RoutingId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint; // .NET ulong
  readonly scheduledIndex: bigint; // .NET ulong
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSpotEventKind;
  readonly status?: ZLinkSpotNodeStatus;
  readonly peers?: readonly ZLinkSpotNodePeerEntry[];
  readonly subjects?: readonly ZLinkSpotNodeSubjectEntry[];
  readonly timerDiagnostic?: ZLinkSpotTimerDiagnostic;
}
```

`ZLinkSpotNodeStatus`, `ZLinkSpotNodePeerEntry`, `ZLinkSpotNodeSubjectEntry`
snapshot DTO 의 정식 필드는 [nestjs-spot](nestjs-spot.ko.md) 가 소유한다(`.NET`
`Contracts/Spots/MonitoringModels.cs` 대응). 핵심 필드는 다음과 같다.

- `ZLinkSpotNodeStatus`: `channelName`, `localEndpoint`, `nodeRoutingId?`,
  `state`(`ZLinkSpotNodeState`), `configuredPeerCount`, `activePeerCount`,
  `connectedPeerCount`, `subjectCount`, `readySubjectCount`, `lastError`,
  `lastChangedMs`.
- `ZLinkSpotNodePeerEntry`: `channelName`, `localEndpoint`, `peerEndpoint`,
  `source`(`ZLinkSpotPeerSource`), `kind`(`ZLinkSpotPeerKind`),
  `state`(`ZLinkSpotPeerState`), `weight`, `connectedSinceMs`, `lastChangedMs`.
- `ZLinkSpotNodeSubjectEntry`: `role`(`ZLinkSpotRole`), `subject`,
  `subjectKind`(`ZLinkSubjectKind`), `readyPeerCount`, `activePeerCount`,
  `lastChangedMs`.

`ZLinkSpotNodeStatus` 와 `ZLinkSpotNodePeerEntry` 의 첫 필드는 `channelName` 이다.
spot node 에서 채널 이름은 `.addSpotMesh(...)` 로 등록한 node 이름(예: `"stage-node"`)을
기준으로 들어간다.

## 5. 샘플 코드

handler 는 NestJS provider 로 등록하고 `@zlinkRuntimeEventHandler()` 로 runtime event
handler 임을 표시한다. class 는 `ZLinkRuntimeEventHandler<TEvent>` 를 구현한다.
framework 는 NestJS `DiscoveryService` 로 표시된 provider 를 찾아 runtime publisher 에
등록한다.

### 5.1 socket 이벤트

```ts
@zlinkRuntimeEventHandler()
export class ProfileServerSocketMonitor
  implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  constructor(private readonly logger: Logger) {}

  async handle(event: ZLinkSocketEvent): Promise<void> {
    switch (event.event) {
      case ZLinkSocketEventKind.ConnectionReady:
        this.logger.log(
          `socket ready: ${event.sourceName} ${event.remoteAddr}`,
        );
        break;

      case ZLinkSocketEventKind.Disconnected:
        this.logger.warn(
          `socket disconnected: ${event.sourceName} ${event.remoteAddr} ` +
            `value=${event.diagnostic?.nativeValue}`,
        );
        break;
    }
  }
}
```

### 5.2 registry 이벤트

```ts
@zlinkRuntimeEventHandler()
export class RegistryMonitor
  constructor(private readonly logger: Logger) {}

    switch (event.event) {
        this.logger.log(`registry status changed: ${event.status?.state}`);
        break;

        this.logger.log(
          `registry topology changed: ${event.topology?.length ?? 0}`,
        );
        break;
    }
  }
}
```

registry 는 하부에 raw monitor 가 없다. 그래서 framework 가 주기적으로 snapshot 을
읽고, 직전 값과 비교하는 방식으로 event 를 합성한다.

### 5.3 spot 이벤트

```ts
@zlinkRuntimeEventHandler()
export class StageNodeMonitor
  implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  constructor(private readonly logger: Logger) {}

  async handle(event: ZLinkSpotEvent): Promise<void> {
    switch (event.event) {
      case ZLinkSpotEventKind.PeersChanged:
        this.logger.log(
          `spot peers changed: ${event.sourceName} peers=${event.peers?.length ?? 0}`,
        );
        break;

      case ZLinkSpotEventKind.SubjectsChanged:
        this.logger.log(
          `spot subjects changed: ${event.sourceName} ` +
            `subjects=${event.subjects?.length ?? 0}`,
        );
        break;

      case ZLinkSpotEventKind.TimerHandlerFailed:
      case ZLinkSpotEventKind.TimerStoppedAfterUnhandledException:
        this.logger.error(
          `spot timer failed: ${event.sourceName} ` +
            `${event.timerDiagnostic?.timerName} ` +
            `${event.timerDiagnostic?.handlerType} ` +
            `${event.timerDiagnostic?.exceptionType}`,
        );
        break;
    }
  }
}
```

spot 도 registry 와 같은 이유로, raw monitor 보다 snapshot diff 표면이 더 잘 맞는다.
`status()`, `peers()`, `subjects()` 를 주기적으로 읽고, 변화가 있을 때 typed event
로 올리는 방향을 기본으로 본다.

timer handler failure 는 snapshot diff 가 아니다. `TimerHandlerFailed` 와
`TimerStoppedAfterUnhandledException` 은 timer callback 에서 처리되지 않은 예외가
발생한 시점에 즉시 발행된다. `spot[].intervalMs` 는 status / peer / subject snapshot
diff 에만 적용하고, timer failure event 를 지연시키지 않는다.

timer failure event 의 세부 정보는 `ZLinkSpotTimerDiagnostic` 에 담는다. 이 payload
에는 `spotRid`, Entry Spot 여부, timer 이름, handler 타입, callback 번호(`deliveryIndex`),
fixed-rate 시간표의 tick 번호(`scheduledIndex`), exception 타입과 메시지가 들어간다.
exception 객체 자체는 public event payload 로 노출하지 않는다.

framework 내부에서 위 두 snapshot 을 묶는 `ZLinkSpotMonitoringSnapshot` 은 internal
타입이다. 따라서 application 코드에서 직접 다루지 않는다.

### 5.4 discovery 상태 조회

discovery 는 runtime event 가 아니다. 현재 provider 상태가 필요하면 registry query
client 를 주입해서 직접 조회한다.

```ts
@Injectable()
export class DiscoveryStatusProbe {
  constructor(
    @Inject(ZLINK_REGISTRY_QUERY)
  ) {}

  async probe(): Promise<void> {
    const topology = await this.registry.topology({ channelName: 'profile' });
    const summary = await this.registry.serviceSummary({ channelName: 'profile' });
    // topology / summary 로 현재 provider up/down 을 판단한다.
  }
}
```

## 6. 왜 raw monitor 를 그대로 노출하지 않는가

이 절은 source 별로 표면을 따로 둔 이유를 정리한다.

하나의 API 로 네 source 를 전부 덮으려면, 결국 가장 낮은 수준의 모양으로 내려가야
한다. 그러면 registry 와 spot 은 실제 하부 표면이 가진 능력보다 더 많은 것을 약속하게
된다.

따라서 현재 스펙은 다음과 같이 source 를 나누는 편을 기본으로 본다.

- socket
  - raw monitor 기반
- registry/spot 상태
  - snapshot diff 기반
- spot timer failure
  - timer loop 에서 즉시 발행하는 point-in-time event
- discovery
  - registry snapshot/query 기반 조회
- application
  - typed runtime event handler 기반

이렇게 구분해 두어야 framework 가 source 별 구현 차이를 숨기면서도, 없는 기능을 있는
것처럼 보이지 않게 할 수 있다.

## 7. 결정된 기준

이 절은 monitoring 표면이 따르는 고정된 결정 사항을 모아둔 것이다.

- registry / spot polling 주기(`intervalMs`)는 monitoring 등록 시점에 항상 명시한다.
  숨은 기본 주기를 두지 않는다. 운영 코드가 polling cost 를 설정에서 바로 읽을 수
  있게 하는 편을 기본으로 본다.
- registry event 종류는 `StatusChanged`, `TopologyChanged`, `ServiceSummaryChanged`
  세 가지로 고정한다.
- spot event 종류는 `StatusChanged`, `PeersChanged`, `SubjectsChanged`,
  `TimerHandlerFailed`, `TimerStoppedAfterUnhandledException` 다.
- socket event payload 는 raw native enum(`nativeEvent`)과 상태 코드(`nativeValue`)를
  optional `diagnostic` 으로 함께 노출한다. 반면 registry event 와 spot 상태 event 는
  snapshot diff 기반의 합성 event 다. timer failure event 는 framework timer loop 에서
  즉시 만든다. discovery 는 runtime event 자체가 아니므로 별도 event payload 를 두지
  않는다.
- 등록되지 않은 메시지와 dispatch 실패는 monitoring source 가 아니라
  `configureDispatch().setMessageFlowObserver(...)` 로 등록한 전역 observer 가 받는다.
  request 실패는 error reply 로 돌아가고 one-way 실패는 drop 되지만 로그, counter, observer event 로
  남는다. observer 실패는 runtime error sink 로 분리한다.

## 8. 회귀 테스트

이 절은 monitoring 표면이 어떤 테스트로 회귀를 막는지를 정리한다. node 테스트는 `.NET`
회귀 항목과 동일한 동작을 검증한다([regression-test-matrix](../../../../node/internals/regression-test-matrix.ko.md)).

Monitoring 문서의 항목은 다음을 확인한다.

- 등록한 source 이름이 실제 runtime 역할과 맞는지
- Registry 와 SPOT 상태 변화가 typed event 와 snapshot 으로 관찰되는지
- timer handler failure 가 polling interval 을 기다리지 않고 typed event 로 관찰되는지
- raw monitor event 를 그대로 외부로 새어 보내지 않는다는 정책이 public surface
  테스트에서도 유지되는지

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistryAndMonitoring.throws_whenSocketSourceDoesNotMatchRegisteredCapability` | 존재하지 않는 monitoring source 이름은 startup validation 예외로 이어진다. |
| `Events.registryMonitoring_emits_statusChanged_forEmbeddedRegistry` | embedded Registry 의 상태 변경 event 가 발생한다. |
| `Events.registryMonitoring_emits_topologyAndServiceSummary_whenFrameworkHostRegisters` | framework host 등록 후 topology 와 service summary event 가 발생한다. |
| `Events.spotMonitoring_emits_subjectsChanged_whenSpotIsCreated` | spot 생성 후 subject 변화 event 가 발생한다. |
| `Events.spotMonitoring_emits_peersChanged_whenRemoteNodeAppears` | remote spot node 가 나타나면 peer 변화 event 가 발생한다. |
| `Timer.spotTimer_reports_handlerException_toMonitoring` | timer handler 예외가 `TimerHandlerFailed` event 와 `ZLinkSpotTimerDiagnostic` payload 로 발생한다. |

## 9. 메시지 흐름 추적 (dispatch 관측)

monitoring 이 socket/registry/spot **runtime 변화**를 다룬다면, 메시지 흐름 추적은 한 메시지의
생애주기(왔나/처리됐나/응답됐나/보냈나/응답받았나)를 dispatch 길목에서 관측한다. 공통 의미는
[공통 스펙 — 메시지 흐름 추적](../../message-flow-tracing.ko.md)이 소유하고, 이 절은
Node/TypeScript 표면만 적는다. dispatch 제어가 아니라 관측이며, observer 실패가 처리/응답을
깨지 않는다.

### 9.1 표면

| 공통 개념 | Node 타입 / 멤버 |
|-----------|------------------|
| 로그 모드 | `ZLinkMessageFlowLogMode` { `Off`, `ErrorsOnly`(기본), `KeyTransitions`, `Verbose`, `Diagnostic` } |
| outcome | `ZLinkMessageFlowOutcome` { `Received`, `Dispatched`, `Replied`, `Dropped`, `Sent`, `ReplyReceived` } |
| event | `ZLinkMessageFlowEvent`: `phase`, `surface`, `messageKind`, `packetName?`, `channelName?`, `topic?`, `correlationId?`, `sourceRid?`, `spotRid?`, `actorId?`, `messageSize?` |
| observer | `ZLinkMessageFlowObserver.onMessageFlow(flow): Promise<void> \| void` |
| 진단 옵션 | `ZLinkDiagnosticsOptions` { `messageFlowLogMode?`, `sampleRate?`, `includeMessageSizes?`, `logFile?`, `label?` } |
| 런타임 토글 | host `ZLinkMessageFlowControl.setMessageFlowMode(mode)` / `messageFlowMode()` |

게이팅(공통 규칙): `Dropped`·에러는 `ErrorsOnly` 이상, 성공 전이는 `KeyTransitions` 이상에서
발화한다. `sampleRate<1`은 성공 전이만 thinning하고 `Dropped`·에러는 항상 통과한다.

### 9.2 설정 (builder 전용)

core(`zlinkFramework()`)와 NestJS 양쪽에서 같은 `configureDispatch()` 체인을 쓴다.

```ts
const builder = zlinkFramework();
builder.configureDispatch()
  .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
  .traceLogFile(`${process.env.BINGO_LOG_DIR ?? 'logs'}/flow-api.log`)  // 지정=전용 파일
  .traceLabel('api')                       // 구조화 필드 label=
  .includeMessageSizes(true)                // Verbose에서 size=
  .setMessageFlowObserver(ApiFlowObserver); // 선택: 콜렉터/OTel 어댑터(앱 레이어)
```

- `traceLogFile` 지정 시 트레이싱/에러는 전용 파일로만(`writeTraceFile`), 미지정이면 `console.error`
  폴백. 출력은 구조화 필드 + `label=`로 콜렉터 ingest 가능.
- 트레이서는 `enabled(outcome)` 가드 + `flowIfEnabled(reporter?.flow, outcome)?.trace(...)` 패턴으로
  `Off`일 때 이벤트 객체를 만들지 않아(제로-alloc) 운영 성능에 영향이 없다.
- observer는 `setMessageFlowObserver(ObserverType)`(클래스/`Type<...>`)로 등록한다. OTel 어댑터는
  앱 레이어 책임이다(공통 스펙 §6).

### 9.3 런타임 토글

host가 공유 live cell(`messageFlowModeCell`)을 모든 surface에 전달하므로, host의
`setMessageFlowMode(...)`로 재시작 없이 즉시 모드를 바꾼다. `messageFlow(...)`는 seed(기본값)다.

### 9.4 샘플

Bingo.Ts 3노드(Api/Play/Session)는 각자 `messageFlow(KeyTransitions)` +
`traceLogFile(.../flow-<role>.log)` + `traceLabel(role)`로 분리 파일 로깅을 시연한다
(`BINGO_LOG_DIR` override). 한 요청을 `corr=`로 grep하면 노드 간 흐름이 이어진다.

## 10. 런타임 메트릭 (runtime metrics)

공통 의미는 [공통 스펙 — 런타임 메트릭](../../runtime-metrics.ko.md)이 소유한다. 이 절은 Node.js 표면만
적는다.

> **설계 원칙(깊은 모듈): 공통 케이스는 무설정.** framework는 안정된 이름의 OpenTelemetry `Meter`로
> 카탈로그 계기를 방출한다. 앱은 전역 `MeterProvider`만 구성하면 되고 계기를 하나도 선언하지 않는다.

### 10.1 표면

| 공통 개념 | Node.js |
|-----------|---------|
| meter 이름(상수) | `ZLinkMeters.Framework` = `'zlink.framework'` |
| 계기 방출 | OpenTelemetry Metrics API `Meter` — `Counter`/`UpDownCounter`/`ObservableGauge`/`Histogram` |
| 앱 연결(공통 케이스) | 전역 OTel `MeterProvider`(SDK) 구성 — 별도 zlink 설정 없음 |
| 커스텀(선택) | `ZLinkModule.forRoot(zlinkFramework().options({ metrics: { meterProvider } }).build())`로 provider 주입 |

- 공통 §3 매핑: `updown`=`UpDownCounter`, `observable`=`ObservableGauge`(관찰 콜백), histogram=`Histogram`(`s`).
- MeterProvider가 no-op이면 계기 갱신 비용만 남고 export는 0(공통 §7.2). exporter·대시보드는 앱 몫.

## 11. 메시지 흐름 상관관계 (flow correlation)

공통 의미는 [공통 스펙 — 메시지 흐름 상관관계](../../flow-correlation.ko.md)가 소유한다. §9(메시지
흐름 추적)의 additive 확장이며 새 최상위 표면을 만들지 않는다.

### 11.1 표면

| 공통 개념 | Node.js |
|-----------|---------|
| 생성 게이트 | 기존 `configureDispatch().messageFlow(...)` 설정을 그대로 사용한다. 별도 flow id 설정은 없다. |
| event 필드(추가) | `readonly flowId: string`, `readonly flowOrigin: ZLinkFlowOrigin` — 오류 이벤트에도 동일한 root 값 |

- 생성은 모드 게이트, 전파는 무조건(공통 §2.2). stream/actor gateway 로거 자동 배선(공통 §7),
  게이팅 불변(`Off`면 완전 침묵). 로그 토큰 `flow=`는 언어 간 바이트 동일.

## 12. Graceful Drain & Handoff

공통 의미는 [공통 스펙 — Graceful Drain & Handoff](../../graceful-drain-handoff.ko.md)가 소유한다.
lifecycle 제어 표면(관측 아님)의 Node.js 투영이다.

> **설계 원칙(복잡도 하향): 공통 케이스는 무설정.** framework가 NestJS `onApplicationShutdown`
> (`enableShutdownHooks()`)에 자동 참여해 drain한다. 앱은 코드를 쓰지 않는다.

### 12.1 표면

```ts
export type ZLinkFlowOrigin = 'Inbound' | 'Timer' | 'Application' | 'Lifecycle';
export type ZLinkSpotDrainPolicy = 'DrainNatural' | 'ReleaseAndRecreate';
export type ZLinkDrainForceReason =
  | 'DeadlineExceeded' | 'DrainingStatePublishFailed'
  | 'OwnerCleanupFailed' | 'TeardownFailed';
export type ZLinkDrainResult =
  | { readonly kind: 'drained' }
  | { readonly kind: 'force-stopped'; readonly reason: ZLinkDrainForceReason };
export interface ZLinkDrainControl {
  drain(deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkDrainResult>;
  awaitDrained(signal?: AbortSignal): Promise<ZLinkDrainResult>;
  isReady(): boolean;
}
```

| 공통 개념 | Node.js |
|-----------|---------|
| 자동 drain(기본) | framework가 `onApplicationShutdown(signal)`에서 drain — 앱 코드 0 |
| SPOT drain 정책 | spot mesh 등록의 `useDrainPolicy('ReleaseAndRecreate')`(기본 `'DrainNatural'`) |
| 명시 제어(선택) | `ZLinkDrainControl` { `drain(deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkDrainResult>`, `awaitDrained(signal?: AbortSignal): Promise<ZLinkDrainResult>`, `isReady(): boolean` } (기본 30,000ms, injectable) |
| 종료 결과 | `ZLinkDrainResult` = `{ kind: 'drained' } | { kind: 'force-stopped'; reason: 'DeadlineExceeded' | 'DrainingStatePublishFailed' | 'OwnerCleanupFailed' | 'TeardownFailed' }` |
| readiness probe | framework가 NestJS Terminus `ZLinkDrainHealthIndicator`를 제공, health controller에 등록. 또는 `ZLinkDrainControl.isReady()` 직접 조회 |
| 상태 관측 | 기존 `ZLinkRuntimeEventHandler<ZLinkDrainEvent>` 재사용. `ZLinkDrainEvent.state` { `Serving`/`Draining`/`Drained`/`ForceStopping` }, `sourceName` = 고정값 `'drain'` |

- 비동기 반환에 `Async` 접미사를 쓰지 않는 이 코드베이스 관례(`handle(): Promise`)에 맞춰 `drain`으로 둔다.
- 여러 호출자가 동시에 drain을 요청하면 하나의 drain에 참여하고 같은 종료 결과를 받는다. 개별
  `AbortSignal` 취소는 해당 대기만 중단하며, 이미 시작된 drain을 취소하지 않는다.
- drain 상태 관측은 monitoring의 `ZLinkRuntimeEventHandler<T>`를 그대로 쓴다(같은 개념 → 같은
  메커니즘). **drain 이벤트는 source 등록이 필요 없다** — 저빈도 lifecycle 이벤트라 handler provider
  존재만으로 수신한다(공통 §9, 조용한 무관측 없음).

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake 는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^discovery]: discovery 는 분산 환경에서 어떤 서비스가 어느 endpoint 에 있는지를 자동으로 알아내는 메커니즘이다. ZLink 에서는 registry 가 그 역할을 한다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node 는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor 는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff 는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event 를 합성하는 방식이다.
[^polling]: polling 은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event 가 없을 때 사용한다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework NestJS Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ZLink Framework NestJS Registry](nestjs-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
