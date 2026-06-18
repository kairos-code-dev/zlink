# Framework API 패리티 — 코드 수정 추적

> 상태: **수집 중**. 작성 2026-06-15. 짝 문서: [framework-guide-cpp-dotnet-alignment](framework-guide-cpp-dotnet-alignment.ko.md).
> 목적: cpp ↔ dotnet (이후 node·java 등) framework 가이드를 정렬하는 과정에서 드러난
> **언어 간 framework API 차이**를 두 부류로 가른다. 가이드는 이 분류에 따라 쓴다.

## 분류 원칙

cross-language framework **능력(capability)** 은 모든 언어가 동등하게 제공해야 한다.
단, 그 능력을 **노출하는 방식**은 언어 특성을 따른다.

- **(A) 언어 특성 차이** — 언어가 가진/못 가진 기능(리플렉션·어트리뷰트, 멤버함수
  포인터 템플릿, async 모델, 명명 규칙)에서 자연히 갈리는 부분.
  → **코드 수정 대상 아님.** 가이드는 각 언어 idiom 대로 **다르게** 쓴다.
- **(B) 능력 갭** — 언어 특성과 무관하게 한쪽 framework 에만 없거나, 같은 능력인데
  표면(이름·역할 선언)이 어긋난 부분.
  → **코드 수정 대상.** 아래 §2 에 기록하고, 가이드는 **target 표면**으로 맞춘다.

## 1. 언어 특성 차이 (코드 수정 대상 아님 — 가이드는 다르게 쓴다)

| 주제 | cpp / node | dotnet | 근거 |
|------|-----------|--------|------|
| 메시지·핸들러 등록 | **수동 등록만** (`options.handlers().add<T>(...)`, `add_actor_packet<&T::method>()`) | **수동 + 어트리뷰트 기반 자동 등록** (`[ZLinkHandlerGroup]`/`[ZLinkRequest]` + scan) | cpp/node 는 어노테이션·리플렉션이 없어 attribute scan 불가. dotnet 은 reflection 으로 자동 등록 지원 |
| SPOT 핸들러 작성 | spot **클래스의 멤버 메서드** (`add_actor_packet<&T::method>()` — 멤버함수 포인터 템플릿) | spot 에 바인딩된 **별도 핸들러 class** (`IZLinkSpotRequestHandler<TSpot,…>` + `Context.AddPacket<THandler>()`) | cpp 의 멤버함수 포인터 non-type 템플릿 인자를 dotnet 제네릭으로 그대로 옮길 수 없음 |
| 명명·실행 모델 | `snake_case`, `co_await`, `task_t/result_t` | `PascalCase`, `async/await`, `ValueTask<T>` | 언어 관례 |
| 구성 패키징 | `module_t` + `add_zlink_framework<TModule>()` | ASP.NET Core DI / `IHostedService` 분산 | 호스팅 고유 — 플랜 §4 비대상 |

> 위 항목은 03-concepts §6.1 등에서 **언어별로 다르게** 서술하는 것이 정답이다.
> "한쪽에 맞춰 통일"하면 안 된다.

## 2. 능력 갭 (코드 수정 대상 — 가이드는 target 으로 맞춤)

### 확정 모델 — capability vs socket role (2026-06-15)

두 층을 구분한다.

- **core socket role** = `DEALER` / `ROUTER` / `PUB` / `SUB` (스펙
  [auto-connect-channel-types](../spec/draft/auto-connect-channel-types.ko.md) 기준).
  채널 종류가 소켓 타입을 고정한다.
- **framework capability** = **server**(제공: bind + handler 로 inbound 처리) /
  **client**(소비: connect/route + outbound 호출).

**"DEALER = client 전용"은 client-server channel 에서만이다.** mesh 에서는 한 소켓이
server 이면서 client 다 — inbound 를 handler 로 받고(server) 동시에 outbound 로 다른
peer 에 routing(client). 그래서 capability→socket 매핑이 채널마다 다르다:

| 채널 | EnableServer (제공) | EnableClient (소비) | 한 노드 둘 다 |
|------|--------------------|--------------------|:---:|
| client-server | ROUTER 소켓 | DEALER 소켓 | 보통 분리 |
| **dealer mesh** | **DEALER** (bind+handler) | **DEALER** (connect) | **예** |
| **route mesh** | **ROUTER** (bind+handler) | **ROUTER** (connect/route) | **예** |
| fanout | PUB | SUB | 보통 분리 |

핵심: mesh 의 server/client capability 는 **둘 다 같은 소켓 타입**(dealer mesh=DEALER,
route mesh=ROUTER)에 매핑된다. server 라고 ROUTER 가 되지 않으므로 스펙의
"DEALER_MESH 는 DEALER role only" 와 충돌하지 않는다.

### 코드 수정 항목

| # | 항목 | cpp 현재 | dotnet 현재 | target | 상태 |
|---|------|---------|------------|--------|------|
| G1 | dealer mesh `EnableServer`/`EnableClient` | ~~`bind()`/`connect()`~~ → **`enable_server`/`enable_client(())`/`enable_client(ep)` 추가** | ~~`EnableClient` 만~~ → **`EnableServer` 추가됨** | 양쪽 mesh 빌더가 server(bind+handler)/client(connect) capability 노출, 둘 다 **DEALER** 소켓 | dotnet ✅ / cpp ✅ (Codex+리뷰) |
| G2 | route mesh `EnableServer`/`EnableClient` | ~~`bind()`/`set_routing_id()`/`connect()`~~ → **`enable_server`/`enable_client`/`enable_client(ep)` 추가** | ~~`Bind()`/...~~ → **`EnableServer`/`EnableClient` 추가** | server/client capability 로 정리, 둘 다 **ROUTER** 소켓 | dotnet ✅ / cpp ✅ (Codex+리뷰) |

> G2 리뷰 완료(cpp): route mesh `enable_client()`(무인자)가 discovery 를 잘못 주장하던 결함을
> Codex 수정 → `discovery_backed_capabilities` 등록 제거(리뷰 통과). route mesh client 의 정상
> 경로는 `enable_client(endpoint)`=connect. **G1·G2 코드 트랙 전부 완료.**
| G3 | dealer mesh validator: bind=peer path | (cpp 는 `dealer_mesh_channels_with_peer_path` 로 이미 bind 인정) | ~~`Client` 면 peer source 강제~~ → **bind 면 server 전용 노드 허용** | bind(제공)한 dealer mesh 노드는 discovery/connect 없이 유효 | dotnet ✅ |
| G4 | request 기본 timeout | framework 기본값 없음 → 소켓 native 기본(현재 **~5초**) | 전역 `options.DefaultTimeout` = **30초** | **차이 확인됨**(계획 §6 플래그) — dotnet 은 framework-level 30s, cpp 은 native ~5s | 🔎 정렬 여부 결정(cpp 에도 framework DefaultTimeout 도입할지) |
| G5 | cpp spot timer handler 호출 | `add_timer<THandler>` 가 `typeid(THandler)` 만 등록(handle 미wiring), `dispatch_fire_count` 는 test-only — production tick→user handler 경로 미발견 | (dotnet `IZLinkSpotTimerHandler<TSpot>` 는 실제 호출) | **검증 필요** — cpp timer handler 가 실제로 호출되는지/직렬화되는지. 문서는 현 코드(타입 태그, 비직렬)에 맞춤 | 🔎 코드 확인 |
| G6 | registry heartbeat 기본값 | `registry.hpp:64` — interval **1s** / timeout **3s** / broadcast **1s** | `ZLinkRegistryRegistration` — interval **5s** / timeout **15s** / broadcast **30s** | **차이 확인됨** — cpp 1s/3s vs dotnet 5s/15s. 문서는 각 언어 실제값으로 맞춤(02-getting-started 양쪽 정정) | 🔎 정렬 여부 결정 |

### dotnet 구현 메모 (G1·G3, 2026-06-15)

- `IZLinkDealerMeshChannelBuilder.EnableServer(Action<IChannelServerCapabilityBuilder>)` 추가.
  dealer mesh 의 server·client 는 **같은 DEALER 소켓 공유** → `EnableServer` 는 그 DEALER
  (`registration.Client`)의 bind 를 설정한다. ROUTER 를 만드는 `registration.Server` 는 쓰지
  않으므로 **런타임 변경 불필요**(기존 `CreateClientBundle` + `RunDealerMeshLoopAsync` 가 이미
  bind+connect+dispatch 양방향 처리).
- validator: dealer mesh 에서 bind 를 valid peer path 로 인정(cpp 동작과 일치).
- 테스트: `HandlerExposure` 의 server 전용/server+client 동시 노드 2건 + 기존 dealer mesh + E2E 통과.
- **남은 코드 트랙**: G1 cpp(dealer_mesh 빌더에 enable_server/enable_client 정렬), G2 route mesh(양 언어).

> G1/G2/G3 메모: dotnet `CreateServerBundle` 은 ROUTER 를 만든다(client-server 전용).
> **dealer mesh 의 server 는 binding DEALER** 여야 하므로, mesh server 는 그 경로가 아니라
> DEALER(현재 `CreateClientBundle` 의 bind 경로)로 빌드해야 한다. route mesh server 는
> binding ROUTER. 즉 capability→socket 매핑이 `AutoConnectType` 에 따라 갈린다.

## 진행 규칙

1. 가이드 정렬 중 차이가 보이면 먼저 **(A)냐 (B)냐** 판정한다.
2. (A) 면 가이드에 언어별로 쓰고 끝. (B) 면 여기 §2 에 한 줄 추가하고, 가이드는
   target 표면으로(또는 양쪽 실제 API + 개념 동일 서술로) 맞춘다.
3. §2 항목은 **문서 트랙이 아니라 코드 트랙** — 별도 승인/구현으로 처리한다.
