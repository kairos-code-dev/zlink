# Framework channel 런타임 옵션 제어 계획 (1차 deliverable: weight/drain)

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
허용된다. 이 계획은 그 core 능력을 **framework 의 런타임 옵션 제어 표면** 으로 끌어올린다.

### 표면 방향: build-time 옵션 인터페이스를 런타임과 공유한다

drain 전용 메서드(`Drain()`/`Restore()`)나 평행 옵션 어휘를 새로 만들지 않는다. framework 에는 이미
channel 종류별 빌더가 `ConfigureXxx()` getter 로 framework 어휘 config 객체(ROUTER/DEALER 미노출)를
제공한다.

- client-server: `ConfigureServerSocket()`/`ConfigureClientSocket()` → `IZLinkSocketConfig`,
  `ConfigureServerRouting()`/`ConfigureClientRouting()` → `IZLinkRouteConfig`/`IZLinkOutboundRouteConfig`
- route mesh: `ConfigureSocket()` → `IZLinkSocketConfig`, `ConfigureRouting()` → `IZLinkRouteConfig`

이 계획은 그 빌더의 `ConfigureXxx()` getter 들을 **channel 종류별 옵션 인터페이스로 추출**하고
(`IZLinkClientServerChannelOptions`/`IZLinkRouteMeshChannelOptions`/…), 빌더가 그 인터페이스를
**상속(extend)** 하게 한다. 그러면 **build-time(빌더)과 runtime(접근자)이 같은 옵션 인터페이스를
공유**한다.

- build-time: `builder.AddClientServerChannel("orders").ConfigureServerSocket().Weight = 30;`
- runtime: `runtime.ClientServerChannel("orders").ConfigureServerSocket().Weight = 0;` (drain)

`runtime.<Kind>Channel(name).Configure<Aspect>()` 가 `builder.Add<Kind>Channel(name).Configure<Aspect>()`
와 **같은 인터페이스·같은 getter**를 쓴다. 옵션 surface 정의가 한 곳뿐이다. 추가로 `IZLinkSocketConfig`
에 `Weight` 속성을 신설해 drain 을 그 위에 얹는다(weight 는 기존 어느 config 에도 없다).

세 가지 제약이 "build-time 객체를 그대로 런타임에 돌려주기"를 막는다. 이 계획은 이 셋을 해소한다.

1. **weight 가 기존 config 에 없다.** `IZLinkSocketConfig` 에 `Weight` 속성을 신규 추가한다.
2. **startup config ≠ 런타임 변경 가능.** `IZLinkSocketConfig` 대부분이 bind/connect 前 옵션
   (`IPv6`/`Immediate`/`SendBufferSize`/`ConnectTimeout`/`HandshakeInterval`/`TcpNoDelay`)이라 런타임
   set 이 무의미하다. **같은 인터페이스라도 impl 이 phase 별로 다르다** — build-time impl 은 레시피라
   전 속성 set 이 정상 동작하고, runtime impl 은 live socket backing 이라 read 는 전 속성, set 은
   런타임-안전 속성만 반영하고 startup 전용 속성 set 은 조용히 무시하지 않고 명확한 오류로 거부한다.
3. **config 는 build-time 레시피지 live 핸들이 아니다.** runtime impl 은 live socket 을 backing 으로
   둬서, 속성 read 는 socket 에서 읽고 runtime-mutable 속성 set 은 socket 에 즉시 반영한다.

이 기능은 dispatch 제어가 아니라 **운영 가용성 제어**다. drain 을 호출하지 않으면 framework 기본
동작은 변하지 않아야 하고(weight 기본 `100`), drain 중에도 in-flight 처리·reply 전송은 깨지면 안 된다.

## 적용 범위

적용 대상은 serving 역할이 ROUTER 또는 DEALER socket 으로 표현되는 channel 종류다. 즉 weight 개념이
있는 transport 만 대상이다. **공개 주소는 `<Kind>Channel(channelName)` + 빌더와 동일한 `ConfigureXxx()`
getter** 다. role(server/client)은 빌더처럼 getter 이름에 박혀 있고 파라미터가 아니다. 내부 socket
타입(ROUTER/DEALER)은 resolver 안에만 있고 공개 표면·오류 메시지에 노출하지 않는다.

| channel 종류 | runtime 접근자 getter | 내부 serving socket (resolver 전용) | core 옵션 |
|--------------|-----------------------|-------------------------------------|-----------|
| client-server | `ClientServerChannel(name).ConfigureServerSocket()` | 로컬 ROUTER (bound) | `ROUTER_WEIGHT` |
| dealer mesh | `DealerMeshChannel(name).ConfigureSocket()` | 로컬 mesh DEALER (bound, handler 보유) | `DEALER_WEIGHT` |
| route mesh | `RouteMeshChannel(name).ConfigureSocket()` | 로컬 ROUTER (`RoutingId` 보유) | `ROUTER_WEIGHT` |

| 언어 | 적용 범위 |
|------|-----------|
| `.NET` | client-server, dealer mesh, route mesh serving 역할 |
| Java/Kotlin | Spring Boot client-server, dealer mesh, route mesh serving 역할 |
| Node/NestJS | client-server, dealer mesh, route mesh serving 역할 |
| C++ | client-server, dealer mesh, route mesh serving 역할 |

**범위 밖**:

- **fanout(PUB/SUB)** — PUB 에는 peer weight 개념이 없다. `FanoutChannel`/`SpotNode` 접근자는 인터페이스
  확장 슬롯으로만 두고 1차에서 wiring 하지 않는다. 구독 fan-out 의 부분 중단은 별도 기능이다.
- **client 전용 노드** — 그 노드는 요청을 받는 serving 역할이 없으므로 drain 대상이 아니다. 해당
  channel 의 serving getter 를 호출하면 조용히 무시하지 않고 명확한 오류로 끝낸다.
- **weight 외 런타임 mutation** — 옵션 인터페이스에는 routing config(`ConfigureServerRouting` →
  `IZLinkRouteConfig` 등)도 포함되지만(빌더 공유), 그 속성 대부분은 startup 전용이라 런타임 set 은
  오류이고 read 만 가능하다. 1차 runtime-mutable 속성은 `Weight` 하나다.
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
| framework 빌더 옵션 표면 | per-kind 빌더가 `ConfigureServerSocket`/`ConfigureClientSocket`/`ConfigureSocket`/`ConfigureServerRouting` 등을 **인라인 메서드**로 들고 build-time `IZLinkSocketConfig`/`IZLinkRouteConfig`(framework 어휘)를 제공한다. | (a) `Weight` 속성이 없다. (b) 옵션 getter 가 빌더 인라인이라 runtime 과 공유할 별도 인터페이스가 없다. (c) build-time 1회 소비라 런타임 mutation 미배선. (d) **dealer mesh 빌더엔 `ConfigureSocket` 자체가 없다**(갭). (e) 노출 속성 대부분이 startup 전용이라 런타임 set 은 subset 으로 제한해야 한다. |
| framework channel builder enable | `EnableServer`/`EnableClient` 등 enable 메서드는 build-time 전용이다. 실행 중 channel 옵션을 가리키는 핸들이 없다. | 운영 중 옵션을 읽거나 drain 을 호출할 공개 표면이 전무하다. |
| framework runtime state | `ZLinkFrameworkRuntimeState` 가 channel 이름 keyed dict(`ServerBundles`/`ClientBundles`/`RouteChannels`)로 live socket 을 들고 있고, `ZLinkChannelRuntimeManager.GetMonitoringSocket` 이 이미 `channel.capability` → live socket lookup 을 한다. | 이 lookup 을 채널이름으로 재사용해 live-backed config 객체를 돌려줄 resolver 만 없다. |
| framework 문서 | `common/e2e/config-5-resilience-lifecycle.ko.md` RL-B3 가 "진행 중 request 를 끝까지 drain 하는 public admin/drain 모드는 가정하지 않는다 — drain 모드가 추가되면 별도 검증" 이라고 명시한다. | 이 계획이 그 가정을 바꾼다. 공통 spec 에 drain 의미 정의가 없다. |

## 목표 정책

### 1. drain 은 로컬 serving socket 의 weight 를 즉시 바꾼다

drain 은 호출한 노드의 해당 channel serving socket 의 advertised weight 를 `0` 으로 설정한다. socket
은 닫히지 않고 계속 recv·dispatch·reply 한다. weight 변경은 즉시(non-blocking) 로컬에 적용되고,
연결된 peer 로의 전파는 core 의 best-effort 신호로 **최종적(eventual)** 으로 도달한다. 따라서 공개
API 는 "drain 신호를 보냈다"까지를 보장하고, peer 가 실제로 후보에서 제외한 시점은 보장하지 않는다.
실제 적용 확인은 `PEER_WEIGHT_CHANGED` 모니터 이벤트와 channel 모니터링 snapshot 으로 한다.

weight set 은 admin 스레드 등 channel receive loop 와 다른 스레드에서 호출될 수 있다. 구현은 live
socket 의 동시성 모델을 준수해야 한다(필요 시 core 의 command 경유). 이 정합이 깨지면 in-flight
처리 보장이 위태로워진다.

### 2. in-flight 는 끝까지 처리하고 reply 는 항상 허용

drain 후에도 이미 도착한 요청은 정상 처리하고 reply 를 보낸다. route mesh ROUTER 의 reply 경로는
weight 와 무관하게 항상 허용된다(core 계약). 즉 drain 은 "새 요청 수신 차단"이지 "현재 작업 취소"가
아니다. 작업 종료까지의 graceful 완료는 운영자가 모니터링으로 확인한 뒤 호스트를 내리는 흐름을 따른다.

### 3. 표면은 build-time 옵션 인터페이스의 런타임 공유 — read 전체 / set 런타임-안전 subset

런타임 접근자는 새 옵션 어휘·평행 타입을 발명하지 않는다. 빌더가 상속하는 per-kind 옵션 인터페이스
(`IZLinkClientServerChannelOptions` 등)를 채널이름으로 돌려주고, 운영자는 빌더와 동일한 `ConfigureXxx()`
getter 로 config 객체를 얻어 속성을 직접 읽고 쓴다.

- **read**: 돌려받은 config 객체의 모든 속성을 읽을 수 있다(소켓 타입은 노출하지 않는다). draining
  여부는 `Weight == 0` 으로 판정한다.
- **set**: runtime-mutable 속성만 live socket 에 즉시 반영하고, startup 전용 속성 set 은 명확한
  오류로 거부한다. **이번 1차 구현의 runtime-mutable 속성 = `Weight` 하나.**

#### 3.1 `Weight` 는 신규 knob — build-time 초기값 + 런타임 mutable 을 한 속성으로 통합

`Weight` 는 기존 socket/route config 어디에도 없으므로 `IZLinkSocketConfig` 에 신규 추가한다. 같은
`Weight` 속성을 **build-time 초기값**(`ConfigureServerSocket().Weight = 30`)과 **런타임 mutation**
(drain) 양쪽에서 쓴다. 그러면 재시작 시 노드는 startup config 의 초기 weight 로 떠오르고, 운영
중에는 런타임 접근자가 같은 속성을 바꾼다. startup 과 runtime 이 한 속성으로 합쳐진다.

| weight | 의미 |
|--------|------|
| `0` | drain — 새 요청 유입을 막아 in-flight 작업이 빠져나가도록(drain out) 둔다 |
| `1..99` | weighted — 연결된 peer 의 LB 분배 비율을 낮춘다(아래 유효 범위 주의) |
| `100` | 정상(기본) |
| 범위 밖 | 오류 |

#### 3.2 framework 는 "이전 weight" 를 기억하지 않는다

drain 은 `Weight = 0`, 정상 복귀는 운영자가 자신이 의도한 값으로 `Weight = w` 를 set 하는 것이다.
framework 에 in-memory baseline 을 두지 않는다. 노드의 정상 weight 는 외부 운영 설정(startup
config·컨트롤 플레인)이 source of truth 이고, 재시작 시에도 그 설정에서 떠오른다. baseline 을 들면
정작 1차 시나리오인 재시작을 못 버티는 환상만 추가한다. "이전 30 으로 똑똑하게 복귀하는 척" 하지
않는 편이 정직하다.

> **weighted(`1..99`) 유효 범위.** weight 의 비율 분산은 **이미 연결된 peer 의 LB**(`lb.cpp` weighted
> 선택)에만 작용한다. discovery 의 auto-connect 후보 게이팅은 advertised weight 를 `0`/non-`0` 으로만
> 보므로(이진, `discovery_update.cpp` 가 `0` 또는 `100` 으로 정규화), weight `30` 인 노드도 discovery
> 후보로는 정상(연결됨) 취급되고 다만 연결된 peer 가 보내는 양에서 비율이 낮아진다. 즉 weighted 는
> "후보 제외"가 아니라 "이미 연결된 경로의 분배 조절"이다. drain(`0`)만이 후보에서 빠지는 이진 게이팅이다.

> **용어 주의.** "drain" 은 LB·오케스트레이션 관례(HAProxy `DRAIN` 상태, AWS connection
> draining, k8s `kubectl drain`, Envoy draining)에서 **"새 유입을 막아 기존 작업을 비워낸다"**
> 를 가리킨다. 즉 이름의 핵심은 "남은 in-flight 가 빠져나감"이고, "새 요청 차단"은 그것이 가능하게
> 하는 수단이다. `Weight = 0` 전환은 draining 상태 진입만 수행하고 in-flight 가 0 이 될 때까지 block
> 하지 않는다. 실제 비워짐 확인은 운영자가 모니터링(§5)으로 한다 — 이 또한 위 LB 들의 drain 토글과
> 같은 관례다.

#### 3.3 런타임 옵션 로스터 (read 전체 / set 분류)

`ConfigureXxx()` 가 돌려주는 config 객체의 속성별 런타임 거동은 아래와 같다(`IZLinkSocketConfig`
기준). **read 는 전부 가능**하고, **set 은 런타임-안전 속성만 반영**하며 1차 runtime-mutable 속성은
`Weight` 하나다. startup 전용 속성을 런타임에 set 하면 명확한 오류로 거부한다(build-time 객체에서는
정상 set).

| 속성 (framework 이름) | 의미 | 런타임 read | 런타임 set | 비고 |
|------------------------|------|:-----------:|:----------:|------|
| `Weight` | advertised 가용성/부하 weight(`0..100`) | O | **O (1차)** | `0`=drain. build-time 초기값도 동일 속성 |
| `MaxMessageSize` | 수신 메시지 크기 상한 | O | △ 확장 후보 | live 적용 |
| `SendHighWaterMark` | 송신 큐 HWM | O | △ 확장 후보 | **신규 연결부터만 반영(주의)** |
| `ReceiveHighWaterMark` | 수신 큐 HWM | O | △ 확장 후보 | **신규 연결부터만 반영(주의)** |
| `SendTimeout` | send 호출 블로킹 한도 | O | ✕ 오류 | framework 내부 receive/send loop 가 소유 |
| `ReceiveTimeout` | recv 호출 블로킹 한도 | O | ✕ 오류 | framework 내부 receive/send loop 가 소유 |
| `Linger` | close 시 잔여 전송 대기 | O | ✕ 오류 | 종료 시점 의미, startup |
| `SendBufferSize` | OS 송신 버퍼 | O | ✕ 오류 | bind/connect 前, startup |
| `ReceiveBufferSize` | OS 수신 버퍼 | O | ✕ 오류 | bind/connect 前, startup |
| `ConnectTimeout` | connect 한도 | O | ✕ 오류 | connect 시점, startup |
| `HandshakeInterval` | 핸드셰이크 주기 | O | ✕ 오류 | connect 시점, startup |
| `IPv6` | IPv6 사용 | O | ✕ 오류 | bind/connect 前, startup |
| `TcpNoDelay` | Nagle 비활성 | O | ✕ 오류 | connect 前, startup |
| `Immediate` | 연결 완료 전 큐잉 억제 | O | ✕ 오류 | bind/connect 前, startup |

> 범례: `O` 가능 · `△` 런타임 set 확장 후보(주의 필요) · `✕ 오류` startup 전용이라 런타임 set 시 명확한 오류(read 만 가능).

routing config(`ConfigureServerRouting()` → `IZLinkRouteConfig`: `RequireKnownPeer`·`AllowPeerHandover`·
`RoutingId` 등)도 같은 인터페이스에 포함되지만(빌더 공유), 그 속성은 대부분 startup 전용이라 런타임
set 은 오류이고 read 만 가능하다. 1차에서는 routing/pubsub config 를 wiring 하지 않는다.

### 4. 종류·getter·역할이 안 맞으면 명확한 오류

다음은 모두 조용히 무시하지 않고 즉시 명확한 오류(`ZLinkConfigurationException` 계열, 언어별 표면)로
끝낸다. 오류 메시지는 channel 종류·역할 어휘로 말하며 소켓 타입(ROUTER/DEALER)을 노출하지 않는다.

- 미지 channel 이름.
- 채널 종류에 없는 getter 호출(예: route mesh 채널에 `ClientServerChannel(...)`, 또는 빌더에 그
  `ConfigureXxx` 가 없는 aspect).
- 이 노드에 그 serving 역할이 없음(예: client 전용 노드의 server socket).
- 옵션이 그 종류·역할에 무효(예: client-server 의 `ConfigureClientSocket().Weight` — server 가
  client 를 select 하지 않으므로 무의미).
- 범위 밖 weight, startup 전용 속성의 런타임 set, fanout drain.

### 5. 가용성은 관측 가능해야 함

각 channel 의 현재 weight·draining(`Weight==0`) 여부를 config 객체 read 와 모니터링 snapshot 으로
읽을 수 있어야 하고, weight 변경은 기존 모니터링 surface(`PEER_WEIGHT_CHANGED`, spot peer entry 의
`Weight`)와 정합해야 한다.

## 공통 public 계약 초안

이름은 언어별 관례를 따르되 의미는 동일해야 한다. weight 변경은 로컬 in-process 즉시 mutation 이므로
공개 메서드는 동기형으로 둔다(소켓 옵션 set 은 non-blocking, 비동기 submit 정책 대상이 아니다).
주소는 `<Kind>Channel(channelName)` + 빌더 미러 `ConfigureXxx()` 이고 소켓 타입은 노출하지 않는다.

| 개념 | `.NET` | Java/Kotlin | Node/NestJS | C++ |
|------|--------|-------------|-------------|-----|
| 런타임 옵션 접근자 | `IZLinkChannelRuntimeOptions` | `ZLinkChannelRuntimeOptions` | `ZLinkChannelRuntimeOptions` | `channel_runtime_options_t` |
| per-kind 옵션 인터페이스 (빌더 공유) | `IZLinkClientServerChannelOptions`/`IZLinkRouteMeshChannelOptions`/… | `ZLink…ChannelOptions` | `ZLink…ChannelOptions` | `…_channel_options_t` |
| leaf config 타입 (재사용) | `IZLinkSocketConfig`/`IZLinkRouteConfig`/… | `ZLinkSocketConfig`/… | `ZLinkSocketConfig`/… | `socket_config_t`/… |

### per-kind 옵션 인터페이스 추출 + 빌더 상속

빌더에 인라인으로 박힌 `ConfigureXxx()` getter 를 종류별 옵션 인터페이스로 추출하고, 빌더가 이를
상속한다. **기존 build-time 호출은 상속 멤버라 그대로 동작한다(호출부 무변경).**

```csharp
// 추출된 공유 인터페이스 — build-time·runtime 공통
public interface IZLinkClientServerChannelOptions
{
    IZLinkSocketConfig        ConfigureServerSocket();
    IZLinkSocketConfig        ConfigureClientSocket();
    IZLinkRouteConfig         ConfigureServerRouting();
    IZLinkOutboundRouteConfig ConfigureClientRouting();
}

public interface IZLinkRouteMeshChannelOptions
{
    IZLinkSocketConfig ConfigureSocket();
    IZLinkRouteConfig  ConfigureRouting();
}

public interface IZLinkDealerMeshChannelOptions
{
    IZLinkSocketConfig ConfigureSocket();   // 빌더 갭: build-time getter 도 이때 추가(additive)
}

// 빌더는 옵션 인터페이스 + enable/handler 만 추가
public interface IZLinkClientServerChannelBuilder : IZLinkClientServerChannelOptions
{
    IZLinkClientServerChannelBuilder EnableServer(string endpoint);
    IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TReq, TRep>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TReq, TRep>;
    // ... 기존 enable/handler 메서드 ...
}
```

### `IZLinkSocketConfig` 에 `Weight` 추가

build-time 과 runtime 이 같은 속성을 쓰도록 socket config 에 `Weight` 를 더한다(기존 속성 제거·의미
변경 없음).

```csharp
public interface IZLinkSocketConfig
{
    // ... 기존 속성(MaxMessageSize, SendHighWaterMark, ... §3.3 로스터) ...
    int Weight { get; set; } // 0..100, advertised weight (기본 100, 0 = drain)
}
```

### 런타임 접근자

DI 로 주입받는 **singleton 접근자**가 채널이름으로 per-kind 옵션 인터페이스를 돌려준다
(`IZLinkChannelClient` 와 같은 등록 위치).

```csharp
public interface IZLinkChannelRuntimeOptions
{
    IZLinkClientServerChannelOptions ClientServerChannel(string channelName);
    IZLinkRouteMeshChannelOptions    RouteMeshChannel(string channelName);
    IZLinkDealerMeshChannelOptions   DealerMeshChannel(string channelName);
    // 확장 슬롯: FanoutChannel(name) → IZLinkFanoutChannelOptions, SpotNode(name) → IZLinkSpotNodeOptions
}

// build-time 과 동일 인터페이스. 단 runtime impl 은 live socket backing —
//   read 는 전 속성, set 은 runtime-mutable(Weight)만 반영, startup 전용 속성 set 은 오류.
//   미지 channel·종류에 없는 getter·역할 부재는 오류.
//
// drain (client-server)     runtime.ClientServerChannel("orders").ConfigureServerSocket().Weight = 0;
// drain (route/dealer mesh)  runtime.RouteMeshChannel("mesh").ConfigureSocket().Weight = 0;
// 정상 복귀                   runtime.ClientServerChannel("orders").ConfigureServerSocket().Weight = 100;
// build-time(대칭)           builder.AddClientServerChannel("orders").ConfigureServerSocket().Weight = 30;
```

**DI 모델.** 채널별 config 를 DI 로 주입하지 않는다(어느 채널 거냐가 모호해져 keyed/named DI 가
필요해진다). **채널-무관한 singleton 접근자 하나만 DI** 하고 채널은 메서드 인자로 넘긴다. 내부적으로
`ZLinkFrameworkRuntimeState` 의 channel-name keyed dict 를 인자로 조회하는 기존 `GetMonitoringSocket`
패턴을 재사용한다 — 채널을 아는 건 DI 컨테이너가 아니라 런타임 state 다.

> 1차 구현은 serving socket 의 `Weight` 만 세 channel 종류에 wiring 한다(client-server 는
> `ConfigureServerSocket`, route/dealer mesh 는 `ConfigureSocket`). client socket·routing·pubsub
> getter 는 같은 인터페이스의 확장 슬롯이다(fanout/spot 은 계획 범위 밖, `IZLinkSpotPublisherConfig`/
> `IZLinkSpotSubscriberConfig` 는 현재 set-only 라 read 노출 시 getter 추가 필요).

다른 언어는 같은 구조를 자기 관례로 미러링한다(Java `clientServerChannel(name).configureServerSocket()`,
Node 동형, C++ `client_server_channel(name).configure_server_socket()`). Kotlin 은 Java 표면을 그대로
쓰되 필요하면 얇은 확장 함수만 제공한다.

### 운영툴 연동 예시 (`.NET`)

framework 는 in-process 서비스만 제공한다. 애플리케이션이 자신의 admin 엔드포인트에서, 자기가 선언한
channel 종류에 맞는 getter 로 호출한다.

```csharp
// 이 앱은 "orders" 가 client-server 채널임을 안다(자기가 등록함)
app.MapPost("/admin/channels/orders/weight/{weight:int}",
    (int weight, IZLinkChannelRuntimeOptions options) =>
    {
        var cfg = options.ClientServerChannel("orders").ConfigureServerSocket();
        cfg.Weight = weight;                       // 0 = 새 요청 수신 중단(in-flight 는 계속)
        return Results.Ok(new { weight = cfg.Weight, isDraining = cfg.Weight == 0 });
    });
```

drain/restore 라는 운영 어휘는 애플리케이션 admin 레이어에서 `Weight = 0`/`Weight = 100` set 에 이름을
붙여 노출하면 된다. (종류 무관 generic admin 툴이 필요하면, serving socket 을 종류에 상관없이 찾는
kind-agnostic 편의 getter 를 후속으로 더할 수 있다 — 1차 범위 밖.)

## 런타임 구현 정책

`.NET` 을 레퍼런스로 한다. 다른 언어는 같은 seam 을 자기 런타임 구조에 맞춰 미러링한다.

1. **옵션 인터페이스 추출 + 빌더 상속** — per-kind 빌더의 `ConfigureXxx()` getter 를
   `IZLinkClientServerChannelOptions`/`IZLinkRouteMeshChannelOptions`/`IZLinkDealerMeshChannelOptions`
   로 추출하고 각 빌더가 상속하게 한다. 상속이라 기존 호출부는 무변경. dealer mesh 의 `ConfigureSocket`
   갭은 이때 additive 로 메운다.
2. **`IZLinkSocketConfig.Weight` 추가** — build-time 초기값 + 런타임 mutable 로 쓰는 신규 속성.
3. **backend socket 계약에 weight setter/getter 추가** — `IZLinkBackendRouterSocket`/
   `IZLinkBackendDealerSocket`(`Runtime/Backend/Contracts/IZLinkBackendSocketContracts.cs`)에
   `SetPeerWeight(int)`/`int GetPeerWeight()` 를 추가하고, 각 wrapper(`ZLinkBackendRouterSocketWrapper`/
   `ZLinkBackendDealerSocketWrapper`)에서 native `Options.PeerWeight` 로 구현한다. `NativeInstance`
   캐스팅 대신 계약 메서드로 노출해 정보 은닉을 지킨다. 동시성은 live socket 모델을 준수한다(§목표 1).
4. **route mesh 노출** — `ZLinkRouteChannelRuntime._router` 가 private 이므로 `SetPeerWeight(int)`/
   `GetPeerWeight()` 메서드를 추가한다.
5. **채널이름 → per-kind 옵션 객체 resolver** — `ZLinkChannelRuntimeManager` 에
   `GetClientServerOptions(state, name)`/`GetRouteMeshOptions(...)`/`GetDealerMeshOptions(...)` 를
   추가한다. 기존 `GetMonitoringSocket` 의 capability resolution 패턴(server/client/route 매핑)을
   재사용해 종류·역할별 live socket 을 찾고, 그 socket 을 backing 으로 하는 옵션 인터페이스 구현을
   돌려준다. 소켓 타입 분기는 resolver 안에만 둔다. 미지 channel·종류 불일치·역할 부재·옵션-무효는
   단일 검증 지점에서 명확한 오류로 끝낸다.
6. **live-backed config 구현** — 런타임용 `IZLinkSocketConfig` 구현은 속성 read 를 live socket 에서
   읽고, runtime-mutable 속성(`Weight`) set 을 backend `SetPeerWeight` 로 즉시 반영하며, startup
   전용 속성 set 은 명확한 오류로 거부한다(§3.3 로스터).
7. **build-time 초기 weight 배선** — build-time `IZLinkSocketConfig.Weight` 를 socket 생성 시 native
   `PeerWeight` 초기값으로 흘려 넣는다. 미설정 시 기본 `100`.
8. **facade 경유 노출** — `ZLinkFrameworkChannelFacade` 에 forwarding 메서드를 추가한다.
9. **공개 서비스 등록** — `IZLinkChannelRuntimeOptions` 구현을 만들어 singleton `ZLinkFrameworkRuntime`
   에 의존하게 하고, `ZLinkFrameworkServiceRegistrar` 에서 `IZLinkChannelClient` 옆에 DI 등록한다.
10. **모니터링 정합** — config 객체의 `Weight`(및 `Weight == 0` draining 판정)를 기존
    `ZLinkSpotNodePeerEntry.Weight` 및 `PEER_WEIGHT_CHANGED` 이벤트와 의미를 맞춘다.

## 공개 API 불변 기준

| 표면 | 불변 기준 |
|------|-----------|
| channel builder enable | `EnableServer`/`EnableClient`/`EnablePublisher`/`EnableSubscriber` 등 enable 메서드는 불변. 런타임 옵션은 builder enable 이 아니라 별도 접근자로만 추가. |
| 빌더 `ConfigureXxx()` | per-kind 옵션 인터페이스로 추출하되 빌더가 상속하므로 **기존 호출부는 무변경**. 메서드 이름·시그니처 유지(추가는 dealer mesh `ConfigureSocket`, `Weight` 속성뿐). |
| handler 등록 | 변경 없음. |
| 기본 weight | weight 를 바꾸지 않으면 모든 serving socket weight 는 `100` 유지. |
| in-flight 처리 | drain 은 recv/dispatch/reply 를 멈추지 않는다. socket 을 닫지 않는다. |
| registry 등록 | drain 은 registry deregister 가 아니다. 노드는 topology 에 남되 weight `0` 으로 후보에서 빠진다. |
| 소켓 타입 은닉 | 공개 표면·config 객체·오류에 ROUTER/DEALER·native socket 이 새지 않는다. |
| build/runtime 대칭 | runtime impl 의 startup 전용 속성 set 만 오류로 다르고, 인터페이스·getter·속성은 build-time 과 동일. |

## 구현 단계

### Stage 0. 현재 동작 재현

각 언어에서 "serving 노드가 떠 있는 한 새 요청이 계속 그 노드로 들어온다"를 테스트로 재현해, 런타임
옵션/drain 표면이 없다는 현재 상태를 명확히 드러낸다. 이 테스트의 기대값은 목표 정책 구현 후 drain
동작 기준으로 바뀐다.

### Stage 1. 공통 모델 추가

- per-kind 빌더의 `ConfigureXxx()` 를 옵션 인터페이스로 추출하고 빌더가 상속하게 한다(호출부 무변경,
  dealer mesh `ConfigureSocket` 갭 메움).
- `IZLinkChannelRuntimeOptions`(per-kind getter)를 각 언어에 추가한다. 반환은 추출된 옵션 인터페이스
  (leaf 는 기존 config 타입 재사용, 평행 record 없음).
- `IZLinkSocketConfig` 에 `Weight`(build-time 초기값 + 런타임 mutable)를 추가한다.
- backend socket 계약에 `SetPeerWeight`/`GetPeerWeight` 를 추가한다(binding 신규 작업은 없음 — native
  `PeerWeight` 를 호출만).
- weight 범위(`0..100`) 검증과 미지 channel·종류 불일치·역할 부재·옵션-무효·startup 전용 set 오류를
  공통 의미로 고정한다.

### Stage 2. `.NET` 구현

런타임 구현 정책 1~10 을 `.NET` 에 적용한다. client-server·dealer mesh·route mesh 세 경로 모두에서
resolver 가 올바른 live socket 에 닿는지, build-time 초기 weight 가 반영되는지, build-time 호출이
상속으로 무변경인지 확인한다.

### Stage 3. Java/Kotlin 구현

`zlink-framework-core` 런타임에 같은 seam(옵션 인터페이스 추출·빌더 상속·접근자)을 적용하고, Spring
Boot starter 에서 `ZLinkChannelRuntimeOptions` bean 을 등록한다. Kotlin 은 Java 표면을 그대로 노출한다.

### Stage 4. Node/NestJS 구현

node 런타임 channel registry 에서 채널이름으로 serving socket 을 찾아 live-backed 옵션 객체를 돌려주고,
`peerWeight` 를 설정한다. NestJS provider 로 `ZLinkChannelRuntimeOptions` 을 주입 가능하게 한다.

### Stage 5. C++ 구현

`capability_builder_t` 기반 런타임에서 채널이름으로 serving socket 을 찾아 `peer_weight()` 를 설정하는
`channel_runtime_options_t` 를 host 표면에 추가한다. CTest label 은 기존 `framework-zlink-*` 체계에
맞춘다.

### Stage 6. Cross-language parity 점검

- 세 channel 종류 모두에서 drain 이 새 요청 수신을 멈추는가
- in-flight 요청이 drain 후에도 완료되는가
- `ConfigureServerSocket().Weight = 100`(또는 `ConfigureSocket()`)이 정상 복귀시키는가
- build-time 초기 weight 가 startup 에 반영되고, build-time 호출이 상속으로 무변경인가
- 미지 channel·종류 불일치·역할 부재·fanout·옵션-무효·범위 밖 weight·startup 전용 set 이 모두 명확한 오류인가
- 돌려받은 config 객체가 소켓 타입 없이 `Weight` 를 정확히 read 하는가
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
| DRAIN-003 | weight 복귀 | `ClientServerChannel(name).ConfigureServerSocket().Weight = 100` 후 그 노드가 다시 후보가 되어 request 를 받는다. |
| DRAIN-004 | dealer mesh drain | `DealerMeshChannel(name).ConfigureSocket().Weight = 0` 후 그 mesh 노드가 round-robin 후보에서 빠지고 나머지 노드가 받는다. |
| DRAIN-005 | route mesh drain | 그 노드를 target 으로 한 새 submit 은 `NOT_ADMITTED`, reply 경로는 계속 허용된다. |
| DRAIN-006 | 역할 없음 | client 전용 노드에서 server socket getter 호출 시 명확한 오류. |
| DRAIN-007 | 미지 channel 이름 | 명확한 오류. |
| DRAIN-008 | 종류 불일치 getter | route mesh 채널에 `ClientServerChannel(...)` 호출 시 명확한 오류. |
| DRAIN-009 | fanout drain | 거부(명확한 오류). |
| DRAIN-010 | `Weight` 범위 | `0..100` 적용, 범위 밖은 오류. |
| DRAIN-011 | weighted 분산 | weight `1..99` 에서 **이미 연결된 peer 의 LB 분배**가 비율에 따라 적게 선택된다(discovery 후보 게이팅은 이진이라 비대상). |
| DRAIN-012 | 옵션 객체 read | `ConfigureXxx()` 가 돌려준 객체가 `Weight`(및 `Weight==0` draining)를 정확히 read 하고 소켓 타입을 노출하지 않는다. |
| DRAIN-013 | startup 전용 set 거부 | 런타임에 `IPv6` 등 startup 전용 속성 set 시 명확한 오류. |
| DRAIN-014 | `PEER_WEIGHT_CHANGED` | drain/restore 후 모니터 이벤트가 관측된다. |
| DRAIN-015 | drain 후 graceful shutdown | drain → in-flight 완료 → 호스트 종료 순서가 깨지지 않는다. |
| DRAIN-016 | build-time 초기 weight | `ConfigureServerSocket().Weight = 30` 으로 뜬 노드가 startup 부터 weight `30` 으로 광고된다. |
| DRAIN-017 | build/runtime 대칭 | build-time `ConfigureServerSocket()` 호출이 상속 후에도 무변경으로 동작한다. |
| DRAIN-018 | 옵션-무효 (종류·역할) | client-server 의 `ConfigureClientSocket().Weight` set 시 명확한 오류(server 가 client 를 select 하지 않음). |

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
| `framework/doc/framework/common/spec/channel-availability.ko.md` (신규) | 런타임 옵션 접근자 모델(빌더 옵션 인터페이스 공유, read 전체/set subset), drain/peer-weight 의미, 이진 + weighted 모델, build-time 초기 weight, in-flight·reply 보장, 종류·역할 한정, 소켓 타입 은닉, 운영툴 연동 경계, 모니터링 정합. **이 문서가 정본 의미 정의.** |
| `framework/doc/framework/common/README.ko.md` | §2 spec 표·§3 scope 표에 새 spec 등록, nav 헤더 배선. |
| `framework/doc/framework/common/spec/channel-topology.ko.md` | 가용성 제어를 topology availability 로 교차 참조(§5 registry/monitoring 인근). |
| `framework/doc/framework/common/spec/framework-api.ko.md` | 언어별 `ChannelRuntimeOptions` + per-kind 옵션 인터페이스 surface 표 추가. |
| `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md` | **RL-B3 의 "public admin/drain 모드는 가정하지 않는다" 문장을 갱신**하고, drain 검증용 신규 RL-B 시나리오(client-server/dealer mesh/route mesh drain·restore·in-flight 완료)를 추가. |

### 언어별 문서 (공통 반영 후)

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/dotnet/guide/04-channel-messaging.ko.md` | §6 "연결 제어" 인근에 운영 drain 절 추가(현재 "실행 중 socket 직접 제어 핸들 없음" 서술을 런타임 옵션 접근자로 보완). |
| `framework/doc/framework/dotnet/guide/09-monitoring.ko.md` | 가용성 관측·`PEER_WEIGHT_CHANGED` 관측 예시. |
| `framework/doc/framework/dotnet/spec/aspnet-core-channel-messaging.ko.md` | `IZLinkChannelRuntimeOptions`·per-kind 옵션 인터페이스 계약·종류·역할 한정·오류 정책. |
| `framework/doc/framework/java/guide/04-channel-messaging.ko.md`·`spec/spring-boot-channel-messaging.ko.md`·`guide/09-monitoring.ko.md` | Java/Kotlin 표면·Spring bean 등록·모니터링. |
| `framework/doc/framework/kotlin/guide/04-channel-messaging.ko.md` | Kotlin idiom 노트(Java 표면 공유). |
| `framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md`·`spec/nestjs-monitoring.ko.md` | Node/NestJS provider 등록·모니터링. |
| `framework/doc/framework/cpp/spec/cpp-channel-messaging.ko.md` | C++ `channel_runtime_options_t` 계약. |
| 각 언어 `internals/regression-test-matrix.ko.md` | DRAIN 회귀 항목과 실제 테스트 이름. |

guide 에는 native socket·소켓 타입·weight 정수·frame 세부를 넣지 않는다. guide 는 운영자가
drain/restore 를 호출하고 가용성을 확인하는 방법만 설명한다. 내부 weight 전파·socket resolution
세부는 internals 또는 공통 spec 에 둔다.

## POSD 기반 리팩토링 단계

각 언어 구현이 끝난 뒤 바로 문서 반영으로 넘어가지 않고 POSD 기준으로 점검한다.

### Red flag 점검

| 위험 신호 | 확인 질문 |
|-----------|-----------|
| 얕은 모듈 | 런타임 접근자가 native socket option key·enum 을 노출하지 않고, 빌더 공유 옵션 인터페이스 + 의미가 문서화된 `Weight` 로 감싸는가 |
| 정보 누출 | native socket·`NativeInstance`·option enum·**소켓 타입(ROUTER/DEALER)** 이 공개 표면·config 객체·오류에 새지 않는가 |
| 특수/범용 혼합 | 종류·역할별 socket resolution 이 한 resolver 에 모이고 호출부에 흩어지지 않는가 |
| 패스스루 메서드 | facade→manager→backend 로 weight 만 넘기는 얕은 wrapper 가 불필요하게 층층이 생기지 않는가 |
| 오류를 정의로 제거 | 미지 channel·종류 불일치·역할 부재·옵션-무효·범위 밖 weight·startup 전용 set 을 단일 검증 지점에서 명확한 오류로 처리하는가 |
| 평행 어휘 발명 | build-time 옵션 인터페이스를 공유하는가, 아니면 같은 의미의 옵션 언어를 runtime 용으로 또 만들고 있지 않은가 |

### 설계 대안 재검토

| 선택지 | 장점 | 단점 |
|--------|------|------|
| **build-time 옵션 인터페이스 공유(per-kind 추출 + 빌더 상속 + 접근자 반환, +`Weight` 신설)** | 옵션 정의 한 곳, build/runtime 대칭, 소켓 타입 미노출, pubsub/spot 까지 자연 확장 | 빌더 계약 추출(상속이라 호출부 무변경) + live 배선 + read/set phase 분리 필요 |
| 평행 runtime 옵션 record(`SetWeight`+snapshot) | runtime 코드만 | 같은 의미의 표면을 두 벌 유지, build-time 과 어긋남 |
| 채널 종류별로 옵션 **필드 정의**를 복제 | 종류별 명시 | 같은 socket option 을 종류마다 재정의 → 중복 폭발 (※ 본안은 정의를 공유하고 getter 구조만 종류별이라 이와 다름) |
| 소켓 타입(ROUTER/DEALER)별 인터페이스 | 옵션 집합이 타입과 일치 | 사용자가 내부 소켓 모델을 알아야 함(추상화 붕괴), parity 깨짐 |
| drain 전용 서비스(`Drain`/`Restore`) | 운영 어휘 직관적 | baseline 기억이 재시작에 무효, weight 일반화 못 함, 평행 표면 |

1차 구현은 **build-time 옵션 인터페이스 공유 + `Weight` 신설**로 간다. runtime-mutable subset 을 넓히자는
요구가 실제로 확인되기 전까지 set 가능 속성을 `Weight` 외로 넓히지 않는다.

### 리팩토링 완료 조건

- per-kind 빌더가 옵션 인터페이스를 상속하고, runtime 접근자가 같은 인터페이스를 반환한다(정의 한 곳).
- weight setter 가 각 런타임에 흩어진 ad hoc 캐스팅이 아니라 backend 계약 메서드를 통한다.
- 종류·역할별 serving socket resolution 이 단일 resolver 에 모이고 소켓 타입 분기가 그 안에만 있다.
- 돌려받은 config 객체에는 framework 어휘 속성(`Weight` 포함, draining = `Weight==0`)만 있고 소켓
  타입·`NativeInstance`·transport 세부는 없다.
- 언어별 API 이름은 관례에 맞지만 `Weight` 의 의미(`0`=drain·`100`=정상)와 오류 정책은 같다.

## 완료 기준

이 계획은 아래 조건을 모두 만족해야 완료로 본다.

1. `.NET`, Java/Kotlin, Node/NestJS, C++ framework 에서 per-kind 빌더가 옵션 인터페이스를 상속하고,
   `IZLinkChannelRuntimeOptions` 류 접근자가 채널이름으로 같은 인터페이스를 돌려주며, 소켓 타입을
   노출하지 않는다.
2. client-server·dealer mesh·route mesh 세 종류에서 drain 이 새 요청 수신을 멈추고 in-flight 와 reply 는 유지된다.
3. runtime `ConfigureXxx().Weight` set 이 가용성을 drain(`0`)·정상(`100`)·weighted(`1..99`)로 조절하고,
   build-time `ConfigureXxx().Weight` 가 초기값을 정한다(같은 속성·같은 인터페이스).
4. 미지 channel·종류 불일치·역할 부재·fanout·옵션-무효·범위 밖 weight·startup 전용 set 이 모두 명확한 오류로 끝난다.
5. 가용성이 config 객체 read·모니터링 snapshot·`PEER_WEIGHT_CHANGED` 이벤트로 관측되고, 어디에도 소켓 타입이 없다.
6. DRAIN 회귀 테스트가 각 언어에 추가되고 언어별 regression matrix 에 연결된다.
7. POSD 점검과 필요한 리팩토링이 끝난 뒤 공통 spec(특히 신규 `channel-availability.ko.md`)과 언어별
   spec/guide/internals 문서, RL-B3 갱신이 구현 기준으로 반영된다.
8. Codex 에이전트가 이 계획 문서의 모든 항목이 적용되었다고 리뷰할 때까지 누락 수정과 재리뷰를 반복한다.
