# 가이드 집필 중 발견한 구현·샘플 갭

> 조사 기준: 2026-07-31 working tree
>
> 언어별 가이드를 쓰면서 **spec에는 있는데 구현에 없거나, 문서가 가리키는 샘플이 없거나,
> 빌드가 깨져 있는** 자리를 모은다. 구현을 바꾸는 문서가 아니라 담당자에게 넘기는
> handoff 자료다. 진행 상태는 [통합 execution ledger](route-mesh-11.0.0-execution-ledger.ko.md)가 소유한다.
>
> **계약이 기준이다.** spec에 있고 구현에 없으면 구현이 따라가야 하는 쪽이다
> ([framework 공개 계약 spec 권위](../../framework/common/spec/README.ko.md)). 가이드는
> spec대로 쓰고 이 문서에 갭으로 적는다.

## 이 문서에 적는 기준

| 적는다 | 적지 않는다 |
| --- | --- |
| spec이 선언했는데 구현에 없는 public 표면 | 구현에만 있고 spec에 없는 것(별도 판단이 필요하다) |
| 문서·가이드가 가리키는데 존재하지 않는 샘플 | 아직 계획 단계인 기능 |
| 빌드나 게이트가 깨져 있어 검증이 막힌 자리 | 성능·품질 개선 제안 |

각 항목은 **직접 확인한 근거**를 함께 적는다. 확인하지 않은 것은 §4 조사 단서에 둔다.

## 1. spec에 있고 구현에 없는 표면

> **G1 · G2 · G3 · G4 · G5 · G6 · G8은 처리를 마쳤다.** 각 항목의 처리 내용은 §6에 있다.
> 아래 서술은 발견 당시 기록이므로 현재 구현 상태는 §6이 기준이다. 남은 것은 **G7 ·
> G9**와 §2 이후다.

### G1 · C++ `enable_actor_dispatch()`

| | |
| --- | --- |
| 언어 | C++ |
| 선언 | [STREAM session 공개 계약](../../framework/common/spec/server/languages/cpp/interfaces/06-stream-session.ko.md) `stream_node_options_builder_t &enable_actor_dispatch();` |
| 구현 | `framework/languages/cpp/framework/include`에 없다 |
| 확인 | 해당 트리 전체 grep에서 spec 문서 두 곳만 나온다 |

STREAM session이 Actor로 relay하려면 이 표면이 필요하다. Java · Node에는 대응이 있고
(`enableActorDispatch(...)`), C++만 없다. 가이드
[9. STREAM](../../framework/common/guide/server/09-stream.ko.md)의 C++ 탭과
[16. Options](../../framework/cpp/guide/server/16-options.ko.md) §6이 spec대로 적어 두었다.

### G2 · Java `ZLinkActorManager.findSpot(String)`

| | |
| --- | --- |
| 언어 | Java(및 표면을 공유하는 Kotlin) |
| 선언 | [Java actors 공개 계약](../../framework/common/spec/server/languages/java/interfaces/actors.ko.md) `CompletionStage<Optional<SpotRef>> findSpot(String spotId);` |
| 구현 | `framework/languages/java` 전체(.java)에 없다 |
| 확인 | build·bin 제외 grep에서 spec 문서만 나온다 |

Actor가 지금 어느 Spot에 속해 있는지 조회하는 표면이다. `find`는 있고 `findSpot`만 없다.
가이드 [7. Actor와 Spot](../../framework/common/guide/server/07-actor-spot.ko.md) §3의
Java · Kotlin 탭이 spec대로 적어 두었다.

### G3 · Java inbound dispatch 옵션 전체

| | |
| --- | --- |
| 언어 | Java(및 Kotlin) |
| 선언 | [Java configuration과 host 공개 계약](../../framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md) `configureInboundDispatch()`와 그 아래 `applicationHwmBytes` · `applicationHwmProfile` · `processMemoryLimitBytes` setter·getter |
| 구현 | `configureInboundDispatch`가 없다 |
| 확인 | build·bin 제외 grep에서 spec 문서만 나온다 |

host 전체 Application HWM을 설정하는 자리다. C++ · Node에는
`configure_inbound_dispatch()` · `configureInboundDispatch()`가 있다.

> 이 항목은 [4. Backpressure §6](../../framework/common/guide/server/04-backpressure.ko.md#6-framework에-아직-적용되지-않은-부분)이
> 밝힌 "계약만 확정되고 runtime이 아직 쓰지 않는" 범위와 겹친다. 다만 **표면 자체가
> 없다**는 점은 다른 언어와 갈린다.

### G4 · 같은 개념에 언어별로 다른 public 타입 이름

가이드를 네 언어로 쓰면서 **같은 개념의 public 타입 이름이 갈리는 자리** 넷을 만났다.
fanout subscriber endpoint 호출(`connect`로 통일함)과 같은 부류다. 이번에는 임의로
통일하지 않고 각 언어의 실제 이름을 그대로 문서에 썼다 — 어느 쪽으로 맞출지는 계약
판단이 필요하다.

| 뜻 | .NET | Java · Kotlin | Node | C++ |
| --- | --- | --- | --- | --- |
| Spot join admission 결과 | `ZLinkSpotActorJoinResult` | `ZLinkSpotActorJoinResponse` | `ZLinkSpotActorJoinResponse` | `spot_actor_join_response_t` |
| session dispatch context | `ZLinkSessionDispatchContext` | `ZLinkSessionMessageContext` | `ZLinkSessionMessageContext` | `session_message_context_t` |
| filter 다음 단계 | `ZLinkHandlerFilterNext` | `ZLinkHandlerFilterNext` | `ZLinkHandlerDelegate` | `handler_filter_next_t` |

세 자리 모두 **.NET만 다르고** 나머지가 같다. `Result` ↔ `Response`, `Dispatch` ↔
`Message`처럼 뜻이 겹치는 단어라 읽는 쪽에서 같은 개념인지 바로 알기 어렵다.

> 처음에 fanout 수신 handler도 갈리는 줄 알고 넣었다가 뺐다. 세 언어 모두
> `ZLinkFanoutHandler`가 맞고, Node의 `zlinkPublishHandler`는 그 계약을 등록하는
> **데코레이터 이름**이라 층이 다르다.

확인 방법은 `doc/site/scripts/check_guide_identifiers.py`다. 탭 코드의 타입 이름을 그
언어의 실제 표면과 대조하므로, 한 언어 이름을 다른 언어 탭에 잘못 쓰면 걸린다.

### G5 · Java `ZLinkMeshNodeSocketConfig`가 spec과 다르다

| | |
| --- | --- |
| 언어 | Java(및 Kotlin) |
| 선언 | [Java configuration과 host 공개 계약](../../framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md) — HWM 넷이 `long`, `mailboxMessageBudget` · `mailboxByteBudget` 있음 |
| 구현 | `sendHighWaterMark` · `receiveHighWaterMark`가 `int`, mailbox 둘은 interface에 없음 |
| 확인 | `ZLinkMeshNodeSocketConfig.java`와 spec의 signature 목록 대조 |

차이가 둘이다.

- **타입이 좁다.** spec은 `long`인데 구현은 `int`다. HWM은 byte 단위이므로 `int`로는
  2 GiB를 넘길 수 없다.
- **mailbox 두 값이 public 표면에 없다.** 내부(`ZLinkJavaRawMeshNode`)에는
  `mailboxMessageBudget` 필드가 있고 기본값 4096이지만 `ZLinkMeshNodeSocketConfig`로
  노출되지 않는다.

### G6 · owner lease 기본값이 세 언어에서 모두 다르다

| 언어 | 갱신 주기 | TTL | 배수 | 근거 |
| --- | --- | --- | --- | --- |
| C++ | 5초 | 15초 | 3배 | `contracts/locations/options.hpp` |
| Java · Kotlin | 5초 | **30초** | 6배 | `ZLinkLocationOptions.java` |
| Node | **10초** | 15초 | **1.5배** | `contracts/Locations/Options.ts` |

세 언어가 각각 다르다. 특히 **Node는 배수가 1.5배로 가장 빡빡하다** — 갱신 한 번을
놓치면 lease가 거의 만료된다. 같은 mesh에 여러 언어 node가 섞이면 같은 장애 상황에서
owner 판정이 언어마다 달라진다.

기본값은 계약 문서가 명시하지 않은 자리다. 어느 값으로 맞출지, 아니면 언어별로 달라도
되는지 판단이 필요하다. 맞춘다면 배수도 함께 정하는 것이 낫다 — 갱신 실패 몇 번까지
견딜지가 그 배수다.

### G7 · runtime metric 구현이 언어마다 크게 갈린다

[Runtime metric과 집계 규칙](../../framework/common/spec/25-runtime-metrics.ko.md)이 계기
47개를 정의한다(계기 둘을 뺀 경위는 G8이다). **spec에 있는데 어느 구현에도 없는 이름은 없다** — 문제는 반대다.
구현 쪽이 언어마다 크게 다르고, 일부는 spec에 없는 이름으로 방출한다.

| 언어 | spec 계기 중 방출하는 수 |
| --- | --- |
| `.NET` | 58개(spec 전량 + 확장) |
| Java · Kotlin | **14개** |
| C++ | **4개** |
| Node | 재확인 필요(아래 단서) |

Java가 방출하는 이름 중 spec에 없는 것이 넷이다. 앞의 셋은 **spec에 같은 개념이 다른
이름으로 이미 있다.**

| Java가 쓰는 이름 | spec의 이름 |
| --- | --- |
| `zlink.channel.request.duration` | `zlink.mesh_node.request.duration` |
| `zlink.channel.request.inflight` | `zlink.mesh_node.requests.inflight` |
| `zlink.channel.request.timeouts` | `zlink.mesh_node.request.timeouts` |
| `zlink.actor.transfer.duration` · `zlink.actor.transfer.pending_requests.count` | spec에 대응 없음 |

Java 구현 트리에서 `zlink.mesh_node`로 시작하는 계기는 **하나도 방출하지 않는다.**
대시보드를 언어별로 따로 만들어야 하고, 같은 mesh에 여러 언어 node가 섞이면 같은 지표가
두 이름으로 흩어진다.

확인 방법은 각 언어의 계기 등록 호출을 grep하는 것이다 — `.NET`은
`CreateCounter`·`CreateHistogram` 계열, Java는 `ZLinkRuntimeMetrics.add`·`record`다.

### G8 · spec에서 뺀 계기 둘을 구현이 아직 방출한다

§1의 다른 항목과 방향이 반대다. **구현이 먼저 있고 spec이 나중에 지운 자리**이므로
"별도 판단이 필요한 구현 전용 표면"이 아니라 이미 판단이 끝난 정리 대상이다.

| | |
| --- | --- |
| 대상 | `zlink.relocation.recovered` · `zlink.relocation.journal.messages` |
| 판단 | [Runtime metric과 집계 규칙](../../framework/common/spec/25-runtime-metrics.ko.md)에서 삭제했다 |
| 근거 | 전자는 `zlink.relocation.completed{outcome=recovered}`의 부분집합이다(`ZLinkRuntimeMetrics.cs:869-870`이 `outcome == Recovered`일 때만 더한다). 후자는 envelope 크기를 `zlink.relocation.bytes`가 이미 담당하고, message 한 건 단위 기록은 spec §1이 [26-message-flow-tracing](../../framework/common/spec/26-message-flow-tracing.ko.md)에 넘긴 범위다 |

`outcome` 허용값의 `recovered`는 남겼다. 삭제한 counter가 파생되던 원본이라 이것까지
빼면 정보가 사라진다.

두 이름이 남아 있는 자리는 다음과 같다. **양쪽 contract test가 계기 이름 전체 목록을
하드코딩해서 단언하므로 구현만 고치고 테스트를 두면 바로 깨진다.**

| 대상 | 위치 |
| --- | --- |
| 공통 가이드 표 | `framework/doc/framework/common/guide/server/12-operations.ko.md:101-102` |
| .NET 구현 | `ZLinkRuntimeMetrics.cs:107,109`(등록) · `:869-870`(recovered 기록) · journal 기록 지점 |
| .NET contract test | `RuntimeMetricsTests.cs:52,53,379,444,457` |
| Node 구현 | `runtime-metrics.ts:77,98`(등록) · `:357,374`(기록) |
| Node contract test | `runtime-metrics.test.js:80-81` |

ObservabilityOps E2E에는 두 이름이 없다. C++ · Java는 애초에 방출하지 않아 손댈 곳이 없다.

> G7의 "Node 0개"는 다시 세야 한다. 이번에 확인한 `runtime-metrics.ts`는 계기 이름
> 44개를 선언하고 그중 최소 12개를 실제로 기록한다(`this.count(...)` · `this.histogram(...)`
> 호출 기준). G7 표는 다른 스캔 결과이므로 여기서 고치지 않고 재확인 대상으로만 남긴다.

### G9 · Node `ZLinkMeshNodeSocketConfig`에 mailbox 두 값이 없다

| | |
| --- | --- |
| 언어 | Node |
| 선언 | [Node.js foundation과 configuration 공개 계약](../../framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md) — `mailboxMessageBudget` · `mailboxByteBudget` |
| 구현 | `contracts/Configuration/Builders.ts`의 `ZLinkMeshNodeSocketConfig`에 없다 |
| 확인 | 해당 interface 선언 대조. 런타임 내부(`service-mailbox`)에는 개념이 있다 |

Java의 [G5](#g5--java-zlinkmeshnodesocketconfig가-spec과-다르다)와 같은 부류다 —
**두 언어가 같은 두 값을 public 표면에서 빠뜨렸다.** C++에는 있다
(`mesh_node_socket_config_t`의 `mailbox_message_budget` · `mailbox_byte_budget`).

### G10 · C++ `add_subscribe`가 ChannelName을 받지 않는다

| | |
| --- | --- |
| 언어 | C++ |
| 선언 | [C++ Spot 공개 계약](../../framework/common/spec/server/languages/cpp/interfaces/04-spots.ko.md) — `add_subscribe (std::string channel_name, std::string topic)` |
| 구현 | `spot.hpp`의 `add_subscribe (std::string topic)` — topic 하나만 받는다 |
| 확인 | 선언 대조와 샘플 호출부(`tictactoe_entry_spot.hpp:38`, `bingo_room_spot.hpp:81`) 모두 인자 하나다 |

[SPOT 메시징 §5.1](../../framework/common/spec/12-spot-messaging.ko.md)이 subscription
등록 값을 **`ChannelName` · `topic` · packet name 셋**으로 정의한다. `.NET`
(`AddSubscribe<THandler>(channelName, topic)`)과 Node
(`addSubscribe(handlerType, channelName, topic)`)는 그대로 받는다.

**C++만 ChannelName 범위를 지정할 수 없다.** 같은 topic을 여러 Channel에서 쓰면 C++ Spot이
어느 범위의 event를 받을지 구분하지 못한다. §5.1이 "등록한 Spot이 해당 ChannelName에
참여하지 않으면 host를 시작할 수 없다"고 정한 시작 검사도 지금 C++에서는 성립하지 않는다.

가이드는 계약대로 두 인자를 적어 두었다.

## 2. 문서가 가리키는데 없는 샘플

### S1 · ZoneWorld — C++ · Java · Kotlin

| | |
| --- | --- |
| 있는 곳 | `.NET`(`framework/languages/dotnet/samples/ZoneWorld`) · Node(`framework/languages/node/samples/ZoneWorld`) |
| 없는 곳 | C++ · Java · Kotlin |
| 영향 | 공통 가이드 [14. 샘플 고르기](../../framework/common/guide/server/14-samples.ko.md)가 일곱 샘플을 모든 언어의 공통 자산으로 소개한다 |

런북 §6이 "모든 샘플을 모든 언어에" 목표로 잡았고 ZoneWorld만 셋이 빈다.

## 3. 빌드·게이트가 막혀 있는 자리

### B1 · C++ binding 샘플 타깃 전체가 빌드되지 않는다

| | |
| --- | --- |
| 대상 | `bindings/cpp` 샘플 전체(`-DZLINK_CPP_BUILD_SAMPLES=ON`) |
| 증상 | `samples/sample_common.hpp`가 `zlink::service::mesh_node_t`를 참조하는데 그 타입이 `bindings/cpp/include`에 없다 |
| 확인 | 공식 인자(`run_samples.sh`와 같은 configure)로 빌드해도 같다. 기존 샘플(`dealer_router_recv_sample`)도 같은 이유로 실패한다 |

v11 작업에서 Service contracts가 binding include에서 빠지면서 남은 참조로 보인다.
설치 산출물(`.artifacts/.../include/zlink/Contracts/Service/mesh_node.hpp`)에는 있다.

이 때문에 core 가이드용으로 새로 추가한 `bindings/cpp/samples/request_reply_async_sample.cpp`도
함께 막혀 있다. 그 파일 자체는 축소한 헤더로 컴파일·링크·실행을 확인했다.

### B2 · core 문서 사이트가 v11에서 삭제된 서비스 계층 장을 아직 낸다

| | |
| --- | --- |
| 대상 | `doc/site/docs/guide/{07-0-services,07-3-spot,07-4-actor}.{ko.,}md` · `doc/site/docs/internals/services-internals.{ko.,}md` |
| 증상 | 정본(`core/doc/`)에서 지운 문서의 사본이 사이트 미러에 남아 nav에도 걸려 있다 |
| 확인 | `git show 05a7061bad6 --name-status -- core/doc/guide/07-3-spot.ko.md` → `D`. v11이 raw core와 framework를 가르면서 서비스 계층 장을 core에서 뺐다 |

삭제된 그 경로를 가리키는 링크가 저장소에 **16개** 남아 있다.

| 어디 | 몇 개 |
| --- | --- |
| `doc/README.ko.md` · `doc/README.md` (core 문서 색인) | 7 |
| `bindings/doc/guide/{dotnet,java,node,python,rust}/index.ko.md` | 9 |

고치려면 **서비스 계층 서술을 v11 이후 어디로 보낼지부터 정해야 한다** — framework 정본으로
옮길지, core에 축소판으로 되살릴지, 사이트에서 내릴지. 링크만 다른 데로 돌리면 삭제된 층을
계속 내보내게 된다.

`doc/site/scripts/check_doc_links.py`가 이 16개를 계속 보고한다. 나머지 링크 결함
(`repo-doc` · `core-doc` · `bindings-doc` · `framework` · `framework-plan` 다섯 트리)은
이번에 정리했다.

### B3 · core 가이드가 인용하는 `:doc` 마커가 바인딩 샘플에 없다

| | |
| --- | --- |
| 대상 | `doc/site/docs/guide/06-monitoring.{ko.,}md`가 인용하는 9개 언어 `monitor_recv_sample` |
| 증상 | `--8<-- "…:doc"`의 `doc` 마커가 어느 샘플에도 없어 코드 블록 18개가 **빈 채로** 렌더된다 |
| 확인 | `python3 doc/site/scripts/check_doc_tabs.py core` |

pymdownx snippets는 없는 section을 조용히 빈 문자열로 만든다. mkdocs도 경고하지 않아
그동안 드러나지 않았다. 고치려면 9개 샘플에 `--8<-- [start:doc]` · `[end:doc]`를
심어야 한다 — 바인딩 샘플 쪽 작업이라 이 lane에서 하지 않았다.

## 4. 조사 단서 (미확인)

기계 스캔으로 나온 후보다. **확인 전에는 갭으로 취급하지 않는다** — 스캔이 예제 코드와
설명용 선언까지 함께 잡아 오탐이 섞여 있다(예: Node `addTimer`는 실제로는 구현에 있다).

| 언어 | 후보 | 나온 곳 |
| --- | --- | --- |
| C++ | `set_application_version` · `set_maintenance_wave` · `configure_network` · `set_max_pending` | `02-configuration-host.ko.md` |
| C++ | `allow_session_to_actor` · `allow_actor_to_session` | `02-configuration-host.ko.md` |
| C++ | `min_threads` · `max_threads` · `idle_timeout` · `max_queue_length` | `01-common-runtime.ko.md` |
| C++ | `local_address` · `remote_address` · `routing_id`(session) | `06-stream-session.ko.md` |
| C++ | `redis_location_store_t` · `redis_relocation_store_t` | `07-location-store.ko.md` |
| Java | `operationTimeout` · `setOperationTimeout` | `location-maintenance.ko.md` |
| Java | `effectiveTargetApplicationVersion` | `common-runtime.ko.md` |
| Java | `mailboxByteBudget` · `setMailboxByteBudget` | `configuration-host.ko.md` |
| Node | `ownerLeaseRenewIntervalMs` · `ownerLeaseFencingMarginMs` | `07-nestjs-host.ko.md` |
| Node | NestJS host의 spot·actor 등록 표면 | `07-nestjs-host.ko.md` |

확인 방법은 §1의 각 항목과 같다 — 그 언어 구현 트리에서 build·bin·node_modules를 뺀
grep으로 선언이 실재하는지 본다. 확인되면 §1로 올리고 근거를 적는다.

## 5. 이미 처리한 것

| 항목 | 처리 |
| --- | --- |
| fanout subscriber endpoint 호출 이름이 네 언어에서 갈림 | `connect(endpoint)`로 통일. 선언·구현·호출부·spec·가이드·API 스냅샷을 함께 고쳤다(커밋 `3c6530c5ff`) |
| core 가이드 `03-3-dealer`가 없는 샘플 넷을 인용 | python·node·rust는 실제 파일 이름으로 정정, C++ 샘플은 신규 작성(커밋 `4a1bf6dbf0`) |
| C++ timer handler 예제가 등록 규약과 어긋남 | 별도 handler 타입 형태로 정정(커밋 `1e58c9…` 계열) |

## 6. 2026-07-31 처리 기록

기준은 spec이고, 이름과 기본값이 갈리는 자리는 `.NET` 구현을 정본으로 맞춘다.

### G6 · owner lease TTL 기본값 — 처리 완료

`ZLinkLocationOptions`의 기본값을 네 언어에서 전수 대조했다. `.NET`과 C++은 13개 값이
모두 같았고 Java와 Node.js만 `ownerLeaseTtl`이 달랐다.

| 값 | .NET | C++ | Java(변경 전 → 후) | Node(변경 전 → 후) |
| --- | --- | --- | --- | --- |
| `ownerLeaseRenewInterval` | 5s | 5s | 5s | 5s |
| `ownerLeaseTtl` | **15s** | 15s | **30s → 15s** | **30s → 15s** |
| `pollingInterval` | 1s | 1s | 1s | 1s |
| `storeFailureGrace` | 30s | 30s | 30s | 30s |
| `ownerLeaseFencingMargin` | 5s | 5s | 5s | 5s |
| `ownerLeaseRenewTimeout` | 3s | 3s | 3s | 3s |
| `routeCacheMaxAge` | 15s | 15s | 15s | 15s |
| `messageFollowDuration` | 30s | 30s | 30s | 30s |
| `maxActiveOutboundRelocations` | 64 | 64 | 64 | 64 |
| `maxActiveInboundRelocations` | 64 | 64 | 64 | 64 |
| `maxConcurrentRelocationCaptures` | 8 | 8 | 8 | 8 |
| `maxConcurrentRelocationRestores` | 8 | 8 | 8 | 8 |
| `maxRelocationPayloadInFlightBytes` | 256 MiB | 256 MiB | 256 MiB | 256 MiB |

갱신 주기 대비 TTL 배수가 `.NET`·C++은 3배인데 Java·Node는 6배였다. 같은 mesh에 여러
언어 node가 섞이면 owner 판정 시점이 갈리므로 정합성 문제다. 두 언어를 15초로 맞췄다.
나머지 12개 값은 이미 네 언어가 모두 같다.

### G4 · 언어별로 다른 public 타입 이름 — 처리 완료

`.NET` 이름을 정본으로 맞춘다. `.NET`의 `I` 접두사는 그 언어의 관용이므로 다른 언어로
옮길 때 떼어낸다.

| 뜻 | 정본(.NET) | 상태 |
| --- | --- | --- |
| filter 다음 단계 | `ZLinkHandlerFilterNext` | **완료.** Node의 `ZLinkHandlerDelegate`를 바꿨다. 파일명, 구현, e2e 호출부, Node 공개 계약 spec과 공통 가이드를 함께 고쳤다. build와 typecheck 통과 |
| fanout 수신 handler | `ZLinkFanoutHandler` | **완료.** Java의 `ZLinkPublishHandler`와 Node의 같은 이름을 바꿨다. Java 파일명, 두 언어 구현, 샘플·e2e 호출부, 두 언어 공개 계약 spec과 가이드를 함께 고쳤다. Java `compileJava`, Node build·typecheck 통과 |
| Spot join admission 결과 | `ZLinkSpotActorJoinResult` | **완료.** Java·Kotlin·Node의 `ZLinkSpotActorJoinResponse`와 C++의 `spot_actor_join_response_t`를 바꿨다. 130개 파일이며 Java 파일명도 함께 바꿨다. Java `compileJava`, Node build·typecheck, C++ 전체 빌드가 통과한다 |
| session dispatch context | `ZLinkSessionDispatchContext` | **완료.** 조사해 보니 C++은 이미 `stream_dispatch_context_t`였고 Node는 `ZLinkSessionDispatchContext`가 정본이며 `ZLinkSessionMessageContext`는 별칭만 남아 있었다. Node 별칭과 공개 표면 목록에서 제거하고 Java·Kotlin의 이름과 파일명을 바꿨다. 79개 파일이며 Java `check`, Node build·typecheck가 통과한다 |

각 이름은 구현뿐 아니라 언어별 공개 계약 spec에도 선언되어 있으므로 spec·가이드·공개
계약 trace를 함께 갱신해야 한다.

### Node.js location option 이름이 spec과 달랐다 — 처리 완료

G4를 조사하다 별도로 발견했다. Node 공개 계약
[`01-foundation-configuration`](../../framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md)은
`ownerLeaseRenewIntervalMs`와 `ownerLeaseFencingMarginMs`를 선언하는데 구현은
`heartbeatIntervalMs`와 `routingIdFencingMarginMs`를 쓰고 있었다. spec 이름으로 맞췄다.
샘플·e2e를 포함해 24개 파일이며 build와 typecheck가 통과한다.

### G5 · Java `ZLinkMeshNodeSocketConfig`가 spec과 다름 — 처리 완료

spec이 요구한 대로 고쳤다. 차이 두 가지를 모두 닫았다.

- HWM 넷의 타입을 `int`에서 `long`으로 넓혔다. HWM은 accounted byte 수이므로 `int`로는
  2 GiB를 넘길 수 없었다. bindings 계층은 이미 64-bit였다
  (`SocketOptionsTypeMapTest.byteHighWaterMarksRoundTrip64BitBoundaries`). 좁힌 것은
  framework 계층뿐이었으므로 framework 쪽 사슬 전체를 넓혔다. `MeshNode`,
  `ZLinkInternalMeshNode`, `ZLinkJavaMeshNode`의 `setRouterHighWaterMark`가 함께 바뀐다.
- `mailboxMessageBudget`과 `mailboxByteBudget`을 public 표면에 추가했다. 기본값은 `.NET`과
  같이 `0`이며 runtime 기본값을 그대로 둔다는 뜻이다.

넓히면서 드러난 자리 둘은 값의 뜻이 다르다. router pending admission capacity와 mesh
application dispatcher의 local pending capacity는 byte가 아니라 message 개수다. byte
HWM이 int 범위를 넘을 때 wrap하지 않도록 두 곳 모두 `Integer.MAX_VALUE`로 saturate하게
바꿨다. test double의 `setRouterHighWaterMark`도 함께 넓혔다.

JVM `check`가 통과한다.

### G1 · C++ `enable_actor_dispatch()` — 처리 완료

spec이 선언한 `stream_node_options_builder_t &enable_actor_dispatch();`를 구현했다.
`.NET`의 `EnableActorDispatch()`와 Java·Node의 `enableActorDispatch()`를 그대로 옮겼고
중복 호출을 거부하는 동작도 같다. options state에
`stream_nodes_with_actor_dispatch` 집합을 추가해 STREAM node 이름 단위로 기록한다.

C++ 전체 CTest **49/49**가 통과한다.

### G2 · Java `ZLinkActorManager.findSpot(String)` — 처리 완료

spec이 선언한 `CompletionStage<Optional<SpotRef>> findSpot(String spotId);`를 구현했다.
`.NET`의 `FindSpotAsync`와 같은 자리를 읽는다. Actor authority row가 이미 현재 Spot id를
들고 있으므로 `find`가 쓰는 그 row를 그대로 읽고, 별도 조회 경로를 만들지 않는다.
Spot 소속이 없거나 Actor를 찾지 못하면 빈 결과다.

`ZLinkActorLocationCoordinator.findStoredSpotRef`를 추가하고 `ZLinkActorRuntime`이
노출한다. Spring bean과 Kotlin test double의 구현도 함께 채웠다.

JVM `check`가 통과한다.

### G3 · Java inbound dispatch 옵션 — 처리 완료

spec이 선언한 표면을 그대로 구현했다. `.NET`의
`ZLinkInboundDispatchOptionsModel`을 기준으로 옮겼다.

- `ZLinkApplicationHwmProfile` enum 넷(`COMPACT`, `LOW_LATENCY`, `BALANCED`, `THROUGHPUT`)
- `ZLinkInboundDispatchOptions` interface — `applicationHwmBytes`,
  `applicationHwmProfile`, `processMemoryLimitBytes`의 getter와 setter
- `ZLinkFrameworkOptions.configureInboundDispatch()` 진입점

검증 규칙도 `.NET`과 같다. profile 기본값은 `BALANCED`이고, `processMemoryLimitBytes`는
양수여야 하며 `0`을 넣으면 configuration error다. `applicationHwmBytes`는 생략하면 Auto,
`0`이면 제한 없음, 양수면 그 값을 쓴다. 값을 담는 자리는
`ZLinkInboundDispatchRegistration`이며 `ZLinkFrameworkRegistration`이 소유한다.

여기까지는 표면과 값 보관이다. runtime이 이 값을 실제 Application receive 중단에
연결하는 것은 [4. Backpressure §6](../../framework/common/guide/server/04-backpressure.ko.md#6-framework에-아직-적용되지-않은-부분)이
밝힌 범위로 남는다.

JVM `check`가 통과한다.

### G8 · spec에서 뺀 계기 둘 — 처리 완료

`zlink.relocation.recovered`와 `zlink.relocation.journal.messages`를 구현과 문서에서
지웠다. spec이 이미 판단을 끝낸 정리 대상이므로 방향은 삭제다.

`.NET`에서 지운 자리는 계기 등록 둘, `outcome == Recovered`일 때의 기록,
`RecordJournalMessages` 메서드와 `ZLinkActorRemoteJoiner`의 호출부다.
`StartRelocation`의 enabled 검사에서 `RelocationRecovered`도 뺐다.
Node.js에서는 계기 이름 선언 둘, `recordJournalMessages` 선언과 구현,
`outcome === 'recovered'` 기록, `actor-transfer-runtime`의 호출부를 지웠다.
쓰이지 않게 된 `objectAttributes` 지역 변수도 함께 정리했다.

계기 이름 전체 목록을 하드코딩해 단언하던 양쪽 contract test도 같이 고쳤다.
`.NET`의 `Relocation_Journal_And_Bytes_Use_Their_Exact_Label_Sets`는 journal 단언을
빼고 `Relocation_Bytes_Uses_Its_Exact_Label_Set`으로 바꿨다.
`Relocation_Metric_Uses_Only_The_Closed_Terminal_Outcomes`는 삭제한 counter를 듣던
listener를 걷어내고 terminal outcome 다섯만 확인한다. `outcome` 허용값의 `recovered`는
그대로 남는다. 공통 가이드 `12-operations`의 표 두 줄도 지웠다.

C++와 Java는 애초에 방출하지 않아 손댈 곳이 없었다. `.NET` runtime metric test
**22/22**와 Node build·typecheck가 통과한다.

### 이 회차 최종 회귀

| 언어 | 결과 |
| --- | --- |
| C++ | 전체 CTest **49/49** |
| Java/Kotlin | `check` `BUILD SUCCESSFUL` |
| Node.js | build, typecheck와 변경 범위 contract **118/118** |
| .NET | solution 전체 test |

이름을 바꾼 계기와 타입은 contract test가 이름 목록이나 정규식으로 하드코딩하고 있어
구현만 고치면 반드시 깨진다. 이번 회차에서 함께 고친 자리는 다음과 같다.

- Node `runtime-metrics.test.js`의 계기 개수 단언. 계기 둘을 지웠으므로 44에서 42가 된다
- Node `contract-surface.test.js`의 `ZLinkHandlerDelegate` 정규식 두 곳
- `.NET` `RuntimeMetricsTests`의 계기 이름 목록과 journal·recovered 단언

### G7 · runtime metric 구현 편차 — 이름 셋 처리 완료

G8이 지운 계기 둘은 처리했다. Java가 spec과 다른 이름으로 방출하는 셋은 이번 회차에서
고치지 않았고 그 이유를 남긴다.

| Java가 쓰는 이름 | spec의 이름 | spec이 요구하는 label |
| --- | --- | --- |
| `zlink.channel.request.duration` | `zlink.mesh_node.request.duration` | `mesh_name`, `surface`, `outcome` |
| `zlink.channel.request.inflight` | `zlink.mesh_node.requests.inflight` | `mesh_name`, `surface` |
| `zlink.channel.request.timeouts` | `zlink.mesh_node.request.timeouts` | `mesh_name`, `surface` |

`.NET`은 세 이름을 spec대로 등록한다(`ZLinkRuntimeMetrics.cs:51,54,56`). Java도 이름만
바꾸면 되는 것처럼 보이지만 그렇지 않다. 방출 지점인
`ZLinkChannelDirectCalls.RequestCall.submit`은 label을 `Map.of()`로 비워서 보낸다. 그
자리에서 닿는 값은 `ZLinkChannelCallRuntime`의 `channelName`뿐이고 `mesh_name`도
`surface`도 없다.

이름만 바꾸면 spec 이름을 쓰면서 필수 label이 빠진 계기가 된다. 이름이 다른 것보다
나쁘다. 수집기 쪽에서는 계약을 지킨 것처럼 보이지만 집계 축이 없어 대시보드가 만들어지지
않기 때문이다. 따라서 label 원천을 먼저 연결하고 이름을 함께 바꿨다.

`RequestCall`이 label 집합을 들고, 생성 지점인 `ZLinkChannelRuntime.requestToChannel`이
이미 가진 `channelName`과 표면 종류 `"channel"`을 넘긴다. `.NET`이
`StartRequest(activation.ChannelName, "channel")`으로 같은 두 값을 넘기는 것과 같은
구조다. duration의 outcome 값도 `.NET`과 같이 `completed`·`failed`·`timed_out`이며,
timeout 판정을 한 번만 계산해 duration의 outcome과 timeouts counter가 같은 근거를 쓴다.

**요청 경로에 할당을 만들지 않는다.** label을 붙이려고 요청마다 map을 만들면 계기는
맞아도 hot path가 느려진다. Java `Map.of()`는 인자가 없을 때만 싱글턴이고 인자가 있으면
매번 새 map이므로, 고치기 전의 `Map.of()`는 무할당이었지만 그대로 label을 넣으면 요청마다
두 개를 할당하게 된다. 그래서 `ZLinkRequestMetricTags`가 mesh와 surface 조합마다 base
label과 outcome 3종을 **한 번만 만들어 공유**한다. 캐시 키도 조합 문자열을 만들지 않고
호출자가 이미 가진 이름 문자열을 그대로 쓴다. 방출 전체를 `ZLinkRuntimeMetrics.enabled()`
안쪽에 두어 계측이 꺼져 있으면 label 생성도 completion callback 등록도 하지 않는다.

Spring `ZLinkMicrometerMetricSinkTest`가 쓰던 예시 이름도 spec 이름으로 맞췄다.
JVM `check`가 통과한다.

`zlink.actor.transfer.duration`과 `zlink.actor.transfer.pending_requests.count`는 방향이
반대다. spec에 대응 이름이 없으므로 spec에 넣을지 구현에서 뺄지 판단이 필요하다. 이 둘은
`.NET`에도 있으므로 Java만의 확장이 아니다.

### Node.js `ZLinkMeshNodeSocketConfig`의 mailbox 둘 — 처리 완료

Java의 G5와 같은 부류다. spec이 선언한 `mailboxMessageBudget`과 `mailboxByteBudget`이
Node 공개 표면에 없었다. C++에는 이미 있다. `ZLinkMeshNodeSocketConfig`와 그 backing인
`ZLinkSpotRouterCapabilityOptions`에 함께 추가했다. build, typecheck와 contract 표면
test **43/43**이 통과한다.

HWM 타입은 이번 회차에서 바꾸지 않았다. spec은 `bigint`인데 구현은 `number`다. Java의
`int`와 달리 실질 위험은 다르다. JavaScript `number`는 2^53까지 안전하므로 byte 단위
HWM으로는 약 9 PB까지 정확하다. 넘길 수 있는 값이 아니다. 반면 Java의 `int`는 2 GiB에서
막혔으므로 그쪽은 실제 결함이었고 이번에 고쳤다. Node 쪽은 spec과 구현이 다른 것은
맞지만 잘못된 값을 만들지는 않으며, 바꾸려면 backend contract의 HWM 타입까지 함께
`bigint`로 옮겨야 하므로 별도 회차로 둔다.

### B1 · C++ binding 샘플 빌드 — 처리 완료

`bindings/cpp` 샘플 전체가 `-DZLINK_CPP_BUILD_SAMPLES=ON`에서 빌드되지 않던 원인은
`samples/sample_common.hpp`가 Core 11에서 제거된 `zlink::service` contract를 계속
참조한 것이다. `BLK-053`이 Java·Kotlin과 Node.js·JavaScript bindings sample에 적용한
정리와 같은 부류이며 C++만 빠져 있었다.

같은 방식으로 처리했다. bindings sample은 raw socket과 socket monitor 공개 API만
사용한다. `sample_common.hpp`에서 RouteMesh pull-dispatch helper 전체와
`mesh_start_single_node`를 지우고, 그 자리에 Actor·Spot·session binding·timer·Logical
Multicast 시나리오의 정본이 `framework/languages/cpp/samples`임을 밝히는 주석을 남겼다.
service API를 쓰던 sample 아홉 개는 삭제하고 `ZLINK_CPP_SAMPLE_SOURCES` 등록도 함께
지웠다. 시나리오 자체는 C++ Framework sample이 이미 소유한다.

남은 raw 전용 sample 일곱 개가 모두 빌드된다. 이 때문에 함께 막혀 있던
`request_reply_async_sample`도 정식 구성에서 빌드된다.

| 빌드되는 sample |
| --- |
| `pair_recv_sample`, `pubsub_recv_sample`, `dealer_router_recv_sample`, `request_reply_async_sample`, `stream_recv_sample`, `stream_packet_callback_sample`, `monitor_recv_sample` |

### S1 · ZoneWorld 샘플 — 문서 정합성으로 처리 완료

이 항목은 샘플 셋을 새로 쓰는 일이 아니었다. 이미 내려진 결정과 가이드 서술이 어긋난
것이 실체다.

통합 execution ledger는 sample 지원 범위를 명시한다. Bingo, TicTacToe, SupportChat,
DeliveryDispatch, ShoppingMall, GameQuest 여섯이 다섯 언어 공통이고 ZoneWorld는 `.NET`과
Node.js만 제공하며, 지원 범위 밖 sample을 새로 만들지 않는다는 결정이다. 반면 공통 가이드
[14. 샘플 고르기](../../framework/common/guide/server/14-samples.ko.md)는 일곱 샘플을 모두
공통 자산처럼 소개해서 C++·Java·Kotlin 독자가 없는 코드를 찾게 만들었다.

가이드를 결정에 맞췄다. 선택 표의 ZoneWorld 행에 제공 언어를 적고, §8 도입부에 앞의 여섯과
달리 두 언어에만 있다는 것과 다른 언어에서는 설명과 공통 시나리오 문서를 읽고 코드는 두
언어 중 하나를 참고하라는 안내를 넣었다.

`.NET` ZoneWorld는 79개 소스 파일이고 Node는 34개다. 세 언어로 새로 쓰는 것은 ledger가
정한 범위를 넘는 일이므로 이 문서의 판단 기준(§0 "계약이 기준이다")대로 결정을 따랐다.
