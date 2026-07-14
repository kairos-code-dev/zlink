# Framework public contract 구현 gap 해소 계획

## 1. 목적

이 계획은 `framework/doc/framework/spec/`에 고정된 목표 계약과 각 언어의 실제
framework 구현 사이의 차이를 모두 없애기 위한 실행 문서다. 첫 작업 언어는 `.NET`이다.
각 언어의 구현과 완료 판정은 서로 독립적으로 진행한다.

작업 순서는 다음과 같이 고정한다.

```text
.NET -> Java -> Kotlin -> Node.js -> C++
```

위 순서는 결과를 정리하는 기준이다. 여러 언어를 동시에 작업할 수 있으며, 앞 언어의 gate가
끝나지 않았다는 이유만으로 뒤 언어의 작업이나 완료 표시를 막지 않는다. Java와 Kotlin은
runtime과 build를 공유하지만 public 사용성과 테스트 표면이 다르므로 완료 판정을 분리한다.

이 문서는 실행 가이드이자 현재 상태 보드다. 작업이 끝난 뒤 한꺼번에 채우지 않고, 구현과
검증이 끝날 때마다 체크박스와 결과 요약을 갱신한다. 시간순 명령 결과와 반복 리뷰 이력은
[별도 구현 로그](./log/framework-public-contract-gap-implementation/README.ko.md)에 기록한다.

## 2. 단일 기준과 우선순위

계약과 구현이 다르면 아래 순서로 판단한다.

1. 공통 정식 spec의 기능, 동작, 오류와 완료 조건
2. 해당 언어 디렉터리의 정식 public interface 문서 집합
3. 공통 또는 언어별 guide에 기록된 사용 원칙
4. `90-implementation-gap.ko.md`의 현재 구현 차이
5. 실제 public source, package export와 contract test
6. sample과 E2E

정식 spec을 현재 구현의 최소 공통분모에 맞춰 축소하지 않는다. 구현 중 정식 spec 자체의
모순이나 구현 불가능한 계약을 발견하면 코드를 우회하지 않고 작업을 중단한다. 공통 spec,
언어별 interface spec, 구현 차이 표를 먼저 설계 리뷰한 뒤 다시 시작한다.

### 2.1 Public interface freeze

각 언어의 public contract interface는 이미
`framework/doc/framework/spec/server/languages/<lang>/`의 정식 계약 문서 집합에 확정되어
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

- [공개 계약 관리](../framework/spec/00-public-contract-governance.ko.md)
- [언어별 구현 차이](../framework/spec/90-implementation-gap.ko.md)
- [비동기 실행 정책](../framework/spec/04-async-execution-policy.ko.md)
- [Spot 주소 메시징](../framework/spec/server/24-spot-address-messaging.ko.md)
- [소프트웨어 설계 원칙](../../../doc/principal/software-design-principles.md)

언어별 계약 소유권은 다음과 같다. 한 문서만 읽고 전체 public surface를 판정하지 않는다.

| 언어 | 전체 interface 기준 | 함께 읽어야 하는 기능별 정식 계약 | 비고 |
|------|---------------------|----------------------------------|------|
| `.NET` | [handler-interfaces](../framework/spec/server/languages/dotnet/02-handler-interfaces.ko.md) | 같은 디렉터리의 ASP.NET Core, actor, channel, location, monitoring, Spot, stream, session dispatch, Spot node 문서 | `README.ko.md`의 범위와 취소 규칙도 적용 |
| Java | [handler-interfaces](../framework/spec/server/languages/java/02-handler-interfaces.ko.md) | Spring Boot channel/Spot/actor-session/stream/registry/monitoring과 Stream Connector 문서 | handler 문서는 interface, annotation, context, option만 소유 |
| Kotlin | [handler-interfaces](../framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md)와 Java 계약 전체 | Kotlin 전용 coroutine, extension, `Flow` 계약 | Java 표면을 복사하지 않고 함께 적용 |
| Node.js | [handler-interfaces](../framework/spec/server/languages/node/02-handler-interfaces.ko.md) | 같은 디렉터리의 Stream Connector 계약과 공통 framework spec | `README.ko.md`의 범위와 취소 규칙도 적용 |
| C++ | [cpp-framework-interfaces](../framework/spec/server/languages/cpp/02-framework-interfaces.ko.md) | channel, Spot, stream, registry, monitoring, application framework, HTTP hosting/server, actor gateway relay 문서 | [handler-interfaces](../framework/spec/server/languages/cpp/02-framework-interfaces.ko.md)는 handler 정렬 규칙이며 전체 surface 목록이 아님 |

`02-framework-interfaces.ko.md`는 상위 모델을 위한 guide이므로 public interface 권위 문서로
간주하지 않는다. 다만 guide의 표준 사용 패턴이 확정 계약과 충돌하지 않는지는 G7에서 확인한다.

## 3. 완료 판정 원칙

### 3.1 체크박스 규칙

체크박스는 다음 증거가 모두 있을 때만 `[x]`로 바꾼다.

- 목표 spec 항목과 실제 public symbol의 대응이 기록되어 있다.
- 구현 또는 제거 작업이 끝났다.
- public contract test가 정확한 타입과 시그니처를 검증한다.
- 관련 unit test가 성공한다.
- 명령, exit code, 실행 시각과 commit이 해당 언어의 별도 구현 로그에 기록되어 있다.

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
언어 fixture를 고쳐도 같은 규칙을 적용한다. 재개 상태는 전체 진행 보드에 반영하고, 시간순
이력은 해당 언어의 별도 구현 로그에 기록한다.

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
| 1 | `.NET` | [x] | [x] | [x] | [x] | [x] | [x] | [x] | [x] | 완료 |
| 2 | Java | [x] | [x] | [x] | [x] | [ ] | [x] | [ ] | [ ] | 진행 |
| 3 | Kotlin | [x] | [x] | [x] | [x] | [ ] | [x] | [ ] | [ ] | 진행 |
| 4 | Node.js | [x] | [x] | [x] | [x] | [x] | [x] | [x] | [x] | 완료 |
| 5 | C++ | [x] | [x] | [x] | [x] | [ ] | [x] | [ ] | [ ] | 진행 |

현재 Java, Kotlin, C++ 작업을 언어별 gate에 따라 독립적으로 진행한다. Node.js는 모든 gate를
완료했다. 완료한 gate의 상세 finding, 명령과 검증 결과는 해당 언어의
계약 ledger, G4 finding ledger와 구현 로그에서 확인한다.

`53-flow-correlation.ko.md`, `51-runtime-metrics.ko.md`, `54-graceful-drain-handoff.ko.md`와 Config 11도
이 계획의 필수 범위다. 별도 후속 계획으로 분리하지 않고, 각 언어의 G0~G7 안에서 함께 구현하고
검증한다.

## 5. 공통 spec coverage matrix

각 셀은 해당 문서의 모든 규범 문장을 symbol/동작/test ledger로 옮겼을 때만 체크한다.
문서 일부만 관련 있어도 `N/A`로 처리하지 않고 비적용 근거를 ledger에 기록한다.

| 공통 spec | `.NET` | Java | Kotlin | Node.js | C++ |
|-----------|--------|------|--------|---------|-----|
| `README.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `01-overview.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `05-framework-api.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `02-interaction-model.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `03-message-model.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `04-async-execution-policy.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `10-channel-topology.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `01-system-structure.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `20-spot-messaging.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `21-spot-node.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `22-actor-model.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `23-spot-actor.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `02-framework-interfaces.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `30-stream-session.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `31-session-actor-dispatch.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `32-stream-connector.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `24-spot-address-messaging.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `40-location-runtime.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `41-location-store-redis.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `50-runtime-monitoring.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `52-message-flow-tracing.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `53-flow-correlation.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `51-runtime-metrics.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `54-graceful-drain-handoff.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `00-public-contract-governance.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| `90-implementation-gap.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |

이 표도 고정 개수로 간주하지 않는다. G0에서 다음 명령의 결과와 표의 행을 비교하고 새
공통 정식 spec이 있으면 해당 행을 먼저 추가한다.

```bash
rg --files framework/doc/framework/spec -g '*.ko.md' \
  | sed 's#framework/doc/framework/spec/##' | rg -v '/' | sort
```

### 5.1 언어별 정식 계약 coverage ledger

아래 표의 한 행은 문서 파일 하나를 뜻한다. 각 문서의 규범 문장마다 §6 ledger ID를
연결하고, public symbol이 없는 동작 계약도 동작 test ID에 연결한다. 단순히 파일을
읽었다는 이유로 체크하지 않는다. 실행 시 새 파일이 발견되면 이 표에 행을 추가한다.

| 언어 | 정식 계약 문서 | ledger/test 연결 | 상태 |
|------|----------------|------------------|------|
| `.NET` | `README.ko.md` | `DN-DOC-001` | [x] |
| `.NET` | `handler-interfaces.ko.md` | `DN-DOC-002` | [x] |
| `.NET` | `handler-interfaces.ko.md` §17 | `DN-016`, 고정 API/package snapshot | [x] |
| `.NET` | `handler-interfaces.ko.md` | `DN-DOC-004` | [x] |
| `.NET` | `system-structure.ko.md` | `DN-DOC-005` | [x] |
| `.NET` | `system-structure.ko.md` | `DN-DOC-006` | [x] |
| `.NET` | `system-structure.ko.md` | `DN-DOC-007` | [x] |
| `.NET` | `system-structure.ko.md` | `DN-DOC-008` | [x] |
| `.NET` | `system-structure.ko.md` | `DN-DOC-009` | [x] |
| `.NET` | `31-session-actor-dispatch.ko.md` | `DN-DOC-010` | [x] |
| `.NET` | `21-spot-node.ko.md` | `DN-DOC-011` | [x] |
| `.NET` | `32-stream-connector.ko.md` | `DN-DOC-012`, connector contract/package test | [x] |
| `.NET` | `02-framework-interfaces.ko.md` | interface spec이 아닌 상위 사용 모델 guide이며 G7 guide 검토에 연결 | 비적용 |
| Java | `README.ko.md` | `JV-DOC-001` | [x] |
| Java | `handler-interfaces.ko.md` | `JV-DOC-002` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-003` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-004` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-005` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-006` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-007` | [x] |
| Java | `system-structure.ko.md` | `JV-DOC-008` | [x] |
| Java | `32-stream-connector.ko.md` | `JV-DOC-009` | [x] |
| Kotlin | `README.ko.md` | - | [ ] |
| Kotlin | `handler-interfaces.ko.md` | - | [ ] |
| Node.js | `01-system-structure.ko.md` | `ND-DOC-001` | [x] |
| Node.js | `02-handler-interfaces.ko.md` | `ND-DOC-002` | [x] |
| Node.js | `03-stream-connector.ko.md` | `ND-DOC-003` | [x] |
| C++ | `README.ko.md` | `CPP-DOC-001` | [x] |
| C++ | `cpp-framework-interfaces.ko.md` | `CPP-DOC-002` | [x] |
| C++ | `actor-gateway-session-relay.ko.md` | `CPP-DOC-008` | [x] |
| C++ | `cpp-application-framework.ko.md` | `CPP-DOC-004` | [x] |
| C++ | `cpp-channel-messaging.ko.md` | `CPP-DOC-005` | [x] |
| C++ | `cpp-embedded-http-server.ko.md` | `CPP-DOC-012` | [x] |
| C++ | `cpp-http-hosting.ko.md` | `CPP-DOC-011` | [x] |
| C++ | `cpp-monitoring.ko.md` | `CPP-DOC-009` | [x] |
| C++ | `cpp-registry.ko.md` | `CPP-DOC-010` | [x] |
| C++ | `cpp-spot.ko.md` | `CPP-DOC-006` | [x] |
| C++ | `cpp-stream.ko.md` | `CPP-DOC-007` | [x] |
| C++ | `handler-interfaces.ko.md` | `CPP-DOC-003` | [x] |

다음 파일은 정식 interface coverage 분모에서 제외하되 G7 문서 정합성 검토에는 포함한다.

| 언어 | guide 문서 | 제외 근거 | G7 검토 |
|------|------------|-----------|---------|
| `.NET` | `02-framework-interfaces.ko.md` | 상위 모델 guide; G0 hash와 G7 정합성 리뷰에서 정식 interface와 충돌 없음 확인 | [x] |
| Java | `02-framework-interfaces.ko.md` | 상위 모델 guide | [ ] |
| Node.js | 공통 `02-framework-interfaces.ko.md` | 상위 모델 guide | [x] |
| C++ | `02-framework-interfaces.ko.md` | 상위 모델 guide | [ ] |

Kotlin 행은 Kotlin 전용 계약만 나타낸다. Java 계약의 각 행에는 Kotlin 적용 여부와 Kotlin
test ID도 함께 기록해, 상속된 public surface가 coverage에서 빠지지 않게 한다.

## 6. 필수 gap ledger

각 언어의 상세 gap, 실제 symbol, 구현 차이, 연결된 test와 검증 증거는 계획 문서에 복사하지
않는다. 언어별 G0 계약 ledger에서 관리하고, 이 계획에는 ledger 작성 및 완료 여부만 표시한다.

- [`.NET` G0 공개 계약 ledger](./log/framework-public-contract-gap-implementation/dotnet-g0-contract-ledger.ko.md)
- Java: G0 시작 시 `java-g0-contract-ledger.ko.md`를 만든다.
- Kotlin: G0 시작 시 `kotlin-g0-contract-ledger.ko.md`를 만든다.
- Node.js: G0 시작 시 `node-g0-contract-ledger.ko.md`를 만든다.
- [C++ G0 공개 계약 ledger](./log/framework-public-contract-gap-implementation/cpp-g0-contract-ledger.ko.md)

각 ledger는 overload, nullable, generic 제약과 default parameter를 서로 다른 검증 대상으로
기록한다. 현재 상태는 §4 진행 보드와 각 언어의 완료 확인표에서만 갱신한다.

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
- [ ] flow id의 자동 생성, 모든 홉 전파, 네 origin과 async/coroutine/Promise 문맥 정리
- [ ] flow header codec 일괄 교체, unknown mandatory flag protocol error와 구형 decoder/dual relay 부재
- [ ] 언어 표준 계측기 기반 runtime metric catalog, 계기 소유권과 닫힌 label
- [ ] peer row typed `Draining` 필드, readiness/admission, owner lease와 actor/SPOT 정리 순서
- [ ] 공유 drain의 단일 terminal result, 기본 30초 deadline과 waiter cancellation 분리
- [ ] versioned `session-closing` 제어 프레임, connector close reason과 bounded 종료
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

- [x] 공통 문서를 `config-8-automatic-turn-dispatch.ko.md`로 바꾸고 public `Yield` 호출을
  요구하는 설명, scenario ID와 marker를 제거한다.
- [x] 단일 completion terminator를 기다릴 때 framework가 self-deadlock을 피하고 인과 관계가
  있는 후속 작업만 같은 논리적 turn에서 처리한다는 계약을 검증한다.
- [x] 같은 Spot, actor, timer의 보호 상태에는 callback 완료 전 관련 없는 작업이 재진입하지
  않으며, 다른 actor/timer와 관련 없는 실행 줄은 진행할 수 있음을 검증한다.
- [x] continuation이 원래 실행 문맥으로 돌아가고 timeout, 언어별 cancellation, shutdown 뒤
  대기와 실행 줄이 정리되는지 검증한다.
- [x] local/remote Spot, route bridge, session relay와 worker 경계의 기존 업무 불변식을 새
  public 표면으로 이관한다.
- [x] 현재 작업 언어의 fixture 디렉터리, project/package 이름, runner 배열, selector,
  evidence marker와 문서 링크를 새 scenario 이름으로 함께 바꾼다.
- [x] 현재 작업 언어 범위 검색에서 `YieldDispatch`, public `Yield` terminator, 이전 YD marker가
  남지 않았음을 기록한다. 일반 언어 키워드나 내부 scheduler 용어는 public API와 구분한다.
- [x] 새 scenario가 현재 언어 coverage matrix에 있고 실제 runner에서 한 번만 실행되는지
  확인한다.

Config 11도 동일한 원칙으로 각 언어 G0에서 fixture와 runner를 만들고 G6에서 전체 시나리오를
실행한다. OBS-A1~A4, OBS-B1~B4, OBS-C1~C5는 우선순위와 관계없이 모두 필수다. flow wire
protocol mismatch와 async 문맥 누수는 E2E만으로 충분히 관찰할 수 없으므로 G2 unit/contract test를
별도로 둔다.

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
4. package 이름, version, hash, archive 내용과 local repository 경로를 해당 언어의 구현 로그에
   기록한다.
5. 중앙 version pin만 갱신한다. `.NET`은 `Directory.Packages.props`, Java/Kotlin은
   `gradle/libs.versions.toml`, Node.js는 root `package.json`, C++는
   `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION`을 사용한다.
6. framework가 새 package artifact를 실제로 resolve했는지 dependency graph와 절대경로로
   확인한 뒤 현재 언어 G1부터 다시 실행한다.
7. 공용 native/core, shared dependency 또는 이미 완료한 언어의 package가 바뀌었으면 §3.3의
   역방향 gate 무효화를 적용한다.

bindings 기능이 이미 충분해도 audit 행과 version/hash 증거 없이 이 gate를 닫지 않는다.

### 7.3 core/bindings 기능 재사용 감사

각 언어의 framework가 core 또는 해당 언어 bindings에 이미 있는 기능을 다시 구현하지 않는지
G0에서 구현 전에 먼저 확인하고, G4와 G7에서 다시 감사한다. 코드 모양이 비슷하다는 이유만으로
중복으로 판정하지 않고,
입력·출력, 오류, 소유권, thread/lifecycle과 완료 의미가 같은 기능인지 계약 기준으로 비교한다.

G0에서 찾은 모든 후보는 실제 framework 수정 전에 §6 gap ledger 또는 삭제 목록에 작업 ID로
등록한다. 각 후보는 `bindings 위임과 framework 중복 삭제`, `bindings 공개 기능 선행`, `책임이
달라 유지` 중 하나로 판정해야 한다. 판정 근거와 검증 계획이 없는 후보가 남아 있으면 G1을 시작하지
않는다. 따라서 이 감사 결과는 사후 개선 제안이 아니라 현재 언어의 필수 구현·삭제 작업 목록이다.

언어별 또는 framework 계층별 type은 해당 계층의 사용성, 도메인 의미와 언어 관례에 맞게 따로
정의할 수 있다. type 이름, 필드 모양이나 표현이 비슷하다는 이유만으로 중복 구현으로 판정하거나
bindings type을 public framework 계약에 그대로 노출하도록 요구하지 않는다. 이 감사의 대상은
이미 public core/bindings operation으로 제공되는 같은 동작, 알고리즘, protocol 처리 또는 resource
lifecycle을 framework가 다시 구현한 경우다. 내부에서 의미가 완전히 같은 typed 값을 문자열이나
raw buffer로 바꿨다가 되돌리는 불필요한 변환은 별도 정보 손실·복잡도 finding으로 검토한다.

감사 순서는 다음과 같다.

1. `core/include/zlink.h`와 관련 core 정식 spec에서 제공 기능과 계약을 확인한다.
2. 현재 언어 bindings의 실제 배포 package가 그 기능을 public API로 제공하는지 확인한다.
3. framework production source에서 같은 protocol, codec, connection, routing, monitoring, timeout,
   lifecycle 또는 resource ownership을 자체 구현한 후보를 찾는다.
4. 계약이 같은 기능이면 bindings public API에 위임하고 framework의 중복 함수, type, 파일, test와
   package entry를 삭제한다.
5. core에는 있지만 bindings public API가 없으면 native symbol 직접 호출, reflection, friend assembly나
   private 접근으로 우회하지 않는다. §7.2의 bindings 공개 기능 선행 절차로 분리한다.
6. framework가 더 높은 수준의 의미를 제공하거나 core 계약과 다르면 유지 근거와 서로 다른 책임을
   ledger에 기록한다. “현재 코드가 이미 있음”은 유지 근거가 아니다.
7. 삭제 또는 위임 뒤 bindings/core 회귀, framework contract/unit test와 실제 package consumer를
   다시 실행한다.

G4 read-only reviewer는 framework source만 검색하지 않고 core header와 현재 bindings의 public export를
함께 읽는다. G7에서는 확정된 중복 후보가 남아 있지 않다는 exact-symbol 검색과 package dependency
증거를 다시 확인한다.

| 언어 | core 기능/symbol | bindings public symbol | framework 중복 후보 | 판정과 책임 차이 | 삭제/위임 검증 | 상태 |
|------|------------------|------------------------|----------------------|------------------|----------------|------|
| `.NET` | 해당 없음; framework 상위 correlation | connector internal `ZlinkStreamCorrelation` | framework가 동일 correlation 생성 기능을 별도 구현 | connector owner로 단일화하고 framework counter 삭제 | mixed generation uniqueness/flow E2E | 완료 |
| Java | 실행 시 capability별 행 추가 | - | - | - | - | 대기 |
| Kotlin | 실행 시 capability별 행 추가 | - | - | - | - | 대기 |
| Node.js | bindings socket/context와 stream wire 기능 | binding package 공개 socket/context API, Stream Connector correlation codec | framework flow correlation 생성, reconnect 계수와 context 종료 책임 중복 후보 | correlation codec은 connector 소유로 단일화하고 framework는 상위 flow context만 소유; reconnect 계수는 connector lifecycle 소유; context 종료는 backend adapter 소유 | ABI/runtime matrix, mixed flow E2E, 실제 package consumer | 완료 |
| C++ | ledger §6에 후보 4행 등록 | `spot_node_t::create_route_bridge()`, CAPI timer `fire_count` | route relay·timer scheduler 재구현 없음; stream header codec·LZ4 codec은 framework/connector 의도된 mirror | bridge/timer는 bindings 위임 유지, 두 mirror는 책임 분리 유지 | mirror 바이트/동작 동일성 test를 G2에서 고정 | 진행 |

## 8. 언어별 실행 절차

### 8.1 G0 — inventory와 실패 테스트 고정

1. 현재 언어의 계약 소유권 표와 모든 정식 상세 spec을 읽는다.
2. public source와 실제 export를 추출한다.
3. bindings public capability와 package version/hash를 audit하고 §7.2 충족 여부를 기록한다.
4. core와 bindings public API를 framework 구현과 비교하고 §7.3 재사용 후보를 먼저 확정한다.
5. §6 gap ledger와 삭제 목록을 symbol 단위로 완성한다.
6. contract test가 정식 spec 경로를 읽도록 먼저 고친다.
7. 목표 시그니처와 동작을 검증하는 실패 테스트를 추가한다.
8. 삭제 대상 public symbol도 “없음”을 검증하는 테스트를 추가한다.
9. 공통 E2E와 확정 spec의 모순을 inventory하고 §7.1 이관 ledger를 닫는다.

언어별 spec 파일 목록은 기억이나 고정 개수로 판단하지 않고 작업 시점의 checkout에서
다음 명령으로 만든다.

```bash
rg --files framework/doc/framework/spec/server/languages/<lang> -g '*.ko.md' | sort
```

출력된 모든 파일에 `검토 완료`, `비적용 근거` 또는 ledger ID 중 하나가 있어야 한다.

체크리스트:

- [x] spec-to-symbol inventory 완료
- [x] public export snapshot 완료
- [x] 기존 caller와 sample compile 영향 목록 완료
- [x] stale E2E/fixture/marker 이관 목록 완료
- [x] Config 8 계약 이관 완료
- [x] bindings public capability/package audit 완료
- [x] core/bindings 기능 재사용 감사와 중복 후보 판정 완료
- [x] 실패 contract test 추가
- [x] 실패 unit test 추가
- [x] 구현 순서와 책임 owner 확정

### 8.2 G1 — public interface와 export 정렬

- 이미 확정된 언어별 목표 interface를 시그니처 변경 없이 구현한다.
- 제거 대상 symbol은 package root와 배포 산출물에서도 제거한다.
- interface 변경과 runtime 구현을 한 번에 섞지 말고 compile 가능한 작은 단위로 진행한다.
- public source comment는 정식 계약과 같은 의미로 갱신한다.

체크리스트:

- [x] 전체 public type과 member 시그니처 일치
- [x] overload/generic/nullable/default 값 일치
- [x] cancellation 표현 일치
- [x] internal helper/export 비노출
- [x] compatibility alias가 정식 package root에 없음
- [x] 이전 interface와 overload가 source 및 binary export에서 제거됨
- [x] 이전 표면 전용 구현, fixture와 문서가 함께 제거됨
- [x] 이전 동작의 업무 불변식을 검증하던 테스트는 삭제하지 않고 새 public 표면으로 이관됨
- [x] 실제 package/assembly/header export snapshot 일치
- [x] 현재 언어의 `verify_packaged_contract.sh` 구현과 clean consumer 실행 성공

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

- [x] 정상 경로 unit test
- [x] invalid state를 타입 또는 API 정의로 만들 수 없음을 검증
- [x] timeout/cancellation/close/shutdown test
- [x] stale location 및 retry 경계 test
- [x] queue full/route-not-ready/수락 후 실패 test
- [x] callback 순서와 self-deadlock 방지 test
- [x] ownership/disposal/leak test
- [x] 동시성 및 반복 실행 test
- [x] 제거한 구형 경로로 진입하는 runtime branch가 없음
- [x] 삭제된 file이 build/package/install 목록에 남아 있지 않음

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
- [ ] core/bindings public API와 같은 기능을 framework가 다시 구현하지 않는가

각 반복의 review manifest에는 다음 범위를 반드시 기록한다.

- 해당 언어의 production source 전체
- public package/export와 DI/annotation/reflection/generated-source registration
- project, solution, Gradle, npm, CMake, install과 package wiring
- 해당 언어가 사용하는 공용 runtime 경계
- 제외한 디렉터리와 제외 근거

변경 파일과 인접 파일만 읽은 리뷰로는 `NO DDD/POSD FINDINGS`를 선언할 수 없다. repository
전체를 무조건 리팩터링하는 뜻은 아니지만, 현재 언어 framework 안의 멀리 떨어진 얕은
모듈, 중복 abstraction, dead registration과 stale build entry도 검토 분모에 포함한다.

finding마다 위험 신호, 두 가지 이상 대안, 선택 이유, 테스트와 상태를 언어별 G4 ledger에 기록한다.
상세 finding, 비교 대안과 테스트 증거는 plan에 누적하지 않는다.
현재 `.NET` 기록은 [`.NET G4 DDD/POSD 리팩터링 ledger`](log/framework-public-contract-gap-implementation/dotnet-g4-refactoring-ledger.ko.md)를 참조한다.
plan에는 반복 절차, 종료 조건과 gate 상태만 유지한다.
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
- 언어별 sample 문서를 계약 기준으로 사용하지 않는다. 각 sample은
  `framework/doc/framework/common/sample/`의 대응하는 공통 spec을 서버 역할,
  메시지 이름·필드, 상태 전이, codec, self-check 순서와 완료 기준별로 대조한다.
- 대조에서 찾은 불일치는 현재 public contract로 표현할 수 있는지 먼저 확인한다.
  가능하면 sample 구현·runner·self-check를 공통 spec에 맞게 수정하고, 새 public API가
  필요하면 sample에서 우회하지 않고 public contract gap으로 분리한다.
- 각 sample은 정적 대조만으로 완료 표시하지 않는다. 개별 runner의 실제 client
  self-check와 통합 runner 통과 결과를 함께 기록한다.
- 공통 sample spec 6종 각각에 대해 언어별로 `역할과 연결`, `메시지 이름과 필드`,
  `상태 전이`, `codec`, `client self-check`, `runner와 완료 marker`를 한 행씩 확인한다.
  결과는 [Java/Kotlin G5 공통 sample spec ledger](./log/framework-public-contract-gap-implementation/java-kotlin-g5-sample-ledger.ko.md)에
  언어별로 기록하며, Node.js 결과는
  [Node.js G5 공통 샘플 대조 기록](./log/framework-public-contract-gap-implementation/node-g5-sample-ledger.ko.md)에
  기록한다. 여섯 영역 중 하나라도 `gap` 또는 `검토 중`이면 해당 언어 G5는 완료로 표시하지 않는다.
- 기존 runner PASS나 `.NET` 파일 대응 inventory는 구현 증거로 재사용할 수 있지만 공통 spec 대조를
  대신하지 않는다. 공통 spec과 다른 이름·필드·역할·검증 순서를 찾으면 inventory의 과거 `done`
  표기도 다시 열고 구현과 runner를 함께 수정한다.
- E2E all runner 전체를 실행한다.
- runner 목록과 공통 E2E 문서의 모든 scenario ID를 대조해 누락 scenario가 없는지 확인한다.
- retry로 성공한 경우 실제 transient bind 실패인지 로그로 확인한다.
- framework/runtime 수정이 발생하면 G3와 G4로 되돌아간다.

sample coverage는 다음 표로 기록한다. all runner에 포함되지 않은 실행 가능한 디렉터리는
개별 runner를 별도로 실행한다.

| 언어 | 종류 | scenario/sample | 공통 문서 | runner 포함 | 개별 실행 필요 | 결과 | 증거 |
|------|------|-----------------|-----------|-------------|----------------|------|------|
| `.NET` | sample | Bingo 관측·운영 §17 | `sample/bingo/README.ko.md` | [x] | [x] | 완료 | `.NET` 구현 로그 |
| Java | sample | Bingo 관측·운영 §17 | `sample/bingo/README.ko.md` | [x] | [x] | 완료 | `ZLINK_SAMPLE_LANGUAGES=java ./samples/run_samples.sh` |
| Kotlin | sample | Bingo 관측·운영 §17 | `sample/bingo/README.ko.md` | [x] | [x] | 완료 | `ZLINK_SAMPLE_LANGUAGES=kotlin ./samples/run_samples.sh` |
| Node.js | sample | Bingo 관측·운영 §17 | `sample/bingo/README.ko.md` | [x] | [x] | 완료 | protobuf 생성 타입, flow 연속성, 실제 metric, drain, self-join과 client별 marker를 shell·PowerShell runner에서 검증 |
| C++ | sample | Bingo 관측·운영 §17 | `sample/bingo/README.ko.md` | [ ] | [ ] | 대기 | - |

Bingo는 기존 게임 smoke 성공만으로 완료하지 않는다. G5에서 각 언어 샘플이 별도 flow id 설정 없이
노드별 flow 로그를 남기고, 언어 표준 meter/registry 연결 예제를 제공하며, Play의
`DrainNatural` 정책과 자동 또는 명시 drain을 실제 public API로 시연하는지 확인한다. 샘플 runner는
기존 게임 self-check를 유지하고 관측 기능을 켠 실행에서도 `bingo=completed`를 확인한다. 다중 노드
계기 정합과 실제 drain handoff는 Config 11이 소유하므로 샘플에 test-only evidence API를 추가하지
않는다.

E2E는 먼저 현재 공통 문서 전체를 아래 config inventory에 고정한다. Config 8은 §7.1의
이관된 파일명을 사용한다.

| config | 공통 문서 | `.NET` | Java | Kotlin | Node.js | C++ |
|--------|-----------|--------|------|--------|---------|-----|
| 1 | `config-1-location-messaging.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 2 | `config-2-spot-service.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 3 | `config-3-pubsub.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 4 | `config-4-registration-codec.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 5 | `config-5-resilience-lifecycle.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 6 | `config-6-store-failure-recovery.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 7 | `config-7-monitoring.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 8 | `config-8-automatic-turn-dispatch.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 9 | `config-9-to-actor-messaging.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 10 | `config-10-spot-actor-transfer.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |
| 11 | `config-11-observability-ops.ko.md` | [x] | [ ] | [ ] | [x] | [ ] |

config 셀은 해당 문서의 모든 세부 scenario 행이 닫힌 뒤에만 체크한다. 각 문서의 heading에
있는 scenario ID를 한 행씩 다음 표에 옮긴다. 이름이 다른 구현을 의미가 같다고 추정하지
않고 동일한 topology, 자극, 불변식과 evidence를 확인한다.

| config/scenario ID | 언어 | runner selector | 구현/fixture | all runner 포함 | 결과 | 로그/marker | 비적용 승인 근거 |
|--------------------|------|-----------------|--------------|-----------------|------|-------------|------------------|

.NET의 181개 상세 행과 최종 aggregate 증거는
[`.NET G6 E2E 시나리오 ledger`](./log/framework-public-contract-gap-implementation/dotnet-g6-e2e-ledger.ko.md)에서
관리한다.

Node.js의 181개 상세 행과 aggregate 및 cross-language 증거는
[Node.js G6 E2E 시나리오 ledger](./log/framework-public-contract-gap-implementation/node-g6-e2e-ledger.ko.md)에서
관리한다.

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

한 언어의 자체 E2E 성공을 cross-language 성공으로 간주하지 않는다. `store`, `codec`, `messaging`,
`flow-wire`, `draining-row`, `session-closing` 각각에 대해 producer 언어와 consumer 언어가 다른 모든
방향 조합을 inventory한다.
topology는 해당 공통 spec이 요구하는 direct, registry/store discovery, relay/route 경계를
각각 행으로 분리한다.

| feature | producer | consumer | topology | contract 근거 | runner/selector | 기대 marker | 결과 | 비적용 승인 근거 |
|---------|----------|----------|----------|---------------|-----------------|-------------|------|------------------|
| messaging | Node.js | `.NET` | public channel/fanout topology | Node.js G6 ledger | `framework/languages/node/cross-language/run_cross_language_smoke.sh` | request/send/fanout marker | [x] | - |
| messaging | `.NET` | Node.js | public channel/fanout topology | Node.js G6 ledger | 같은 runner | request/fanout marker | [x] | - |
| messaging/flow-wire | Node.js | `.NET` | public route-mesh | Node.js G6 ledger | 같은 runner | route request/reply와 typed JSON marker | [x] | - |
| messaging/flow-wire | `.NET` | Node.js | public route-mesh | Node.js G6 ledger | 같은 runner | route request/reply와 typed JSON marker | [x] | - |
| codec/flow-wire | Node.js | `.NET` | STREAM | Node.js G6 ledger | 같은 runner | UUIDv7 flow, JSON reply | [x] | - |
| codec/flow-wire | `.NET` | Node.js | STREAM | Node.js G6 ledger | 같은 runner | UUIDv7 flow, JSON reply | [x] | - |
| store/draining-row | Node.js | `.NET` | shared Redis location store | Node.js G6 ledger | 같은 runner | typed `Draining=true` row | [x] | - |
| store/draining-row | `.NET` | Node.js | shared Redis location store | Node.js G6 ledger | 같은 runner | typed `draining=true` row | [x] | - |
| session-closing | Node.js server | `.NET` connector | STREAM drain | Node.js G6 ledger | 같은 runner | `ServerDrain` 뒤 disconnect | [x] | - |
| session-closing | `.NET` server | Node.js connector | STREAM drain | Node.js G6 ledger | 같은 runner | `ServerDrain` 뒤 disconnect | [x] | - |
| messaging | C++ | `.NET` | public client-server channel | C++ G6 로그 | `framework/languages/cpp/cross-language/run_cross_language_smoke.sh` | request/reply + one-way send marker | [x] | - |
| messaging | `.NET` | C++ | public client-server channel | C++ G6 로그 | 같은 runner | request/reply marker | [x] | - |
| flow-wire | C++ | `.NET` | fanout channel(topic) | C++ G6 로그 | 같은 runner | `<topic>:<value>` subscriber marker | [x] | - |
| flow-wire | `.NET` | C++ | fanout channel(topic) | C++ G6 로그 | 같은 runner | `<topic>:<value>` subscriber marker | [x] | - |
| codec | C++ | `.NET` | STREAM(frame+LZ4 압축) | C++ G6 로그, CPP-STREAM-LZ4-001 | 같은 runner | raw ping/pong marker | [x] | - |
| codec | `.NET` | C++ | STREAM(frame+LZ4 압축) | C++ G6 로그, CPP-STREAM-LZ4-001 | 같은 runner | raw ping marker | [x] | - |
| messaging | C++ | Node.js | public client-server channel | C++ G6 로그 | 같은 runner(`node_peer_host.js`) | request/reply + one-way send marker | [x] | - |
| messaging | Node.js | C++ | public client-server channel | C++ G6 로그 | 같은 runner | request/reply + one-way send marker | [x] | - |
| flow-wire | C++ | Node.js | fanout channel(topic) | C++ G6 로그 | 같은 runner | `<topic>:<value>` subscriber marker | [x] | - |
| flow-wire | Node.js | C++ | fanout channel(topic) | C++ G6 로그 | 같은 runner | `<topic>:<value>` subscriber marker | [x] | - |
| codec | Node.js | C++ | STREAM(frame+LZ4 압축) | C++ G6 로그, CPP-STREAM-LZ4-001 | 같은 runner | raw ping + `pong` reply marker | [x] | - |
| store/draining-row | C++ | `.NET`/Node.js | 공유 redis location store | location-store 공통 골든 픽스처(row codec 바이트 동일) | `test_cpp_framework_locations_redis`(RowCodecMatchesCommonFixtureBytes) | 공통 픽스처 바이트 일치 | [x] | 언어별 store 행은 공통 골든 픽스처로 고정된다 |
| flow-wire | C++ | `.NET`/Node.js | SPOT mesh pub/sub | `flow-correlation` §4.1, CPP-FANOUT-WIRE-001 | - | envelope 2-part 수신 | 열림 | **core 결함 차단**: framework 부착 SPOT의 multipart publish가 첫 파트만 전달해 C++은 self-delimited 단일 프레임(`ZLFE`)으로 발행한다. 피어 언어의 SPOT 구독자는 2-part envelope만 해석하므로 이 행은 core의 multipart publish 수정 후 실행한다(ledger CPP-FANOUT-WIRE-001). channel fanout 행(위 4행)은 2-part envelope로 양방향 PASS |
| session-closing | C++ server | `.NET`/Node.js connector | STREAM drain | `graceful-drain-handoff` §7.1 | - | `server_drain` 뒤 disconnect | 비적용 | 피어 언어의 test host에 close-reason 관측 표면이 없어 실행 불가. C++ 방향 계약은 OBS-C4(자체 E2E, connector 공개 `closeReason`)로 검증됨. 피어 test host 확장은 해당 언어 소유 |
| flow-wire | 현재 언어 | 이전 언어 | stream/channel/actor relay | `flow-correlation` §3 | 언어 단계에서 추가 | UUIDv7 id와 root origin 바이트 동일 | [ ] | - |
| draining-row | 현재 언어 | 이전 언어 | shared location store | `location-runtime` §2.1 | 언어 단계에서 추가 | typed `Draining=true` 소비 | [ ] | - |
| session-closing | 현재 언어 server | 이전 언어 connector | STREAM | `graceful-drain-handoff` §7.1 | 언어 단계에서 추가 | `server_drain` 뒤 disconnect | [ ] | - |

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
- [ ] `90-implementation-gap.ko.md`의 해결 항목 제거 또는 완료 증거 연결
- [ ] feature map, README, guide와 sample 설명 갱신
- [ ] 배포 package의 public export 재검증
- [ ] repository 전체에서 제거 대상 symbol과 compatibility 이름이 검색되지 않음
- [ ] 미사용 source/file과 stale project/package entry가 남지 않음
- [ ] §7.3 core/bindings 재사용 ledger의 모든 후보가 완료 또는 근거 있는 유지로 닫힘
- [ ] 전체 spec coverage matrix 갱신
- [ ] 별도 Codex read-only 최종 리뷰 `NO ISSUES`
- [ ] 작업 언어의 모든 gate 증거 기록
- [ ] 다음 언어로 넘어갈 수 있음을 명시

## 9. `.NET` 실행 계획

### 9.1 필수 gap checklist

- [x] `SpotRef`와 route-ref resolver를 opaque `SpotHandle`로 교체
- [x] handle 내부 snapshot/watch/refresh 및 안전한 1회 retry 구현
- [x] request/join/worker의 public Yield 계열 제거
- [x] Config 8을 `AutomaticTurnDispatch`로 이관하고 단일 완료 terminator 의미 검증
- [x] actor send를 `Submit(CancellationToken): void`로 변경
- [x] `ZLinkDispatchMode`, Spot/Stream dispatch mode 제거
- [x] typed call의 `PacketName(...)` override 제거
- [x] generic actor context의 `GetSpot()` overload 제거
- [x] `IsJoined` 제거, nullable `SpotRid`를 membership 단일 기준으로 사용
- [x] actor join 결과를 승인/거절 sealed record로 변경
- [x] capability별 `IZLinkEndpointConnections` runtime handle 구현
- [x] monitoring event를 kind별 sealed record로 변경
- [x] internal `IZLinkBoundSessionFactory`가 public DI/export에 노출되지 않음을 검증
- [x] one-way queue 수락과 monitoring 오류 관측 의미 구현
- [x] 정식 spec 경로를 읽도록 documentation regression test 수정
- [x] interface inventory의 보완 타입 전체 구현/검증
- [x] `System.Diagnostics.Metrics` catalog와 `MeterListener` contract test 구현
- [x] message-flow 설정에 연결된 자동 flow id, `AsyncLocal` 전파·정리와 `0xF2` marker codec 교체
- [x] `IZLinkDrainControl`의 공유 결과, hosted-service 종료 순서와 typed `Draining` field 구현
- [x] `session-closing` 제어 프레임과 connector `CloseReason` 구현
- [x] 공통 connector 계약과 API/package snapshot을 연결하는 `.NET` 정식 계약 문서 `languages/dotnet/03-stream-connector.ko.md` 작성·검증
- [x] Config 11 OBS-A1~C5 fixture, runner와 evidence 구현
- [x] Bingo §17의 .NET flow/metrics/drain 예제와 관측 기능을 켠 sample smoke 구현

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

- [x] G0 inventory/실패 테스트
- [x] G1 interface/export
- [x] G2 runtime/unit test
- [x] G3 전체 contract/unit/integration green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS` — 2차 finding 수정 완료, 독립 reviewer 대기
- [x] G5 `samples/run_samples.sh` PASS
- [x] G6 `e2e/run_e2e_all.sh` PASS
- [x] G6 이전 완료 언어와 cross-language 양방향 matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰 — 문서와 package 정렬 완료, 별도 read-only 최종 리뷰 대기
- [x] Java 작업 시작 승인

## 10. Java 실행 계획

### 10.1 필수 gap checklist

- [x] handler/lifecycle/factory를 `CompletionStage` 완료 계약으로 변경
- [x] Java callback/runtime이 blocking 없이 완료될 수 있는 capability 구현
- [x] Java production 범위의 `join()`과 blocking wait 경로 제거
- [x] one-way `ZLinkSubmitStage`와 blocking `await()` 제거
- [x] typed session handler와 raw dispatcher 경계 분리
- [x] actor context의 항상 예외인 default join method 제거
- [x] Java 전용 framework cancellation token 제거
- [x] 비동기 method의 불필요한 `Async` suffix 제거
- [x] `ZLinkLocationKey`와 누락 interface inventory 구현
- [x] 단일 `ZLinkEndpointConnections` 재사용 및 runtime handle 구현
- [x] `SpotHandle`와 resolver 구현
- [x] dispatch mode와 typed packet-name override 제거
- [x] `getSpot()`과 `isJoined()` 제거
- [x] actor join sealed result와 공통 join call 구현
- [x] Spot context registry/close와 manager request overload 구현
- [x] socket/registry/Spot/location monitoring 등록과 sealed event 구현
- [x] route-mesh runtime options와 public export 정렬
- [x] 정식 spec 경로 regression test 수정
- [x] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [x] Micrometer catalog와 connector 소유 reconnect 계기 구현
- [x] 자동 flow id, `CompletionStage` 문맥 전파·정리와 `0xF2` marker codec 교체
- [x] `ZLinkDrainControl` 결과, `SmartLifecycle` 종료 순서와 typed `Draining` field 구현
- [x] `session-closing` 제어 프레임과 connector close reason 구현
- [x] Config 11 OBS-A1~C5 fixture, runner와 evidence 구현
- [x] Bingo §17의 Java flow/metrics/drain 예제와 관측 기능을 켠 sample smoke 구현

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

- [x] G0 inventory/실패 테스트
- [x] G1 interface/export
- [x] G2 runtime/unit test
- [x] G3 전체 Gradle test source set green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [x] G5 Java 공통 sample spec 6종 대조와 sample 전체 PASS
- [ ] G6 Java E2E 전체 PASS
- [ ] G6 `.NET`↔Java cross-language matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [x] Kotlin 작업 시작 승인

core ROUTER handover 결함은 9.0.2에서 수정하고 회귀 검사를 추가했다. Java
`AutomaticTurnDispatch` 전체 selector는
`e2e/AutomaticTurnDispatch/logs/20260713-205516-3215299/`에서 통과했고, 같은 routing ID로
`play-a`를 재기동하는 `ATD-E3`도 handshake timeout을 기다리지 않고 완료됐다. framework나 E2E에는
재시도 횟수, 대기 시간 또는 서버 구동 순서를 지정하는 우회를 추가하지 않았다. Java G3 전체
`test contractTest fakeBackendTest integrationTest sampleTest`는 2026-07-13에 다시 통과했다. G6는
전체 E2E runner가 통과한 결과로만 닫는다.

## 11. Kotlin 실행 계획

### 11.1 필수 gap checklist

- [x] Java 목표 interface를 coroutine에서 자연스럽게 사용할 wrapper 구현
- [x] `ZLinkSuspendingHandlers.kt`, `ZLinkCoroutineTurnAwait.kt` 등 Kotlin bridge의
  `runBlocking`, `join()`과 blocking wait 제거
- [x] value bridge와 Unit-to-Void bridge의 nonblocking 완료 검증
- [x] public yield extension 제거
- [x] JVM signature clash가 없는 `await`, `awaitReply`, `awaitJoinReply` 구현
- [x] typed/raw stream request와 send wrapper 정렬
- [x] coroutine cancellation이 Java framework token을 새로 노출하지 않음을 검증
- [x] `Flow` wrapper가 callback registration과 cleanup을 소유함을 검증
- [x] Java runtime의 모든 공통 기능이 Kotlin surface에서도 도달 가능한지 검증
- [x] Kotlin interface/function inventory 전체 contract test 추가
- [x] Kotlin `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [x] Java의 metrics/flow/drain 공개 기능이 Kotlin에서 별도 중복 설정 없이 도달 가능함을 검증
- [x] coroutine 전환 뒤 flow 문맥이 유지되고 완료·취소 뒤 다음 작업으로 누출되지 않음을 검증
- [x] drain의 `CompletionStage<ZLinkDrainResult>`를 nonblocking `await()`로 대기하고 waiter 취소만
  전파함을 검증
- [x] Config 11 OBS-A1~C5 Kotlin fixture, runner와 evidence 구현
- [x] Bingo §17의 Kotlin flow/metrics/drain 예제와 관측 기능을 켠 sample smoke 구현

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

- [x] G0 Kotlin extension/interface inventory
- [x] G1 Kotlin public surface
- [x] G2 coroutine/runtime unit test
- [x] G3 Kotlin Gradle test green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [x] G5 Kotlin 공통 sample spec 6종 대조와 sample 전체 PASS
- [ ] G6 Kotlin E2E 전체 PASS
- [ ] G6 Kotlin↔`.NET`/Java cross-language matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰

core 9.0.2 적용 뒤 Kotlin `AutomaticTurnDispatch ATD-E3`는
`e2e-kotlin/AutomaticTurnDispatch/logs/20260713-205642-3225711/`에서 통과했고,
`ObservabilityOps OBS-C2`도 `e2e-kotlin/ObservabilityOps/logs/20260713-210155-3254245/`에서
통과했다. 종료·재기동 직후 새 location owner를 polling interval 동안 조회하지 못하던 Java framework
문제에는 즉시 재조회와 회귀 검사를 추가했다. Kotlin G6는 전체 E2E runner가 통과한 뒤에만 닫는다.

## 12. Node.js 실행 계획

### 12.1 필수 gap checklist

- [x] 모든 handler가 목표 `Promise` 반환형만 노출
- [x] actor/bound-session one-way `Promise<void>` 제거
- [x] yield call과 worker callback completion 제거
- [x] `AbortSignal`을 장기 작업에만 제한
- [x] optional lifecycle과 동기 factory union 제거
- [x] branded `SpotHandle`과 내부 refresh 구현
- [x] `getSpot`, `isJoined`, 중복 actor join call 제거
- [x] actor join discriminated union 구현
- [x] dispatch mode 제거와 message-kind별 unhandled policy 구현
- [x] channel dispatch 실패 로그 수준을 공통 정책으로 고정하고 one-way 중복 로그 제거
- [x] reply frame이 없는 local Spot request에 `FailCaller` observer 결과 구현
- [x] actor 소유권 갱신과 같은 actor의 session relay를 직렬화해 binding 중간 상태 노출 제거
- [x] handler 없는 server/subscriber와 불완전한 SpotNode 구성을 socket 생성 전 startup에서 거부
- [x] SPOT 수동 peer와 location store를 함께 사용할 때 peer 자동 연결만 끄는 역할별 계약 설계
  (TicTacToe의 수동 peer와 원격 actor 위치 조회를 모두 유지해야 하며 sample wrapper로 우회하지 않음)
- [x] immutable metadata와 forwarding policy 구현
- [x] typed session handler/registry 추가, raw stream escape hatch 제거
- [x] manual runtime connection accessor와 worker producer 구현
- [x] route builder 중복과 bound-session factory public export 제거
- [x] 전역/채널별 request timeout 구현
- [x] monitoring registration과 discriminated event union 구현
- [x] location interface의 `I` prefix와 internal registration export 제거
- [x] channelless channel/route/publisher 중복 표면 제거
- [x] 정식 spec 경로 regression test 수정
- [x] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [x] contract/supporting artifact manifest 정렬과 배포 package의 `private` 제거
- [x] OpenTelemetry metric catalog와 connector 소유 reconnect 계기 구현
- [x] 자동 flow id, `AsyncLocalStorage` 전파·정리와 `0xF2` marker codec 교체
- [x] `ZLinkDrainControl` 결과, NestJS shutdown 종료 순서와 typed `Draining` field 구현
- [x] `session-closing` 제어 프레임과 connector `closeReason` 구현
- [x] native stream disconnect routing id를 Node monitor와 다중 session 정리에 전달
- [x] Stream Connector reply name, Error JSON object와 metadata 1024바이트 계약 구현
- [x] Stream Connector browser entrypoint와 네이티브 WebSocket transport 구현
- [x] browser handler의 관련 async 작업은 `flowFrom(message)`로 flow를 명시적으로 전달하고
  관련 없는 callback은 새 application flow를 사용하도록 격리
- [x] Config 11 OBS-A1~C5 fixture, runner와 evidence 구현
- [x] Bingo §17의 Node.js flow/metrics/drain 예제와 관측 기능을 켠 sample smoke 구현
  (protobuf·flow·metric·drain·self-join·client marker를 shell과 PowerShell runner에서 검증)
- [x] 공통 sample spec 6종과 Node.js sample 6종의 역할·메시지·상태·codec·self-check
  항목별 대조, 불일치 수정과 개별·통합 runner 재검증
  ([Node.js G5 공통 샘플 대조 기록](./log/framework-public-contract-gap-implementation/node-g5-sample-ledger.ko.md))

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

- [x] G0 inventory/실패 테스트
- [x] G1 interface/export
- [x] G2 runtime/unit test
- [x] G3 build/typecheck/lint/test/coverage green — ABI, CI, Node 20/22 runtime matrix와 release 구성 명령까지 통과
- [x] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS` — browser runtime 이중 source와 생성 결과 drift를 제거한 뒤 재검토 완료
- [x] G5 sample 전체 PASS — 공통 sample 6종 대조, shell과 PowerShell 통합 runner PASS
- [x] G6 E2E와 cross-language 전체 PASS — E2E 181개 scenario와 전체 runner, cross-language 구성 통과
- [x] G6 Node.js와 이미 G7을 통과한 `.NET`의 양방향 matrix PASS — messaging, flow wire, codec, drain과 location store 양방향 통과
- [x] G7 gap/doc/package 최종 리뷰 — gap, guide, tarball surface와 browser bundle graph 재검토 완료

## 13. C++ 실행 계획

### 13.1 필수 gap checklist

- [x] lifecycle/transfer의 `.result()` blocking bridge 제거
- [x] public error enum의 공통 계약 밖 값 제거
- [x] callback 이름을 `snake_case`로 통일
- [x] typed stream handler와 raw runtime 경계 분리
- [x] route-mesh runtime options 구현
- [x] one-way `result_t<void>`/actor `async()`를 `void submit()`으로 변경
- [x] relay/disconnect를 `task_t<void>` 완료 계약으로 구현
- [x] location watch와 message-flow runtime control 구현
- [x] `spot_handle_t`와 handle resolver 구현
- [x] `is_joined()`를 nullable `spot_rid()`로 교체
- [x] join 결과를 승인/거절 `std::variant`로 변경
- [x] user Spot/Entry Spot context와 worker/destroy 역할 분리
- [x] async `find_spot`, `list_spots`, `close_spot` 구현
- [x] `endpoint_connections_t` runtime handle 구현
- [x] dispatch mode와 typed `packet_name(...)` override 제거
- [x] monitoring event가 유효 variant만 표현하는지 검증
- [x] installed header에서 runtime state/helper 비노출
- [x] 정식 spec 경로 regression test를 fail-closed로 수정
- [x] `AutomaticTurnDispatch` fixture와 runner로 Config 8 이관
- [x] CMake Framework/StreamConnector/FrameworkDependency/HttpClient install component와
  export set 분리
- [x] `metric_event_payload_t` 기반 catalog와 test collector 집계 구현
- [x] 자동 flow id, coroutine 문맥 전파·정리와 `0xF2` marker codec 교체
- [x] `drain_result_t`, typed `Draining` field와 애플리케이션 소유 signal 종료 순서 구현
- [x] `session-closing` 제어 프레임과 connector close reason 구현
- [x] Config 11 OBS-A1~C5 fixture, runner와 evidence 구현
- [x] Bingo §17의 C++ flow/metrics/drain 예제와 관측 기능을 켠 sample smoke 구현

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

- [x] G0 inventory/실패 테스트
- [x] G1 public/install header
- [x] G2 runtime/unit test
- [x] G3 build/ctest/install consumer green
- [ ] G4 Codex DDD/POSD loop `NO DDD/POSD FINDINGS`
- [x] G5 sample 전체 PASS(개별 runner 기준)
- [ ] G5-b 공통 sample spec 대조 — 정본 6종(Bingo/TicTacToe/SupportChat/DeliveryDispatch/
      ShoppingMall/GameQuest)을 `framework/doc/framework/common/sample/`의 시나리오 문서와
      역할 분리·메시지 이름(`Req`/`Res`/`Msg`/`Notify`)·DTO 필드·codec·서버 간 연결 방식·
      자동 turn dispatch·dispatch 오류 로그·runner Redis 격리 기준으로 대조하고, 불일치는
      공통 spec에 맞게 sample/runner/self-check를 수정한다(새 public API가 필요하면 계약
      gap으로 분리). 수정 후 개별 runner와 통합 runner를 다시 통과시킨다.
      진행 상태와 항목별 판정은
      [cpp sample conformance ledger](./log/framework-public-contract-gap-implementation/cpp-sample-conformance.ko.md).
      1차 정렬 완료(닫힘 23/열림 23): TicTacToe(수동 endpoint scale-out·`JoinGameReq{RoomId}`·
      EnsurePlayerActor 제거·redis-plus-plus room route store), Bingo(status 대소문자·사장된
      `BingoStateNotify` 제거·self-check 선인증 순서), SupportChat(API 서버 실체화·conversation
      Spot idle timer·multi-room/reconnect/closed 오류 시나리오·notify `State` 중첩·client는
      Session stream만 사용), DeliveryDispatch(`AssignDeliveryMsg`·DTO 필드·node 배치 단언),
      공통(runner `docker rm -fv`·수기 message-flow 로그 제거).
      남은 열림 항목은 event sourcing(ShoppingMall/GameQuest)과 DeliveryDispatch 역할 분리처럼
      샘플 재작성 규모의 작업이다.
      **통합 러너 상태**: 개별 runner는 전 샘플 반복 통과. `run_samples.sh`(연속 실행)는 부하
      의존 core 결함 2건(CPP-SPOT-SUB-ACT-001, CPP-AUTOCONNECT-CFG-001, 위 ledger §9)으로
      간헐 실패하며, 이 두 건은 sample 계약 편차가 아니라 core 소유 결함이다.
- [ ] G6 E2E 전체 start-order variant PASS
- [x] G6 C++↔`.NET` 양방향 matrix PASS(messaging/flow-wire/codec 6행; 신규 runner `cross-language/run_cross_language_smoke.sh`)
- [x] G6 C++↔Node.js 양방향 matrix PASS/승인된 비적용
- [ ] G7 gap/doc/package 최종 리뷰
- [ ] 모든 언어 gap closure 완료

## 14. 구현 로그 관리

시간순 명령 결과와 Codex 리뷰 이력은 계획 문서에 누적하지 않는다. 다음 별도 로그 디렉터리에
언어별로 기록한다.

- [로그 작성 규칙과 언어별 색인](./log/framework-public-contract-gap-implementation/README.ko.md)
- [.NET 구현 로그](./log/framework-public-contract-gap-implementation/dotnet.ko.md)
- [.NET G4 DDD/POSD finding ledger](./log/framework-public-contract-gap-implementation/dotnet-g4-refactoring-ledger.ko.md)

계획 문서에는 작업 절차, 완료 조건, 현재 진행표와 각 항목의 짧은 결과 요약만 유지한다. 이후
각 언어의 실행 이력은 위 색인에 연결된 언어별 로그 문서에만 기록한다. 계획 문서에는 시간순 실행
기록, 명령 출력, 실패 원인, 수정 이력과 반복 리뷰 결과를 추가하지 않는다. gate를 다시 열어야 하면
해당 언어 로그의 기존 기록을 덮어쓰지 않고 새 실행 행을 추가한다.

## 15. 최종 전체 완료 조건

다음 조건을 모두 만족해야 이 계획을 완료로 바꿀 수 있다.

- [ ] 5개 언어의 G0~G7이 모두 체크되어 있다.
- [ ] 공통 spec coverage matrix의 모든 셀이 체크되어 있다.
- [ ] 모든 ledger 행이 완료 또는 승인된 비적용 근거를 가진다.
- [ ] `90-implementation-gap.ko.md`에 해결되지 않은 대상 언어 gap이 없다.
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
- [ ] 각 언어의 core/bindings 기능 재사용 감사가 완료되고 framework 중복 구현이 남아 있지 않다.
- [ ] README, guide, feature map, sample 문서와 진행표가 실제 상태를 반영한다.
- [ ] 최종 read-only 종합 리뷰가 `NO ISSUES`를 반환한다.

완료 선언에 필요한 마지막 commit, 각 언어별 최종 명령의 exit code, Codex clean verdict와
남은 gap이 없다는 검색 결과는 해당 언어의 구현 로그에 기록한다. 계획 문서에는 모든 완료 조건이
충족되었는지를 체크하고 최종 상태만 `완료`로 바꾼다.
