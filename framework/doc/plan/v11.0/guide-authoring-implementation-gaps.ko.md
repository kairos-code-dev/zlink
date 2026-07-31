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
