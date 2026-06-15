<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework NestJS Session Actor Dispatch](./session-actor-dispatch.ko.md) | [다음: ZLink Framework NestJS Registry](./nestjs-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node.js 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [SPOT](./nestjs-spot.ko.md) | [STREAM](./nestjs-stream.ko.md) | [Registry](./nestjs-registry.ko.md)

# ZLink Framework NestJS Monitoring

> 이 문서는 [.NET ASP.NET Core Monitoring 스펙](../../../dotnet/doc/spec/aspnet-core-monitoring.ko.md)
> 을 NestJS 표면으로 옮긴 정식 스펙이다. **개념·의미론·동작은 .NET 과 동일**하고,
> 표면만 TypeScript / NestJS 모양으로 바꾼다. 번역 규칙은
> [.NET → Node.js 표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md)
> 을 따른다. 이 문서대로 구현하면 .NET 과 동일한 monitoring 동작을 얻는다.

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
- 실제 callback payload 는 불변 `interface` 로 둔다(.NET 의 `record struct` 대응).
- socket 은 하부 monitor 를 그대로 감싼다.
- registry / spot 은 polling[^polling] 과 snapshot diff 로 event 를 합성한다.
- timer handler failure 는 polling interval 을 기다리지 않고 즉시 발행한다.
- discovery 상태는 registry snapshot / query 결과로 조회한다.
- application 은 `ZLinkRuntimeEventHandler<TEvent>` 를 구현해서 이벤트를 받는다.

enum 하나만으로는 충분하지 않다. 운영 코드에서는 event 종류뿐 아니라 source 이름,
endpoint, routing id, snapshot 본문도 함께 필요하기 때문이다.

`.NET` 과의 대응은 다음과 같다.

| .NET | node |
|------|------|
| `IZLinkMonitoringOptions` | `ZLinkMonitoringOptions` |
| `IZLinkRuntimeEvent` | `ZLinkRuntimeEvent` |
| `IZLinkRuntimeEventHandler<TEvent>` | `ZLinkRuntimeEventHandler<TEvent>` |
| `ValueTask HandleAsync(e, ct)` | `handle(event): Promise<void>` |
| `readonly record struct` payload | `interface` payload |
| `enum` event kind | `enum` event kind |
| `TimeSpan interval` | `intervalMs: number` |
| `RoutingId` | `RoutingId`(branded `string`) |
| `DateTimeOffset Timestamp` | `timestamp: Date` |

## 3. 등록 모델

framework 등록은 module options 의 `monitoring` 키로 둔다. `.NET` 의
`AddZLinkMonitoring(monitor => ...)` 람다는 선언적 options 객체로 옮긴다.

```ts
@Module({
  imports: [
    ZLinkModule.forRoot({
      discovery: {
        registries: ['tcp://registry-1:5551'],
      },
      clientServerChannels: {
        profile: {
          server: { bind: 'tcp://0.0.0.0:7101' },
          client: {},
        },
      },
      spotNodes: {
        'stage-node': {
          pubSub: { bind: 'tcp://0.0.0.0:9000' },
        },
      },
      },
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
      providers: [
        ProfileServerSocketMonitor,
        RegistryMonitor,
        StageNodeMonitor,
      ],
    }),
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
같은 애플리케이션의 `clientServerChannels` / `fanoutChannels` / `routerMeshes` /
`spotNodes` / `discovery`(또는 별도 registry
module) 로 이미 올라와 있어야 한다.

> `.NET` 의 `AddZLinkMonitoring(...)` 은 `AddZLinkFramework(...)` 와 분리된 두 번째
> 등록 호출이지만, NestJS 에서는 동일 `ZLinkModule.forRoot(...)` options 안의
> `monitoring` 키로 합친다. 분리가 꼭 필요하면 `forRootFactory` 로 monitoring 만 다른
> 모듈에서 주입하는 방식도 허용한다. 의미는 동일하다 — source 등록은 framework 등록과
> 같은 application 에 있어야 한다.

여기서 한 가지 짚어 둘 점이 있다. 일반 channel capability[^capability] 와 SPOT
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
    `memberPeers(...)`) 로 조회한다. ([nestjs-registry](./nestjs-registry.ko.md))
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

> **드래프트와의 차이**: 기존 node 드래프트의 `monitoring.discovery: [...]` 등록
> 키는 제거한다. `.NET` 코드(`IZLinkMonitoringOptions`)에 discovery 등록 메서드가
> 없고, discovery 는 runtime event 가 아니라 registry query 로만 관측되기 때문이다
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
> `AddRegistryEvents(sourceName, interval)` / `AddSpotEvents(sourceName, interval)`
> 세 메서드는 위 `socket` / `registry` / `spot` 배열 키에 1:1 대응한다.
> 내부적으로 .NET 은 source 이름이 비어 있거나 중복이면 `ZLinkConfigurationException`
> 을 던진다. node 는 동일하게 startup validation 에서 거부한다(빈 이름, interval ≤ 0,
> 중복 source 이름).

socket, registry, spot 은 각각 framework 가 소유한 event kind enum 과 payload 를
가진다. backend 의 raw monitor enum 이나 status 값이 필요하면, event 안의 optional
diagnostic detail 로만 노출한다([backend-dependency-policy §6](../internals/backend-dependency-policy.ko.md)).

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
export enum ZLinkRegistryEventKind {
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2,
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkRegistryEventKind;
  readonly status?: ZLinkRegistryStatus;
  readonly topology?: readonly ZLinkRegistryTopologyEntry[];
  readonly serviceSummary?: readonly ZLinkRegistryServiceSummaryEntry[];
}
```

`ZLinkRegistryStatus`, `ZLinkRegistryTopologyEntry`,
`ZLinkRegistryServiceSummaryEntry` snapshot DTO 의 정식 필드는
[nestjs-registry](./nestjs-registry.ko.md) 가 소유한다(`.NET`
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
snapshot DTO 의 정식 필드는 [nestjs-spot](./nestjs-spot.ko.md) 가 소유한다(`.NET`
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
spot node 에서 채널 이름은 `spotNodes` 에 등록한 node 이름(예: `"stage-node"`)을
기준으로 들어간다.

## 5. 샘플 코드

handler 는 NestJS provider 로 등록하고, 구현한 event 타입으로 발견한다(`@Injectable()`
+ `ZLinkRuntimeEventHandler<TEvent>` 구현). framework 는 NestJS `DiscoveryService` 로
provider 를 훑어 event 타입별로 라우팅한다.

### 5.1 socket 이벤트

```ts
@Injectable()
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
@Injectable()
export class RegistryMonitor
  implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
  constructor(private readonly logger: Logger) {}

  async handle(event: ZLinkRegistryEvent): Promise<void> {
    switch (event.event) {
      case ZLinkRegistryEventKind.StatusChanged:
        this.logger.log(`registry status changed: ${event.status?.state}`);
        break;

      case ZLinkRegistryEventKind.TopologyChanged:
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
@Injectable()
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
즉 `status()`, `peers()`, `subjects()` 를 주기적으로 읽고, 변화가 있을 때 typed event
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
    private readonly registry: ZLinkRegistryQuery,
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

## 8. 회귀 테스트

이 절은 monitoring 표면이 어떤 테스트로 회귀를 막는지를 정리한다. node 테스트는 `.NET`
회귀 항목과 동일한 동작을 검증한다([regression-test-matrix](../internals/regression-test-matrix.ko.md)).

Monitoring 문서의 항목은 다음을 확인한다.

- 등록한 source 이름이 실제 runtime capability 와 맞는지
- Registry 와 SPOT 상태 변화가 typed event 와 snapshot 으로 관찰되는지
- timer handler failure 가 polling interval 을 기다리지 않고 typed event 로 관찰되는지
- raw monitor event 를 그대로 외부로 새어 보내지 않는다는 정책이 public surface
  테스트에서도 유지되는지

| 테스트 케이스(.NET 대응) | 확인 기준 |
|---------------|-----------|
| `RegistryAndMonitoring.throws_whenSocketSourceDoesNotMatchRegisteredCapability` | 존재하지 않는 monitoring source 이름은 startup validation 예외로 이어진다. |
| `Events.registryMonitoring_emits_statusChanged_forEmbeddedRegistry` | embedded Registry 의 상태 변경 event 가 발생한다. |
| `Events.registryMonitoring_emits_topologyAndServiceSummary_whenFrameworkHostRegisters` | framework host 등록 후 topology 와 service summary event 가 발생한다. |
| `Events.spotMonitoring_emits_subjectsChanged_whenSpotIsCreated` | spot 생성 후 subject 변화 event 가 발생한다. |
| `Events.spotMonitoring_emits_peersChanged_whenRemoteNodeAppears` | remote spot node 가 나타나면 peer 변화 event 가 발생한다. |
| `Timer.spotTimer_reports_handlerException_toMonitoring` | timer handler 예외가 `TimerHandlerFailed` event 와 `ZLinkSpotTimerDiagnostic` payload 로 발생한다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^handshake]: handshake 는 연결 초기에 양쪽이 프로토콜 버전이나 인증 정보를 주고받아 통신 조건을 맞추는 절차다.
[^discovery]: discovery 는 분산 환경에서 어떤 서비스가 어느 endpoint 에 있는지를 자동으로 알아내는 메커니즘이다. ZLink 에서는 registry 가 그 역할을 한다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^spot-node]: spot node 는 여러 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^raw-monitor]: raw monitor 는 하부 socket 계층에서 직접 발생하는 저수준 이벤트(연결 성공, 끊김 등)를 그대로 수신하는 메커니즘이다.
[^snapshot-diff]: snapshot diff 는 일정 주기로 상태 스냅샷을 읽고, 이전 스냅샷과 비교해서 차이가 있을 때만 event 를 합성하는 방식이다.
[^polling]: polling 은 주기적으로 상태를 직접 조회해서 변화를 감지하는 방식이다. push 기반 event 가 없을 때 사용한다.
[^capability]: capability 는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, client, subscriber, publisher)를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework NestJS Session Actor Dispatch](./session-actor-dispatch.ko.md) | [다음: ZLink Framework NestJS Registry](./nestjs-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
