# Framework channel drain (peer weight) 운영 제어 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤
> 공통 spec과 언어별 정식 spec/guide/internals 문서에 나누어 반영한다.

## 목적

운영 중에 특정 노드의 한 channel serving 역할을 **새 요청 수신만 멈추도록** 전환할 수 있어야 한다.
이미 들어온(in-flight) 요청은 끝까지 처리하고, 그 시점 이후 peer 들은 그 노드로 **새 요청을 보내지
않는다.** 노드를 죽이거나 socket 을 닫지 않고, registry 에서 빼지도 않은 채로 트래픽만 흘려보내는
graceful drain 이다. 유지보수·롤링 재시작·scale-in 직전에 운영툴에서 채널 단위로 호출하는 것을
1차 사용 시나리오로 한다.

core 에는 이 동작이 이미 **peer weight** 로 존재한다(ROUTER `0x3106` / DEALER `0x3203`, 범위
`0..100`, 기본 `100`). serving socket 의 weight 를 `0` 으로 바꾸면 그 socket 은 계속 recv·dispatch·
reply 를 수행하지만 연결된 peer 들이 새 outbound 대상으로 더 이상 선택하지 않는다. peer 가 모두
weight `0` 이 되면 caller 의 submit 은 `ZLINK_SUBMIT_NOT_ADMITTED` 로 실패한다. reply 경로는 항상
허용된다. 이 계획은 그 core 능력을 **모든 framework 언어의 공통 운영 API** 로 끌어올린다.

이 기능은 dispatch 제어가 아니라 **운영 가용성 제어**다. drain 을 호출하지 않으면 framework 기본
동작은 변하지 않아야 하고(weight 기본 `100`), drain 중에도 in-flight 처리·reply 전송은 깨지면 안 된다.

## 적용 범위

적용 대상은 serving 역할이 ROUTER 또는 DEALER socket 으로 표현되는 channel 종류다. 즉 weight 개념이
있는 transport 만 대상이다.

| channel 종류 | 등록 표면 | drain 대상 socket | core 옵션 |
|--------------|-----------|-------------------|-----------|
| client-server | `EnableServer` | 로컬 ROUTER (bound) | `ROUTER_WEIGHT` |
| dealer mesh | `EnableServer` | 로컬 mesh DEALER (bound, handler 보유) | `DEALER_WEIGHT` |
| route mesh | `EnableServer` | 로컬 ROUTER (`RoutingId` 보유) | `ROUTER_WEIGHT` |

| 언어 | 적용 범위 |
|------|-----------|
| `.NET` | client-server, dealer mesh, route mesh serving 역할 |
| Java/Kotlin | Spring Boot client-server, dealer mesh, route mesh serving 역할 |
| Node/NestJS | client-server, dealer mesh, route mesh serving 역할 |
| C++ | client-server, dealer mesh, route mesh serving 역할 |

**범위 밖**:

- **fanout(PUB/SUB)** — PUB 에는 peer weight 개념이 없다. 구독 fan-out 의 부분 중단은 별도 기능이며
  이 계획에서 다루지 않는다.
- **client 전용 노드** — 그 노드는 요청을 받는 serving 역할이 없으므로 drain 대상이 아니다. 해당
  channel 에 serving 역할이 없는 노드에서 drain 을 호출하면 조용히 무시하지 않고 명확한 오류로 끝낸다.
- **SPOT actor / spot node serving socket** — spot node 는 route mesh transport 를 공유하지만 weight
  setter 가 native `ISpotNode` 표면에 아직 없다(`bindings` 조사 결과 ROUTER/DEALER 는 노출, spot node
  serving 역할은 미노출). spot node drain 은 후속 확장으로 분리하고, 이 계획은 위 세 channel 종류만
  공개 계약으로 고정한다.
- **운영툴 자체** — 외부 운영툴이 drain 을 트리거하는 transport·인증은 framework 책임이 아니다.
  framework 는 in-process DI 서비스만 제공하고, 애플리케이션이 자신의 admin 엔드포인트(예: ASP.NET
  minimal API, management command)에서 그 서비스를 호출한다. 샘플로 admin 엔드포인트 예시만 제공한다.

## 현재 상태 요약

이 절은 2026-06-21 checkout 기준이다.

| 영역 | 현재 상태 | 문제 |
|------|-----------|------|
| core peer weight | ROUTER/DEALER `WEIGHT` 옵션과 `PEER_WEIGHT_CHANGED`(`0x8000`) 모니터 이벤트가 완성되어 있다. weight `0` = drain, `1..99` = weighted, `100` = 기본. discovery 는 advertised weight 를 `0` 또는 `100` 으로 정규화해 auto-connect 후보 선택에 반영한다. | 없음. 이미 동작한다. |
| 모든 binding (`.NET`/Java/Node/Rust/Go/Python/C++) | live socket handle 의 `Options.PeerWeight`(또는 `set_weight`/`peerWeight`)로 **런타임에** weight 를 설정할 수 있다. `PEER_WEIGHT_CHANGED` 이벤트도 각 binding event enum 에 노출되어 있다. | 없음. **binding 신규 작업 불필요.** |
| framework channel builder | `EnableServer`/`EnableClient` 등 enable 전용이다. 실행 중 channel 을 가리키는 핸들이나 weight/availability setter 가 없다. | 운영 중 drain 을 호출할 공개 표면이 전무하다. |
| framework runtime state | `ZLinkFrameworkRuntimeState` 가 channel 이름 keyed dict(`ServerBundles`/`ClientBundles`/`RouteChannels`)로 live socket 을 들고 있고, `ZLinkChannelRuntimeManager.GetMonitoringSocket` 이 이미 `channel.capability` → live socket lookup 을 한다. | 이 lookup 을 재사용할 weight setter 와 공개 서비스만 없다. |
| framework 문서 | `common/e2e/config-5-resilience-lifecycle.ko.md` RL-B3 가 "진행 중 request 를 끝까지 drain 하는 public admin/drain 모드는 가정하지 않는다 — drain 모드가 추가되면 별도 검증" 이라고 명시한다. | 이 계획이 그 가정을 바꾼다. 공통 spec 에 drain 의미 정의가 없다. |

## 목표 정책

### 1. drain 은 로컬 serving socket 의 weight 를 즉시 바꾼다

drain 은 호출한 노드의 해당 channel serving socket 의 advertised weight 를 `0` 으로 설정한다. socket
은 닫히지 않고 계속 recv·dispatch·reply 한다. weight 변경은 즉시(non-blocking) 로컬에 적용되고,
연결된 peer 로의 전파는 core 의 best-effort 신호로 **최종적(eventual)** 으로 도달한다. 따라서 공개
API 는 "drain 신호를 보냈다"까지를 보장하고, peer 가 실제로 후보에서 제외한 시점은 보장하지 않는다.
실제 적용 확인은 `PEER_WEIGHT_CHANGED` 모니터 이벤트와 channel 모니터링 snapshot 으로 한다.

### 2. in-flight 는 끝까지 처리하고 reply 는 항상 허용

drain 후에도 이미 도착한 요청은 정상 처리하고 reply 를 보낸다. route mesh ROUTER 의 reply 경로는
weight 와 무관하게 항상 허용된다(core 계약). 즉 drain 은 "새 요청 수신 차단"이지 "현재 작업 취소"가
아니다. 작업 종료까지의 graceful 완료는 운영자가 모니터링으로 확인한 뒤 호스트를 내리는 흐름을 따른다.

### 3. 가용성은 이진 + weighted 둘 다 노출하되 운영 기본은 이진

1차 공개 표면은 운영 친화적으로 **Drain / Restore 이진 동작**을 기본으로 하고, 고급 사용(점진적 부하
조절·weighted rollout)을 위해 `SetWeight(0..100)` 를 함께 노출한다.

| 동작 | 의미 | weight |
|------|------|--------|
| `Drain` | 새 요청 유입을 막아 in-flight 작업이 빠져나가도록(drain out) 둔다 | `0` |
| `Restore` | 정상 복귀 | `100` |
| `SetWeight(w)` | 명시 weight 지정 | `0..100`, 범위 밖은 오류 |

> **용어 주의.** "drain" 은 LB·오케스트레이션 관례(HAProxy `DRAIN` 상태, AWS connection
> draining, k8s `kubectl drain`, Envoy draining)에서 **"새 유입을 막아 기존 작업을 비워낸다"**
> 를 가리킨다. 즉 이름의 핵심은 "남은 in-flight 가 빠져나감"이고, "새 요청 차단"은 그것이 가능하게
> 하는 수단이다. `Drain()` 은 weight `0` 으로의 전환(=draining 상태 진입)만 수행하고, in-flight 가
> 0 이 될 때까지 block 하지 않는다. 실제 비워짐 확인은 운영자가 모니터링(§5)으로 한다 — 이 또한
> 위 LB 들의 drain 토글과 같은 관례다.

### 4. serving 역할이 없으면 명확한 오류

drain 대상 channel 에 이 노드의 serving 역할이 없거나(예: client 전용), 알 수 없는 channel 이름이면
조용히 무시하지 않고 즉시 명확한 오류(`ZLinkConfigurationException` 계열, 언어별 표면)로 끝낸다.
fanout channel 에 drain 을 호출하는 것도 같은 방식으로 거부한다.

### 5. 가용성은 관측 가능해야 함

각 channel 의 현재 weight·draining 여부를 모니터링 snapshot 으로 읽을 수 있어야 하고, weight 변경은
기존 모니터링 surface(`PEER_WEIGHT_CHANGED`, spot peer entry 의 `Weight`)와 정합해야 한다.

## 공통 public 계약 초안

이름은 언어별 관례를 따르되 의미는 동일해야 한다. drain 은 로컬 in-process 즉시 mutation 이므로
공개 메서드는 동기형으로 둔다(소켓 옵션 set 은 non-blocking, 비동기 submit 정책 대상이 아니다).

| 개념 | `.NET` | Java/Kotlin | Node/NestJS | C++ |
|------|--------|-------------|-------------|-----|
| 제어 서비스 | `IZLinkChannelControl` | `ZLinkChannelControl` | `ZLinkChannelControl` | `channel_control_t` |
| 상태 snapshot | `ZLinkChannelAvailability` | `ZLinkChannelAvailability` | `ZLinkChannelAvailability` | `channel_availability_t` |
| channel 종류 enum | `ZLinkChannelKind` | `ZLinkChannelKind` | `ZLinkChannelKind` | `channel_kind_t` |

### 상태 snapshot 필드

| 필드 | 설명 |
|------|------|
| `ChannelName` | channel 이름 |
| `Kind` | `ClientServer`, `DealerMesh`, `RouteMesh` |
| `Weight` | `0..100` 로컬 advertised weight |
| `IsDraining` | `Weight == 0` |
| `HasServingRole` | 이 노드가 해당 channel 의 serving 역할을 갖는가 |

필드 이름은 언어 관례에 맞게 바꿀 수 있지만 정보 손실은 없어야 한다.

### 언어별 public interface 초안

새 API 는 기존 channel/handler 등록 방식을 바꾸지 않는다. enable 전용 builder 표면은 그대로 두고,
DI 로 주입받는 **별도 제어 서비스**로만 추가한다(`IZLinkChannelClient` 와 같은 등록 위치).

`.NET`:

```csharp
public interface IZLinkChannelControl
{
    void Drain(string channelName);
    void Restore(string channelName);
    void SetWeight(string channelName, int weight);   // 0..100
    ZLinkChannelAvailability GetAvailability(string channelName);
}

public sealed record ZLinkChannelAvailability(
    string ChannelName,
    ZLinkChannelKind Kind,
    int Weight,
    bool IsDraining,
    bool HasServingRole);
```

Java:

```java
public interface ZLinkChannelControl {
    void drain(String channelName);
    void restore(String channelName);
    void setWeight(String channelName, int weight);   // 0..100
    ZLinkChannelAvailability getAvailability(String channelName);
}
```

Kotlin 은 Java interface 를 그대로 쓰되 필요하면 얇은 확장 함수만 제공한다.

Node/NestJS:

```ts
export interface ZLinkChannelControl {
  drain(channelName: string): void;
  restore(channelName: string): void;
  setWeight(channelName: string, weight: number): void; // 0..100
  getAvailability(channelName: string): ZLinkChannelAvailability;
}
```

C++:

```cpp
class channel_control_t
{
  public:
    void drain (const std::string &channel_name);
    void restore (const std::string &channel_name);
    void set_weight (const std::string &channel_name, int weight); // 0..100
    channel_availability_t get_availability (const std::string &channel_name);
};
```

### 운영툴 연동 예시 (`.NET`)

framework 는 in-process 서비스만 제공한다. 애플리케이션이 자신의 admin 엔드포인트에서 호출한다.

```csharp
app.MapPost("/admin/channels/{name}/drain",
    (string name, IZLinkChannelControl control) =>
    {
        control.Drain(name);                       // 새 요청 수신 중단, in-flight 는 계속
        return Results.Ok(control.GetAvailability(name));
    });

app.MapPost("/admin/channels/{name}/restore",
    (string name, IZLinkChannelControl control) =>
    {
        control.Restore(name);
        return Results.Ok(control.GetAvailability(name));
    });
```

## 런타임 구현 정책

`.NET` 을 레퍼런스로 한다. 다른 언어는 같은 seam 을 자기 런타임 구조에 맞춰 미러링한다.

1. **backend socket 계약에 weight setter 추가** — `IZLinkBackendRouterSocket`/`IZLinkBackendDealerSocket`
   (`Runtime/Backend/Contracts/IZLinkBackendSocketContracts.cs`)에 `SetPeerWeight(int)`/`int GetPeerWeight()`
   를 추가하고, 각 wrapper(`ZLinkBackendRouterSocketWrapper`/`ZLinkBackendDealerSocketWrapper`)에서
   native `Options.PeerWeight` 로 구현한다. `NativeInstance` 캐스팅 대신 계약 메서드로 노출해 정보
   은닉을 지킨다.
2. **route mesh 노출** — `ZLinkRouteChannelRuntime._router` 가 private 이므로 `SetPeerWeight(int)`/
   `GetPeerWeight()` 메서드를 추가한다.
3. **이름 → live socket lookup** — `ZLinkChannelRuntimeManager` 에 `SetChannelWeight(state, channelName, weight)`
   /`GetChannelAvailability(state, channelName)` 를 추가한다. 기존 `GetMonitoringSocket` 의 capability
   resolution 패턴(server/client/route 매핑)을 재사용해 channel 종류별 serving socket 을 찾는다.
   serving 역할이 없으면 명확한 오류로 끝낸다.
4. **facade 경유 노출** — `ZLinkFrameworkChannelFacade` 에 forwarding 메서드를 추가한다.
5. **공개 서비스 등록** — `IZLinkChannelControl` 구현을 만들어 singleton `ZLinkFrameworkRuntime` 에
   의존하게 하고, `ZLinkFrameworkServiceRegistrar` 에서 `IZLinkChannelClient` 옆에 DI 등록한다.
6. **모니터링 정합** — channel 모니터링 snapshot 에 `Weight`/`IsDraining` 을 노출하고, 기존
   `ZLinkSpotNodePeerEntry.Weight` 및 `PEER_WEIGHT_CHANGED` 이벤트와 의미를 맞춘다.

## 공개 API 불변 기준

| 표면 | 불변 기준 |
|------|-----------|
| channel builder | `EnableServer`/`EnableClient`/`EnablePublisher`/`EnableSubscriber` 등 enable 전용 유지. drain 은 builder 가 아니라 별도 제어 서비스로만 추가. |
| handler 등록 | 변경 없음. |
| 기본 weight | drain 을 호출하지 않으면 모든 serving socket weight 는 `100` 유지. |
| in-flight 처리 | drain 은 recv/dispatch/reply 를 멈추지 않는다. socket 을 닫지 않는다. |
| registry 등록 | drain 은 registry deregister 가 아니다. 노드는 topology 에 남되 weight `0` 으로 후보에서 빠진다. |

## 구현 단계

### Stage 0. 현재 동작 재현

각 언어에서 "serving 노드가 떠 있는 한 새 요청이 계속 그 노드로 들어온다"를 테스트로 재현해, drain
표면이 없다는 현재 상태를 명확히 드러낸다. 이 테스트의 기대값은 목표 정책 구현 후 drain 동작 기준으로
바뀐다.

### Stage 1. 공통 모델 추가

- `IZLinkChannelControl`, `ZLinkChannelAvailability`, `ZLinkChannelKind` 를 각 언어에 추가한다.
- backend socket 계약에 `SetPeerWeight`/`GetPeerWeight` 를 추가한다(binding 신규 작업은 없음 — native
  `PeerWeight` 를 호출만).
- weight 범위(`0..100`) 검증과 serving 역할 부재·미지 channel 오류를 공통 의미로 고정한다.

### Stage 2. `.NET` 구현

런타임 구현 정책 1~6 을 `.NET` 에 적용한다. client-server·dealer mesh·route mesh 세 경로 모두에서
weight setter 가 올바른 live socket 에 닿는지 확인한다.

### Stage 3. Java/Kotlin 구현

`zlink-framework-core` 런타임에 같은 seam 을 적용하고, Spring Boot starter 에서 `ZLinkChannelControl`
bean 을 등록한다. Kotlin 은 Java 표면을 그대로 노출한다.

### Stage 4. Node/NestJS 구현

node 런타임 channel registry 에서 serving socket 을 이름으로 찾아 `peerWeight` 를 설정하고, NestJS
provider 로 `ZLinkChannelControl` 을 주입 가능하게 한다.

### Stage 5. C++ 구현

`capability_builder_t` 기반 런타임에서 serving socket 을 찾아 `peer_weight()` 를 설정하는
`channel_control_t` 를 host 표면에 추가한다. CTest label 은 기존 `framework-zlink-*` 체계에 맞춘다.

### Stage 6. Cross-language parity 점검

- 세 channel 종류 모두에서 drain 이 새 요청 수신을 멈추는가
- in-flight 요청이 drain 후에도 완료되는가
- restore 가 정상 복귀시키는가
- serving 역할 부재·미지 channel·fanout·범위 밖 weight 가 모두 명확한 오류인가
- `Weight`/`IsDraining` 이 모니터링 snapshot 으로 읽히는가
- `PEER_WEIGHT_CHANGED` 이벤트가 drain/restore 후 관측되는가

### Stage 7. Codex 에이전트 적용 완료 리뷰

구현·회귀 테스트·POSD 리팩토링·정식 문서 반영이 끝난 뒤 Codex 에이전트로 이 계획 문서의 모든 항목이
실제 checkout 에 반영되었는지 리뷰한다. 적용 누락, 언어별 정책 불일치, 테스트 공백, 문서와 구현의
불일치를 찾는다. 리뷰가 통과하기 전에는 완료로 보지 않는다.

## 회귀 테스트 목록

### 공통 회귀 시나리오

| ID | 시나리오 | 기대값 |
|----|----------|--------|
| DRAIN-001 | client-server drain | drain 후 client 의 새 request 가 그 노드로 가지 않는다(다른 후보로 가거나 후보가 없으면 `NOT_ADMITTED`). |
| DRAIN-002 | drain 중 in-flight | drain 직전 도착한 request 는 정상 처리되고 reply 가 돌아온다. |
| DRAIN-003 | restore | restore 후 그 노드가 다시 후보가 되어 request 를 받는다. |
| DRAIN-004 | dealer mesh drain | drain 한 mesh 노드가 round-robin 후보에서 빠지고 나머지 노드가 받는다. |
| DRAIN-005 | route mesh drain | 그 노드를 target 으로 한 새 submit 은 `NOT_ADMITTED`, reply 경로는 계속 허용된다. |
| DRAIN-006 | serving 역할 없음 | client 전용 노드에서 drain 호출 시 명확한 오류. |
| DRAIN-007 | 미지 channel 이름 | 명확한 오류. |
| DRAIN-008 | fanout drain | 거부(명확한 오류). |
| DRAIN-009 | `SetWeight` 범위 | `0..100` 적용, 범위 밖은 오류. |
| DRAIN-010 | weighted 분산 | weight `1..99` 에서 비율에 따라 적게 선택된다. |
| DRAIN-011 | 모니터링 snapshot | `GetAvailability` 가 `Weight`/`IsDraining`/`HasServingRole` 을 정확히 반환한다. |
| DRAIN-012 | `PEER_WEIGHT_CHANGED` | drain/restore 후 모니터 이벤트가 관측된다. |
| DRAIN-013 | drain 후 graceful shutdown | drain → in-flight 완료 → 호스트 종료 순서가 깨지지 않는다. |

### 언어별 테스트 위치

| 언어 | 테스트 위치 |
|------|-------------|
| `.NET` | `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime`, `framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Channels` |
| Java/Kotlin | `framework/languages/java/zlink-framework-core/src/test`·`src/integrationTest`, `framework/languages/java/zlink-framework-spring-boot-starter/src/test`, `framework/languages/java/zlink-framework-kotlin/src/test` |
| Node/NestJS | `framework/languages/node/test/contract` 의 `node:test` 기반 테스트(channel/mesh 경로, `nestjs-module.test.js` 등록 표면) |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.UnitTests`, `framework/languages/cpp/tests/Zlink.Framework.ContractTests` |

## 구현 뒤 정식 문서 반영 계획

구현 전에는 정식 spec 문서에 계약처럼 쓰지 않는다. 구현과 회귀 테스트가 끝난 뒤 아래 문서에 나누어
반영한다. 모든 문서는 Korean-only(`.ko.md`)이며 영문 mirror 는 두지 않는다.

### 공통 문서 (source of truth, 먼저 반영)

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/channel-availability.ko.md` (신규) | drain/peer-weight 의미, 이진 + weighted 모델, in-flight·reply 보장, serving 역할 한정, 운영툴 연동 경계, 모니터링 정합. **이 문서가 정본 의미 정의.** |
| `framework/doc/framework/common/README.ko.md` | §2 spec 표·§3 scope 표에 새 spec 등록, nav 헤더 배선. |
| `framework/doc/framework/common/spec/channel-topology.ko.md` | 가용성 제어를 topology availability 로 교차 참조(§5 registry/monitoring 인근). |
| `framework/doc/framework/common/spec/framework-api.ko.md` | 언어별 `ChannelControl` API surface 표 추가. |
| `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md` | **RL-B3 의 "public admin/drain 모드는 가정하지 않는다" 문장을 갱신**하고, drain 검증용 신규 RL-B 시나리오(client-server/dealer mesh/route mesh drain·restore·in-flight 완료)를 추가. |

### 언어별 문서 (공통 반영 후)

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/dotnet/guide/04-channel-messaging.ko.md` | §6 "연결 제어" 인근에 운영 drain 절 추가(현재 "실행 중 socket 직접 제어 핸들 없음" 서술을 drain 제어 서비스로 보완). |
| `framework/doc/framework/dotnet/guide/09-monitoring.ko.md` | 가용성 snapshot·`PEER_WEIGHT_CHANGED` 관측 예시. |
| `framework/doc/framework/dotnet/spec/aspnet-core-channel-messaging.ko.md` | `IZLinkChannelControl` 계약·serving 역할 한정·오류 정책. |
| `framework/doc/framework/java/guide/04-channel-messaging.ko.md`·`spec/spring-boot-channel-messaging.ko.md`·`guide/09-monitoring.ko.md` | Java/Kotlin 표면·Spring bean 등록·모니터링. |
| `framework/doc/framework/kotlin/guide/04-channel-messaging.ko.md` | Kotlin idiom 노트(Java 표면 공유). |
| `framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md`·`spec/nestjs-monitoring.ko.md` | Node/NestJS provider 등록·모니터링. |
| `framework/doc/framework/cpp/spec/cpp-channel-messaging.ko.md` | C++ `channel_control_t` 계약. |
| 각 언어 `internals/regression-test-matrix.ko.md` | DRAIN 회귀 항목과 실제 테스트 이름. |

guide 에는 native socket·weight 정수·frame 세부를 넣지 않는다. guide 는 운영자가 drain/restore 를
호출하고 가용성을 확인하는 방법만 설명한다. 내부 weight 전파·socket lookup 세부는 internals 또는
공통 spec 에 둔다.

## POSD 기반 리팩토링 단계

각 언어 구현이 끝난 뒤 바로 문서 반영으로 넘어가지 않고 POSD 기준으로 점검한다.

### Red flag 점검

| 위험 신호 | 확인 질문 |
|-----------|-----------|
| 얕은 모듈 | `IZLinkChannelControl` 이 weight 정수·socket option key 를 그대로 노출하지 않고 drain/restore 의미로 감싸는가 |
| 정보 누출 | native socket·`NativeInstance`·option enum 이 공개 표면이나 snapshot 에 새지 않는가 |
| 특수/범용 혼합 | channel 종류별 serving socket resolution 이 한 helper(`SetChannelWeight`)에 모이고 호출부에 흩어지지 않는가 |
| 패스스루 메서드 | facade→manager→backend 로 weight 만 넘기는 얕은 wrapper 가 불필요하게 층층이 생기지 않는가 |
| 오류를 정의로 제거 | serving 역할 부재·미지 channel·fanout·범위 밖 weight 를 단일 검증 지점에서 명확한 오류로 처리하는가 |

### 두 가지 설계 대안 재검토

| 선택지 | 장점 | 단점 |
|--------|------|------|
| 별도 제어 서비스(`IZLinkChannelControl`) | builder 표면 불변, 운영 의미가 분리됨 | 서비스 하나가 추가됨 |
| channel builder 에 runtime 핸들 부여 | 호출 지점이 channel 선언과 가까움 | enable 전용 불변 기준이 깨지고 startup/runtime 경계가 흐려짐 |

1차 구현은 별도 제어 서비스로 유지한다. builder 에 runtime 핸들을 붙이자는 요구가 실제로 확인되기
전까지 표면을 넓히지 않는다.

### 리팩토링 완료 조건

- weight setter 가 각 런타임에 흩어진 ad hoc 캐스팅이 아니라 backend 계약 메서드를 통한다.
- channel 종류별 serving socket resolution 이 단일 helper 에 모인다.
- 공개 snapshot 에는 운영자가 판단할 context(weight/draining/serving 여부)만 있고 transport 세부는 없다.
- 언어별 API 이름은 관례에 맞지만 drain/restore/weight 의미와 오류 정책은 같다.

## 완료 기준

이 계획은 아래 조건을 모두 만족해야 완료로 본다.

1. `.NET`, Java/Kotlin, Node/NestJS, C++ framework 에 `IZLinkChannelControl` 류 공개 서비스가 있다.
2. client-server·dealer mesh·route mesh 세 종류에서 drain 이 새 요청 수신을 멈추고 in-flight 와 reply 는 유지된다.
3. restore/`SetWeight` 가 가용성을 정상 복귀·조절한다.
4. serving 역할 부재·미지 channel·fanout·범위 밖 weight 가 모두 명확한 오류로 끝난다.
5. 가용성이 모니터링 snapshot 과 `PEER_WEIGHT_CHANGED` 이벤트로 관측된다.
6. DRAIN 회귀 테스트가 각 언어에 추가되고 언어별 regression matrix 에 연결된다.
7. POSD 점검과 필요한 리팩토링이 끝난 뒤 공통 spec(특히 신규 `channel-availability.ko.md`)과 언어별
   spec/guide/internals 문서, RL-B3 갱신이 구현 기준으로 반영된다.
8. Codex 에이전트가 이 계획 문서의 모든 항목이 적용되었다고 리뷰할 때까지 누락 수정과 재리뷰를 반복한다.
