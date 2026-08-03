# Framework 구현 갭 목록

[spec 확장 제안](framework-spec-extension-proposals.ko.md) 12건 반영에 따라, 네 언어
runtime이 무엇을 고쳐야 하는지 정리한 목록이다. 배경과 판정 기준은
[계획 문서 묶음](README.ko.md)에 있다.

## 요약

### 작업 묶음

같은 코드를 고치는 항목은 따로 진행하면 queue를 여러 번 다시 설계하게 된다. 아래 묶음
단위로 잡는다.

| 묶음 | 포함 항목 | 무엇을 고치는가 |
|---|---|---|
| **W1 scheduler** | A4 · A6 · A8 · A10 · B2 · D2 | `제출 → 두 축 예약 → lane 선택 → claim → 점유 상한 → handler → 회계 반납 → public 오류` 전 구간 |
| **W2-RM selector** | A1 RouteMesh · D1 | RouteMesh 채널 선택 절차와 tiebreak. **선행 과제 없이 즉시 진행한다** |
| **W2-CS selector** | A1 ClientServer · A1b | ClientServer 선택 절차. per-server 연결 구조는 유지한다(A1b 판정 완료) |
| **W3 relay 통지** | A2 · D5 | wire command 정의가 선행. 그 뒤 relay 성공 → 통지 → 캐시 무효화 |
| **W4 유휴 정리** | A3 | exact interface + Location Store CAS + admission seal + timer 구조 |
| **W5 관찰자** | A11 · A12 | source 모델 + 합치기 + terminal 보관 + 유실 counter + 전달 envelope |
| **W6 수신 공정성** | A9 | topology 전 경로 batch 상한과 회전 cursor |
| **W7 오류 ABI** | A5 · B4 | Java error kind enum을 공통 13-kind로, 무음 드롭 제거 |
| **독립** | A7(갭 없음) · B3 · B4b · B5 · B6 · C부 · D3 · D4 · D6 | — |

### 언어별 무게

| 언어 | 확정 갭 | 특히 큰 것 |
|---|---|---|
| C++ | A1(둘 다) · A2 · A3 · A4 · A6 · A8 · A9 · A10 · A11 · B2 · B3 · D1 · D2 | Channel DEALER fan 경로에 **효과 없는 선택 로직**이 남아 있다. 삭제하고 core `lb_t`를 고쳐야 한다 |
| .NET | A1(둘 다) · A2 · A3 · A4 · A6 · A8 · A10 · A11 · B4 · D2 · D3 · D4 | 관찰자 두 stream이 합치지 않고 버린다 |
| Java | A1(둘 다) · A2 · A3 · A4 · A5 · A6 · A8 · A10 · A11 · B6 · D2 · D4 | **error kind enum이 구형 40-kind 모델**이라 wire 의미가 다르다. Spot queue 사실상 무한 |
| Node | A1(RouteMesh) · A2 · A3 · A4 · A6 · A8 · A10 · A11 · B4b · B5 · D1 · D4 | Spot·Actor queue에 한도가 아예 없다 |

### 선행 과제 (구현 전에 정해야 하는 것)

| 무엇 | 막고 있는 항목 |
|---|---|
| relay 통지 wire command(ID·body·인증·fence) | W3 전체 |
| ~~`IdleEvicted` enum 수치와 timeout 설정을 네 exact interface에~~ | **해소** — `IdleEvicted=3`, `InstanceSpotIdleTimeout` 네 언어 반영 |
| ~~관찰자 전달 envelope(status + 유실 누계)를 네 exact interface에~~ | **해소** — `ZLinkObservedStatus<T>` + 두 누계 네 언어 반영 |
| 실행 대기열 기본 한도·작업당 고정 비용, exact interface의 "payload byte" 표현 교체 | W1 |
| 상한 값 6종(D6) — 측정 후 판정 | W1 · W6 |


---

## 읽는 법

- **A부**는 이번에 새로 들어간 spec 조항이 만든 갭이다. spec이 없던 동안 각 구현이
  임의로 정했던 동작이 이제 위반이 된 것이므로, 이전에는 갭이 아니었다.
- **B부**는 이번 개정 이전부터 있던 spec 위반이다.
- **C부**는 application이 관찰할 수 없는 구조 부채다.
- **D부**는 조항을 지키면서 성능을 잃지 않기 위한 구현 과제다.

각 항목에는 **확인한 `file:line`**을 붙였다. 확인하지 못한 자리는 `감사 필요`로 남겼으며,
`감사 필요`를 "갭 없음"으로 읽으면 안 된다.

경로는 다음을 기준으로 한다.

| 표기 | 실제 경로 |
|---|---|
| `cpp/…` | `framework/languages/cpp/framework/src/…` |
| `cpp-inc/…` | `framework/languages/cpp/framework/include/zlink/framework/…` |
| `dotnet/…` | `framework/languages/dotnet/src/Zlink.Framework/…` |
| `java/…` | `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/…` |
| `node/…` | `framework/languages/node/packages/framework/src/…` |
| `core/…` | `core/src/…` |

---

## A부. 새 spec 조항이 만든 갭

### A1. Target 선택 순서 — `08-channel-messaging` §선택 순서

조항은 후보별 누적값 3단계 절차와 **후보 식별자 오름차순 tiebreak**를 고정했다. 식별자는
topology마다 다르다 — RouteMesh는 NodeRid, ClientServer는 Server RID다.

**선택 경로는 언어마다 둘이다.** RouteMesh channel 선택과 ClientServer 선택이 별도
구조를 쓰므로 각각 따로 본다.

#### RouteMesh channel 선택

| 언어 | 현재 알고리즘 | 확인 위치 | 판정 |
|---|---|---|---|
| C++ | cursor를 weight 합으로 나눈 **연속 구간**에서 고른다. 후보 map key가 NodeRid라 tiebreak 키는 맞다 | `cpp/runtime/mesh/service_topology_registry.cpp:422-465` (`cursor++ % total_weight`) | **갭.** 절차가 누적값 방식이 아니다 |
| .NET | 서로소 step으로 weighted ring을 순회한다. 게다가 local target을 먼저 넣고 peer만 정렬하므로 **전체 후보가 NodeRid 순서가 아니다** | selector `dotnet/Runtime/Service/ZLinkManagedMeshNode.cs:6688-6701`, 정렬 `:6739-6759`, 절차 `dotnet/Runtime/Channels/ZLinkWeightedSelector.cs:16` | **갭 2건.** 절차와 정렬 |
| Java | cursor를 `weight 합` 범위에 넣고 **연속 구간**에서 고른다 | `java/runtime/internal/service/ZLinkServiceTopologyRegistry.java:224-243`, 후보 정렬 `:187-192` | **갭.** `A,A,A,B` burst |
| Node | 같은 방식(gcd로 줄인 연속 block) | `node/runtime/foundation/service-topology-registry.ts:333-354` | **갭.** 같은 결함 |

**네 언어 모두 갭이다.** Java·Node·C++은 후보를 NodeRid로 정렬하므로 절차만 누적값
방식으로 바꾸면 닫힌다. .NET은 정렬도 함께 고쳐야 한다.

> **C++의 DEALER fan은 fallback 경로이며, 정식 ClientServer는 framework가 고른다.**
>
> `cpp/runtime/client_server/client_server_location_runtime.cpp:699`가
> `bind_client_server_transport`로 sender·requester를 등록하고,
> `cpp/runtime/channels/channel_outbound_exchange.cpp:931`이 그 requester가 있으면 그쪽을
> 쓴다. 하나의 DEALER에 endpoint를 모두 연결하는 `channel_native_client_t`(`:1015`)는
> **requester가 없을 때만 쓰는 fallback**이다.
>
> 정식 경로는 `client_server_location_runtime.cpp:1316-1328`에서 framework가 고르고,
> 선택된 owner의 DEALER에는 endpoint가 하나뿐이다(`raw_client_server_owner.cpp:668`).
> **따라서 C++도 다른 세 언어와 같고, Core 수정으로 닫히는 framework 갭은 없다.**
>
> **fallback 경로의 죽은 코드는 삭제했다.** `connections_from_next()`가 winner를 계산해
> 목록을 회전시켰지만 받는 쪽이 집합에 넣어 순서를 지우고 있었다. 조사 결과
> `_auto_connections`도 죽은 경로여서 함께 지웠고, 호출처는 `list_manual_connections()`로
> 돌렸다. `_connection_version`은 남겼다.
>
> Core 쪽 계약 확장은
> [dealer 가중 선택 순서](dealer-weighted-selection-order.ko.md)가 소유하며,
> **W2의 선행 과제가 아니라 독립 제안이다.**

#### ClientServer 선택

| 언어 | 절차 | tiebreak 키 | 확인 위치 | 판정 |
|---|---|---|---|---|
| Node | 누적값 3단계 ✓ | `serverRoutingId` ✓ | `node/runtime/foundation/service-discovery-registry.ts:80-115` | **충족** |
| .NET | 누적값 3단계 ✓ | **없음** ✗ | `dotnet/Runtime/Channels/ZLinkClientServerClientRuntime.cs:430-451` | **갭.** `OrderByDescending(SelectionCurrent).First()`라 동점이면 열거 순서가 결정한다 |
| C++ | 누적값 3단계 ✓ | **연결 map key** ✗ | 절차 `cpp/runtime/client_server/weighted_selector.hpp:24-60`, 후보 구성 `cpp/runtime/client_server/client_server_location_runtime.cpp:1316-1327` | **갭.** key가 자동 연결은 `serverRid+lifecycleGeneration`, 수동 연결은 endpoint 기반이다 |
| Java | 연속 구간 cursor ✗ | Server RID ✓ | `java/runtime/channels/ZLinkChannelSocketRegistry.java:112-159` | **갭.** 절차 |

**세 언어의 ClientServer 수정은 작다.** C++은 후보에 Server RID를 별도 field로 실어
보내고 tiebreak가 그 값만 비교하게 한다. .NET은 `OrderByDescending`에 Server RID
오름차순 두 번째 키를 붙인다. Java는 절차를 누적값 방식으로 바꾼다.

### A1b. Framework가 Core의 연결 관리를 대신하는 자리

[6. 대상 선택과 위치 캐시 §4](../../framework/common/internals/06-routing-and-cache.ko.md)는
framework가 Core에 알리는 것을 **후보 endpoint와 weight까지**로 정한다. 언제 연결하고,
끊기면 언제 다시 붙이고, 어느 연결로 이 message를 보낼지는 Core의 몫이다.

#### 후보마다 socket을 하나씩 만들고 framework가 고른다 — 네 언어 전부

ClientServer client가 **server마다 DEALER를 하나씩** 만들고 그중 하나를 골라 보낸다.
socket 하나에 여러 server를 연결하고 Core가 고르게 하는 대신이다.

| 언어 | socket 생성 | 선택 |
|---|---|---|
| C++ | `cpp/runtime/client_server/raw_client_server_owner.cpp:668` — dealer마다 endpoint 하나 | `cpp/runtime/client_server/client_server_location_runtime.cpp:1316-1328` |
| .NET | `dotnet/Runtime/Channels/ZLinkClientServerClientRuntime.cs:393` `CreateDealerSocket` | `:430-451` |
| Java | `java/runtime/channels/ZLinkChannelSocketRegistry.java:196-204` — connection마다 dealer 보유 | `:134-159` |
| Node | `node/runtime/channels/client-server-location-runtime.ts:272` `openClientServerConnection` | `node/runtime/foundation/service-discovery-registry.ts:80-115` |

**현재 판정 — 위반이 아니다.** 판정 기준은 "대체 가능한가"가 아니라 다음이다.

> 하위 계층이 선택 시점에 eligibility 조건·weight·안정적 식별자를 모두 알고 강제할 수
> 있는가?

ClientServer server는 application 관점에서 대체 가능하지만, 선택에 필요한 조건이 framework
쪽에만 있다.

| 조건 | 근거 |
|---|---|
| ready·drain | `spec/08-channel-messaging.ko.md:112` |
| Server RID tiebreak | `spec/08-channel-messaging.ko.md:127` |
| descriptor와 연결의 identity·세대 일치 | `spec/09-client-server-channel.ko.md:190` |
| 수동 연결의 ChannelName·RID·세대·weight·drain·보안 검증 | `spec/09-client-server-channel.ko.md:223` |

C++은 이 정보를 admission record로 받은 뒤에야 ready를 확정한다
(`cpp/runtime/client_server/raw_client_server_owner.cpp:849,858,879`). 반면 하위 계층의
송신 표면은 대상 지정이 없다. **승인 상태를 하위 계층에 투영할 경로가 없으므로 지금
구조로 합칠 수 없다.**

**합치려면 다음 중 하나를 먼저 설계해야 한다.**

- RID별 승인·weight·활성 상태를 갱신하는 API
- framework가 고른 RID로 대상을 지정하는 송신 API

그 전에는 per-server 연결과 framework 선택기를 삭제하지 않는다. **A1의 ClientServer 선택
절차 결함은 그대로 유효하며, 각 언어에서 고친다.**

#### ~~연결 순서로 Core의 선택을 유도한다 — C++~~ (해소)

`connections_from_next()`가 winner를 계산해 endpoint 목록을 회전시켰지만 받는 쪽이 집합에
넣어 순서를 지웠다. Core는 연결 순서를 약속하지 않으므로 **효과가 없는 코드**였다.
선택·회전과 죽은 `_auto_connections`를 함께 삭제했다.

#### 위반이 아닌 자리 (참고)

다음은 후보 집합을 Core에 알리는 정상 경로다. 감사에서 제외한다.

| 자리 | 하는 일 |
|---|---|
| `cpp/runtime/locations/location_auto_connect_host_service.hpp:145,161` | 위치 저장소 변화에 따라 endpoint를 붙이고 뗀다 |
| `cpp/runtime/mesh/raw_mesh_node_owner.cpp:437` | mesh ROUTER에 peer endpoint를 연결한다. 전송은 RoutingId 지정이다 |
| `cpp/runtime/fanout/raw_fanout_owner.cpp:435` | subscriber가 publisher endpoint에 연결한다. 선택 자체가 없다 |
| `cpp/runtime/channels/channel_outbound_exchange.cpp:596,605` | 후보 endpoint 집합을 socket에 반영한다. 회전 시도만 빼면 정상이다 |

### A1c. 새 spec이 언어 표면에서 표현 불가능한 자리

개정한 조항 중 셋은 **현재 공개 표면으로 구현할 수 없다.** 구현 갭이 아니라 **조항을
다시 정해야 하는 자리**다.

#### codec 선택 입력을 "선언 type"으로 고정했는데 Node에는 그 정보가 없다

`06-framework-api.ko.md:452-458`이 codec 선택 입력을 **호출 지점에 선언된 message type**으로
고정했다. 그런데 Node 공개 API는 `message: unknown`만 받는다
(`node/contracts/Channels/IZLinkChannelClient.ts:4-5`). TypeScript의 선언 type은 runtime에
남지 않으므로 **전달할 방법이 없다.** 실제 Node 구현은 값 자체를 `canSerialize(value)`에
넣어 고른다(`node/runtime/messaging/payload-codec.ts:18-44,130-161`).

**동점 규칙도 어긋난다.** 조항은 "등록 순서가 늦은 것을 우선한다"인데 세 언어 모두
**모호하면 오류**를 낸다.

| 언어 | 위치 |
|---|---|
| Node | `node/runtime/messaging/payload-codec.ts:138-161` |
| .NET | `dotnet/Runtime/Codecs/ZLinkCodecRegistryBuilder.cs:107-144` |
| Java | `java/runtime/configuration/ZLinkCodecRegistration.java:172-186` |

**먼저 정할 것.** 네 언어가 모두 표현할 수 있는 선택 키를 정해야 한다 — Node에는 명시적
type token이나 안정적인 message contract ID가 필요하다. "선언 type"을 유지하려면 그 값을
전달하는 API를 네 exact interface에 먼저 추가한다. Node의 `canSerialize`를 공개 계약으로
올리는 것만으로는 닫히지 않는다 — 그것도 여전히 값 기반이다.

동점 규칙도 "늦은 등록 우선"과 "모호하면 오류" 중 하나로 통일한다.

> Node의 B4b(계약에 없는 `canSerialize`)는 이 항목의 일부다. 따로 닫지 않는다.

#### 관찰자 전달 envelope를 정했는데 Java 계약이 status만 반환한다

`24-runtime-monitoring.ko.md:244-265`가 전달 단위를 `{status, 유실 누계}` 쌍으로 정했다.
**해소되었다.** 네 언어 exact interface에 전달 envelope와 두 누계를 반영했다.

| 언어 | envelope | 유실 누계 타입 |
|---|---|---|
| C++ | `observed_status_t<TStatus>` (`spec/server/languages/cpp/interfaces/08-monitoring.ko.md:28`) | `std::uint64_t` × 2 |
| .NET | `ZLinkObservedStatus<TStatus>` (`.../dotnet/interfaces/10-topology-monitoring.ko.md:97`) | `ulong` × 2 |
| Java | `Flow.Publisher<ZLinkObservedStatus<T>>` (`.../java/interfaces/monitoring.ko.md:123`) | `long` × 2 |
| Node | `ZLinkObservedStatus<TStatus>` (`.../node/interfaces/03-location-observability.ko.md:210-211`) | `bigint` × 2 |

누계는 **둘로 나뉜다** — `coalescedCount`(합치기로 사라진 수)와
`discardedTerminalCount`(terminal 보관 상한 초과로 버린 수). 관찰자가 "따라잡기로 건너뛴
것"과 "영영 못 보는 것"을 구분할 수 있어야 하기 때문이다. 구독마다 0에서 시작하고 표현
범위를 넘으면 최댓값으로 고정한다.

> 이 문서 A11의 "status field 또는 별도 loss status" 대안 서술은 폐기한다. 공통 spec은
> 관찰자별 값을 status에 넣지 말라고 명시한다.

**W5 구현을 시작할 수 있다.**

#### idle eviction의 경쟁 조건이 정의되지 않았다

`11-spot-model.ko.md:430-450`이 callback → local teardown → owner record 삭제 순서를 정했다.
그러나 **새 call과 eviction이 경쟁할 때 어느 쪽이 이기는지** 정하지 않았다. A3의 선행
과제와 같은 내용이며, 네 언어 W4가 모두 여기에 걸린다.

### A2. Relay 통지로 위치 캐시 무효화 — `18-object-routing`

Message Follow relay가 message를 새 owner로 넘기면 원 송신 runtime에 통지해야 한다.

**Message Follow 자체는 네 언어 모두 있다**(B부의 삭제 항목 참조). 문제는 relay 성공을
송신측 캐시 무효화로 잇는 경로다.

**네 언어 모두 갭이다.** 어느 구현도 relay 성공을 송신측에 알리지 않는다.

| 언어 | 현재 | 확인 위치 |
|---|---|---|
| C++ | relay **실패** 때만 캐시를 지운다 | `cpp/runtime/actors/actor_client.cpp:591-609,746-756` |
| .NET | follow 제출 성공 후 회계만 정리하고 반환한다. client 캐시는 `TargetNotFound`·stale 오류에만 반응한다 | `dotnet/Runtime/Spots/ZLinkActorMessageFollower.cs:647-705`, `dotnet/Runtime/Actors/ZLinkActorClient.cs:77,137` |
| Java | target 제출 후 회계만 반납한다. 캐시 무효화도 stale 오류에만 연결되어 있다 | `java/runtime/actors/ZLinkActorTransferHandoff.java:174-201`, `java/runtime/actors/ZLinkActorSpotJoinCall.java:948`, `java/runtime/actors/ZLinkActorClientRuntime.java:90,143` |
| Node | 통지가 optional method 호출이고 **구현체가 없다** | `node/runtime/host/index.ts:404-410`, `node/runtime/actors/actor-transport-delivery-gate.ts:3-16` |

**선행 과제 — wire command가 없다.** 정본 wire protocol 표
(`framework/doc/framework/common/internals/service-wire-protocol.ko.md`)에 relay 통지에
대응하는 command가 없다. 응답을 기다리지 않는 호출에는 통지를 실을 자리가 없으므로 **새
비요청 record**가 필요하다. command ID와 body(객체 키, 낡은 owner, owner 세대 또는 store
version, relay 출처)를 schema에 추가해야 한다.

수신 쪽 조건도 정의해야 한다 — 캐시 항목이 이미 더 새로운 값으로 바뀌었으면 지우지 않는다.
중복 통지 처리와 인증·fence 조건도 함께 정한다. 검증은 언어가 섞인 relay와 one-way
message를 포함한 상호운용 test가 완료 조건이다.

각 언어마다 `relay 성공 → 송신측 통지 → 해당 route 캐시 무효화` 전 구간을 하나의 작업
단위로 잡는다.

### A3. `IdleEvicted` 종료 사유 — `11-spot-model` §6.2

Instance Spot이 유휴 기준을 넘겨 local instance를 내릴 때 호출하는 종료 사유다.

| 언어 | 확인 위치 | 판정 |
|---|---|---|
| C++ | `cpp-inc/contracts/spots/spot.hpp:101-106` | **갭** |
| .NET | `dotnet/Contracts/Spots/ZLinkSpot.cs:92-97` | **갭** |
| Java | `java/spots/ZLinkSpotCloseReason.java:3-6` | **갭** |
| Node | `node/contracts/Spots/ZLinkSpot.ts:19-23` | **갭** |

**enum 추가만으로 닫지 않는다.** 한 단위로 함께 구현해야 하는 것은 다음이다.

- `InstanceSpotIdleTimeout` 설정 API와 검증(기본값 `0` = 정리 안 함)
- 유휴 시간과 진행 중 작업 없음 **두 조건**을 함께 보는 판정 — application queue, timer
  queue, 대기 중 operation, relocation 참여를 모두 확인
- `IdleEvicted`로 `OnClosingAsync` 호출 → local instance 내림 → Location Store owner
  record 제거
- 정리 뒤 Instance intent call은 새 `ObjectGeneration`으로 cold activation, 일반
  message는 `NotFound`

**exact interface 반영은 끝났다.** `IdleEvicted`를 네 언어 모두 **`3`**으로 고정했다 —
기존 세 값이 `0·1·2`로 이미 계약 수치라 3만이 비파괴적이다.

| 언어 | enum | timeout 설정 |
|---|---|---|
| C++ | `idle_evicted = 3` (`.../cpp/interfaces/04-spots.ko.md:88`) | `set_instance_spot_idle_timeout(std::chrono::milliseconds)` |
| .NET | `IdleEvicted = 3` (`.../dotnet/interfaces/05-spots.ko.md:35`) | `SetInstanceSpotIdleTimeout(TimeSpan)` |
| Java | `IDLE_EVICTED(3)` (`.../java/interfaces/spots.ko.md:33`) | `setInstanceSpotIdleTimeout(Duration)` |
| Node | `IdleEvicted = 3` (`.../node/interfaces/04-spots.ko.md:40`) | `setInstanceSpotIdleTimeout(timeoutMs: number)` |

기본값 `0`(= 정리 안 함), 음수는 시작 시 설정 오류, MeshNode 시작 전 고정, 실행 중 setter
없음까지 네 언어에 동일하게 적었다. Kotlin은 Java 계약을 그대로 쓰므로 별도 반영이 없다.

**남은 것은 아래 경쟁 조건 정의다.** 그것이 정해져야 구현을 시작할 수 있다.

**정의가 더 필요한 자리.** 다음은 조항이 아직 답하지 않으며 구현 전에 정해야 한다.

- 마지막 활동과 새 call이 경쟁할 때 admission을 언제 닫는가
  (`Active → IdleEligible → Closing(IdleEvicted) → Deactivated` 전이와 seal 시점)
- owner record 제거가 조건부인가 — generation·fence 또는 CAS 조건이 필요한가
- `OnClosingAsync`가 실패하거나 시간을 넘기면 정리를 취소하는가, 그대로 진행하는가

**선행 과제 — Location Store에 봉인 전이가 없다.** 유휴 검사와 owner record `Delete`
사이에 들어온 call이 사라진다. 일반 direct call은 Ready owner를 확인한 뒤 local queue에
제출할 수 있기 때문이다(`18-object-routing`). 그런데 Location Store의 변경 목록에는
`Delete`만 있고 `Active → Closing` 전이가 없다(`21-location-runtime`). 현재 `StoreVersion`과
owner lease, queue turn을 묶어 **admission을 먼저 봉인하는 CAS 전이**를 정의하고, 봉인 뒤
재검사 → callback → `Delete` 순서와 그 사이 도착한 call의 오류 결과를 고정해야 한다.

**구현 비용 — timer 구조를 함께 정해야 한다.** Instance Spot마다 timer를 만들면 객체 수만큼
timer 자원이 늘고, 전체 map을 주기 scan하면 주기마다 `O(활성 Spot 수)`다.

**활동할 때마다 새 항목을 넣고 낡은 것을 만료까지 두면 안 된다.** 그러면 쌓이는 항목 수가
활성 Spot 수가 아니라 `활동률 × 유휴 기준 시간`에 비례한다. 자주 쓰이는 Spot 하나가
유휴 기준 시간 동안 수백만 항목을 만들 수 있다.

**Spot당 예약 항목을 최대 하나로 유지한다.** 색인이 있는 heap이나 침입형 timing wheel을
써서 활동 시 기존 항목을 옮긴다. 삭제 비용이 부담이면 wheel bucket이 바뀔 때만 새 항목을
넣고, 꺼낼 때 현재 deadline을 확인해 필요할 때 한 번만 다시 등록한다.

한 tick에서 검사하는 후보 수에 batch 상한을 둔다. 마지막 활동 시각은 scheduler가 갱신하는
coarse clock을 재사용할 수 있는지 측정한다. 검증에는 쌓인 항목 수 상한과 **활동이 잦은
Spot의 churn benchmark**를 포함한다.

### A4. 실행 대기열의 건수·byte 두 축 — `06-framework-api`

한도는 **건수와 byte 두 축을 모두 강제해야** 한다. 한 축만 두면 다른 축으로 우회할 수
있다. Byte 회계에는 작업당 고정 비용(envelope·metadata·queue node)이 포함되어야 하므로,
payload 크기만 더하는 구현도 갭이다.

**참조 구조는 이미 존재한다.** 두 언어의 두 도메인 mailbox가 두 축을 모두 강제한다.

```
cpp/runtime/mesh/service_mailbox.cpp:52-53
  if (_closed || target.messages >= target.message_budget
      || record_bytes > target.byte_budget - target.bytes)

node/runtime/foundation/service-mailbox.ts:89-93
  || target.messages >= target.messageBudget
  || bytes > target.byteBudget - target.bytes
```

문제는 **Spot·Actor 실행 queue**다.

| 언어 | 실행 queue 한도 | 확인 위치 |
|---|---|---|
| C++ | 건수만 | `cpp/runtime/execution/serial_execution_queue.cpp:296-317` |
| .NET | 건수만 | 기본값 `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:5`, 한도 검사 `:565-574` |
| Java | **사실상 무한.** Spot context가 기본 생성자를 쓰고 기본 capacity가 `Integer.MAX_VALUE`다 | 생성 `java/runtime/spots/ZLinkDefaultSpotContext.java:43`, `java/runtime/spots/ZLinkDefaultInstanceSpotContext.java:33`; 기본값 `java/execution/ZLinkAsyncSerialQueue.java:35-42` |
| Node | **한도 없음.** promise chain tail에 무한히 붙는다 | `node/runtime/spots/spot-serial-executor.ts:109-113`, `node/runtime/actors/actor-mailbox.ts:1-12` |

**exact interface 반영은 끝났다.** 네 계약 문서 모두
`payload 크기 + metadata 크기 + 작업당 고정 비용`으로 바꿨다
(`spec/server/languages/{cpp/03-channel-messaging,java/configuration-host,node/01-foundation-configuration,dotnet/03-configuration-topology}.ko.md`).

**남은 것은 기본값이다.** 작업당 고정 비용의 실제 값과 기본 건수·byte 한도가 정해지지
않아, 같은 입력·같은 한도에서 언어마다 포화 지점이 달라질 수 있다(D6).

**수정 방향.** 실행 queue를 `service_mailbox` 계열과 같은 두 축 구조로 맞추고, 건수와
byte를 **원자적으로** 예약·반납한다. Java와 Node는 한도 도입 자체가 선행 과제다.

> Java `ZLinkAsyncSerialQueue.java:148,160`은 `outstanding > pendingCapacity`라 한도보다
> 하나 더 받는다. `>=`로 고치되, production 경로가 `tryEnqueue`를 쓰도록 바꾸지 않으면
> 효과가 없다.

### A5. `CapacityExceeded` 오류 kind — `32-framework-error-model`

**Java의 public error kind enum이 공통 계약과 다른 모델이다.**

| 언어 | enum | 확인 위치 |
|---|---|---|
| C++ | 공통 13-kind, `capacity_exceeded = 6` | `cpp-inc/contracts/errors/error.hpp:12-26` |
| .NET | 공통 13-kind, `CapacityExceeded = 6` | `dotnet/../Zlink.Framework.Contracts/Errors/ZLinkFrameworkException.cs:34-48` |
| Node | 공통 13-kind, `CapacityExceeded = 6` | `node/contracts/Errors/ZLinkFrameworkException.ts:2-16` |
| Java | **40-kind 구형 모델.** `CAPACITY_EXCEEDED`가 없고 값 6은 `SPOT_TYPE_MISMATCH`다 | `java/errors/ZLinkFrameworkErrorKind.java:3-43` |

Java는 대신 `WORKER_QUEUE_FULL=17`, `PLACEMENT_CAPACITY_EXHAUSTED=30`을 쓴다.

> **wire 위험을 정정한다.** "수치가 wire를 넘어 다른 뜻이 된다"고 적었으나 **틀렸다.**
> Java의 error reply는 enum **이름 문자열**을 보내고 `valueOf()`로 읽는다
> (`java/runtime/messaging/ZLinkFrameworkErrorReply.java:19-29,48-60`). 숫자는 wire를
> 건너지 않는다.
>
> 실제 위험은 **이름 불일치**다. Java가 보내는 `WORKER_QUEUE_FULL`을 다른 언어는 모르고,
> 다른 언어가 보내는 `CapacityExceeded`를 Java는 `valueOf()`로 읽지 못한다.

**따라서 두 가지를 먼저 정해야 한다.**

| 결정 | 선택지 |
|---|---|
| wire 표현 | 공통 **숫자**로 바꿀 것인가, 공통 **이름**을 유지할 것인가 |
| 이행 | 구형 이름 alias를 얼마 동안 받아줄 것인가 |

**public Java enum 교체와 wire 형식 결정은 별개 작업이다.** 전자는 Java 사용자 코드를
깨고, 후자는 언어 간 상호운용을 바꾼다. 섞어서 진행하면 되돌리기 어렵다.

시작 전에 **네 언어의 error envelope encode·decode 표**를 만든다. 지금은 Java만 확인했다.

### A6. Owner 점유 시간 상한 — `06-framework-api`

한 owner가 scheduler를 연속으로 점유하는 시간에 상한이 있어야 한다.

| 언어 | 현재 | 확인 위치 |
|---|---|---|
| C++ | 작업 하나를 실행한 뒤 다음 drain을 다시 예약한다. owner별 경과 시간과 ready owner 공정성 제어는 없다 | `cpp/runtime/execution/serial_execution_queue.cpp:449,485-488,503-508,542-544` |
| .NET | `TryTakeNext`가 빌 때까지 drain한다 | `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:605-663` |
| Java | 한 entry가 끝나면 즉시 다음 entry를 잇는다 | `java/execution/ZLinkAsyncSerialQueue.java:208-223` |
| Node | owner별 promise tail을 계속 잇는다 | `node/runtime/spots/spot-serial-executor.ts:109-113` |

**갭 4/4.** 네 구현 모두 owner별 경과 시간을 재지 않고 ready owner 사이의 공정성 제어도
없다. 따라서 "건수 한도에 시간을 추가"가 아니라 다음을 새로 만들어야 한다.

- ready owner 집합
- claim 시작 시각 기록
- handler 경계에서 경과 시간 확인
- 상한 도달 시 남은 작업을 ready로 되돌리고 재등록

D6이 값을 정하기 전까지는 구조만 만들고 상한값은 설정으로 둔다.

### A7. 도착 기반 wakeup — `06-framework-api`

**갭 없음.** 네 언어 모두 queue 제출이 도착 즉시 drain을 예약한다.

| 언어 | 확인 위치 |
|---|---|
| C++ | `cpp/runtime/execution/serial_execution_queue.cpp:296-306,443-450` |
| .NET | `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:133-136,596-603` |
| Java | `java/execution/ZLinkAsyncSerialQueue.java:190-204` |
| Node | `node/runtime/spots/spot-serial-executor.ts:109-113` |

앞서 갭으로 기록했던 두 loop는 scheduler wakeup 경로가 아니다.

- `cpp/runtime/fanout/fanout_location_runtime.cpp:205-220` — descriptor reconcile과 fanout
  수신 pump
- `node/runtime/host/route-mesh-runtime.ts:505-508` — status 관찰용 placement observer

> 다음도 갭이 아니다. connect·bind 재시도와 accept 준비 확인이다:
> `cpp/runtime/channels/route_channel_runtime.cpp:433-437,451-466`,
> `dotnet/Runtime/Backend/DotNet/Wrappers/ZLinkBackendStreamSocketWrapper.cs:119-131`,
> `cpp/runtime/streams/stream_host_service.cpp:1467-1479`.

### A8. Spot 대기열 포화 시 동작 — `12-spot-messaging` §5.3

한도를 넘긴 제출은 **기다리지 않고** 실패해야 하며, 오류 kind는 같은 runtime이면
`CapacityExceeded`, 다른 node면 `Unavailable`이다.

| 언어 | 현재 | 확인 위치 | 판정 |
|---|---|---|---|
| C++ | Spot 제출은 nonblocking `try_post_async`를 쓰지만 full을 `Rejected`로 반환한다 | `cpp/runtime/spots/spot_runtime.cpp:924-935,2426-2429` | **갭.** 대기는 아니고 **오류 kind가 틀렸다** |
| .NET | full이면 `InvalidOperationException("…closed or full")` | queue `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:110-118`, Spot 호출 `dotnet/Runtime/Spots/ZLinkSpotSerialExecutor.cs:858-891` | **갭.** public `CapacityExceeded` 매핑이 없고 closed와 full을 구분할 수 없다 |
| Java | production Spot 경로가 `tryEnqueue`가 아니라 무제한 `enqueue`다 | `java/runtime/spots/ZLinkDefaultSpotContext.java:43,148,551-616` | **갭.** 포화 자체가 발생하지 않는다(A4) |
| Node | 한도가 없다 | `node/runtime/spots/spot-serial-executor.ts:79-113` | **갭.** A4가 닫힌 뒤 함께 판정 |

**control claim 예외 — 현재 상태는 제각각이다.**

| 언어 | 현재 |
|---|---|
| Java | `enqueueBarrierNext`가 capacity 검사를 우회하고 앞에 넣는다 (`java/execution/ZLinkAsyncSerialQueue.java:65-94`) |
| .NET | essential slot이 application capacity를 우회한다 (`dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:577-585`) |
| C++ | barrier가 앞쪽 제출을 쓰지만 **같은 capacity**에 걸린다 (`cpp/runtime/execution/serial_execution_queue.cpp:360-374`) |
| Node | barrier가 같은 무한 tail을 쓴다 (`node/runtime/spots/spot-serial-executor.ts:75-77`) |

조항이 요구하는 것은 **별도 한도를 가진 별도 lane**이다. 앞쪽 삽입도, capacity 공유도,
capacity 우회도 아니다([2. 직렬 실행 함정 3](../../framework/common/internals/02-serialization.ko.md)).

### A9. 수신 batch 공정성 — `29-transport-liveness`

한 peer가 수신 단계를 독점해서 다른 peer의 확인 신호가 밀리면 안 된다.

| 언어 | 현재 | 확인 위치 | 판정 |
|---|---|---|---|
| C++ | 한 subscriber를 `for(;;)`로 완전히 drain한다. 내부 connection 순회도 매번 처음부터 시작한다 | `cpp/runtime/fanout/fanout_location_runtime.cpp:298-340`, `cpp/runtime/fanout/raw_fanout_owner.cpp:266-333` | **갭** |
| Java | connection마다 최대 64건 | `java/runtime/channels/ZLinkFanoutLocationRuntime.java:49,393-404` | 충족 |
| .NET | subscriber별 loop가 한 record씩 처리 | `dotnet/Runtime/Channels/ZLinkChannelReceiveLoop.cs:227-251` | 충족 |
| Node | connection별 loop가 한 record 처리 후 event loop에 양보 | `node/runtime/channels/channel-receive-loops.ts:283-306` | 충족 |

**위 표는 fanout·channel 경로만 본 것이다.** `29`의 조항은 이제 RouteMesh, ClientServer,
service connection, STREAM을 포함한 **모든 multi-connection 수신 경로**에 적용되므로,
Java·.NET·Node의 `충족`은 **부분 확인**으로 내려야 한다. 예를 들어 Node의 STREAM은 별도
frame batch와 event loop 양보를 쓰므로(`node/runtime/streams/stream-session-runtime.ts:835-841`)
channel loop 판정으로 증명되지 않는다.

**감사를 `언어 × RouteMesh × ClientServer × service × STREAM × fanout` 행렬로 다시
한다.** framework loop뿐 아니라 core·binding의 `recv` 한 번이 내부에서 특정 connection을
완전히 drain하는지도 확인한다.

C++은 fanout 외에 **ClientServer도 확정 갭이다.** 각 server·client의 `pump_one()`을
데이터가 없어질 때까지 무제한 반복한 뒤에야 liveness tick과 다음 connection을 처리한다
(`cpp/runtime/client_server/client_server_location_runtime.cpp:947-948,966-967`). 연속 ingress에서
뒤쪽 connection과 확인 신호가 밀린다.

수정은 두 경로가 같다 — 각 drain에 건수·byte·경과 시간 상한을 적용하고 다음 실행은 회전
cursor에서 시작한다. 관측·control drain에도 같은 상한이 필요한지는 따로 측정한다.

### A10. Lifecycle 우선 실행 — `14-actor-model` §3

Lifecycle lane과 application lane이 함께 ready이면 lifecycle을 먼저 실행하되, owner 점유
상한이 함께 적용된다.

| 언어 | 현재 | 확인 위치 | 판정 |
|---|---|---|---|
| C++ | join barrier를 **앞쪽 삽입**으로 처리한다. 우선 실행은 되지만 시간 상한이 없다 | `cpp/runtime/execution/serial_execution_queue.cpp:360-374` | **갭.** lane 분리와 시간 상한 |
| Java | relocation barrier는 앞쪽, teardown은 뒤쪽 | `java/execution/ZLinkAsyncSerialQueue.java:65-124`, 호출 `java/runtime/actors/ZLinkActorDispatchSerials.java:115-162` | **갭.** 종류마다 다르게 처리한다 |
| .NET | 일반 dequeue는 FIFO이고, infrastructure 우선 탐색은 relocation seal 중에만 동작한다. `reservedPrioritySlots`는 기본값 `0`이고 0이 아닌 값을 넘기는 호출자가 없다 | `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:666-689,693-714,42,50-52` | **갭.** 상시 우선 실행이 없다 |
| Node | `postBarrierTurn`이 기존 promise tail 뒤에 붙는다 | `node/runtime/spots/spot-serial-executor.ts:75-77,109-113` | **갭.** 우선 실행이 아예 없다 |

**네 언어 모두 갭이며, 현재 구조가 전부 다르다.** 검증 기준은 하나다 — lifecycle이 ready면
application보다 먼저 선택되고, 시간 상한이 지나면 application turn이 실행된다.

### A11·A12. 관찰자 stream — `24-runtime-monitoring`

조항은 source별 최신 slot 합치기, terminal 비덮어쓰기, 누계 보존, 보관 상한 초과 시
가장 오래된 terminal 폐기와 drop counter 반영, 그리고 **stream 미종료**를 요구한다.

| 언어 | 현재 | 확인 위치 | 판정 |
|---|---|---|---|
| .NET | `DropOldest` | `dotnet/Runtime/Locations/ZLinkFanoutRuntimeService.cs:302-312` | **갭.** terminal이 먼저 사라진다 |
| .NET | `DropWrite` | `dotnet/Runtime/Channels/ZLinkClientServerRuntimeService.cs:136-151` | **갭.** 최신 `Sequence`가 전달되지 않는다 |
| C++ | full이면 oldest 폐기 | `cpp/runtime/client_server/client_server_location_runtime.cpp:217-268` | **갭** |
| Node | full이면 oldest 폐기 + **terminal을 넣은 뒤 stream을 닫는다** | drop `node/runtime/host/route-mesh-runtime.ts:670-679`, `close()` `:701`, 공용 queue drop `node/runtime/diagnostics/topology-runtime-projections.ts:327`, close `:342` | **갭 2건** |
| Java | 최신 중간 상태를 교체하고 preserve 항목을 남긴다 — 합치기 방향은 맞다. 그러나 **terminal 전달 뒤 `onComplete()`로 stream을 닫는다** | 합치기 `java/runtime/internal/monitoring/ZLinkStatusPublisher.java:103-135`, 종료 `:129-132`, 미적용 capacity `:21-34` | **갭 2건.** stream 종료와 무한 누적 |

**수정 방향.** 네 언어 모두 drop을 source별 합치기로 바꾸고, terminal 보관에 상한을 두고,
버린 횟수를 **관찰자별 누적 field**로 노출한다.

**source별 map을 관찰자마다 두면 memory가 `관찰자 수 × source 수`로 늘어난다.** 그래서
관찰자마다 status 객체를 복제하지 말고 불변 snapshot 하나를 공유하고 queue에는 참조와
순번만 넣는다. source lifecycle이 끝나면 최신 slot도 제거한다.

**관찰자 실행 구조도 함께 고쳐야 한다.**

| 위치 | 문제 |
|---|---|
| `java/runtime/internal/monitoring/ZLinkStatusPublisher.java:80` | 관찰자마다 virtual thread를 만든다 |
| `java/runtime/internal/monitoring/ZLinkStatusPublisher.java:103` | 수요와 무관하게 25ms마다 snapshot·fingerprint를 계산한다 — 관찰자당 초당 40회 |
| `cpp/runtime/client_server/client_server_location_runtime.cpp:230` | 관찰자마다 OS thread를 만든다 |

polling 기반 snapshot 생성을 없애고 상태 변경 시 publish하는 구조로 바꾼다. 관찰자별
thread 대신 공유 dispatcher와 수요 기반 queue를 쓴다.

**계약은 정해졌다.** 네 언어 exact interface에 전달 envelope와 두 누계가 들어갔다(A1c 참조).
유실 수는 status가 아니라 **전달 쌍**에 담는다 — status는 관찰자 사이에 공유하는 값이라
관찰자별 값을 넣을 수 없기 때문이다.

**남은 것은 구현이다.** 네 언어 public 반환형·callback 인자가 아직 raw status이고, 유실을
세는 코드가 없다.

| 언어 | public 표면 | 유실 회계 |
|---|---|---|
| C++ | raw callback (`cpp-inc/contracts/monitoring/framework_runtime.hpp:63`, `route_mesh_runtime.hpp:97`) | 없음 |
| .NET | `IAsyncEnumerable<TStatus>` (`dotnet/Contracts/Configuration/ZLinkDrainContracts.cs:117`) | overflow를 runtime metric에만 기록 |
| Java | `Flow.Publisher<ZLinkMeshNodeSnapshot>` (`java/monitoring/ZLinkRouteMeshRuntime.java:8`) | 없음 |
| Node | raw status (`node/contracts/RouteMesh/RuntimeTopology.ts:92`) | 없음 |

---

## B부. 이전부터 있던 spec 위반

### B2. 직렬 queue의 inline 실행 — C++

`cpp/runtime/execution/serial_execution_queue.cpp:129-146`. `resume_scheduler()`가 await
continuation을 queue에 다시 넣지 못하면 호출 스레드에서 그대로 실행한다. 이미 실행 중인
작업과 동시에 돌 수 있어 직렬성이 깨진다.

> **범위 주의.** `try_post_async`의 일반 실패 경로가 아니라 **await 재개** 경로에만
> 해당한다. 앞선 판정에서 이 범위를 잘못 잡았다.

**수정 방향.** queue가 닫혔거나 가득 차서 continuation을 재등록하지 못하면 inline 실행이
아니라 terminal failure로 완료한다.

### B3. `preparing → serving` 순서 위반 — C++

`13:261-262`는 peer 수락·handler·객체 runtime 준비를 마친 **뒤에** `serving`을 공개하도록
정한다. C++은 descriptor를 `serving`으로 게시한 뒤 `_port`와 completion control을 만든다
(`cpp/runtime/mesh/raw_mesh_node_owner.cpp:307-310`, 이어지는 port·control 준비 `:312-325`).

**수정 방향.** bind와 주소 확정 뒤에도 descriptor는 `preparing`으로 두고, port·handler·peer
admission 준비가 끝난 뒤 별도 단계에서 `serving`을 게시한다.

### B4. Completion table 무음 드롭 — .NET

`dotnet/Runtime/Backend/DotNet/ZLinkMeshCompletionTable.cs:58-73`. 4096개를 넘으면 parts를
dispose하고 그냥 반환한다. 기다리던 caller는 timeout으로만 알게 된다.

**수정 방향.** 해당 operation을 `CapacityExceeded`로 원자적으로 완료하고 overflow를
계측한다.

### B4b. 공개 계약에 없는 선택자를 runtime이 사용 — Node

Node의 공개 codec registry 계약은 `addSerializer(contentType, serializer)` 두 인자다
(`spec/server/languages/node/interfaces/01-foundation-configuration.ko.md`, 구현은
`node/contracts/Codecs/IZLinkCodecRegistryBuilder.ts:11`). 그런데 runtime은 계약에 없는
optional `canSerialize`를 타입 단언으로 읽고 검사한다(`node/runtime/messaging/payload-codec.ts:150,169`).

계약에 없는 값을 runtime이 읽으면 application이 그 동작에 의존하게 된다. 선택자를 공개
계약에 올리거나 runtime에서 제거해야 한다. .NET은 세 인자 overload로 이미 공개 계약에
있으므로 parity 판정이 필요하다.

### B5. 취소 판정을 오류 메시지 정규식으로 — Node

`node/runtime/execution/index.ts:365-370`이 오류 message를 `/aborted|cancelled|canceled/i`로
매칭한다. 오류 분류를 문자열에 의존하면 런타임 버전에 따라 판정이 달라진다.

**수정 방향.** typed abort/cancellation 오류와 `AbortSignal` 상태만 쓰고, 일반 오류
문자열은 분류 입력에서 제거한다.

### B6. `HANDLER_EXECUTOR`가 JVM 전역 static — Java

`java/execution/ZLinkAsyncSerialQueue.java:15-18`. 종료 경로가 없어 같은 JVM에 두 runtime이
있으면 실행 자원을 공유하고, 하나를 닫아도 스레드가 남는다.

**수정 방향.** runtime이 소유하는 executor를 생성자로 주입하고 runtime close가 수명을
소유한다.

### 삭제된 항목

앞선 판정이 코드와 어긋나 삭제했다.

- ~~**B1. Message Follow 미구현 — .NET·Java·Node**~~ — 네 언어 모두 구현되어 있다:
  .NET `dotnet/Runtime/Actors/ZLinkActorHandoffState.cs:900-993`, Java
  `java/runtime/actors/ZLinkActorSpotJoinCall.java:812-834`, Node
  `node/runtime/actors/actor-handoff.ts:582-630,719-773`, C++
  `cpp/runtime/spots/actor_transfer_coordinator.cpp:107`(route 설치·획득·만료)과
  `cpp/runtime/mesh/mesh_node_runtime.cpp:1831`(실제 relay). 실제 누락은 A2의 캐시 무효화
  연결이다.
- ~~**B7. Java `ZLinkServiceMailbox`·`ZLinkServiceLivenessRegistry` production 미배선**~~ —
  둘 다 배선되어 있다: 생성 `java/runtime/binding/ZLinkJavaRawMeshNode.java:195-232,600-612`,
  mailbox admission `:3417-3428`, drain `:5082-5116`, liveness tick `:4838-4847`. Java의
  A4는 배선 문제가 아니라 Spot·Actor queue의 admission 부재다.

---

## C부. 구조 부채 (관찰 불가)

application이 알아챌 수 없으므로 spec 위반은 아니다. 우선순위가 가장 낮다.

**C++**
- operation ID 3종(`host::operation_id_t`, `foundation::operation_id_t`,
  `uint64 request_seq`)과 exactly-once 3종(erase-under-mutex, atomic CAS, slot
  reservation)을 각각 하나로 통일
- `<zlink.hpp>`를 직접 include하는 **15개 파일** 정리(예 `cpp/runtime/stateful/public_host_runtime.hpp:12`,
  `cpp/runtime/spots/spot_runtime.cpp:31`,
  `cpp/runtime/client_server/client_server_location_runtime.cpp:13`,
  `cpp/runtime/mesh/raw_mesh_node_owner.cpp:7`). 헤더와 소스를 구분해 목록화한다
- Spot kind를 bool 플래그가 아니라 타입으로

**.NET**
- relocation 드라이버 3종을 단일 상태기계로
- `dotnet/Runtime/Service/ZLinkManagedMeshNode.cs`(현재 8,292줄) 분해. 줄 수가 아니라
  책임별 분해 후보를 기록한다
- `Runtime/Backend/Contracts`의 interface 26개 중 **구현이 하나뿐인 것**을 이름으로 나열해
  정리한다
- `ZLinkLegacyMonitoringModels` 정리
- `ZLinkSerialExecutionQueue`의 `reservedPrioritySlots` — A10에서 우선 실행으로 바꾸거나
  쓰지 않으면 제거

**Java**
- `ZLinkOneWayCalls` 중복 **5곳**(channels, binding, streams, spots, actors)
- `runtime/messaging/`의 **세 파일**이 `package …runtime.host;`를 선언 —
  `ZLinkAdmissionRuntime.java:1`, `ZLinkOneWayAdmissionStatus.java:1`,
  `ZLinkOneWayAdmission.java:1`
- 빈 디렉터리 **3개** — `systems/zlink/contracts/service`, `runtime/internal/binding`,
  `runtime/service`

**Node**
- 직렬 실행 원시 타입 4종 통합([9. Session과 Actor 연결
  §2](../../framework/common/internals/09-session-binding.ko.md))

> **삭제된 항목.** ~~커밋된 `.hprof` 2개~~ — `git ls-files '*.hprof'` 결과가 0이다.

---

## D부. 성능 리뷰에서 나온 수정 항목

조항 위반이라기보다 **조항을 지키면서 성능을 잃지 않으려면 무엇을 해야 하는가**에
해당한다.

### D1. 선택 절차를 호출마다 수행하면 send가 O(N)이 된다

`08-channel-messaging`의 선택 절차를 글자 그대로 구현하면 후보 수에 비례하는 비용이
모든 send에 붙는다.

| 위치 | 문제 |
|---|---|
| `cpp/runtime/client_server/weighted_selector.hpp:29` | 호출마다 후보 `std::map`을 새로 만들고 전체 credit을 갱신 |

| `node/runtime/foundation/service-discovery-registry.ts:80` | 호출마다 filter·sort·`Set` 생성 후 전체 누적값 갱신 |

**수정 방향.** 절차가 결정적이므로 **같은 누적값 상태가 다시 나타나는 지점**까지가 한
바퀴다. 후보 목록이 바뀔 때 도입부와 주기를 나눠 계산해 두면 호출은 배열 읽기와 cursor
증가로 끝나며 결과 순서는 완전히 같다.

> **두 가지를 단정하면 안 된다.**
>
> `weight 합 ÷ 최대공약수`는 누적값이 전부 0인 상태에서만 주기다. 그리고 **시작 상태
> 복귀를 기다리면 안 된다** — 도입부를 지나 주기에 들어가면 시작 상태는 다시 나오지 않는다.
>
> 같은 weight의 A·B·C에서 A를 고른 뒤 B를 빼면 `(-2,1) → (-1,0) → (0,-1) → (-1,0)`이다.
> 주기는 `(-1,0) → (0,-1)`이고 `(-2,1)`은 도입부다. 시작 상태 복귀를 조건으로 삼으면
> 영영 찾지 못하고 매번 상한까지 헛돌다 fallback한다.

이미 지나온 상태를 기록해 두고 반복을 탐지한다(visited map, 또는 메모리를 아끼려면
Floyd·Brent 방식). 탐색에는 **걸음 수와 시간 두 상한**을 두고, 넘기면 절차 수행 방식으로
되돌린다. 탐색은 후보 변경 경로에서만 하고 send 경로에서 하지 않는다.

후보 배열·정렬·집합 생성은 모두 후보 변경 시점으로 옮긴다. 후보 교체와 cursor 증가의
순서도 함께 정의한다 — 단일 cursor를 여러 스레드가 만지면 그 동기화가 send 경로에 남는다.

### D2. 수신 byte 회계가 모든 handler를 하나의 lock으로 직렬화한다

**세 언어가 같은 구조다.** 수신 확인, handler 시작, 완료, snapshot이 모두 같은 lock을
잡으므로 message마다 process 전역 lock을 두세 번 통과한다. 코어 수를 늘려도 처리량이
늘지 않는다.

| 언어 | 확인 위치 |
|---|---|
| .NET | `dotnet/Runtime/Dispatch/ZLinkInboundDispatchBudget.cs:64,93,99` |
| C++ | `cpp/runtime/dispatch/inbound_dispatch_budget.hpp:32,42,53,62,69` |
| Java | `java/runtime/internal/dispatch/ZLinkInboundDispatchBudget.java:71,81,102,119,133` |

Node는 event loop 하나이므로 해당하지 않는다. **불필요한 atomic을 강제하지 않는다.**

**수정 방향.** HWM permit 획득·반환과 관측용 누계를 분리한다.

> **worker별 덩어리 예약은 쓰지 않는다.** 쓰지 않은 worker local 잔량을 pending으로 세면
> HWM보다 일찍 수신을 멈추고, 세지 않으면 여러 worker가 동시에 잔량을 소비해 초과량이
> 다시 커진다. D3의 message별 선예약과도 어긋난다.

- HWM 판정용 `pendingBytes`는 process 단위 atomic counter 하나로 둔다.
- 관측용 누계(수신량·완료량)만 cache-line 분리한 shard counter로 나누고 snapshot 시점에
  합산한다.
- pause·resume lock은 message마다가 아니라 **HWM 경계를 지날 때만** 잡는다.
- `HandlerStarted`가 HWM 판정에 필요 없다면 별도 metric counter로 분리해 hot path의 세
  번째 전역 동기화를 없앤다.

### D3. HWM은 동시 수신 수만큼 초과할 수 있다

`06-framework-api`는 HWM 아래에서 시작한 message를 끝까지 받도록 정한다. 여러 connection이
HWM 직전에 동시에 수신을 시작하면 실제 상한은 `HWM + 동시 수신 수 × 최대 message 크기`에
가까워진다. `dotnet/Runtime/Dispatch/ZLinkInboundDispatchBudget.cs:72`도 message 완성 후
현재 합계만 검사한다.

**현재 binding 경계에서는 선예약이 불가능하다.** `.NET` 수신 loop은 용량을 확인한 뒤
message 전체를 받고 **그 다음에야** 크기를 잰다
(`dotnet/Runtime/Channels/ZLinkChannelReceiveLoop.cs:38,41,56`). `Recv` 전에 실제 길이를
알 방법이 없다. 최대 message 크기를 매번 예약하면 작은 message 위주 처리량이 크게 떨어지고,
`HWM < 최대 message 크기`일 때 한 건은 허용한다는 계약과도 따로 처리해야 한다.

**수정 방향은 둘 중 하나를 고르는 것이다.**

| 방향 | 필요한 것 | 대가 |
|---|---|---|
| 엄밀한 상한 | binding에 길이 peek 또는 수신 예약 API 추가 | binding 계약 변경 |
| 계약 유지 | `HWM + 동시 수신 수 × 최대 message 크기` 공식을 spec에 명시하고 동시 수신 수를 제한 | 상한이 느슨해진다 |

payload 크기 분포별 benchmark로 비교한 뒤 고른다.

### D4. 불변 payload 타입이 접근할 때마다 복사한다

| 위치 | 복사 지점 |
|---|---|
| `java/ZLinkEncodedPayload.java:13,18` | 생성 시 한 번, `bytes()` 접근 시 또 한 번 |
| `java/messaging/ZLinkMessage.java:31,50` | 생성과 decode에서 위 복사를 다시 수행 |
| `node/contracts/Common/ZLinkEncodedPayload.ts:4,12` | 생성과 `data()`에서 각각 복사 |
| `dotnet/Runtime/Messaging/ZLinkMessageRuntime.cs:82` | envelope에서 payload 복사 |
| `dotnet/Runtime/Execution/ZLinkSerialExecutionQueue.cs:257` | queue 수락 시 이동 기록용 배열 생성 |

**수정 방향.** 공개 API의 불변성은 유지하되 runtime 내부 소유권 이전에는 복사하지 않는
경로를 따로 둔다. `.NET`의 이동 기록은 봉인 이후에만 만든다
([11. Payload 소유권과 복사 §3](../../framework/common/internals/11-message-ownership.ko.md)).

**Node는 payload를 두 번 parse한다.** `isJsonPayload()`가 `JSON.parse`를 수행하고
(`node/runtime/messaging/payload-codec.ts:173`), 성공하면 같은 message를 다시 parse한다
(`:79-80`). buffer 복사가 아니라 CPU와 객체 생성 비용이며, internals가 금지한 "형식 판별을
위한 이중 해석"에 정확히 해당한다. wire의 content-type으로 codec을 골라 본문 sniffing을
없앤다. 불가피하게 검사해야 한다면 첫 parse 결과를 handler 입력으로 재사용한다.

회계 대상은 buffer 복사·view·객체 생성만이 아니다. **parse 횟수와 turn 객체 할당**도 함께
센다.

### D5. Relay 통지가 message마다 발생하면 record가 두 배가 된다

`18-object-routing`의 relay 통지를 relay마다 보내면 이동 직후 트래픽이 몰리는 객체에서
통지 record가 업무 message만큼 늘어난다.

**이 항목은 설계 후보이며 A2의 wire schema가 정해진 뒤 판정한다.** command ID와 body,
인증·fence, 응답에 실을 수 있는 조건이 확정되기 전에는 중복 억제 구조를 고를 수 없다.
아래는 후보다([6. 대상 선택과 위치 캐시 §2](../../framework/common/internals/06-routing-and-cache.ko.md)와
같은 내용이다).

**후보.** `(보낸 runtime, 객체, owner 세대)`마다 처음 한 번만 보내고, 전송 중인
같은 통지는 합친다. 응답이 있는 호출은 응답에 실어 별도 record를 만들지 않는다. 통지가
유실되어도 캐시 수명이 끝나면 만료되므로 재전송을 보장하지 않는다.

**중복 억제 표식에 수명을 함께 정해야 한다.** 별도 전역 집합을 만들면 이동한 객체와 owner
세대 수만큼 상태가 계속 늘어난다. "통지가 유실돼도 캐시가 만료된다"는 것이 표식이 저절로
사라진다는 뜻은 아니다. 표식은 **기존 route 캐시 항목 안에** 넣는다.

표식은 세 상태를 갖는다. **전송 완료로 지우면 안 된다** — 다음 업무 message가 곧바로 새
통지를 만들어 message당 통지 문제가 그대로 재현된다.

| 상태 | 전이 |
|---|---|
| `idle` | 첫 relay에서 `in-flight`로 |
| `in-flight` | 전송 성공 시 `notified`로. 이 동안 도착한 추가 통지는 합친다 |
| `notified` | **캐시 항목이 무효화되거나 만료될 때만** 사라진다 |

전송 실패는 재전송을 보장하지 않는 계약에 맞춰 `notified`로 두거나, 재시도를 택하면
`idle`로 되돌린다. 어느 쪽인지 계약에서 정한다. 전송 중 합치기는 대기자 수만 유지하고
그 수에도 상한을 둔다.

### D6. Owner 점유 상한과 수신 batch 한도에 값이 없다

`06-framework-api`와 `29-transport-liveness`는 "상한이 있다"만 정하고 값·단위·기본값을
정하지 않는다. 1µs 구현과 100ms 구현이 모두 문구를 만족하지만 하나는 시계 읽기와 재예약
비용이 지배하고 다른 하나는 tail latency를 해친다.

**값이 없는 조항은 이것만이 아니다.** 다음 일곱 개가 모두 값·단위·범위·기본값 없이
"상한이 있다"만 정하고 있다.

| 조항 | 정해야 할 것 |
|---|---|
| owner 점유 상한 (`06`, `14` §3) | 단조 시계 기준 기본값과 허용 범위 |
| lifecycle 연속 실행 상한 (`14` §3) | lifecycle lane을 연속으로 고르는 turn 수의 기본값. 시계가 아니라 counter이므로 turn 경계 비용이 counter 하나여야 한다 |
| 수신 batch 상한 (`29`) | `건수 / byte / 경과 시간` 중 먼저 도달하는 조건 |
| control lane 한도 (`12` §5.3) | 건수·byte 기본값 |
| terminal status 보관 상한 (`24`) | 관찰자당 보관 개수 |
| 작업당 최소 회계 비용 (`06`) | byte 값과 언어별 산정 방식 |
| mailbox 두 축 한도 (`06`) | host 기본값과 검증 범위, 공개 설정 여부 |

포화 시 public 오류가 달라지므로 순수 internal 재량으로 둘 수 없다. 각 값에 기본값, 단위,
허용 범위, overflow 처리 방식을 고정하고, 공개 설정이라면 언어별 exact interface에도
추가한다. 값을 정하려면 측정이 선행되므로 이 항목은 **측정 후 판정**이다. A6와
`14-actor-model` §3의 starvation 보장이 이 값에 걸려 있다.

---

## 감사 필요 (구현 갭과 분리)

1·2차의 감사 항목 다섯 건은 모두 답이 나와 각 절에 반영했다. **현재 남은 감사 항목은
하나다.**

- **A9 범위 확대.** `29`의 수신 공정성이 Classic fanout 한정이 아니라 모든
  multi-connection 수신 경로에 적용되므로, `언어 × RouteMesh × ClientServer × service ×
  STREAM × fanout` 행렬로 다시 감사해야 한다. 지금 표는 fanout·channel 경로만 본 것이다.

---

[내부 구조 목차](../../framework/common/internals/README.ko.md) ·
[계획 문서 묶음](README.ko.md) ·
[spec 확장 제안](framework-spec-extension-proposals.ko.md)
