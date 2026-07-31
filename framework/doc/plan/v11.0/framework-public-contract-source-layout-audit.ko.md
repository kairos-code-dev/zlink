# Framework public contract source 경계 감사

> 조사 기준: 2026-07-30 working tree와 Git 이력
>
> 이 문서는 exact interface 문서의 위치가 아니라 production source에서 public contract가
> `Contracts/contracts` 경계를 벗어난 현황을 기록한다. 구현을 변경하는 문서가 아니며,
> 담당자가 복구 범위와 package 정책을 결정하기 위한 handoff 자료다.
>
> 적용 판단: 경로 이름을 언어마다 같게 만드는 안은 채택하지 않는다. Public contract의 owner,
> dependency 방향과 실제 visibility를 정렬한다. §9의 적용 순서로 진행한다.

## 1. 결론

C++과 Node.js server package는 public contract와 runtime 구현을 source directory로 분리한다.
반면 .NET, Java와 Kotlin은 같은 수준의 경계를 유지하지 않는다.

| 언어 | 확인한 현재 상태 | 판정 |
|---|---|---|
| C++ | `framework/include/zlink/framework/contracts/`가 public contract를 소유하고 layout test가 이 경로를 검사한다 | 기준 구조 |
| Node.js | `packages/framework/src/contracts/`가 public contract를 소유하고 runtime이 이 경로를 import한다 | 기준 구조 |
| .NET | 기존 `src/Zlink.Framework/Contracts/`의 주요 contract 29개 파일이 별도 `src/Zlink.Framework.Contracts/` project로 이동했다 | 원래 source 경계에서 이탈 |
| Java | raw binding declaration은 runtime package에 남기되 package-private 또는 JPMS non-exported package로 제한했다 | P0 경계 적용 |
| Kotlin | Kotlin artifact가 직접 선언하는 public contract를 `kotlin/contracts` source owner로 옮기고 기존 FQN을 유지했다 | P0 경계 적용 |

SPI 확장은 .NET server contract 전체를 별도 project로 옮긴 직접 사유가 아니다.
SPI 전용 project는 이미 `Zlink.Framework.Provider.Abstractions`로 따로 존재하며, Redis provider가 이
project만 참조한다. `Zlink.Framework.Contracts` 분리는 HTTP client가 공통 오류·codec을 재사용하고
server runtime도 같은 assembly를 참조하도록 만든 package layering 결정이다. 이 재사용 요구는
contract source를 server package 밖으로 이동해야만 해결되는 조건이 아니며, SPI dependency와도 별개다.

## 2. 적용해야 할 source 경계

모든 언어에서 directory 이름과 대소문자를 완전히 같게 만들 필요는 없다. 그러나 다음 책임 경계는
같아야 한다.

1. 각 배포 package의 application-facing public interface, call, context, option, result와 error contract는
   그 package의 `Contracts/contracts` source subtree가 소유한다.
2. runtime 구현은 contract subtree를 참조한다. contract subtree는 runtime 구현을 참조하지 않는다.
3. `runtime/internal` 아래 선언은 배포 artifact의 public API가 아니어야 한다. Java처럼 directory만
   `internal`인 경우에는 `public` visibility가 그대로 외부에 노출되므로 내부화로 인정하지 않는다.
4. provider가 독립적으로 구현해야 하는 SPI는 별도 abstraction artifact로 분리할 수 있다. 이 예외는
   provider SPI에만 적용하며 application-facing server contract 전체를 옮기는 근거로 사용하지 않는다.
5. HTTP client와 Stream Connector도 독립 배포 package이므로 각각 같은 contract subtree를 둔다.

언어별로 대응하는 목표 형태는 다음과 같다.

| package | C++ | .NET | Java | Kotlin | Node.js/TypeScript |
|---|---|---|---|---|---|
| Server | `framework/.../contracts/` | `Zlink.Framework/Contracts/` | `.../framework/contracts/` | `.../framework/kotlin/contracts/` | `framework/src/contracts/` |
| HTTP client | `http-client/.../contracts/` | `Zlink.HttpClient/Contracts/` | `.../httpclient/contracts/` | `.../httpclient/kotlin/contracts/` | `http-client/src/contracts/` |
| Stream Connector | `connector/.../contracts/` | `Systems.Zlink.Stream.Connector/Contracts/` | `.../stream/connector/contracts/` | Kotlin connector contract subtree | `stream-connector/src/Contracts/` |

이 표는 namespace를 기계적으로 바꾸라는 뜻이 아니다. Java와 Kotlin은 package 변경이 곧 binary
contract 변경이므로 compatibility plan을 먼저 정해야 한다. 핵심은 source owner를 한 곳으로 모으고
runtime과 반대 방향의 dependency를 금지하는 것이다.

## 3. 저장소에서 확인한 규칙과 충돌

### 3.1 기준으로 사용할 수 있는 기존 증거

- C++ layout contract test
  `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp`는
  `framework/include/zlink/framework/contracts`의 존재를 명시적으로 요구한다.
- C++ exact interface의 `01-common-runtime.ko.md`는 `contracts/*`를 실제 public contract owner로
  설명하고 umbrella header는 진입점으로만 취급한다.
- 실행 ledger는 runtime 전용 type을 `internal`로 바꾼 뒤 `Contracts/` 밖으로 이동했다고 기록한다.
  즉 directory 이동만으로 내부화하지 않고 visibility 변경과 source 이동을 함께 요구한다.
- public contract trace와 core service migration inventory에는 .NET owner가
  `framework/languages/dotnet/src/Zlink.Framework/Contracts/...`로 남아 있다. 현재 별도 project 이동과
  generated evidence의 owner가 일치하지 않는다.

### 3.2 현재 exact spec도 함께 drift한 부분

현재 .NET exact interface의 `02-configuration-host.ko.md`는 `Zlink.Framework.Contracts`를 별도 package로
정의하고, .NET guide도 이 namespace를 public contract owner로 설명한다. 따라서 현재 구현만 보면
문서와 일치한다. 그러나 이는 기존 `Zlink.Framework/Contracts/` owner와 C++·Node.js의 package 내부
contract subtree 원칙을 바꾼 결과다.

복구할 때 구현 파일만 되돌리면 다시 spec과 충돌한다. 다음 문서를 함께 바로잡아야 한다.

- `framework/doc/framework/common/spec/server/languages/dotnet/interfaces/02-configuration-host.ko.md`
- `framework/doc/framework/dotnet/guide/server/01-overview.ko.md`
- `.NET` HTTP client spec에서 `Zlink.Framework.Contracts`를 공유 dependency로 정의한 부분
- public contract trace config와 generated inventory의 source owner

기존 governance는 exact signature의 문서 위치는 정했지만 production source owner를 직접 고정하지
않았다. 이 빈틈 때문에 signature test는 통과하면서 source owner가 이동할 수 있었다. 이번 감사에서
`00-public-contract-governance.ko.md` §2.1에 package별 단일 owner, runtime 역참조 금지, SPI 예외와
실제 visibility 제한을 정식 규칙으로 추가했다.

## 4. 위반 현황

### 4.1 .NET: server contract를 별도 project로 이동

commit `f14abf3c37`은 다음 29개 기존 파일을
`src/Zlink.Framework/Contracts/...`에서 `src/Zlink.Framework.Contracts/...`로 rename했다.

| 영역 | 이동한 파일 |
|---|---|
| Actors | `ActorRef.cs`, `IZLinkActor.cs`, `IZLinkActorClient.cs`, `IZLinkActorContext.cs`, `IZLinkActorFactory.cs`, `IZLinkActorManager.cs` |
| Channels | `Calls.cs`, `IZLinkFanoutClient.cs`, `RouteCalls.cs` |
| Handlers | `Attributes.cs`, `Contexts.cs`, `IZLinkChannelHandlers.cs`, `IZLinkHandlerFilter.cs` |
| Messaging | `ZLinkMessage.cs` |
| Spots | `Contracts.cs`, `InstanceSpots.cs`, `SpotRoutingContracts.cs`, `ZLinkSpot.cs` |
| Streams | `BoundSessionContracts.cs`, `IZLinkSession.cs`, `IZLinkSessionActor.cs`, `IZLinkSessionPacketHandler.cs`, `IZLinkStream.cs`, `ZLinkMessageMetadata.cs`, `ZLinkSessionDispatchContext.cs`, `ZLinkStreamError.cs`, `ZLinkStreamSessionError.cs` |
| Timers·Workers | `IZLinkTimer.cs`, `ZLinkWorkers.cs` |

현재 기존 `Zlink.Framework/Contracts/`에는 configuration, dispatch, location과 relocation 일부만 남았다.
동일한 server public contract가 두 project로 나뉘었고, 어떤 contract가 어느 assembly에 있어야 하는지
feature 책임이 아니라 dependency 편의에 따라 결정된다.

SPI 관련 project는 별도로 판단해야 한다.

- `Zlink.Framework.Provider.Abstractions`는 `IZLinkLocationStore`와 `IZLinkRelocationStore` 같은 provider
  SPI를 독립 구현에 제공하므로 별도 artifact 자체는 타당하다.
- 이 project는 `Zlink.Framework.Contracts`를 참조하지 않는다.
- Redis provider는 `Zlink.Framework.Provider.Abstractions`를 참조한다.
- 따라서 SPI 확장은 위 32개 server contract 이동의 dependency 원인이 아니다.

권고는 server contract를 `Zlink.Framework/Contracts/`로 복구하고, provider SPI만 별도 abstraction
artifact로 유지하는 것이다. HTTP client가 공유해야 하는 오류·codec은 HTTP client가 server contract
assembly 전체를 참조하지 않도록 package-neutral contract의 최소 범위를 별도로 설계해야 한다.

### 4.2 Java: public contract를 runtime internal 경로로 이동

commit `f26ff5945b1`은 `systems/zlink/contracts/service/spot/`의 67개 source file을
`systems/zlink/framework/runtime/internal/binding/spot/`으로 rename했다. 이동 뒤에도 다음 19개
interface가 `public`으로 남아 있다.

`Actor`, `Claim`, `MeshNode`, `MeshNodeMonitor`, `MeshNodePublisher`, `MeshReadyHandler`,
`MessageBuilderStage`, `ReadyBatch`, `ReceiveBatch`, `Spot`, `StreamSessionService`,
`TimeoutSubmitOperation`과 `SpotOperations` 아래의 일곱 operation interface가 해당한다.

나머지 record, enum과 value type도 같은 commit에서 함께 이동했다. 조사 시점에는 JPMS
`module-info.java` export allow-list가 없어 `runtime.internal`이라는 package 이름만으로 public 접근을
차단하지 못했다. P0 적용에서는 같은 package 전용 declaration을 package-private로 낮추고, package를
넘어 공유하는 runtime declaration은 non-exported module package로 제한했다.

추가로 application-facing Java public interface도 `systems.zlink.framework.actors`, `channels`,
`configuration`, `spots`, `streams` 등에 흩어져 있고 package 내부의 단일 `contracts` subtree가 없다.
복구 범위는 두 단계로 나눠야 한다.

1. 위 67개 binding contract가 정말 배포 public contract인지 판정한다. public contract이면 원래
   `systems/zlink/contracts/service/spot/` owner로 복구한다. runtime 전용이면 visibility를 실제로
   제한하고 public API snapshot에서 제외한다.
2. application-facing framework interface는 binary compatibility plan을 세운 뒤 package 내부 contract
   owner로 정리한다. 단순 package rename으로 기존 FQN을 즉시 제거하면 안 된다.

### 4.3 Kotlin: contract directory가 없는 상태

Kotlin source history에서는 production `contracts` directory에서 다른 경로로 rename된 기록을 찾지
못했다. 따라서 Kotlin은 “이동 확인”이 아니라 “분리 규칙 미적용”으로 기록한다.

조사 시점에는 `ZLinkOneWayCalls.kt`에 19개 public call·client·manager interface가 있고,
`ZLinkSuspendingHandlers.kt`에 14개 suspending handler interface가 있었다. P0 적용 뒤 이 declaration은
`systems/zlink/framework/kotlin/contracts/` source subtree가 소유한다. Package 선언은 기존
`systems.zlink.framework.kotlin`을 유지하므로 public FQN은 바뀌지 않는다.

Kotlin 전용 coroutine call, handler와 adapter contract는
`systems/zlink/framework/kotlin/contracts/`에 모으고, extension 구현과 runtime adapter는 기존
implementation subtree에 남겨야 한다. Java type을 그대로 재사용하는 항목은 Kotlin source로
복제하지 말고 Kotlin contract에서 Java owner를 참조한다.

## 5. HTTP client와 Stream Connector 현황

두 package에도 같은 원칙을 적용해야 한다. 현재는 언어마다 다르다.

| 언어 | HTTP client | Stream Connector |
|---|---|---|
| C++ | `http_client/contracts/` 존재 | `stream_connector/contracts/` 존재 |
| .NET | `Zlink.HttpClient/Contracts/` 존재 | `Systems.Zlink.Stream.Connector/Contracts/` 존재 |
| Java | package-local `contracts` subtree 없음 | package-local `contracts` subtree 없음 |
| Kotlin | Kotlin HTTP extension contract subtree 없음 | `ZLinkConnectorExtensions.kt`의 public wrapper·call이 framework Kotlin root에 있음 |
| Node.js/TypeScript | `http-client/src/contracts/` 없음 | `stream-connector/src/Contracts/` 존재 |

따라서 connector와 HTTP client도 public contract를 분리하는 것이 맞다. 다만 모든 public class를
옮기는 것이 아니라 caller가 compile하는 interface, option, result, error와 callback contract를
대상으로 한다. 구현 class와 internal transport adapter는 contract subtree에 넣지 않는다.

.NET의 두 package와 C++의 두 package, Node.js Stream Connector는 이미 목표 형태에 가깝다. Java의
두 package, Kotlin extension layer와 Node.js HTTP client가 우선 정리 대상이다.

## 6. 발생 원인

직접 원인은 contract signature와 source ownership을 서로 다른 gate가 다뤘기 때문이다.

- C++에는 contract directory의 존재를 검사하는 layout test가 있다.
- .NET, Java와 Kotlin의 public API test는 주로 export와 signature를 검사하며 source owner path를
  고정하지 않는다.
- Java는 조사 시점에 package 이름만 `internal`로 두고 visibility 또는 module export로 접근을 막지 않았다.
- .NET exact spec과 guide가 별도 contract assembly를 정식 구조로 기록하면서, 기존 source owner와
  generated inventory 사이의 충돌이 남았다.
- SPI 분리와 application-facing contract 분리를 같은 package layering 문제로 취급했다.

## 7. 담당자 조치 순서

### P0: 정책 확정과 잘못된 이동 복구

1. governance에 §2의 package-local contract source 경계를 추가한다.
2. .NET server의 29개 rename을 `Zlink.Framework/Contracts/` owner 기준으로 복구하고 assembly dependency를
   다시 설계한다. Provider SPI project는 유지한다.
3. Java의 67개 이동 항목을 public contract와 runtime-only type으로 분류한다. public 항목은 contract
   owner로 복구하고 runtime-only 항목은 실제 non-public로 만든다.
4. Kotlin의 33개 직접 선언 interface를 Kotlin contract subtree가 소유하도록 정리한다.
5. 구현 복구와 동시에 drift한 .NET exact spec, guide와 generated inventory를 갱신한다.

### P1: HTTP client와 Stream Connector 정렬

1. Java HTTP client와 Stream Connector에 package-local contract subtree를 만든다.
2. Kotlin HTTP·connector extension이 직접 export하는 contract를 Kotlin contract subtree로 분리한다.
3. Node.js HTTP client의 caller-facing contract를 `src/contracts/`로 분리한다.
4. 이미 분리된 C++, .NET과 Node.js Stream Connector에는 경로를 고정하는 layout test를 추가한다.

### P1: 재발 방지 gate

각 배포 package에 다음 검사를 추가한다.

- public interface가 허용한 contract subtree 또는 명시한 SPI artifact에만 존재하는지 검사한다.
- `runtime/internal` 아래에 exported/public declaration이 있으면 실패한다.
- contract subtree가 runtime namespace를 import하면 실패한다.
- contract file rename이 subtree 밖으로 나가면 architecture decision과 spec 변경 없이는 실패한다.
- Java/Kotlin package relocation은 binary compatibility report가 없으면 실패한다.

## 8. 완료 조건

다음 조건을 모두 만족해야 이번 문제를 해결한 것으로 본다.

- Server, HTTP client와 Stream Connector마다 언어별 contract source owner가 하나로 정해져 있다.
- .NET server contract가 dependency 편의 때문에 두 project로 나뉘지 않는다.
- Java `runtime/internal` 경로에 외부에서 접근할 수 있는 public contract가 없다.
- Kotlin이 직접 export하는 interface가 contract subtree에서 관리된다.
- SPI artifact와 application-facing contract의 책임이 문서와 project dependency graph에서 구분된다.
- exact spec, guide, source owner inventory와 layout test가 같은 구조를 가리킨다.

## 9. Bindings 비교 감사

Framework 규칙을 명시할 때 기준으로 사용한 bindings도 같은 원칙을 모두 지키는지 별도로 확인했다.
조사 기준은 `bindings/doc/spec/README.md`의 public/internal 경계와 각 언어 blueprint, 2026-07-31
working tree다. 현재 진행 중인 binding인 C, C++, .NET, Java와 Node만 조사 범위에 포함했다.
Python, Go와 Rust는 이 감사의 대상이 아니며 준수 여부를 판정하지 않는다. C binding은
`core/include/zlink.h`를 그대로 기준으로 사용하므로 별도 contract/runtime 분리를 강제하지 않는
정식 예외로 판정한다.

| Binding | 확인 결과 | 판정 |
|---|---|---|
| C | `core/include/zlink.h`가 public contract의 단일 기준이다 | 정식 예외 |
| C++ | `include/zlink/Contracts/`가 public header를 소유하고 contract header에서 `src/Runtime` dependency를 찾지 못했다 | 준수 |
| .NET | `SubmitResult`를 `Contracts/Errors`로 옮겼고 runtime public declaration 예외를 제거했다 | 준수 |
| Java | 14개 역참조를 제거했다. `Zlink.java`의 private factory wiring만 검증된 예외로 남는다 | 준수 |
| Node | contract source에서 runtime import를 찾지 못했고 `package.json`은 package root만 export한다 | 준수 |

### 9.1 .NET

`SubmitResult`는 `Contracts/Errors/SubmitResult.cs`로 옮겼다. Namespace와 public signature는
유지했으므로 caller의 source contract는 바뀌지 않는다. Runtime public declaration allow-list는
제거했다.

기본 xUnit architecture test는 modifier 순서와 관계없이 public declaration을 찾는다. `unsafe`,
`new`, record와 delegate도 검사하며, runtime source의 public type은 contract source가 소유한
partial type의 구현 부분일 때만 허용한다. Mutation self-test로 금지 declaration을 실제로 잡는지도
검증한다.

### 9.2 Java

Java blueprint는 contract file이 `systems.zlink.runtime.*`과
`systems.zlink.runtime.nativeapi.*`을 import하는 것을 금지한다. 다음 14개 역참조를 제거했다.

- Messaging: `Message`, `Received`, `TopicMessage`, `SubscriptionEvent`
- Core: `ContextOptions`, `RoutingId`
- Eventing·Errors: `PollEvents`, `ZlinkException`
- Socket options: `CommonSocketOptions`, `DealerSocketOptions`, `PubSocketOptions`,
  `RouterSocketOptions`, `StreamSocketOptions`, `SubSocketOptions`

`Zlink.java`의 `ContractAccess` import만 static factory facade의 private wiring 예외로 유지한다.
Reflection 기반 architecture test는 이 facade의 모든 public parameter와 return type이 contract 또는
JDK type인지 검증한다.

공유 구현은 export하지 않는 `systems.zlink.internal` package로 옮겼다. `module-info.java`는 이 package를
export하거나 open하지 않는다. 기본 JUnit architecture test는 contract source의 금지 import와 module
export를 함께 검사한다.

### 9.3 누락된 gate와 조치 순서

이 검사는 별도 CI 전용 script로 운영하지 않는다. 각 binding의 기본 unit test suite에 architecture
test로 포함한다. 개발자가 로컬에서 평소와 같은 test command를 실행해 위반을 바로 확인할 수 있어야
하며, CI는 별도 규칙을 구현하지 않고 동일한 unit test suite를 실행한다. 선택 build option을 켜야만
등록되는 test나 수동 검사만 존재하는 상태는 완료로 보지 않는다.

- C는 기존 public header contract test에서 `core/include/zlink.h`의 단일 owner를 확인한다.
- C++는 기본 CTest suite가 `include/zlink/Contracts/`의 금지 include와 독립 compile을 확인한다.
- .NET은 기본 xUnit suite가 public declaration owner와 contract-to-runtime dependency를 확인한다.
- Java는 기본 JUnit suite가 contract package의 runtime import와 module export를 확인한다.
- Node는 기본 `node:test` suite가 contract source의 runtime import·re-export와 package export를 확인한다.

Source layout 검사는 unit test process 안에서 filesystem이나 syntax tree를 읽는 방식으로 구현할 수
있다. Native runtime을 시작할 필요는 없으며 위반 파일과 symbol을 assertion message에 출력한다.

1. Java의 14개 역참조 제거와 `Zlink.java` factory 예외 검증을 완료했다.
2. .NET `SubmitResult` 이동과 test allow-list 제거를 완료했다.
3. .NET과 Java의 기본 test suite에 negative architecture test를 포함했다.
4. C++과 Node의 현재 준수 구조에 contract source를 직접 검사하는 negative layout test를 추가했다.
5. C는 `core/include/zlink.h`가 단일 public contract owner인지 기존 header 검증으로 유지한다.

### 9.4 조사 재현과 검증 상태

다음 검색으로 2026-07-31 working tree의 역방향 dependency를 확인했다.

```bash
rg -n '^\s*public ' bindings/dotnet/src/Zlink/Runtime -g'*.cs'
rg -n 'import systems\.zlink\.runtime' \
  bindings/java/src/main/java/systems/zlink/contracts -g'*.java'
rg -n 'runtime' bindings/node/src/zlink/contracts -g'*.ts'
rg -n 'Runtime' bindings/cpp/include/zlink/Contracts -g'*.hpp'
```

Markdown relative link 검사와 수정 파일의 `git diff --check`는 통과했다.
`scripts/verify-framework-doc-contracts.sh`는 service wire schema self-test를 통과한 뒤 public contract
trace의 sealed review 차이에서 실패한다. Review 기준은 2,824개인데 현재 exact interface에서
2,833개가 계산된다. 출력된 차이는 C++ exact-interface member이며 bindings source 감사에서 만든
변경이 아니다. 이 trace 차이를 승인하거나 되돌린 뒤 전체 문서 gate를 다시 실행해야 한다.

Bindings 수정본은 package patch version `11.0.2`로 다시 배포했다. 포함한 Core runtime은
`11.0.0`이며 major와 minor가 같은 상태에서 binding patch만 올렸다. Package를 만들기 전에
현재 Core candidate의 전체 test 84개와 독립 High review를 통과했다. Package 내부 Core
provenance와 native library hash도 검토된 Core package와 비교했다.

| 검증 | 결과 | 증거 |
|---|---|---|
| .NET architecture·unit test | 139/139 통과 | `V11-M4-PKG-DN` package evidence |
| .NET isolated NuGet consumer | 통과 | `Systems.Zlink.11.0.2.nupkg` |
| Java architecture·unit test | 66/66 통과 | `V11-M4-BIND-JVM` test evidence |
| Java isolated Maven consumer | 통과 | `V11-BINDINGS-11.0.2/java-consumer.json` |
| C++ contract architecture test·isolated package consumer | 11/11·통과 | `V11-BINDINGS-11.0.2/cpp-package.json` |
| Node source layout 포함 raw test·CJS·ESM consumer | 36/36·통과 | `zlink-systems-zlink-11.0.2.tgz` |
| Java/Kotlin sample package mode | 290 task 통과, source included build 0개 | `V11-BINDINGS-11.0.2/java-samples-package-mode.log` |

C++ gate는 `Contracts` header의 raw `zlink.h`, `Runtime`, `Native`와 `src` include를 거부한다. Node
gate는 contract source의 runtime·native import, re-export, `require`, dynamic import와 package 내부
subpath export를 거부한다. 두 gate 모두 금지 입력을 실제로 검출하는 mutation case를 기본 suite에서
실행한다.

## 10. 검토 결과와 적용 순서

### 10.1 그대로 적용할 원칙

다음 문제는 source layout 취향이 아니라 public boundary 결함이므로 수정한다.

- `runtime/internal` 아래의 declaration을 외부에서 직접 사용할 수 있으면 안 된다.
- Application-facing contract와 provider SPI를 같은 artifact 분리 이유로 취급하지 않는다.
- Contract owner는 runtime 구현을 참조하지 않는다.
- Exact interface, production owner와 generated inventory가 같은 파일을 가리켜야 한다.
- 각 배포 package는 public declaration이 허용된 owner 밖으로 이동하면 실패하는 layout gate를 둔다.

### 10.2 수정해서 적용할 원칙

`Contracts/contracts`라는 디렉터리 이름 자체를 모든 언어의 공개 계약으로 강제하지 않는다. Java와
Kotlin은 source 경로 변경이 package FQN 변경을 뜻할 수 있고, .NET은 source directory보다 assembly
dependency가 더 중요한 경계다. 다음 두 안을 비교했다.

| 안 | 장점 | 문제 |
|---|---|---|
| 모든 언어를 같은 contract subtree로 이동 | 저장소에서 찾기 쉽고 단순한 layout gate를 만들 수 있다 | 언어별 package·assembly 경계를 무시하고 불필요한 public FQN 변경을 만든다 |
| 배포 artifact별 owner를 선언하고 dependency·visibility를 검사 | 언어별 package 모델을 유지하면서 실제 외부 노출과 역방향 dependency를 차단한다 | 언어별 owner manifest와 gate가 필요하다 |

두 번째 안을 사용한다. Directory는 owner를 표현하는 수단이며 public contract의 정의 자체가 아니다.
Namespace와 package FQN은 exact interface가 정한다. Source를 옮기더라도 exact interface 변경이 필요하지
않으면 public 이름을 바꾸지 않는다.

### 10.3 .NET 적용 결정

현재 `Zlink.HttpClient`는 `Zlink.Framework.Contracts` 전체를 참조하고, 이 contract assembly는 다시
`Systems.Zlink.Stream.Connector`를 참조한다. HTTP client가 server와 Stream Connector contract를
간접적으로 의존하는 구조이므로 package boundary가 깊어지지 않고 dependency만 넓어진다.

다만 source를 옮기기 전에 공유 codec·error contract의 owner를 먼저 정해야 한다. 현재
`Zlink.Framework.Contracts`에는 server application contract 외에 `IZLinkCodecRegistryBuilder`, serializer,
encoded payload와 error type이 함께 있다. HTTP client의 public builder도 이 type을 그대로 사용한다.
Runtime-only codec resolver도 같은 project에 있으므로 파일을 통째로 `Zlink.Framework`로 옮기면 안 된다.

변경 전 public API와 assembly dependency는
`.artifacts/v11/evidence/V11-SOURCE-LAYOUT/dotnet-prechange-snapshot.md`에 고정했다. Release build는
warning·error 없이 통과했다. `Zlink.Framework.Contracts`가 export하는 133개 type은 server application
contract 124개와 공유 codec·error contract 9개로 나뉜다. Public export는 아니지만 같은 project에 있는
runtime-only type 세 개도 snapshot에 따로 기록했다.

| 안 | 결과 | 판정 |
|---|---|---|
| codec·error를 Server와 HTTP client가 각각 선언한다 | package dependency는 사라지지만 같은 역할의 public type과 설정 방법이 두 벌이 된다 | 사용하지 않는다 |
| 공유하는 최소 codec·error contract만 package-neutral artifact가 소유한다 | public type identity를 하나로 유지하고 Server·HTTP client가 runtime 구현 없이 같은 contract를 참조한다 | 기본안 |

두 번째 안을 기본으로 상세 dependency graph와 exact interface를 확정한다. Artifact 이름과 기존 assembly
identity 유지 여부는 public API snapshot을 만든 뒤 결정한다. 이 결정 전에는
`Zlink.Framework.Contracts` project를 삭제하거나 public type의 FQN을 바꾸지 않는다. Runtime-only codec
resolver는 contract artifact에서 제거하고 실제 runtime owner로 옮긴다.

다음 구조로 정리한다.

1. Server application contract는 `Zlink.Framework`가 소유한다.
2. HTTP client와 Stream Connector는 자기 package의 contract만 소유한다.
3. Server와 HTTP client가 함께 사용하는 최소 codec·error contract만 package-neutral artifact가
   소유한다. 각 package가 같은 contract를 복제하지 않는다.
4. 독립 provider가 구현하는 Location·Relocation SPI는
   `Zlink.Framework.Provider.Abstractions`에 유지한다.
5. 기존 `Zlink.Framework.Contracts` project는 application contract consumer를 모두 제거한다. 최소 공유
   artifact로 축소할지 새 artifact로 대체할지는 assembly compatibility 결정에 따른다. 대체하는 경우에만
   package consumer test가 통과한 뒤 기존 project를 삭제한다.

이 변경은 inbound dispatch F-03~F-06과 .NET runtime 회귀가 끝난 뒤 시작한다. Runtime 구현 중에 assembly
owner를 동시에 바꾸면 compile 오류가 기능 gap인지 source 이동 누락인지 구분하기 어렵기 때문이다.
반면 정식 .NET POSD review와 다른 언어의 기준 구현 이식보다 먼저 끝낸다. Reviewer와 다른 언어가 최종
package boundary를 기준으로 비교해야 하기 때문이다.

### 10.4 Java·Kotlin과 다른 package 적용

변경 전 snapshot은
`.artifacts/v11/evidence/V11-SOURCE-LAYOUT/java-kotlin-prechange-snapshot.md`에 고정했다.
Java의 `runtime/internal/binding/spot` 67개 declaration은 exact interface의 public contract가 아니며 모두
runtime-only다. 이 중 같은 package 안에서만 사용하는 17개 declaration은 package-private로 낮췄다.
전체 package는 `zlink-framework-binding-internal` artifact로 옮겼다. Core는 이 artifact를
`implementation` dependency로 사용하므로 application compile classpath에는 raw binding type이 나타나지
않는다. Named module에서는 필요한 Framework companion module에만 package를 export한다.
외부 module과 classpath consumer가 internal binding type을 import하는 fixture는 모두 실패해야 한다.
Public channel contract를 import하는 fixture는 두 방식 모두 성공해야 한다.

Kotlin이 직접 선언한 public interface 33개는 public FQN을 유지한 채 `kotlin/contracts` source owner로
옮겼다. Coroutine lifecycle adapter도 같은 owner가 관리한다. 다음 세 항목은 정식 spec과 사용 안내를
대조해 판정했다.

- `ZLinkSuspendingTypedSessionPacketHandler`는 Kotlin coroutine handler 계약이므로 exact STREAM interface에
  시그니처를 추가하고 public contract로 유지한다.
- `ZLinkSuspendingSession`은 Kotlin guide가 Java `ZLinkSession`의 coroutine adapter로 정의하므로 exact
  STREAM interface에 시그니처를 추가하고 public contract로 유지한다.
- `ZLinkSuspendingInstanceSpot`은 기존 exact Spot interface에 따라 production declaration을 추가한다.

Owner와 gate는
`framework/doc/contract-inventory/jvm-public-contract-source-owners.json`에 고정했다. Java contract를
Kotlin source에 복제하지 않는다.

HTTP client와 Stream Connector는 server 정리가 통과한 뒤 같은 기준을 적용한다. 각 package consumer
build, public API snapshot과 layout gate를 함께 통과해야 이동을 완료한 것으로 판정한다.

### 10.5 .NET P0 적용 결과

2026-07-31에 §10.3의 두 번째 안을 적용했다.

- Actor, Channel, Handler, Message, Spot, Stream, Timer와 Worker application contract source를
  `Zlink.Framework/Contracts/`로 옮겼다. Namespace와 public signature는 바꾸지 않았다.
- 기존 `Zlink.Framework.Contracts` artifact는 codec·error contract 9개만 소유한다. Server와 HTTP
  client는 이 9개 type identity를 계속 함께 사용한다.
- Codec resolver, JSON option, Actor join 내부 결과, Spot 내부 결과와 조회 값, message encode 구현은
  `Zlink.Framework/Runtime/`으로 옮겼다. Shared contract assembly는 runtime-only type을 포함하지 않는다.
- `Zlink.Framework.Provider.Abstractions`와 Redis provider의 dependency는 바꾸지 않았다.
- `SourceLayoutContracts`는 source owner, assembly owner, shared export allow-list, provider SPI 독립성과
  HTTP client의 Server runtime 비의존성을 검사한다.

검증 결과는 다음과 같다.

| 검증 | 결과 |
|---|---|
| `Zlink.Framework` Release build | warning 0, error 0 |
| .NET contract test | 72/72 |
| HTTP client unit test | 63/63 |
| codec·message focused runtime test | 59/59 |
| packaged contract와 standalone HTTP consumer | 통과 |

고정 API inventory는 `framework/languages/dotnet/contract/api/`에 새 assembly owner를 반영했다.
Package inventory는 `framework/languages/dotnet/contract/packages/`에 shared artifact 설명과 XML digest를
반영했다. .NET application contract의 FQN과 signature는 유지했으며 assembly owner 변경은 11.0
major-version source boundary 변경으로 기록한다.

#### .NET high review 보정

초기 P0 결과에서는 shared Contracts가 Stream Connector를 참조해 HTTP client까지 Core binding과 LZ4를
간접 참조했다. 이 dependency는 package-neutral shared contract라는 목표와 맞지 않았다.

STREAM header의 codec 값은 Stream Connector package가 소유하는 `IZlinkStreamCodecRegistration`으로
분리했다. 공통 `IZLinkCodecRegistrar`는 business payload serializer만 등록한다. Package gate는 shared
Contracts와 HTTP client가 Stream Connector, Core binding과 LZ4를 참조하면 실패한다. HTTP client가 Server
runtime을 참조하는 경우도 실패한다.

보정 후 contract test 72개와 packaged contract, standalone HTTP consumer가 통과했다. 최종 검증 값은
`.artifacts/v11/evidence/V11-SOURCE-LAYOUT/dotnet-p0-result.md`에 기록했다.

### 10.6 Java·Kotlin P0 적용 결과

2026-07-31에 §10.4의 package boundary를 적용했다.

- Java core, provider와 raw binding implementation artifact에 JPMS descriptor를 추가했다. Application contract package만 일반
  consumer에 export한다.
- Raw binding package는 별도 internal artifact에서 필요한 companion artifact에만 qualified export한다.
- 같은 package에서만 사용하는 raw binding declaration 17개는 package-private로 낮췄다.
- Spring starter의 lifecycle implementation은 `runtime.host`에서 starter 전용 package로 옮겼다.
  Testkit fixture도 고유 package가 소유하므로 companion artifact 사이에 split package가 없다.
- Kotlin coroutine contract source는 `contracts/` owner로 옮겼다. Package와 public FQN은 유지했다.
- Kotlin Instance Spot·Session coroutine contract는 exact interface와 production declaration을 맞췄다.

`verifyPublicContractModuleBoundary`, Java core·starter·testkit contract test와 Kotlin contract test가
모두 통과했다. Gate는 core, Spring starter와 testkit을 한 module path에서 검증한다. Negative consumer는
internal MeshNode import가 JPMS와 classpath에서 모두 거부되는 것을 확인했다. Public trace의 sealed
count는 동시에 추가된 C++ public member 10개를 review한 뒤 갱신한다. Source layout 변경만 근거로
review baseline을 바꾸지 않는다.

### 10.7 Java high review 보정

독립 high review에서 `ZLinkFrameworkRuntime.start(...)` 두 개가 exact public contract에 없는 direct-start
경로로 확인됐다. 두 member는 package-private로 낮췄다. Core의 internal bootstrap package는 Spring starter
module에만 qualified export한다. Testkit의 direct-start 회귀 test는 test source에만 같은 package access
helper를 둔다.

Spring starter와 testkit에는 explicit JPMS descriptor를 추가했다. Application module은 두 artifact의
internal package를 import할 수 없다. HTTP client와 Stream Connector도 module owner를 명시해 companion
artifact를 같은 module path에서 검증할 수 있게 했다.

Gate는 다음 경계를 별도로 확인한다.

- module path와 classpath application은 `ZLinkFrameworkRuntime.start(...)`를 호출할 수 없다.
- module path application은 starter와 testkit의 internal package를 import할 수 없다.
- core, starter, testkit, HTTP client와 Stream Connector module을 함께 resolve할 수 있다.
- 실제 module path에서 Spring bootstrap을 호출하면 runtime이 `SERVING`에 도달한다.
- core 662개, starter 35개, testkit 1개와 contract 27개, Kotlin 47개 test가 통과한다.

최종 증거는 `.artifacts/v11/evidence/V11-SOURCE-LAYOUT/java-kotlin-high-review-fix.md`에 기록한다.
