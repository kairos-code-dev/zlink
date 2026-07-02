<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [표면 매핑](dotnet-to-java-surface-mapping.ko.md)
<!-- framework-adapter-nav:end -->

# Java Spring DI Capability Exposure Policy

## 1. 결정 요약

| 결정 | 내용 |
|------|------|
| channel client | multi-target client라 항상 bean으로 등록 |
| fanout client | multi-target publisher라 항상 bean으로 등록 |
| route client | route channel id를 호출 시 받으므로 항상 bean으로 등록 |
| Bound session | actor context의 `boundSession()`으로 노출(별도 factory bean 없음) |
| Spot manager | SpotNode가 있을 때만 등록 |
| Spot outbound | SpotNode가 있을 때만 등록 |
| Spot publisher client | attached Spot publisher client 역할이 있을 때만 등록 |
| Actor manager | SpotNode와 actor factory가 모두 있을 때만 등록 |
| Runtime event dispatcher | core runtime이 항상 등록(`ZLinkRuntimeEventDispatcher`) |
| Monitoring lifecycle | monitoring configurer가 있을 때만 등록(polling/attach 구동체). dispatcher에 직접 publish |
| Registry query | embedded registry가 있을 때 등록 |
| Registry query client | query client configurer가 있을 때 등록 |

## 2. 설계 원칙

사용자가 생성자에서 어떤 bean을 주입받으면 그 기능을 쓸 수 있다고 이해한다. 따라서
역할이 없는 service를 빈 proxy로 등록하지 않는다.

예외는 channel, fanout, route처럼 target 이름을 호출 시점에 받는 multi-target
client다. 이 client들은 bean으로 등록할 수 있지만, 없는 channel이나 역할은
호출 시 `ZLinkConfigurationException`으로 실패해야 한다.

## 3. 조건부 bean 표

| Bean type | 등록 조건 | 없을 때 |
|-----------|-----------|---------|
| `ZLinkClient` | framework runtime(항상) | 호출 시 target validation(`ZLinkConfigurationException`) |
| `ZLinkFanoutClient` | framework runtime(항상) | 호출 시 publisher validation(`ZLinkConfigurationException`) |
| `ZLinkRouteClient` | framework runtime(항상) | 호출 시 route channel validation(`ZLinkConfigurationException`) |
| `ZLinkActorContext.boundSession()` | framework runtime(항상) | actor에 현재 session binding이 없으면 호출 시 `ZLinkConfigurationException`("actor has no bound session") |
| `ZLinkSpotManager` | 최소 1개 SpotNode | Spring DI resolve 실패 |
| `ZLinkSpotOutbound` | 최소 1개 SpotNode | Spring DI resolve 실패 |
| `ZLinkSpotPublisherClient` | attached Spot publisher client 역할 | Spring DI resolve 실패 |
| `ZLinkActorManager` | SpotNode + actor factory | Spring DI resolve 실패 |

bound session은 별도 factory bean이 아니라 `ZLinkActorContext.boundSession()`으로 노출된다.
actor runtime state에 저장된 현재 session rid와 binding token을 쓰며, binding이 없는
actor에서 호출하면 `ZLinkConfigurationException`("actor has no bound session")로 실패한다.

### 3.1 monitoring 게이팅

monitoring runtime은 두 부분으로 나눈다.

- `ZLinkRuntimeEventDispatcher`는 **core runtime이 항상** 등록한다. dispatcher가 직접
  `register`/`publish`를 제공한다(별도 publisher 타입 없음).
- monitoring configurer는 그 위에 monitoring lifecycle(polling/attach 구동체)만 추가한다.
  monitoring runtime은 그 lifecycle에서 dispatcher에 직접 publish한다. dispatcher 자체를
  새로 만들지 않는다.

따라서 monitoring을 켜지 않아도 dispatcher bean은 존재하지만, polling/attach 구동체는
monitoring configurer가 있을 때만 붙는다.

## 4. Startup validation

- actor factory만 있고 SpotNode가 없으면 startup validation 오류다.
- Registry-backed Spot remote address resolver를 쓰는데 discovery가 없으면 startup
  validation 오류다.
- route mesh channel 없이 Registry-backed resolver를 쓰면 startup validation 오류다.
- monitoring source 이름이 실제 source와 맞지 않으면 startup validation 오류다.

## 5. 회귀 기준

DI 노출 회귀는 Java Spring Boot starter 테스트(`ZLinkFrameworkAutoConfigurationTest`)로
고정한다. 아래는 각 확인 기준에 대응하는 테스트다(메서드명은 실제 Java 테스트와 맞춘다).

| JUnit 테스트 (`ZLinkFrameworkAutoConfigurationTest`) | 확인 기준 |
|--------------------------------------------------------------|-----------|
| `addZLinkFramework_throws_whenActorFactoryWithoutSpotNode` | actor factory만 등록하면 startup validation 오류 |
| `addZLinkFramework_doesNotRegisterActorManager_withoutSpotNode` | SpotNode 없는 구성에서는 `ZLinkActorManager` bean이 없다 |
| `addZLinkFramework_doesNotRegisterActorManager_withSpotNodeOnly` | SpotNode만 있고 actor factory가 없으면 `ZLinkActorManager` bean이 없다 |
| `addZLinkFramework_registersActorManager_whenSpotNodeAndActorFactoryExist` | SpotNode와 actor factory가 있으면 `ZLinkActorManager`가 등록된다 |
| `addZLinkFramework_doesNotRegisterSpotServices_withoutSpotNode` | SpotNode 없는 구성에서는 Spot service가 없다 |
| `addZLinkFramework_registersSpotServices_whenSpotNodeExists` | SpotNode가 있으면 Spot service가 등록된다 |
| `addZLinkFramework_doesNotRegisterSpotPublisher_withoutPublisherCapability` | SpotNode가 있어도 publisher 역할이 없으면 Spot publisher service가 없다 |
| `addZLinkFramework_registersSpotPublisher_whenPublisherCapabilityExists` | attached Spot publisher 역할이 있으면 Spot publisher service가 등록된다 |
| `addZLinkFramework_registersBoundSessionFactory` | bound session factory는 framework runtime과 함께 항상 등록된다 |
| `addZLinkFramework_allowsSpotRemoteAddressResolver_withoutSpotNode` | remote address 정보만 제공하는 서버는 SpotNode 없이 resolver를 등록할 수 있다 |
| `addZLinkFramework_doesNotRegisterSpotOutbound_withResolverOnly` | resolver만 있고 SpotNode가 없으면 `ZLinkSpotOutbound`는 없다 |
| `routeClient_throwsConfigurationException_whenRouteChannelMissing` | route channel 누락 오류가 configuration error로 나온다 |
| `channelClient_throwsConfigurationException_whenClientCapabilityMissing` | channel client 역할 누락 오류가 configuration error로 나온다 |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [표면 매핑](dotnet-to-java-surface-mapping.ko.md)
<!-- framework-adapter-nav:bottom:end -->
