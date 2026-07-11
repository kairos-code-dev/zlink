# Framework public contract 구현 gap 해소 계획

## 1. 목적

이 계획은 `framework/doc/framework/common/spec/`에 고정된 목표 계약과 각 언어의 실제
framework 구현 사이의 차이를 모두 없애기 위한 실행 문서다. 첫 작업 언어는 `.NET`이며,
한 언어의 모든 완료 조건을 통과하기 전에는 다음 언어 작업을 시작하지 않는다.

작업 순서는 다음과 같이 고정한다.

```text
.NET -> Java -> Kotlin -> Node.js -> C++
```

Java와 Kotlin은 runtime과 build를 공유하지만 public 사용성과 테스트 표면이 다르므로
완료 판정을 분리한다. Java 완료 뒤 Kotlin을 검증하며, Kotlin gate가 끝나기 전에는
Node.js로 넘어가지 않는다.

이 문서는 실행 중 상태 보드다. 작업이 끝난 뒤 한꺼번에 채우지 않고, 구현과 검증이
끝날 때마다 체크박스와 증거를 갱신한다.

## 2. 단일 기준과 우선순위

계약과 구현이 다르면 아래 순서로 판단한다.

1. 공통 정식 spec의 기능, 동작, 오류와 완료 조건
2. 해당 언어 디렉터리의 정식 public interface 문서 집합
3. 공통 또는 언어별 guide에 기록된 사용 원칙
4. `implementation-gap.ko.md`의 현재 구현 차이
5. 실제 public source, package export와 contract test
6. sample과 E2E

정식 spec을 현재 구현의 최소 공통분모에 맞춰 축소하지 않는다. 구현 중 정식 spec 자체의
모순이나 구현 불가능한 계약을 발견하면 코드를 우회하지 않고 작업을 중단한다. 공통 spec,
언어별 interface spec, 구현 차이 표를 먼저 설계 리뷰한 뒤 다시 시작한다.

### 2.1 Public interface freeze

각 언어의 public contract interface는 이미
`framework/doc/framework/common/spec/languages/<lang>/`의 정식 계약 문서 집합에 확정되어
있다. 이 구현 계획은 interface를 새로 설계하거나 선택하는 작업이 아니다.

- 구현 agent는 언어별 interface 문서의 type, member, overload, generic, nullable,
  default value와 완료 의미를 그대로 구현한다.
- G0 inventory는 계약 후보를 고르는 과정이 아니라 확정 interface의 모든 항목을 빠짐없이
  구현 ledger로 옮기는 과정이다.
- G1의 “interface 정렬”은 source와 package export를 확정 문서에 맞춘다는 뜻이다.
- DDD/POSD 리팩터링은 확정 public interface 안에서 내부 책임과 복잡성을 개선한다.
- 리팩터링 agent가 public interface 변경이 필요하다고 판단해도 직접 변경하지 않는다.
  별도 finding으로 보고하고 현재 언어 작업을 중단한 뒤 정식 계약 변경 절차로 분리한다.
- 현재 구현, 다른 언어 구현, sample 또는 E2E만을 근거로 확정 interface를 변경하지 않는다.

기준 문서:

- [공개 계약 관리](../framework/common/spec/public-contract-governance.ko.md)
- [언어별 구현 차이](../framework/common/spec/implementation-gap.ko.md)
- [비동기 실행 정책](../framework/common/spec/async-execution-policy.ko.md)
- [Spot 주소 메시징](../framework/common/spec/spot-address-messaging.ko.md)
- [소프트웨어 설계 원칙](../../../doc/principal/software-design-principles.md)

언어별 계약 소유권은 다음과 같다. 한 문서만 읽고 전체 public surface를 판정하지 않는다.

| 언어 | 전체 interface 기준 | 함께 읽어야 하는 기능별 정식 계약 | 비고 |
|------|---------------------|----------------------------------|------|
| `.NET` | [handler-interfaces](../framework/common/spec/languages/dotnet/handler-interfaces.ko.md) | 같은 디렉터리의 ASP.NET Core, actor, channel, location, monitoring, Spot, stream, session dispatch, Spot node 문서 | `README.ko.md`의 범위와 취소 규칙도 적용 |
| Java | [handler-interfaces](../framework/common/spec/languages/java/handler-interfaces.ko.md) | Spring Boot channel/Spot/actor-session/stream/registry/monitoring과 Stream Connector 문서 | handler 문서는 interface, annotation, context, option만 소유 |
| Kotlin | [handler-interfaces](../framework/common/spec/languages/kotlin/handler-interfaces.ko.md)와 Java 계약 전체 | Kotlin 전용 coroutine, extension, `Flow` 계약 | Java 표면을 복사하지 않고 함께 적용 |
| Node.js | [handler-interfaces](../framework/common/spec/languages/node/handler-interfaces.ko.md) | 같은 디렉터리의 NestJS, actor, channel, monitoring, registry, Spot, stream, session dispatch, Spot node, Stream Connector 문서 | `README.ko.md`의 범위와 취소 규칙도 적용 |
| C++ | [cpp-framework-interfaces](../framework/common/spec/languages/cpp/cpp-framework-interfaces.ko.md) | channel, Spot, stream, registry, monitoring, application framework, HTTP hosting/server, actor gateway relay 문서 | [handler-interfaces](../framework/common/spec/languages/cpp/handler-interfaces.ko.md)는 handler 정렬 규칙이며 전체 surface 목록이 아님 |

`stage-wrapper-on-spot.ko.md`는 상위 모델을 위한 guide이므로 public interface 권위 문서로
간주하지 않는다. 다만 guide의 표준 사용 패턴이 확정 계약과 충돌하지 않는지는 G7에서 확인한다.

## 3. 완료 판정 원칙

### 3.1 체크박스 규칙

체크박스는 다음 증거가 모두 있을 때만 `[x]`로 바꾼다.

- 목표 spec 항목과 실제 public symbol의 대응이 기록되어 있다.
- 구현 또는 제거 작업이 끝났다.
- public contract test가 정확한 타입과 시그니처를 검증한다.
- 관련 unit test가 성공한다.
- 명령, exit code, 실행 시각과 commit이 이 문서의 증거 표에 기록되어 있다.

부분 구현, source 존재, build 성공만으로는 완료 처리하지 않는다. `gap` 표시는 완료가
아니며 해결할 작업이 남았다는 뜻이다.

### 3.2 언어 완료 gate

각 언어는 아래 gate를 순서대로 모두 통과해야 한다.

```text
G0 Spec inventory complete
 -> G1 Public interface and export parity complete
 -> G2 Runtime behavior and unit tests complete
 -> G3 Contract + unit + integration tests all green
 -> G4 Codex DDD/POSD refactoring loop clean
 -> G5 Samples all green
 -> G6 E2E all green
 -> G7 Gap/doc/packaged-surface rereview clean
 -> Next language
```

`G4` 이후 code가 바뀌면 `G3`부터 다시 실행한다. sample 또는 E2E 실패를 고치기 위해
framework code를 바꾸면 `G3`와 `G4`도 다시 실행한다.

### 3.3 후속 언어 변경의 역방향 gate 무효화

후속 언어 작업이 이미 G7을 통과한 언어의 production source, public package/export,
shared runtime, sample 또는 E2E fixture를 바꾸면 이전 PASS 증거를 그대로 유지하지 않는다.
영향받은 이전 언어의 진행표를 즉시 다시 열고 아래 gate를 재실행한 뒤 현재 언어 작업을
계속한다.

| 변경 범위 | 다시 여는 최소 gate |
|-----------|---------------------|
| 이전 언어 production/shared runtime | G3 → G4 → G5 → G6 → G7 |
| public source/export/package wiring | G1 → G2 → G3 → G4 → G5 → G6 → G7 |
| sample만 변경 | G5 → G7; framework 수정이 이어지면 G3부터 |
| E2E/cross-language fixture만 변경 | G6 → G7; framework 수정이 이어지면 G3부터 |
| spec 또는 고정 interface 변경 필요 | 구현 중단 후 별도 계약 변경 절차; 영향 언어 G0부터 재개 |

특히 Kotlin은 Java runtime/build를 공유한다. Kotlin 구현이나 cross-language 수정이 Java
source 또는 artifact를 바꾸면 Java의 `NO DDD/POSD FINDINGS`, package, sample과 E2E 증거를
무효화하고 위 표에 따라 Java gate를 먼저 다시 닫는다. 반대로 C++까지 진행한 뒤 이전
언어 fixture를 고쳐도 같은 규칙을 적용한다. 재개 이력은 전체 진행 보드와 증거 표에
기록한다.

### 3.4 금지 사항

- compatibility wrapper나 alias로 이전 public contract를 정식 package root에 유지하지 않는다.
- deprecated facade, adapter, shim, dual dispatch와 구형/신형 API 병행 경로를 만들지 않는다.
- sample/E2E 전용 public API, adapter, raw-frame 우회 또는 메시지별 codec 등록을 추가하지 않는다.
- 실패를 sleep, 무제한 retry, 테스트 전용 분기로 숨기지 않는다.
- public contract 누락을 internal helper나 reflection으로 메우지 않는다.
- 한 언어의 구현 편의를 다른 언어의 public 사용성 차이로 남기지 않는다.
- POSD 리팩터링 중 public interface를 조용히 변경하지 않는다.
- build 산출물만 확인하고 실제 package/assembly/header export 검증을 생략하지 않는다.

### 3.5 비호환 일괄 교체와 삭제 정책

이 작업은 이전 framework public contract와의 source/binary compatibility를 제공하지 않는다.
새 spec으로 일괄 교체하며 구형 호출 표면을 유지하기 위한 호환 계층은 만들지 않는다.

public contract를 교체할 때는 다음 항목을 같은 작업 범위에서 제거한다.

- 이전 public type, member, overload, export와 registration entry
- 이전 표면만 지원하던 runtime branch, adapter, invoker와 serializer/codec 연결
- 이전 표면 전용 unit/contract test와 fixture
- 이전 symbol을 사용하는 sample, E2E, guide, README와 source comment
- 새 경로에서 참조되지 않는 private 함수, class, 파일과 build/package 항목
- 주석 처리된 이전 구현, 임시 migration flag와 항상 한쪽으로만 고정된 feature switch

삭제 전에는 다음을 확인한다.

- repository 전체 exact-symbol 검색에서 정식 사용자가 남아 있지 않다.
- reflection, annotation scan, DI registration, generated source와 build script의 간접 참조를
  확인했다.
- package manifest, project/solution/Gradle/CMake 목록과 install/export 설정에서 파일을
  제거했다.
- 삭제 대상이 다른 진행 중 작업의 미커밋 변경이 아닌지 확인했다.
- 제거 뒤 clean build, contract/unit test와 package surface 검증이 성공한다.

검색 결과가 없다는 이유만으로 framework lifecycle hook이나 reflection entry를 바로
삭제하지 않는다. 반대로 간접 사용 근거가 없고 새 spec에서도 필요하지 않은 코드는
“향후 사용할 수 있음”을 이유로 남기지 않는다.

## 4. 전체 진행 보드

상태는 `대기`, `진행`, `차단`, `완료` 중 하나만 쓴다.

| 순서 | 언어 | G0 | G1 | G2 | G3 | G4 | G5 | G6 | G7 | 상태 |
|------|------|----|----|----|----|----|----|----|----|------|
| 1 | `.NET` | [x] | [x] | [x] | [x] | [ ] | [ ] | [ ] | [ ] | 진행 |
| 2 | Java | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | 대기 |
| 3 | Kotlin | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | 대기 |
| 4 | Node.js | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | 대기 |
| 5 | C++ | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | 대기 |

현재 실행 대상: `.NET` G4 DDD/POSD 반복 검토

## 5. 공통 spec coverage matrix

각 셀은 해당 문서의 모든 규범 문장을 symbol/동작/test ledger로 옮겼을 때만 체크한다.
문서 일부만 관련 있어도 `N/A`로 처리하지 않고 비적용 근거를 ledger에 기록한다.

| 공통 spec | `.NET` | Java | Kotlin | Node.js | C++ |
|-----------|--------|------|--------|---------|-----|
| `README.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `overview.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `framework-api.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `interaction-model.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `message-model.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `async-execution-policy.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `channel-topology.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `actor-model.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `spot-actor.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `session-actor-dispatch.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `spot-address-messaging.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `location-runtime.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `location-store-redis.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `message-flow-tracing.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `runtime-metrics.ko.md` (제안) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `flow-correlation.ko.md` (제안) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `graceful-drain-handoff.ko.md` (제안) | [ ] | [ ] | [ ] | [ ] | [ ] |
| `public-contract-governance.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| `implementation-gap.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |

이 표도 고정 개수로 간주하지 않는다. G0에서 다음 명령의 결과와 표의 행을 비교하고 새
공통 정식 spec이 있으면 해당 행을 먼저 추가한다.

```bash
rg --files framework/doc/framework/common/spec -g '*.ko.md' \
  | sed 's#framework/doc/framework/common/spec/##' | rg -v '/' | sort
```

### 5.1 언어별 정식 계약 coverage ledger

아래 표의 한 행은 문서 파일 하나를 뜻한다. 각 문서의 규범 문장마다 §6 ledger ID를
연결하고, public symbol이 없는 동작 계약도 동작 test ID에 연결한다. 단순히 파일을
읽었다는 이유로 체크하지 않는다. 실행 시 새 파일이 발견되면 이 표에 행을 추가한다.

| 언어 | 정식 계약 문서 | ledger/test 연결 | 상태 |
|------|----------------|------------------|------|
| `.NET` | `README.ko.md` | - | [ ] |
| `.NET` | `handler-interfaces.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-actor.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-channel-messaging.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-location.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-monitoring.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-spot.ko.md` | - | [ ] |
| `.NET` | `aspnet-core-stream.ko.md` | - | [ ] |
| `.NET` | `session-actor-dispatch.ko.md` | - | [ ] |
| `.NET` | `spot-node.ko.md` | - | [ ] |
| Java | `README.ko.md` | - | [ ] |
| Java | `handler-interfaces.ko.md` | - | [ ] |
| Java | `spring-boot-actor-session.ko.md` | - | [ ] |
| Java | `spring-boot-channel-messaging.ko.md` | - | [ ] |
| Java | `spring-boot-monitoring.ko.md` | - | [ ] |
| Java | `spring-boot-registry.ko.md` | - | [ ] |
| Java | `spring-boot-spot.ko.md` | - | [ ] |
| Java | `spring-boot-stream.ko.md` | - | [ ] |
| Java | `stream-connector.ko.md` | - | [ ] |
| Kotlin | `README.ko.md` | - | [ ] |
| Kotlin | `handler-interfaces.ko.md` | - | [ ] |
| Node.js | `README.ko.md` | - | [ ] |
| Node.js | `handler-interfaces.ko.md` | - | [ ] |
| Node.js | `nestjs-actor.ko.md` | - | [ ] |
| Node.js | `nestjs-channel-messaging.ko.md` | - | [ ] |
| Node.js | `nestjs-monitoring.ko.md` | - | [ ] |
| Node.js | `nestjs-overview.ko.md` | - | [ ] |
| Node.js | `nestjs-registry.ko.md` | - | [ ] |
| Node.js | `nestjs-spot.ko.md` | - | [ ] |
| Node.js | `nestjs-stream.ko.md` | - | [ ] |
| Node.js | `session-actor-dispatch.ko.md` | - | [ ] |
| Node.js | `spot-node.ko.md` | - | [ ] |
| Node.js | `stream-connector.ko.md` | - | [ ] |
| C++ | `README.ko.md` | - | [ ] |
| C++ | `cpp-framework-interfaces.ko.md` | - | [ ] |
| C++ | `actor-gateway-session-relay.ko.md` | - | [ ] |
| C++ | `cpp-application-framework.ko.md` | - | [ ] |
| C++ | `cpp-channel-messaging.ko.md` | - | [ ] |
| C++ | `cpp-embedded-http-server.ko.md` | - | [ ] |
| C++ | `cpp-http-hosting.ko.md` | - | [ ] |
| C++ | `cpp-monitoring.ko.md` | - | [ ] |
| C++ | `cpp-registry.ko.md` | - | [ ] |
| C++ | `cpp-spot.ko.md` | - | [ ] |
| C++ | `cpp-stream.ko.md` | - | [ ] |
| C++ | `handler-interfaces.ko.md` | - | [ ] |

다음 파일은 정식 interface coverage 분모에서 제외하되 G7 문서 정합성 검토에는 포함한다.

| 언어 | guide 문서 | 제외 근거 | G7 검토 |
|------|------------|-----------|---------|
| `.NET` | `stage-wrapper-on-spot.ko.md` | 상위 모델 guide | [ ] |
| Java | `stage-wrapper-on-spot.ko.md` | 상위 모델 guide | [ ] |
| Node.js | `stage-wrapper-on-spot.ko.md` | 상위 모델 guide | [ ] |
| C++ | `stage-wrapper-on-spot.ko.md` | 상위 모델 guide | [ ] |

Kotlin 행은 Kotlin 전용 계약만 나타낸다. Java 계약의 각 행에는 Kotlin 적용 여부와 Kotlin
test ID도 함께 기록해, 상속된 public surface가 coverage에서 빠지지 않게 한다.

## 6. 필수 gap ledger

각 언어 작업을 시작할 때 아래 표를 실제 symbol 단위로 복제해서 채운다. 한 행에 여러
public symbol을 묶지 않는다. overload, nullable, generic 제약과 default parameter도
서로 다른 검증 대상이면 행을 나눈다.

| ID | spec 위치 | 목표 symbol/동작 | 현재 source | 차이 | contract test | unit test | 상태 | 증거 |
|----|-----------|------------------|-------------|------|---------------|-----------|------|------|
| DN-001 | `dotnet/handler-interfaces` §16 | `SpotHandle` | `Contracts/Locations/Resolvers.cs` | `SpotRef` 제거, opaque handle 구현 | `Frozen_public_surface_excludes_replaced_contracts` | `Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found`, actor identity/event/polling tests | 완료 | contract 37 PASS, unit 356 PASS |
| DN-002 | `spot-address-messaging` §6~7 | `IZLinkSpotHandleResolver.ResolveSpotHandleAsync` | `Runtime/Locations/ZLinkLocationAddressResolvers.cs` | 내부 snapshot과 안전한 1회 refresh 구현 | `LocationContracts` | `Spot_Address_Resolves_Across_Registered_Meshes` | 완료 | Config 8 PASS |
| DN-003 | `async-execution-policy` §1 | `IZLinkRequestCall.Async<TReply>` | `Contracts/Channels/Calls.cs` | `IZLinkYieldRequestCall`과 `Yield` 제거 | `Frozen_public_surface_excludes_replaced_contracts` | `SerialExecutionQueue_*` | 완료 | package consumer PASS |
| DN-004 | `async-execution-policy` §1 | `IZLinkActorJoinCall.Async` | `Contracts/Actors/IZLinkActorContext.cs` | yield 전용 join call 제거, 자동 turn 관리 | `ActorContracts` | `EntrySpotActorDispatchTests` | 완료 | ATD-B3 PASS |
| DN-005 | `async-execution-policy` §1 | `IZLinkWorkerCall<TResult>.Async` | `Contracts/Workers/ZLinkWorkers.cs` | worker `Yield`와 callback `Submit` 제거 | `WorkerContracts` | `WorkerPoolTests` | 완료 | ATD-A4 PASS |
| DN-006 | `async-execution-policy` §1 | `IZLinkActorSendCall.Submit` | `Contracts/Actors/IZLinkActorClient.cs` | one-way `ValueTask Async`를 `void Submit`으로 교체 | `ActorContracts` | `EntrySpotActorDispatchTests` | 완료 | solution test PASS |
| DN-007 | `dotnet/handler-interfaces` §16 | dispatch mode 비노출 | `Contracts/Dispatch/IZLinkDispatchOptions.cs` | `ZLinkDispatchMode`와 mode property 제거 | `Frozen_public_surface_excludes_replaced_contracts` | `RegistrationValidationTests` | 완료 | package consumer PASS |
| DN-008 | `message-model` | typed packet identity | channel/actor/Spot/session call contracts | typed `PacketName(...)` 제거 | `Frozen_public_surface_excludes_replaced_contracts` | solution compile/regression | 완료 | solution build PASS |
| DN-009 | `actor-model` | `IZLinkActorContext.SpotRid` | `Contracts/Actors/IZLinkActorContext.cs` | `IsJoined`, `GetSpot()` overload 제거 | `ActorContracts` | `EntrySpotActorDispatchTests` | 완료 | sample regression PASS |
| DN-010 | `actor-model` | `ZLinkActorJoinResult.Accepted` | `Contracts/Actors/IZLinkActorContext.cs` | boolean/nullable 필드를 sealed 승인/거절 variant로 교체 | `ActorContracts` | `EntrySpotActorDispatchTests` | 완료 | solution test PASS |
| DN-011 | `dotnet/handler-interfaces` §16 | `IZLinkEndpointConnections.Connect` | `Contracts/Configuration/IZLinkEndpointConnections.cs` | capability별 runtime handle 추가 | `BuilderContracts` | `EndpointConnectionsTests` | 완료 | unit PASS |
| DN-012 | `dotnet/handler-interfaces` §16 | monitoring sealed event | `Contracts/Eventing/Contracts.cs` | kind와 nullable payload를 sealed variant로 교체 | `EventingContracts` | `LocationEventEmitterTests` | 완료 | RuntimeMonitoring compile PASS |
| DN-013 | `dotnet/handler-interfaces` §16 | custom route resolver 비노출 | `Runtime/Locations/ZLinkLocationSpotRouteRefResolver.cs` | public registration/type 제거, 내부 actor 이동 경로로 제한 | `Frozen_public_surface_excludes_replaced_contracts` | DI registration unit tests | 완료 | package consumer PASS |
| DN-014 | `dotnet/handler-interfaces` §16 | `IZLinkBoundSessionFactory` internal | `Runtime/Streams/IZLinkBoundSessionFactory.cs` | public assembly/export 비노출 확인 | exported contract coverage | registration unit test | 완료 | package consumer PASS |
| DN-015 | `config-8-automatic-turn-dispatch` | 단일 terminator 자동 turn | `Runtime/Execution/ZLinkSerialTurn.cs` | `YieldDispatch`를 `AutomaticTurnDispatch`로 이관 | contract exclusion tests | ATD-A1~E3 | 완료 | Config 8 full/shutdown PASS |
| DN-016 | package artifact gate | 6개 package manifest | `scripts/verify_packaged_contract.sh` | source-built assembly만 검사하던 gap 제거 | clean consumer reflection | clean consumer run | 완료 | `dotnet packaged contract result=passed` |

ledger 작성 완료 조건:

- [ ] 언어별 정식 계약 문서 집합의 public type inventory 전체를 행으로 만들었다.
- [ ] 모든 public member, overload, generic 제약과 nullable/default 값을 기록했다.
- [ ] package/assembly/header의 실제 export 목록을 기록했다.
- [ ] 공통 spec의 오류, timeout, cancellation과 완료 의미를 기록했다.
- [ ] lifecycle 순서와 callback 직렬성 규칙을 기록했다.
- [ ] sample/E2E가 요구하지만 public API 근거가 없는 항목을 별도 설계 후보로 분리했다.
- [ ] `implementation-gap.ko.md`의 모든 행이 하나 이상의 ledger ID와 연결된다.
- [ ] spec에는 있으나 `implementation-gap.ko.md`에 없던 차이도 추가했다.
- [ ] `languages/<lang>/*.ko.md` 전체 파일 목록을 저장하고 정식 계약은 ledger와, guide는 G7 검토와 연결했다.
- [ ] 각 정식 계약 문서의 모든 규범 문장을 ledger 또는 동작 test ID와 연결했다.
- [ ] 작업 중 새로 추가된 언어별 spec 파일도 inventory에 포함했다.

## 7. 모든 언어에 공통인 구현 작업 축

각 언어에서 다음 축을 모두 확인한다. 현재 구현이 이미 일치하더라도 contract test 증거
없이 체크하지 않는다.

- [ ] public type, method, property, constructor와 export 전체 inventory
- [ ] namespace/package/module/header 경계와 internal type 비노출
- [ ] one-way `submit()`의 local queue 수락 의미와 후속 오류 관측
- [ ] request/join/worker의 단일 완료 terminator와 실행 문맥 복귀
- [ ] 언어 관례에 맞는 cancellation 범위
- [ ] handler와 lifecycle의 비동기 완료 및 직렬 실행
- [ ] opaque `SpotHandle`, 내부 address refresh와 안전한 1회 retry
- [ ] actor membership의 단일 상태 값과 모순 없는 join 결과
- [ ] generic actor context의 Spot instance getter 제거
- [ ] typed packet identity의 descriptor 단일 소유
- [ ] typed session handler와 raw runtime 경계 분리
- [ ] manual connection의 capability별 runtime handle
- [ ] dispatch 최적화 mode의 public surface 제거
- [ ] message-kind별 unhandled policy와 diagnostics
- [ ] monitoring event의 sealed/tagged/variant 유효 상태
- [ ] Spot/Entry Spot context의 역할별 method와 worker producer
- [ ] location store, resolver, readiness, watch와 runtime query
- [ ] route-mesh runtime options와 기본 request timeout
- [ ] actor/session relay, bound session send/disconnect 완료 의미
- [ ] stream connector typed/raw call, codec, compression과 packet identity
- [ ] 오류 enum과 언어별 exception/result mapping
- [ ] host/DI registration과 optional capability 제공 조건
- [ ] 정식 spec 경로를 읽는 documentation/contract regression test
- [ ] 구형 public symbol, 호환 adapter와 dual runtime path 부재
- [ ] 미사용 함수/type/file와 stale build/package entry 제거

### 7.1 확정 spec과 기존 검증 자산의 충돌 처리

공통 E2E는 구현 검증 기준이며 새 public contract의 출처가 아니다. E2E 문서, fixture,
scenario 이름 또는 marker가 확정 spec과 충돌하면 public interface를 되돌리지 않고 검증
자산을 확정 계약에 맞게 이관한다. 이관 전후에 보존할 업무 불변식과 삭제할 구형 API
가정을 각각 ledger에 기록한다.

현재 확인된 필수 이관 항목은 Config 8이다. 기존
`config-8-yield-dispatch.ko.md`와 모든 언어의 `YieldDispatch` fixture는 public `Yield`
terminator를 계약으로 가정하므로 그대로 둘 수 없다. `.NET` 구현을 시작하기 전에는 공통
문서와 공통 scenario 정의만 먼저 이관한다. 언어별 fixture는 해당 언어 G0에서 순서대로
이관한다.

- [ ] 공통 문서를 `config-8-automatic-turn-dispatch.ko.md`로 바꾸고 public `Yield` 호출을
  요구하는 설명, scenario ID와 marker를 제거한다.
- [ ] 단일 completion terminator를 기다릴 때 framework가 self-deadlock을 피하고 인과 관계가
  있는 후속 작업만 같은 논리적 turn에서 처리한다는 계약을 검증한다.
- [ ] 같은 Spot, actor, timer의 보호 상태에는 callback 완료 전 관련 없는 작업이 재진입하지
  않으며, 다른 actor/timer와 관련 없는 실행 줄은 진행할 수 있음을 검증한다.
- [ ] continuation이 원래 실행 문맥으로 돌아가고 timeout, 언어별 cancellation, shutdown 뒤
  대기와 실행 줄이 정리되는지 검증한다.
- [ ] local/remote Spot, route bridge, session relay와 worker 경계의 기존 업무 불변식을 새
  public 표면으로 이관한다.
- [ ] 현재 작업 언어의 fixture 디렉터리, project/package 이름, runner 배열, selector,
  evidence marker와 문서 링크를 새 scenario 이름으로 함께 바꾼다.
- [ ] 현재 작업 언어 범위 검색에서 `YieldDispatch`, public `Yield` terminator, 이전 YD marker가
  남지 않았음을 기록한다. 일반 언어 키워드나 내부 scheduler 용어는 public API와 구분한다.
- [ ] 새 scenario가 현재 언어 coverage matrix에 있고 실제 runner에서 한 번만 실행되는지
  확인한다.

공통 문서 이관은 `.NET` G0의 선행 gate고 언어별 fixture 이관은 각 언어 G0의 gate다.
이관 과정에서 framework production code를 바꾸면 현재 언어의 G1부터 정상 절차를 적용한다.
이관된 E2E의 최종 실행 성공은 G6에서 판정한다. 모든 언어의 구형 이름이 저장소 전체에서
사라졌다는 검증은 C++ G7과 최종 전체 closure에서 수행한다.

### 7.2 bindings 공개 기능과 local package 선행 gate

G0에서 현재 언어가 사용하는 bindings package의 version, artifact hash와 실제 public API를
기록하고, framework 목표 계약을 구현하는 데 필요한 capability가 모두 있는지 확인한다.
framework는 bindings의 public API만 사용하며 reflection, `InternalsVisibleTo`, internal/private
접근, bindings source/composite/project 직접 참조로 부족한 기능을 우회하지 않는다.

필요한 bindings 공개 기능이 없으면 현재 framework 구현을 중단하고 다음 조건부 절차를 먼저
완료한다.

1. bindings의 정식 계약 또는 draft 절차에 따라 public API를 설계하고 contract/unit test를
   추가한다. framework의 고정 public interface는 이 과정에서 변경하지 않는다.
2. bindings 구현과 전체 해당 언어 test를 통과시킨다. 공용 native/core를 수정했으면 관련
   bindings 전체 영향도 함께 검증한다.
3. `scripts/local-package/README.ko.md`와 `scripts/local-package/`의 정식 script로 새 local
   package를 만든다. bindings 디렉터리에 별도 wrapper를 만들지 않는다.
4. package 이름, version, hash, archive 내용과 local repository 경로를 증거 표에 기록한다.
5. 중앙 version pin만 갱신한다. `.NET`은 `Directory.Packages.props`, Java/Kotlin은
   `gradle/libs.versions.toml`, Node.js는 root `package.json`, C++는
   `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION`을 사용한다.
6. framework가 새 package artifact를 실제로 resolve했는지 dependency graph와 절대경로로
   확인한 뒤 현재 언어 G1부터 다시 실행한다.
7. 공용 native/core, shared dependency 또는 이미 완료한 언어의 package가 바뀌었으면 §3.3의
   역방향 gate 무효화를 적용한다.

bindings 기능이 이미 충분해도 audit 행과 version/hash 증거 없이 이 gate를 닫지 않는다.

## 8. 언어별 실행 절차

### 8.1 G0 — inventory와 실패 테스트 고정

1. 현재 언어의 계약 소유권 표와 모든 정식 상세 spec을 읽는다.
2. public source와 실제 export를 추출한다.
3. §6 ledger를 symbol 단위로 완성한다.
4. contract test가 정식 spec 경로를 읽도록 먼저 고친다.
5. 목표 시그니처와 동작을 검증하는 실패 테스트를 추가한다.
6. 삭제 대상 public symbol도 “없음”을 검증하는 테스트를 추가한다.
7. 공통 E2E와 확정 spec의 모순을 inventory하고 §7.1 이관 ledger를 닫는다.
8. bindings public capability와 package version/hash를 audit하고 §7.2 충족 여부를 기록한다.

언어별 spec 파일 목록은 기억이나 고정 개수로 판단하지 않고 작업 시점의 checkout에서
다음 명령으로 만든다.

```bash
rg --files framework/doc/framework/common/spec/languages/<lang> -g '*.ko.md' | sort
```

출력된 모든 파일에 `검토 완료`, `비적용 근거` 또는 ledger ID 중 하나가 있어야 한다.

체크리스트:

- [ ] spec-to-symbol inventory 완료
- [ ] public export snapshot 완료
- [ ] 기존 caller와 sample compile 영향 목록 완료
- [ ] stale E2E/fixture/marker 이관 목록 완료
- [ ] Config 8 계약 이관 완료
- [ ] bindings public capability/package audit 완료
- [ ] 실패 contract test 추가
- [ ] 실패 unit test 추가
- [ ] 구현 순서와 책임 owner 확정

### 8.2 G1 — public interface와 export 정렬

- 이미 확정된 언어별 목표 interface를 시그니처 변경 없이 구현한다.
- 제거 대상 symbol은 package root와 배포 산출물에서도 제거한다.
- interface 변경과 runtime 구현을 한 번에 섞지 말고 compile 가능한 작은 단위로 진행한다.
- public source comment는 정식 계약과 같은 의미로 갱신한다.

체크리스트:

- [ ] 전체 public type과 member 시그니처 일치
- [ ] overload/generic/nullable/default 값 일치
- [ ] cancellation 표현 일치
- [ ] internal helper/export 비노출
- [ ] compatibility alias가 정식 package root에 없음
- [ ] 이전 interface와 overload가 source 및 binary export에서 제거됨
- [ ] 이전 표면 전용 구현, fixture와 문서가 함께 제거됨
- [ ] 이전 동작의 업무 불변식을 검증하던 테스트는 삭제하지 않고 새 public 표면으로 이관됨
- [ ] 실제 package/assembly/header export snapshot 일치
- [ ] 현재 언어의 `verify_packaged_contract.sh` 구현과 clean consumer 실행 성공

#### 8.2.1 실제 배포 artifact gate

source tree나 source-built assembly만 검사해서 G1/G7을 닫지 않는다. 언어별로 반복 실행
가능한 `verify-packaged-contract` script를 두고 아래 네 단계를 자동화한다.

1. 빈 임시 artifact/consumer 디렉터리를 만든다.
2. 실제 배포 방식으로 package 또는 install tree를 만든다.
3. archive 목록, metadata, export와 public declaration/header를 spec snapshot과 비교한다.
4. repository source를 참조하지 않는 빈 consumer가 임시 repository/package만 사용해
   import, compile, 최소 실행을 성공하는지 확인한다.

| 언어 | artifact 생성 기준 | 깨끗한 consumer 증거 |
|------|--------------------|----------------------|
| `.NET` | 아래 배포 project manifest를 각각 `dotnet pack -c Release -o <temp>/nuget`으로 생성 | 새 project가 framework package는 임시 source에서, 외부 의존성은 허용된 public/local dependency feed에서 받아 contract compile/reflection test 실행 |
| Java | `MAVEN_REPOSITORY_URL=file://<temp>/maven`을 설정하고 아래 Java manifest project의 `publishAllPublicationsToReleaseRepoRepository` task만 실행 | 새 Gradle build가 framework group은 임시 Maven repository에서, 외부 의존성은 허용된 repository에서 받아 interface compile/실행 |
| Kotlin | 같은 임시 Maven repository에 Kotlin manifest project의 publish task만 실행 | 새 Kotlin/JVM build가 framework group은 임시 repository에서 받아 coroutine/extension compile/실행 |
| Node.js | build 뒤 아래 contract/supporting artifact manifest만 `npm pack --pack-destination <temp>/npm` 실행 | 새 npm project가 생성된 `.tgz`만 설치하고 package root/subpath import, typecheck, `npm ls`와 최소 실행 |
| C++ | 아래 소유권 manifest에 따라 framework와 Stream Connector install component만 별도 prefix에 설치 | 새 out-of-tree CMake consumer가 해당 install prefix만 사용해 configure, compile, link와 최소 실행 |

`.NET` 배포 project manifest는 다음 6개로 고정한다. solution 전체를 pack하지 않는다.
sample, E2E, test project는 `IsPackable=false`를 명시하고, 생성된 package ID 집합이 이
manifest와 정확히 같은지 검사한다.

```text
src/Zlink.Framework/Zlink.Framework.csproj
src/Zlink.Framework.AspNetCore/Zlink.Framework.AspNetCore.csproj
src/Zlink.Framework.Codecs.MessagePack/Zlink.Framework.Codecs.MessagePack.csproj
src/Zlink.Framework.Codecs.Protobuf/Zlink.Framework.Codecs.Protobuf.csproj
src/Zlink.Framework.Locations.Redis/Zlink.Framework.Locations.Redis.csproj
src/Systems.Zlink.Stream.Connector/Systems.Zlink.Stream.Connector.csproj
```

Java manifest는 `zlink-framework-core`, `zlink-framework-spring-boot-starter`,
`zlink-framework-locations-redis`, `zlink-stream-connector`,
`zlink-framework-codec-protobuf`, `zlink-framework-codec-msgpack`으로 고정한다. Kotlin 단계는
여기에 `zlink-framework-kotlin`을 추가한다. 실제 게시된 group/artifact/version 집합과
정확히 비교한다. 외부 dependency repository는 사용할 수 있지만
NuGet package source mapping 또는 Gradle content filter로 현재 작업 언어의 framework
group/package ID가 임시 repository에서만 해석되게 한다.

HTTP client는 `framework/doc/http-client/<lang>/`가 계약을 소유하는 별도 component이므로 이
계획의 artifact, coverage와 gap 분모에서 제외한다. `.NET` `Zlink.HttpClient`, Java
`zlink-http-client`, Kotlin `zlink-http-client-kotlin`을 이 manifest에 넣지 않는다.
`zlink-framework-testkit`도 contract artifact가 아니라 내부 검증 지원 package로 분류해
public contract snapshot 분모에서 제외한다. 이 제외는 파일 삭제를 뜻하지 않으며, 실제
참조가 없는지는 §3.5 삭제 정책으로 별도 판정한다.

Node.js contract artifact manifest는 다음 6개 package로 고정한다.

```text
@zlink-systems/framework
@zlink-systems/nestjs
@zlink-systems/stream-connector
@zlink-systems/framework-codec-protobuf
@zlink-systems/framework-codec-msgpack
@zlink-systems/framework-locations-redis
```

`@zlink-systems/stream-wire`는 clean consumer의 dependency 해석에 필요한 supporting artifact
manifest로 분리해 함께 pack하되 public contract coverage 완료 수에는 넣지 않는다.
`@zlink-systems/http-client`는 별도 component라 제외한다. G1에서 배포 대상 contract/supporting
package의 `private` 설정을 제거하고 실제 version dependency로 서로 해석되게 한다. 생성된
tarball package ID 집합과 두 manifest가 정확히 같아야 하며 `npm ls`에 workspace link,
repository source 또는 누락 dependency가 있으면 실패한다.

C++ install/export는 다음 소유권 manifest로 분리한다.

| component | target/header 범위 | 계약 소유권 | 이 계획의 검증 |
|-----------|--------------------|-------------|----------------|
| Framework | `zlink_framework`, framework extension/codec/location targets, `framework/include`, `extensions/**/include` | C++ framework 정식 계약 | 포함 |
| StreamConnector | `zlink_stream_connector*`, `connector/core/include` | C++ `cpp-stream`과 Stream Connector 계약 | 포함 |
| FrameworkDependency | framework package가 요구하는 공개 `zlink_cpp` target/header/library | bindings/package dependency 계약 | dependency 해석만 검증 |
| HttpClient | `zlink_http_client`, `http-client/include` | `framework/doc/http-client/cpp/` | 이 계획에서 제외 |

G1에서 CMake install component와 export set을 이 소유권대로 분리한다. common framework
verifier는 `Framework`, `StreamConnector`, 필요한 `FrameworkDependency`만 빈 prefix에
설치하고 target/header 목록을 manifest와 정확히 비교한다. `HttpClient` target/header가 해당
prefix에 나타나면 실패한다. 별도 HTTP client artifact의 API 검증은 HTTP client 계획에서
수행한다.

script는 생성된 artifact 이름과 hash, archive entry 목록, resolved dependency graph,
framework package의 실제 경로를 출력한다. repository source fallback이나 workspace link가
감지되면 실패한다. package 누락과 export 오류를 고친 뒤 G1에서 한 번, 모든 리팩터링이 끝난
G7에서 다시 실행한다.

### 8.3 G2 — runtime 동작과 unit test 정렬

public 표면만 바꾸고 내부 동작이 예전 의미를 유지하지 않도록 아래 책임 owner를 확인한다.

- messaging owner: queue 수락, request correlation, timeout, cleanup
- location owner: handle snapshot, watch 반영, refresh와 stale 처리
- actor owner: membership, join/leave, transfer, session binding
- Spot owner: serial turn, handler registry, timer와 worker
- stream owner: typed dispatch, reply correlation, compression과 lifecycle
- monitoring owner: 유효 event 생성과 observer dispatch
- configuration owner: validation, role capability와 runtime handle

체크리스트:

- [ ] 정상 경로 unit test
- [ ] invalid state를 타입 또는 API 정의로 만들 수 없음을 검증
- [ ] timeout/cancellation/close/shutdown test
- [ ] stale location 및 retry 경계 test
- [ ] queue full/route-not-ready/수락 후 실패 test
- [ ] callback 순서와 self-deadlock 방지 test
- [ ] ownership/disposal/leak test
- [ ] 동시성 및 반복 실행 test
- [ ] 제거한 구형 경로로 진입하는 runtime branch가 없음
- [ ] 삭제된 file이 build/package/install 목록에 남아 있지 않음

### 8.4 G3 — contract, unit, integration 전체 green

G3에서는 선택 테스트가 아니라 해당 언어의 framework 전체 test suite를 실행한다.
실패가 있으면 원인을 수정하고 전체 명령을 처음부터 다시 실행한다.

### 8.5 G4 — Codex DDD/POSD 반복 리팩터링

G3가 모두 성공한 뒤 별도 Codex agent로 DDD/POSD 리뷰를 시작한다. 기능 구현 agent의
자기 확인만으로 gate를 닫지 않는다.

각 반복은 다음 역할로 나눈다.

1. **Codex read-only reviewer**: 현재 언어의 production framework 전체와 공용 runtime 경계를
   읽고 finding만 작성한다.
2. **Codex refactoring agent**: 확정 finding을 두 가지 이상 설계한 뒤 선택안을 구현한다.
3. **Codex adversarial rereviewer**: 수정 뒤 새 위험 신호와 남은 finding을 다시 찾는다.

필수 검토 항목:

- [ ] 깊은 모듈이며 호출자가 내부 결정을 알 필요가 없는가
- [ ] address, codec, dispatch, queue와 lifecycle 지식이 한 owner에 숨겨졌는가
- [ ] pass-through method와 얕은 wrapper가 남지 않았는가
- [ ] 시간적 분해와 정해진 호출 순서가 caller에게 노출되지 않는가
- [ ] 같은 기능의 nominal interface가 반복되지 않는가
- [ ] general-purpose 코드와 업무 특수 코드가 섞이지 않는가
- [ ] 불가능한 상태가 타입으로 표현되지 않는가
- [ ] DDD aggregate와 domain state owner가 runtime orchestration과 분리되는가
- [ ] actor, Spot, session, location의 경계가 transport detail을 누출하지 않는가
- [ ] 코드가 반복하는 주석과 잘못된 공개 보장이 없는가
- [ ] 사용되지 않는 함수, class, field, 파일과 compatibility code가 남아 있지 않은가
- [ ] 같은 기능의 구형/신형 runtime path가 함께 유지되지 않는가

각 반복의 review manifest에는 다음 범위를 반드시 기록한다.

- 해당 언어의 production source 전체
- public package/export와 DI/annotation/reflection/generated-source registration
- project, solution, Gradle, npm, CMake, install과 package wiring
- 해당 언어가 사용하는 공용 runtime 경계
- 제외한 디렉터리와 제외 근거

변경 파일과 인접 파일만 읽은 리뷰로는 `NO DDD/POSD FINDINGS`를 선언할 수 없다. repository
전체를 무조건 리팩터링하는 뜻은 아니지만, 현재 언어 framework 안의 멀리 떨어진 얕은
모듈, 중복 abstraction, dead registration과 stale build entry도 검토 분모에 포함한다.

finding마다 다음 표를 채운다.

| loop | finding ID | 위험 신호 | DDD/POSD 근거 | 대안 A | 대안 B | 선택 | test | 상태 |
|------|------------|-----------|---------------|--------|--------|------|------|------|
| .NET-1 | G4-F1 | spec 밖 public handler scan 확장 두 개 | 이중 등록 표면, 정보 누출 | scan/DI를 internal registrar가 소유하고 public 확장 삭제 | 확장을 새 계약으로 추가 | A | solution build, package export | 완료 |
| .NET-1 | G4-F2 | 세 method attribute가 runtime에서 소비되지 않음 | 명목 계약과 구현 분리 | actual Spot/session owner의 scanner와 invoker에 연결 | attribute 계약 삭제 | A | Spot scanner와 session attributed dispatch unit test | 완료 |
| .NET-1 | G4-F3 | 동작 없는 public marker와 raw attribute | dead compatibility export, 얕은 명목 추상화 | 네 export/file 삭제 | 동작을 새 계약으로 추가 | A | contract/package absence assertion | 완료 |
| .NET-1 | G4-F4 | `ZLinkSpotManagerService`의 일곱 pass-through | 얕은 모듈, 중복 책임 | runtime이 `IZLinkSpotManager`를 직접 구현 | service로 orchestration을 모두 이동 | A | solution build, manager contract test | 완료 |
| .NET-1 | G4-F5 | 사용되지 않는 multipart route DI/interface | dead registration, raw-frame 우회 경로 | interface, method, DI mapping 삭제 | 실제 use case가 생길 때 소유 transport의 private operation으로 설계 | A | production no-hit, solution build | 완료 |
| .NET-1 | G4-F6 | typed publish의 도달 불가능한 `PacketName` | 구/신 경로 공존, 불필요 mutable state | method 삭제와 inferred name 불변화 | public override 복원 | A | contract exclusion, solution build | 완료 |
| .NET-2 | G4-F7 | actor handle refresh가 actor id를 잃고 이전 SpotRid를 사용 | domain identity 누락, refresh 지식 누출 | handle 내부 refresh strategy가 원래 논리 key를 보존 | actor 전용 public handle 추가 | A | `Actor_Spot_Handle_Refreshes_By_Actor_Id_After_The_Actor_Moves` | 완료 |
| .NET-2 | G4-F8 | one-way send와 request가 같은 refresh/retry helper 사용 | 전달 보장 혼합, 특수·범용 코드 혼합 | request만 target-not-found에서 1회 refresh, send는 최신 snapshot으로 1회 제출 | 호출마다 policy 인자 전달 | A | location resolver unit, solution build | 완료 |
| .NET-2 | G4-F9 | handle snapshot에 location update 입력 없음 | 위치 수명 책임이 send 호출자에게 누출 | weak shared registry와 local event/watch host가 snapshot 갱신 | 매 send마다 store 조회 | A | `Location_Event_Updates_Existing_Actor_Spot_Handle_Snapshot` | 완료 |
| .NET-2 | G4-F10 | endpoint handle이 `IList<string>`으로 cast되고 실패 method 노출 | 우발적 표면, 정의로 없앨 수 있는 오류 | public contract와 internal read-only collection만 구현 | 별도 read-only adapter 추가 | A | endpoint contract/unit, cast no-hit | 완료 |
| .NET-3 | G4-F11 | auto-connect가 startup 시점 manual endpoint 사본을 보관 | live public 연결 변경과 reconcile 상태 분리 | executor가 thread-safe endpoint handle을 직접 조회 | callback으로 별도 set 동기화 | A | solution build, endpoint runtime unit | 완료 |
| .NET-3 | G4-F12 | route connection set의 무잠금 HashSet/snapshot | 내부 동시성 조건 누출, 상태 손상 가능성 | 자료구조와 router connect/disconnect를 내부 gate로 직렬화 | concurrent dictionary와 매번 정렬 | A | route connection unit, solution test | 완료 |
| .NET-3 | G4-F13 | constructor의 `IEnumerable<T>`가 assembly 전체 DI 등록을 유발 | 마법적 전역 정책, 표준 DI 책임 침범 | registrar 삭제, application dependency는 `IServiceCollection`에 명시 등록 | 새 명시 attribute/API 추가 | A | standard DI registration unit, sample runner | 완료 |
| .NET-3 | G4-F14 | join reply의 읽히지 않는 rolling compatibility payload 두 필드 | dead compatibility wire, 정보 중복 | 필드와 생성 인자 삭제 | 별도 versioned legacy decoder | A | actor handoff unit/E2E | 완료 |
| .NET-3 | G4-F15 | `SpotHandle` 공개 주석이 caller re-resolve를 요구 | 구현/계약 반대, 복잡성 호출자 전가 | event/watch 갱신과 안전한 request 1회 retry를 주석에 명시 | 내부 갱신 제거 | A | documentation regression, contract test | 완료 |
| .NET-4 | G4-F16 | session stream builder에 호출되지 않는 packet-name override | dead mutable compatibility 경로 | override 삭제, typed name 불변화 | public packet override 복원 | A | production no-hit, stream/solution test | 완료 |
| .NET-4 | G4-F17 | manual/auto endpoint가 같은 physical 연결을 독립 해제 | 연결 정책과 socket 상태 분리 | channel/route/Spot 연결 모듈이 source별 owner를 추적하고 마지막 owner만 disconnect | manual 변경 때 reconciler invalidate | A | route ownership unit, E2E all | 완료 |
| .NET-4 | G4-F18 | executor 실패도 reconciler active로 기록 | 오류를 삼켜 retry 의미 소실 | executor가 성공 여부를 반환하고 성공 target만 active 기록 | tick 전체 예외 전파 | A | `Failed_Connect_Is_Not_Marked_Active_And_Retries_On_The_Next_Tick` | 완료 |
| .NET-4 | G4-F19 | optional watch/event 유실 시 handle 갱신 correctness 경로 없음 | 선택 최적화를 필수 경로로 오인 | live handle key를 polling interval마다 resolve하고 watch는 wake-up 경로로 유지 | watch를 필수 public 계약으로 변경 | A | handle poll/watch unit, location E2E | 완료 |
| .NET-4 | G4-F20 | 회수된 handle의 logical key가 singleton registry에 누적 | 내부 resource 수명 누수 | polling maintenance가 dead weak reference와 빈 key 정리 | public dispose/lease 추가 | A | registry maintenance unit | 완료 |
| .NET-5 | G4-F21 | ownership 통합 뒤 사용되지 않는 sorted connection helper file | dead shallow module | file 삭제 | 향후 사용 가능성을 이유로 유지 | A | production reference no-hit, solution build | 완료 |
| .NET-5 | G4-F22 | Spot owner 전환과 물리 연결이 서로 다른 임계 구역에서 실행됨 | 논리 owner와 실제 연결 상태가 달라질 수 있음 | connector gate가 owner 전환, 물리 호출, 실패 rollback을 함께 직렬화 | endpoint별 pending 상태와 waiter 추가 | A | Spot peer ownership unit, solution test | 완료 |
| .NET-5 | G4-F23 | route Spot request가 refresh helper 전에 handle snapshot을 읽음 | 갱신 정책 우회, 호출자 재조회 책임을 적은 계약 충돌 | timeout 계산을 refresh attempt 안으로 옮기고 공개 주석을 자동 갱신 계약으로 통일 | helper에 preflight callback 추가 | A | invalidated handle request unit, contract documentation review | 완료 |
| .NET-5 | G4-F24 | 사용되지 않는 endpoint membership helper | dead shallow internal surface | helper 삭제 | 미래 사용을 위해 유지 | A | production reference no-hit, solution build | 완료 |
| .NET-6 | G4-F25 | manual과 auto 연결 방식이 같은 역할에서 함께 실행됨 | 연결 정책 분산, spec의 단일 owner 위반 | validation에서 역할별 mode를 고정하고 manual callback과 auto executor를 함께 제어 | reconcile tick마다 manual 목록을 검사 | A | registration mode unit, sample/E2E | 완료 |
| .NET-6 | G4-F26 | SpotHandle 갱신이 이전 generation을 적용할 수 있음 | 주소 상태 역행, generation invariant 누락 | handle snapshot이 generation을 보관하고 update/remove를 단조 적용 | logical key별 async gate 추가 | A | `Spot_Handle_Does_Not_Apply_An_Older_Generation` | 완료 |
| .NET-6 | G4-F27 | local row 재등록 오류가 fail-static 경계 밖으로 전파됨 | store 장애 뒤 reconcile task 영구 종료 | local publish와 peer read를 하나의 store 오류 경계로 집약 | loop 최상단에서 모든 오류 catch | A | auto-connect outage/recovery unit, StoreFailure E2E | 완료 |
| .NET-6 | G4-F28 | auto-connect peer 조회 취소를 store 장애로 처리 | lifecycle 취소와 장애 복구 의미 혼합 | 요청 token의 `OperationCanceledException`을 먼저 다시 throw | tick 결과 variant 추가 | A | auto-connect cancellation unit, solution test | 완료 |
| .NET-6 | G4-F29 | SpotHandle 언어 문서와 source comment가 caller 재조회와 draft를 설명 | 정책 중복과 계약 drift | 내부 갱신과 안전한 request 1회 재전송, 정식 spec 절을 명시 | 중복 설명 삭제 후 공통 spec만 링크 | A | documentation regression, contract review | 완료 |
| .NET-6 | G4-F30 | application에 전달되는 SpotHandle monitor를 내부 lock으로 사용 | 동기화 결정이 public 객체에 노출 | private gate로 snapshot 상태를 직렬화 | immutable state를 CAS로 교체 | A | location resolver concurrency unit | 완료 |
| .NET-7 | G4-F31 | remove와 같은 generation의 지연 update가 handle을 재활성화 | tombstone 순서 역전 | tombstone 상태에서는 같은 generation update를 거부 | watch/poll 적용을 단일 직렬 큐로 통합 | A | same-generation tombstone unit | 완료 |
| .NET-7 | G4-F32 | Spot peer metadata 변경을 active handover가 감지하지 않음 | pub/sub endpoint 추가 뒤 연결 누락 | metadata semantic diff도 handover 조건에 포함 | metadata 누락을 전체 연결 실패로 처리 | A | auto-connect metadata handover unit, Spot E2E | 완료 |
| .NET-7 | G4-F33 | auto mode endpoint handle 변경이 현재 runtime에는 무효지만 다음 시작에 남음 | 조용한 무효 변경과 mode drift | validation에서 handle mode를 freeze하고 auto mode mutation 거부 | startup 설정과 runtime handle 타입 분리 | A | `AutoConnect_Role_Rejects_Runtime_Manual_Connection_Mutations` | 완료 |
| .NET-7 | G4-F34 | TicTacToe의 미사용 별도 Redis room-route 저장소 | dead compatibility와 route 정보 이중 소유 | 저장소, DTO, DI, write, package, test 예외 삭제 | 도메인 소비 경로 복원 | A | TicTacToe sample, sample regression | 완료 |
| .NET-7 | G4-F35 | change-stamp 단계가 요청 취소를 store 오류로 처리 | lifecycle 취소 의미 혼합 | token 취소 예외를 먼저 다시 throw | stamp 결과 variant 추가 | A | auto-connect cancellation unit | 완료 |
| .NET-7 | G4-F36 | public `StoreFailureGrace`가 runtime에서 사용되지 않음 | 얕은 설정과 spec drift | 마지막 성공 desired의 미완료 연결만 grace 동안 재시도 | 옵션과 확정 spec 삭제 | A | store failure grace unit, StoreFailure E2E | 완료 |
| .NET-7 | G4-F37 | public source comment에 draft와 객체 의인화 표현 잔존 | 정식 계약 drift와 모호한 설명 | 정식 spec 의미의 중립적 표현으로 갱신 | 중복 설명 삭제 | A | source comment review, documentation regression | 완료 |
| .NET-7 | G4-F38 | remote actor join만 별도 route-ref interface/record 사용 | SpotHandle 정책 분산, 미사용 field, 얕은 seam | `IZLinkSpotHandleResolver`와 내부 snapshot을 사용하고 old path 삭제 | concrete route resolver 직접 주입 | A | remote join unit/E2E, exact-symbol no-hit | 완료 |
| .NET-8 | G4-F39 | tombstone generation을 주소 snapshot에만 의존 | remove 뒤 더 오래된 update가 handle을 재활성화 | 별도 observed generation을 update/remove 모두에서 단조 증가 | tombstone snapshot variant 도입 | A | higher-generation tombstone unit | 완료 |
| .NET-8 | G4-F40 | failed connect pending을 change-stamp 동일 tick이 건너뜀 | 최적화가 재시도 correctness를 변경 | reconciler가 pending 여부를 소유하고 pending이면 stamp skip 금지 | loop가 별도 retry 상태 소유 | A | auto-connect loop retry unit | 완료 |
| .NET-8 | G4-F41 | 모든 metadata 변경을 physical handover로 처리 | 진단 metadata와 transport 정보 혼합 | planner가 Spot pub endpoint connection fingerprint만 정규화 | executor가 handover 필요 여부 판단 | A | metadata fingerprint unit, Spot E2E | 완료 |
| .NET-8 | G4-F42 | remote actor admission 전에 SpotHandle을 snapshot으로 평탄화 | 안전한 refresh 정책 우회 | admission까지 handle을 유지해 공통 1회 refresh를 적용하고 승인 snapshot만 commit에 고정 | joiner가 직접 resolve retry | A | actor handoff unit, Bingo sample | 완료 |
| .NET-8 | G4-F43 | .NET guide가 삭제된 SpotRef와 caller 재조회 계약을 안내 | public 사용법과 확정 interface 이중화 | 예제, 표, 오류 설명을 SpotHandle 자동 갱신 계약으로 이관 | 구 절 삭제 후 spec 링크 | A | guide exact-symbol search, documentation regression | 완료 |
| .NET-8 | G4-F44 | transferred actor의 bound session route가 `OnJoinedActorAsync` 뒤에 설정됨 | lifecycle callback에서 확정 public `BoundSession`을 사용할 수 없음 | target 준비 단계에서 bound route를 먼저 설정하고 pending route 상태 삭제 | sample이 joining actor push를 생략 | A | actor handoff unit, Bingo two-node sample | 완료 |
| .NET-9 | G4-F45 | handover 실패 target을 pending 판단에서 제외 | stamp 최적화가 reconnect 재시도를 영구 생략 | active/desired의 endpoint, owner, fingerprint 차이까지 pending으로 계산 | 별도 pending flag 저장 | A | pending connect loop unit | 완료 |
| .NET-9 | G4-F46 | current Spot request 재시도가 첫 attempt에서 dispose된 parts를 재사용 | retry attempt 간 메시지 소유권 누출 | attempt lambda 안에서 header와 parts를 매번 생성 | retry clone 보관 | A | current Spot request unit, Spot E2E | 완료 |
| .NET-9 | G4-F47 | generation guard가 8192 key에서 전체 map 초기화 | stale replica row 재수락 가능 | runtime 수명 동안 key별 exact generation 유지 | authoritative generation 조회 뒤 안전 eviction | A | generation guard unit, location E2E | 완료 |
| .NET-9 | G4-F48 | 정식 .NET guide/spec에 삭제된 주소 API 잔존 | public 계약 이중화 | SpotHandle resolver 계약으로 교체하고 forbidden-symbol 문서 테스트 추가 | 해당 절 삭제 후 정본 링크 | A | `DotNetDocs_DoNotDocument_Replaced_Spot_Address_Contracts` | 완료 |
| .NET-9 | G4-F49 | 제거된 actor address resolver의 sample allowlist 잔존 | dead compatibility test 분기 | allowlist와 판정 helper 삭제 | 범용 임시 예외 구조 유지 | A | sample regression, exact-symbol no-hit | 완료 |
| .NET-10 | G4-F50 | handover 실패를 key 존재만으로 수렴 상태로 판단 | stamp skip이 reconnect 재시도를 생략 | endpoint, owner, fingerprint semantic diff도 pending으로 계산 | 명시 pending flag 저장 | A | pending handover loop unit | 완료 |
| .NET-10 | G4-F51 | current Spot request retry가 dispose된 encoded parts 재사용 | attempt 간 message ownership 누출 | 각 attempt 안에서 header와 parts를 새로 생성 | retry용 clone 보관 | A | current Spot request unit, Spot E2E | 완료 |
| .NET-10 | G4-F52 | observed generation map을 고정 개수에서 전체 초기화 | lagging replica row 재수락 | runtime 수명 동안 exact key generation 유지 | authoritative generation query 뒤 안전 eviction | A | generation guard unit, location E2E | 완료 |
| .NET-10 | G4-F53 | .NET guide/spec에 삭제된 address resolver 이름 잔존 | public 사용법과 확정 계약 불일치 | 현재 SpotHandle resolver로 교체하고 forbidden-symbol 회귀 테스트 추가 | 중복 절 삭제 | A | documentation regression, exact-symbol no-hit | 완료 |
| .NET-10 | G4-F54 | sample regression에 제거된 resolver allowlist 잔존 | dead compatibility branch | allowlist와 helper 삭제 | 범용 임시 예외 구조 유지 | A | sample regression | 완료 |
| .NET-11 | G4-F55 | SpotHandle registry와 refresh가 선택한 mesh identity를 버림 | 논리 key 정보 손실로 다른 mesh의 같은 SpotRid가 handle을 변경할 수 있음 | 최초 선택한 mesh와 SpotRid 복합 key를 refresh, event, polling 전체에서 보존 | 모든 mesh에 SpotRid 전역 유일성 제약 추가 | A | `Spot_Handle_Registry_Does_Not_Cross_Mesh_Boundaries`, location unit | 완료 |
| .NET-11 | G4-F56 | topology와 service summary가 observed-generation guard를 우회 | 같은 runtime의 public read surface마다 단조 view가 달라짐 | peer row 값과 generation 검증을 query service의 공통 read helper에 집약 | 각 projection loop에 검증 반복 | A | `Topology_And_Service_Summaries_Drop_Older_Peer_Generations` | 완료 |
| .NET-11 | G4-F57 | 무효화된 handle의 one-way send가 encode 뒤 snapshot 예외로 parts를 누수 | 전송 전 message ownership 책임이 불명확 | snapshot을 encode보다 먼저 읽어 생성 전 실패 | 생성 뒤 ownership flag와 finally로 dispose | A | solution build, location unit | 완료 |
| .NET-11 | G4-F58 | 정식 sample guide가 삭제된 RoutingId Spot 호출을 안내 | 확정 public contract와 사용법 분리 | resolver로 얻은 SpotHandle 전달로 수정 | 중복 선택 안내 삭제 후 정식 guide만 링크 | A | documentation exact-symbol search | 완료 |
| .NET-11 | G4-F59 | Spot pub endpoint metadata key 문자열이 host와 planner에 중복 | transport handover 지식 누출 | metadata owner의 단일 상수를 planner도 사용 | 별도 metadata utility 추가 | A | solution build, metadata fingerprint unit | 완료 |
| .NET-12 | G4-F60 | polling이 같은 key의 여러 live handle generation을 최댓값 하나로 축약 | 오래된 handle이 새 handle과 함께 존재하면 삭제를 관측하지 못함 | key마다 살아 있는 개별 generation을 보존하고 조건부 무효화 | polling이 모든 handle 객체를 직접 보유 | A | `Polling_Snapshot_Preserves_Each_Live_Handle_Generation` | 완료 |
| .NET-12 | G4-F61 | watch upsert generation보다 오래된 replica row를 handle에 적용 | push 순서와 store replica 지연이 단조 주소 view를 역행시킴 | row generation이 event보다 낮으면 event generation tombstone 적용 | event별 authoritative store 재시도 | A | location resolver monotonic tests, solution build | 완료 |
| .NET-12 | G4-F62 | routed Spot send의 message parts dispose 책임이 여러 target에 분산되고 일부 예외 경로에서 누락 | 소유권 정보 누출과 오류 경로 자원 누수 | 최상위 runtime send 경계가 모든 성공/실패에서 단 한 번 dispose | 각 target이 공통 ownership wrapper 사용 | A | Spot route unit, solution test | 완료 |
| .NET-13 | G4-F63 | routed Spot request도 입력 parts 소유권이 target별로 분산 | runtime 중지와 target resolve 예외에서 입력 누수 | 최상위 request 경계가 모든 성공/실패에서 입력을 dispose | disposable ownership wrapper를 encoder가 반환 | A | Spot route request unit, solution test | 완료 |
| .NET-13 | G4-F64 | async submitter가 취소 확인 전에 입력 소유권을 확정하지 않음 | encode 뒤 취소와 runtime stop 경쟁에서 native message 누수 | submit boundary가 즉시 소유하고 enqueue 이전 모든 예외에서 정리 | encoder가 ownership wrapper 반환 | A | async submitter cancellation unit, solution test | 완료 |
| .NET-13 | G4-F65 | route Spot one-way send가 이미 취소된 token으로도 전송 가능 | 같은 Submit 계약의 취소 의미가 호출 경로마다 다름 | snapshot과 encode 전에 취소 확인 | dispatcher target마다 취소 확인 | A | routed Spot send cancellation unit | 완료 |
| .NET-13 | G4-F66 | generation guard가 read 성공에서만 전진 | authoritative write/remove/watch 뒤 lagging replica가 이전 row를 다시 공개 | shared guard의 Observe/tombstone을 모든 kind mutation과 watch에 연결 | location view 모듈로 read/write/event sequencing 통합 | A | `Mutation_Events_Advance_The_Shared_Generation_Guard`, watch monotonic unit | 완료 |
| .NET-13 | G4-F67 | `AddZLinkFramework` 중복 호출이 hosted task lifecycle을 중복 등록 | background task owner가 덮어써져 첫 task를 회수하지 못함 | 두 번째 등록을 명시 오류로 거부 | composable registration accumulator 도입 | A | `AddZLinkFramework_Rejects_Duplicate_Registration` | 완료 |
| .NET-13 | G4-F68 | request/join/worker call의 timeout 검증 시점과 오류가 다름 | 같은 fluent 개념의 validation 지식 중복 | 모든 call이 공통 timeout guard 사용 | timeout value object 도입 | A | timeout validation unit, solution build | 완료 |
| .NET-13 | G4-F69 | topology의 State와 Actor MeshName filter가 kind별로 누락 | 같은 filter abstraction 의미가 projection branch마다 달라짐 | 공통 entry predicate를 모든 kind에 적용 | kind별 exhaustive filter 함수 | A | `Topology_Applies_State_And_Actor_Mesh_Filters` | 완료 |
| .NET-13 | G4-F70 | runtime status가 watch capability와 read outage를 반영하지 않음 | 운영 상태 지식이 resolver/query/auto-connect에 분산 | shared store health와 실제 watch capability를 status projection에 주입 | subsystem별 runtime health 보고 | A | `Status_Reports_Watch_Capability_And_Shared_Read_Failures` | 완료 |
| .NET-13 | G4-F71 | Spot builder의 두 connection property가 다른 두 property와 동일 객체를 반환 | 이름만 보고 독립 capability로 오해하면 lifecycle과 연결 상태 의미가 모호함 | 정식 계약에서 중복 property 제거 | 확정 interface를 유지하고 동일 capability/state 별칭임을 계약과 test로 고정 | B | `Spot_Connection_Role_Names_Expose_The_Documented_Capability_State`, source contract | 완료 |
| .NET-14 | G4-F72 | upsert 지연과 remove tombstone을 같은 unavailable boolean으로 표현 | 같은 generation 정상 row가 도착해도 handle이 영구 복구되지 않음 | Available/PendingUpsert/Removed 상태를 분리 | upsert miss에서는 기존 snapshot 유지 | A | `Watch_Upsert_Does_Not_Apply_A_Row_Older_Than_The_Event` recovery assertion | 완료 |
| .NET-14 | G4-F73 | Spot/Actor/Route topology가 live-list를 재사용해 Lost를 만들지 못함 | 일반 live 목록과 운영 topology projection 의미 혼합 | raw row에 generation과 lease를 적용해 kind 공통 Ready/Lost 계산 | Lost를 Peer 전용으로 spec 제한 | A | `Topology_Projects_Lost_State_For_Expired_Owners` | 완료 |
| .NET-14 | G4-F74 | store read의 성공/실패/취소 health 분류가 세 모듈에 반복 | health 정책 지식 중복과 정상 취소 오분류 위험 | 공통 `ZLinkLocationStoreRead` 경계로 집약 | 각 wrapper에서 요청 token 취소만 별도 처리 | A | location query/resolver cancellation and health unit | 완료 |
| .NET-15 | G4-F75 | Removed handle을 같은 generation poll miss/upsert가 Pending으로 약화 | authoritative tombstone 뒤 같은 generation row 재활성화 | Removed와 같은 generation의 pending 전이를 금지 | Removed handle을 polling inventory에서 제외 | A | exact-generation tombstone regression in `Spot_Handle_Does_Not_Apply_An_Older_Generation` | 완료 |
| .NET-15 | G4-F76 | store 내부 OCE와 caller token 취소를 같은 정상 취소로 분류 | 실제 store timeout이 health 장애에서 누락 | 공통 read 경계에 caller token을 전달해 요청 취소만 제외 | store result variant 도입 | A | `Store_Health_Distinguishes_Caller_Cancellation_From_Internal_Cancellation` | 완료 |
| .NET-16 | G4-F77 | 같은 hosted singleton이 여러 DI descriptor에서 dispose될 때 watch CTS를 중복 사용 | lifecycle 소유권이 dispose 호출 횟수에 의존해 정상 shutdown이 예외로 끝남 | CTS와 task 배열을 원자적으로 한 번만 인수해 dispose를 idempotent하게 구현 | DI descriptor 하나만 disposable owner로 만들고 proxy descriptor 도입 | A | `Spot_Handle_Watch_Host_Disposal_Is_Idempotent`, ResilienceLifecycle E2E | 완료 |
| .NET-17 | G4-F78 | StopAsync와 DisposeAsync가 서로 다른 field snapshot으로 종료 | 동시 종료에서 CTS가 먼저 dispose되거나 task 배열이 분리될 수 있음 | lifecycle gate와 공유 shutdown task로 모든 종료 호출을 집약 | 단일 disposable DI owner와 proxy descriptor | A | concurrent `Spot_Handle_Watch_Host_Disposal_Is_Idempotent` | 완료 |
| .NET-18 | G4-F79 | actor 요청 handler가 `DestroyActorAsync`를 호출하면 reply relay보다 native teardown이 먼저 실행됨 | handler 반환과 요청 응답 완료의 lifecycle 경계가 분리되어 응답 경로가 삭제됨 | 기존 bound-session dispatch 지연 큐에서 teardown을 reply 전송 뒤 실행 | actor request dispatcher에 별도 teardown 큐 추가 | A | `ActorDestroy_DuringBoundRequest_IsDeferredUntilReplyFinalization`, SpotService SM-B8 | 완료 |

반복 종료 조건:

- 모든 확정 finding이 구현과 테스트로 닫혔다.
- 리팩터링 후 G3 전체 명령이 다시 성공했다.
- 별도 Codex read-only reviewer가 정확히 `NO DDD/POSD FINDINGS`를 반환했다.
- cosmetic rename, formatting, 취향 차이만 남았고 의미 있는 복잡성 감소 후보가 없다.

reviewer가 finding을 하나라도 반환하면 loop 횟수와 관계없이 반복한다. public contract
변경이 필요한 finding은 리팩터링으로 처리하거나 interface 문서에 바로 반영하지 않고,
현재 작업을 중단한 뒤 별도 spec 변경 절차로 분리한다.

### 8.6 G5/G6 — sample과 E2E

sample은 public contract 사용 예제다. framework 내부 helper, raw frame, private policy나
테스트 전용 adapter를 sample에 넣지 않는다.

- sample runner 전체를 실행하고 모든 sample을 통과시킨다.
- E2E all runner 전체를 실행한다.
- runner 목록과 공통 E2E 문서의 모든 scenario ID를 대조해 누락 scenario가 없는지 확인한다.
- retry로 성공한 경우 실제 transient bind 실패인지 로그로 확인한다.
- framework/runtime 수정이 발생하면 G3와 G4로 되돌아간다.

sample coverage는 다음 표로 기록한다. all runner에 포함되지 않은 실행 가능한 디렉터리는
개별 runner를 별도로 실행한다.

| 언어 | 종류 | scenario/sample | 공통 문서 | runner 포함 | 개별 실행 필요 | 결과 | 증거 |
|------|------|-----------------|-----------|-------------|----------------|------|------|

E2E는 먼저 현재 공통 문서 전체를 아래 config inventory에 고정한다. Config 8은 §7.1의
이관된 파일명을 사용한다.

| config | 공통 문서 | `.NET` | Java | Kotlin | Node.js | C++ |
|--------|-----------|--------|------|--------|---------|-----|
| 1 | `config-1-location-messaging.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 2 | `config-2-spot-service.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 3 | `config-3-pubsub.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 4 | `config-4-registration-codec.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 5 | `config-5-resilience-lifecycle.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 6 | `config-6-store-failure-recovery.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 7 | `config-7-monitoring.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 8 | `config-8-automatic-turn-dispatch.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 9 | `config-9-to-actor-messaging.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |
| 10 | `config-10-spot-actor-transfer.ko.md` | [ ] | [ ] | [ ] | [ ] | [ ] |

config 셀은 해당 문서의 모든 세부 scenario 행이 닫힌 뒤에만 체크한다. 각 문서의 heading에
있는 scenario ID를 한 행씩 다음 표에 옮긴다. 이름이 다른 구현을 의미가 같다고 추정하지
않고 동일한 topology, 자극, 불변식과 evidence를 확인한다.

| config/scenario ID | 언어 | runner selector | 구현/fixture | all runner 포함 | 결과 | 로그/marker | 비적용 승인 근거 |
|--------------------|------|-----------------|--------------|-----------------|------|-------------|------------------|

- 모든 공통 scenario와 언어의 조합은 `PASS` 또는 정식 계약에 근거해 리뷰에서 승인된
  `비적용`으로만 닫는다.
- runner 디렉터리가 없거나 all runner 배열에 없으면 자동으로 `비적용` 처리하지 않는다.
- all runner에 포함되지 않은 실행 가능한 fixture는 개별 runner를 실행하고 명령을 기록한다.
- all runner exit code가 0이어도 scenario 행, selector, marker가 하나라도 비어 있으면 G6는
  완료가 아니다.
- `.NET`의 `LocationMessaging`/`StoreFailure`, Java의 `RegistryMessaging`/
  `DiscoveryRegistryHa`, C++의 `DeliveryDispatch`처럼 이름이 다른 항목은 공통 config와
  세부 scenario 대응을 증명한다.
- C++ `SpotActorTransfer`처럼 all runner에 없는 항목은 개별 runner 증거를 반드시 남긴다.

### 8.7 Cross-language 검증

한 언어의 자체 E2E 성공을 cross-language 성공으로 간주하지 않는다. `store`, `codec`,
`messaging` 각각에 대해 producer 언어와 consumer 언어가 다른 모든 방향 조합을 inventory한다.
topology는 해당 공통 spec이 요구하는 direct, registry/store discovery, relay/route 경계를
각각 행으로 분리한다.

| feature | producer | consumer | topology | contract 근거 | runner/selector | 기대 marker | 결과 | 비적용 승인 근거 |
|---------|----------|----------|----------|---------------|-----------------|-------------|------|------------------|
| messaging | Node.js | `.NET` | 기존 smoke topology | ledger 연결 필요 | `framework/languages/node/cross-language/run_cross_language_smoke.sh` | marker 기록 필요 | [ ] | - |

위 행은 현재 존재하는 runner를 나타내는 seed일 뿐 완료 목록이 아니다. 각 feature마다
`.NET`, Java, Kotlin, Node.js, C++의 서로 다른 producer/consumer 방향 조합을 모두 행으로
추가한다. Kotlin이 Java runtime을 공유해도 public serialization/adapter 경계가 다르면 별도
행으로 둔다.

- 현재 언어 G6에서는 현재 언어와 이미 G7을 통과한 모든 이전 언어 사이의 양방향 행을
  구현하고 실행한다. 아직 순서가 오지 않은 언어의 fixture를 미리 수정하지 않는다.
- 기존 Node.js↔`.NET` smoke 하나로 Java, Kotlin, C++ 또는 store/codec 행을 대신하지 않는다.
- runner가 없으면 해당 언어 단계에서 public 표면만 사용하는 runner를 만들고 실행한다.
- 정식 계약상 그 조합에 기능이 적용되지 않을 때만 계약 위치와 reviewer 승인을 기록해
  `비적용`으로 닫는다. 구현이 없거나 어렵다는 사유는 비적용 근거가 아니다.
- 각 행은 실제 producer/consumer package 버전, topology, payload/packet identity, store key,
  codec과 성공 marker를 로그에 남긴다.
- C++ G7에서는 전체 조합을 다시 inventory해 빈 셀이 없는지 확인한다. 모든 행이 `PASS`
  또는 승인된 `비적용`이 아니면 최종 완료가 아니다.

### 8.8 G7 — 최종 closure

- [ ] 언어별 interface 문서의 구현 차이 표 갱신
- [ ] `implementation-gap.ko.md`의 해결 항목 제거 또는 완료 증거 연결
- [ ] feature map, README, guide와 sample 설명 갱신
- [ ] 배포 package의 public export 재검증
- [ ] repository 전체에서 제거 대상 symbol과 compatibility 이름이 검색되지 않음
- [ ] 미사용 source/file과 stale project/package entry가 남지 않음
- [ ] 전체 spec coverage matrix 갱신
- [ ] 별도 Codex read-only 최종 리뷰 `NO ISSUES`
- [ ] 작업 언어의 모든 gate 증거 기록
- [ ] 다음 언어로 넘어갈 수 있음을 명시

## 9. `.NET` 실행 계획

### 9.1 필수 gap checklist

- [ ] `SpotRef`와 route-ref resolver를 opaque `SpotHandle`로 교체
- [ ] handle 내부 snapshot/watch/refresh 및 안전한 1회 retry 구현
- [ ] request/join/worker의 public Yield 계열 제거
- [ ] Config 8을 `AutomaticTurnDispatch`로 이관하고 단일 완료 terminator 의미 검증
- [ ] actor send를 `Submit(CancellationToken): void`로 변경
- [ ] `ZLinkDispatchMode`, Spot/Stream dispatch mode 제거
- [ ] typed call의 `PacketName(...)` override 제거
- [ ] generic actor context의 `GetSpot()` overload 제거
- [ ] `IsJoined` 제거, nullable `SpotRid`를 membership 단일 기준으로 사용
- [ ] actor join 결과를 승인/거절 sealed record로 변경
- [ ] capability별 `IZLinkEndpointConnections` runtime handle 구현
- [ ] monitoring event를 kind별 sealed record로 변경
- [ ] internal `IZLinkBoundSessionFactory`가 public DI/export에 노출되지 않음을 검증
- [ ] one-way queue 수락과 monitoring 오류 관측 의미 구현
- [ ] 정식 spec 경로를 읽도록 documentation regression test 수정
- [ ] interface inventory의 보완 타입 전체 구현/검증

### 9.2 검증 명령

```bash
cd framework/languages/dotnet
dotnet restore Zlink.Framework.sln
dotnet build Zlink.Framework.sln --no-restore
dotnet test tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-build
dotnet test tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-build
dotnet test tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --no-build
dotnet test tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj --no-build
dotnet test tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --no-build
dotnet test Zlink.Framework.sln --no-build
./scripts/verify_packaged_contract.sh
./samples/run_samples.sh
./e2e/run_e2e_all.sh
```

### 9.3 `.NET` 완료 확인표

- [ ] G0 inventory/실패 테스트
- [ ] G1 interface/export
- [ ] G2 runtime/unit test
- [ ] G3 전체 contract/unit/integration green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [ ] G5 `samples/run_samples.sh` PASS
- [ ] G6 `e2e/run_e2e_all.sh` PASS
- [ ] G6 이전 완료 언어와 cross-language 양방향 matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] Java 작업 시작 승인

## 10. Java 실행 계획

### 10.1 필수 gap checklist

- [ ] handler/lifecycle/factory를 `CompletionStage` 완료 계약으로 변경
- [ ] Java callback/runtime이 blocking 없이 완료될 수 있는 capability 구현
- [ ] Java production 범위의 `join()`과 blocking wait 경로 제거
- [ ] one-way `ZLinkSubmitStage`와 blocking `await()` 제거
- [ ] typed session handler와 raw dispatcher 경계 분리
- [ ] actor context의 항상 예외인 default join method 제거
- [ ] Java 전용 framework cancellation token 제거
- [ ] 비동기 method의 불필요한 `Async` suffix 제거
- [ ] `ZLinkLocationKey`와 누락 interface inventory 구현
- [ ] 단일 `ZLinkEndpointConnections` 재사용 및 runtime handle 구현
- [ ] `SpotHandle`와 resolver 구현
- [ ] dispatch mode와 typed packet-name override 제거
- [ ] `getSpot()`과 `isJoined()` 제거
- [ ] actor join sealed result와 공통 join call 구현
- [ ] Spot context registry/close와 manager request overload 구현
- [ ] socket/registry/Spot/location monitoring 등록과 sealed event 구현
- [ ] route-mesh runtime options와 public export 정렬
- [ ] 정식 spec 경로 regression test 수정
- [ ] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관

### 10.2 검증 명령

```bash
cd framework/languages/java
./gradlew --no-daemon clean
./gradlew --no-daemon test contractTest fakeBackendTest integrationTest sampleTest
./gradlew --no-daemon check
./scripts/verify_packaged_contract.sh java
ZLINK_SAMPLE_LANGUAGES=java ./samples/run_samples.sh
./e2e/run_e2e_all.sh
```

### 10.3 Java 완료 확인표

- [ ] G0 inventory/실패 테스트
- [ ] G1 interface/export
- [ ] G2 runtime/unit test
- [ ] G3 전체 Gradle test source set green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [ ] G5 Java sample 전체 PASS
- [ ] G6 Java E2E 전체 PASS
- [ ] G6 `.NET`↔Java cross-language matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] Kotlin 작업 시작 승인

## 11. Kotlin 실행 계획

### 11.1 필수 gap checklist

- [ ] Java 목표 interface를 coroutine에서 자연스럽게 사용할 wrapper 구현
- [ ] `ZLinkSuspendingHandlers.kt`, `ZLinkCoroutineTurnAwait.kt` 등 Kotlin bridge의
  `runBlocking`, `join()`과 blocking wait 제거
- [ ] value bridge와 Unit-to-Void bridge의 nonblocking 완료 검증
- [ ] public yield extension 제거
- [ ] JVM signature clash가 없는 `await`, `awaitReply`, `awaitJoinReply` 구현
- [ ] typed/raw stream request와 send wrapper 정렬
- [ ] coroutine cancellation이 Java framework token을 새로 노출하지 않음을 검증
- [ ] `Flow` wrapper가 callback registration과 cleanup을 소유함을 검증
- [ ] Java runtime의 모든 공통 기능이 Kotlin surface에서도 도달 가능한지 검증
- [ ] Kotlin interface/function inventory 전체 contract test 추가
- [ ] Kotlin `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관

### 11.2 검증 명령

```bash
cd framework/languages/java
./gradlew --no-daemon \
  :zlink-framework-kotlin:test \
  :zlink-framework-kotlin:contractTest \
  :zlink-framework-kotlin:integrationTest
./scripts/verify_packaged_contract.sh kotlin
ZLINK_SAMPLE_LANGUAGES=kotlin ./samples/run_samples.sh
./e2e-kotlin/run_e2e_all.sh
```

### 11.3 Kotlin 완료 확인표

- [ ] G0 Kotlin extension/interface inventory
- [ ] G1 Kotlin public surface
- [ ] G2 coroutine/runtime unit test
- [ ] G3 Kotlin Gradle test green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [ ] G5 Kotlin sample 전체 PASS
- [ ] G6 Kotlin E2E 전체 PASS
- [ ] G6 Kotlin↔`.NET`/Java cross-language matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] Node.js 작업 시작 승인

## 12. Node.js 실행 계획

### 12.1 필수 gap checklist

- [ ] 모든 handler가 목표 `Promise` 반환형만 노출
- [ ] actor/bound-session one-way `Promise<void>` 제거
- [ ] yield call과 worker callback completion 제거
- [ ] `AbortSignal`을 장기 작업에만 제한
- [ ] optional lifecycle과 동기 factory union 제거
- [ ] branded `SpotHandle`과 내부 refresh 구현
- [ ] `getSpot`, `isJoined`, 중복 actor join call 제거
- [ ] actor join discriminated union 구현
- [ ] dispatch mode 제거와 message-kind별 unhandled policy 구현
- [ ] immutable metadata와 forwarding policy 구현
- [ ] typed session handler/registry 추가, raw stream escape hatch 제거
- [ ] manual runtime connection accessor와 worker producer 구현
- [ ] route builder 중복과 bound-session factory public export 제거
- [ ] 전역/채널별 request timeout 구현
- [ ] monitoring registration과 discriminated event union 구현
- [ ] location interface의 `I` prefix와 internal registration export 제거
- [ ] channelless channel/route/publisher 중복 표면 제거
- [ ] 정식 spec 경로 regression test 수정
- [ ] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [ ] contract/supporting artifact manifest 정렬과 배포 package의 `private` 제거

### 12.2 검증 명령

```bash
cd framework/languages/node
npm run build
npm run typecheck
npm run lint
npm test
npm run verify:coverage
npm run verify:samples
npm run verify:runtime-matrix
npm run verify:abi-matrix
npm run verify:cross-language
npm run verify:ci
npm run verify:release
./scripts/verify_packaged_contract.sh
./e2e/run_e2e_all.sh
```

### 12.3 Node.js 완료 확인표

- [ ] G0 inventory/실패 테스트
- [ ] G1 interface/export
- [ ] G2 runtime/unit test
- [ ] G3 build/typecheck/lint/test/coverage green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [ ] G5 sample 전체 PASS
- [ ] G6 E2E와 cross-language 전체 PASS
- [ ] G6 Node.js↔`.NET`/Java/Kotlin 양방향 matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] C++ 작업 시작 승인

## 13. C++ 실행 계획

### 13.1 필수 gap checklist

- [ ] lifecycle/transfer의 `.result()` blocking bridge 제거
- [ ] public error enum의 공통 계약 밖 값 제거
- [ ] callback 이름을 `snake_case`로 통일
- [ ] typed stream handler와 raw runtime 경계 분리
- [ ] route-mesh runtime options 구현
- [ ] one-way `result_t<void>`/actor `async()`를 `void submit()`으로 변경
- [ ] relay/disconnect를 `task_t<void>` 완료 계약으로 구현
- [ ] location watch와 message-flow runtime control 구현
- [ ] `spot_handle_t`와 handle resolver 구현
- [ ] `is_joined()`를 nullable `spot_rid()`로 교체
- [ ] join 결과를 승인/거절 `std::variant`로 변경
- [ ] user Spot/Entry Spot context와 worker/destroy 역할 분리
- [ ] async `find_spot`, `list_spots`, `close_spot` 구현
- [ ] `endpoint_connections_t` runtime handle 구현
- [ ] dispatch mode와 typed `packet_name(...)` override 제거
- [ ] monitoring event가 유효 variant만 표현하는지 검증
- [ ] installed header에서 runtime state/helper 비노출
- [ ] 정식 spec 경로 regression test를 fail-closed로 수정
- [ ] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [ ] CMake Framework/StreamConnector/FrameworkDependency/HttpClient install component와
  export set 분리

### 13.2 검증 명령

실제 package와 동일한 local dependency 경로를 사용해 별도 build directory에서 검증한다.

```bash
BUILD_DIR="$(pwd)/framework/languages/cpp/build-public-contract-gap"
cmake -S framework/languages/cpp -B "$BUILD_DIR" \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_E2E=ON
cmake --build "$BUILD_DIR" -j
ctest --test-dir "$BUILD_DIR" --output-on-failure
framework/languages/cpp/scripts/verify_packaged_contract.sh "$BUILD_DIR"
ZLINK_CPP_BUILD_DIR="$BUILD_DIR" ZLINK_CPP_E2E_BUILD_DIR="$BUILD_DIR" \
  framework/languages/cpp/samples/run_samples.sh
ZLINK_CPP_BUILD_DIR="$BUILD_DIR" ZLINK_CPP_E2E_BUILD_DIR="$BUILD_DIR" \
  framework/languages/cpp/e2e/run_e2e_all.sh
ZLINK_CPP_BUILD_DIR="$BUILD_DIR" ZLINK_CPP_E2E_BUILD_DIR="$BUILD_DIR" \
  framework/languages/cpp/e2e/SpotActorTransfer/run_e2e.sh
```

모든 C++ sample/E2E runner는 시작 시 실제 `ZLINK_CPP_BUILD_DIR`,
`ZLINK_CPP_E2E_BUILD_DIR`, 실행 파일과 core runtime 절대경로를 출력한다. 둘 중 하나가
`$BUILD_DIR` 밖의 산출물로 해석되거나 runtime이 현재 source보다 오래됐으면 즉시 실패한다.
이 출력이 없거나 G3와 다른 build tree를 사용한 실행은 G5/G6 증거로 인정하지 않는다.

coverage를 변경한 경우 다음 gate도 실행한다.

```bash
ctest --test-dir <coverage-build-dir> \
  -R '^test_cpp_framework_coverage_threshold$' -V
```

### 13.3 C++ 완료 확인표

- [ ] G0 inventory/실패 테스트
- [ ] G1 public/install header
- [ ] G2 runtime/unit test
- [ ] G3 build/ctest/install consumer green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [ ] G5 sample 전체 PASS
- [ ] G6 E2E 전체 start-order variant PASS
- [ ] G6 C++↔이전 4개 언어 양방향 matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] 모든 언어 gap closure 완료

## 14. 실행 증거 기록

각 명령은 아래 표에 한 행씩 기록한다. 부분 로그나 source 존재를 PASS 증거로 쓰지 않는다.

| 시각 | 언어 | gate | commit | 명령 | exit | 결과 요약 | 로그/증거 |
|------|------|------|--------|------|------|-----------|-----------|
| 2026-07-11 16:42 KST | `.NET` | G0/G2 | `99c58f4d0` + working tree | `e2e/AutomaticTurnDispatch/run_e2e.sh` | 1 | functional scenario PASS, E3 downstream/client deadline 충돌 발견 | `logs/20260711-164202-103366` |
| 2026-07-11 16:45 KST | `.NET` | G0/G2 | `99c58f4d0` + working tree | `e2e/AutomaticTurnDispatch/run_e2e.sh shutdown` | 0 | shutdown wait/recovery PASS | `logs/20260711-164506-108205` |
| 2026-07-11 16:48 KST | `.NET` | G3 | `99c58f4d0` + working tree | `dotnet build Zlink.Framework.sln --no-restore` | 0 | 0 warning, 0 error | `/tmp/zlink-dotnet-build-g3.log` |
| 2026-07-11 16:49 KST | `.NET` | G3 | `99c58f4d0` + working tree | `dotnet test Zlink.Framework.sln --no-build` | 0 | contract 37, unit 345, Redis 25, Stream Connector 72, sample regression 28, HTTP client 54 PASS | `/tmp/zlink-dotnet-sln-test2.log` |
| 2026-07-11 16:51 KST | `.NET` | G1 | `99c58f4d0` + working tree | `./scripts/verify_packaged_contract.sh` | 0 | manifest 6개 pack과 clean consumer reflection/run PASS | `dotnet packaged contract result=passed` |
| 2026-07-11 16:53 KST | `.NET` | G0 | `99c58f4d0` + working tree | bindings package/version/hash audit | 0 | `Systems.Zlink` 8.6.4, package SHA-256 확인 | `a5f77980710e35aabb1c0233ec4b00c1de53f62a94acb9c1ecdd3a471dd73594` |

Codex 리뷰도 같은 방식으로 기록한다.

| 시각 | 언어 | loop | reviewer 역할 | finding 수 | 판정 | 반영 commit | 증거 |
|------|------|------|---------------|------------|------|-------------|------|

## 15. 최종 전체 완료 조건

다음 조건을 모두 만족해야 이 계획을 완료로 바꿀 수 있다.

- [ ] 5개 언어의 G0~G7이 모두 체크되어 있다.
- [ ] 공통 spec coverage matrix의 모든 셀이 체크되어 있다.
- [ ] 모든 ledger 행이 완료 또는 승인된 비적용 근거를 가진다.
- [ ] `implementation-gap.ko.md`에 해결되지 않은 대상 언어 gap이 없다.
- [ ] 5개 언어의 public contract/unit/integration test가 모두 성공한다.
- [ ] 각 언어별 Codex DDD/POSD 최종 리뷰가 `NO DDD/POSD FINDINGS`다.
- [ ] 각 언어의 sample runner가 모두 성공한다.
- [ ] 각 언어의 E2E all runner가 모두 성공한다.
- [ ] cross-language store/codec/메시징 검증이 모두 성공한다.
- [ ] cross-language matrix의 모든 방향/feature/topology 행이 PASS 또는 승인된 비적용이다.
- [ ] 실제 배포 package/assembly/header의 public surface가 spec과 일치한다.
- [ ] 구형 public contract, compatibility layer와 dual path가 남아 있지 않다.
- [ ] 저장소 전체에서 `YieldDispatch`, public `Yield` terminator와 이전 YD marker가 제거되었다.
- [ ] 사용되지 않는 함수, type, 파일, test fixture와 build/package entry가 남아 있지 않다.
- [ ] README, guide, feature map, sample 문서와 진행표가 실제 상태를 반영한다.
- [ ] 최종 read-only 종합 리뷰가 `NO ISSUES`를 반환한다.

완료 선언에는 마지막 commit, 각 언어별 최종 명령의 exit code, Codex clean verdict와
남은 gap이 없다는 검색 증거를 함께 기록한다.

## 16. 관측·운영 표면 spec 추가 (작업 기록)

이 절은 기존 gap 해소와 별개로, 프로덕션 게임 백엔드에 필요한 **관측·운영 표면 3종**을 공통 spec
으로 새로 정의하고 언어별 public contract·e2e·샘플까지 문서화한 작업의 기록이다. **문서 작업만
완료했고 구현은 아직 시작하지 않았다.** 세 spec은 [공개 계약 관리](../framework/common/spec/public-contract-governance.ko.md)
의 승격 절차를 거치지 않은 **제안(Proposed)** 상태이며, 구현 착수 전 승격 리뷰로 이름·필드·기본값을
확정한다.

### 16.1 추가한 공통 spec (정본)

| 문서 | 소유 계약 | 상태 |
|------|-----------|------|
| `framework/doc/framework/common/spec/runtime-metrics.ko.md` | 계기 카탈로그·종류·라벨·성능·백엔드 경계 | 제안 |
| `framework/doc/framework/common/spec/flow-correlation.ko.md` | flow_id 상위 키·와이어·홉 커버리지·샘플링 | 제안 |
| `framework/doc/framework/common/spec/graceful-drain-handoff.ko.md` | drain 수명주기·location 연동·SPOT 정책 | 제안 |

설계 원칙: **framework는 신호·계약을 주고 백엔드·대시보드·배포 스크립트는 앱이 끼운다**(특정
백엔드 하드 의존 금지). 공개 표면은 최소(깊은 모듈), 공통 케이스는 무설정.

### 16.2 언어별 투영 (기존 문서 확장)

새 spec은 별도 언어 계약 문서를 만들지 않고 기존 monitoring/stream-connector 문서에 절로 투영했다.
§5.1 coverage ledger의 해당 행이 이 절들을 포함하도록 확장한다.

| 언어 | 확장한 문서 / 절 |
|------|------------------|
| `.NET` | `aspnet-core-monitoring.ko.md` §10(metrics)·§11(flow)·§12(drain) |
| Java | `spring-boot-monitoring.ko.md` §8·§9·§10, `stream-connector.ko.md`(closeReason) |
| Kotlin | `handler-interfaces.ko.md` §8(Java 재사용 델타) |
| Node | `nestjs-monitoring.ko.md` §10·§11·§12, `stream-connector.ko.md`(closeReason) |
| C++ | `cpp-monitoring.ko.md` §8·§9·§10 |

### 16.3 원본 계약 문서 반영 (함축 계약 정식화)

| 문서 | 반영 내용 |
|------|-----------|
| `location-runtime.ko.md` §2.1·§6.2 | peer row `Draining` 마커(additive) + 배치 제외/연결 유지 규칙 |
| `message-flow-tracing.ko.md` §3 | `message_flow_event_t`에 `flow_id`/`flow_origin` additive 필드 |
| `languages/{java,node}/stream-connector.ko.md` | disconnect 표면 `closeReason`(닫힌 enum, `server_drain`) |
| `doc/principal/framework-option-builder-naming.ko.md` | `useDrainPolicy(enum)`을 `use*` 패턴으로 승인 |

### 16.4 e2e·샘플

- `framework/doc/framework/common/e2e/config-11-observability-ops.ko.md` — 배포 조건에서 flow 로그·
  메트릭·drain을 검증. §8.6 G5/G6과 이 config를 연결한다.
- `framework/doc/framework/common/sample/bingo/README.ko.md` §17 — 기존 Bingo에 "관측·운영 켜기"
  워크스루 추가(새 샘플 대신 기존 샘플 강화).

### 16.5 구현 시 남는 작업 (이 계획으로의 편입)

- **§6 필수 gap ledger**: 세 spec의 public symbol/동작을 언어별 ledger 행으로 추가한다(승격 후).
- **회귀 매트릭스**: 스펙이 정의한 `RMETRIC-001~015`, `DRAIN-001~017`, `MFLOW-EXT-001~013`을 언어별
  실제 테스트로 만든다(누락은 `implementation-gap.ko.md`에 parity gap으로 기록).
- **§5 coverage matrix**: §16.1 세 행의 셀은 각 언어 구현·테스트 완료 시 체크한다.
- **와이어/저장 변경**: `flow_id` envelope/헤더(`0x10`), peer row `Draining` 필드(Redis JSON additive),
  connector `closeReason`은 구현이 문서 계약을 따라야 한다.

### 16.6 리뷰 근거

두 차례 적대적 리뷰(① 공통 spec 정합성 — location-runtime/spot-actor 대조, ② POSD·사용성 — 깊은
모듈·언어 간 일관성)를 거쳐 P0/P1 결함을 반영했다. 스펙 안의 "정직한 한계"(monotonic flow_id 전역
비유일성, corr keying 한계, reconnect hint 후속 스펙, room `migrate` 후속 스펙)는 구현에서 과장하지
않고 그대로 유지한다.
