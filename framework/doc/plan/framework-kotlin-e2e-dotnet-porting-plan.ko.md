# Kotlin Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/java/e2e-kotlin`의 Kotlin e2e를 같은 폴더 형태, 역할 분리, 파일 분류, 실행 검증
수준으로 다시 정렬하는 절차를 정의한다.

Kotlin e2e는 Java runtime 위의 Kotlin 사용 표면을 검증한다. Java e2e를 그대로 복사하는 것이 아니라,
`.NET` 기준 구조와 공통 e2e scenario를 Kotlin DSL과 Kotlin 사용자 코드 형태로 포팅한다.

## 완료 기준

1. `framework/languages/java/e2e-kotlin/<Config>/`가 대응하는 `.NET` config와 같은 의미의 구조를 가진다.
2. Gradle 파일과 Kotlin package 배치는 Kotlin 관례에 맞출 수 있지만, 역할과 파일 책임은 `.NET` 기준과
   대응된다.
3. `.NET`에 있는 scenario, server role, shared message, support 책임이 빠지지 않는다.
4. Kotlin public 사용 표면으로 구현할 수 없는 항목은 Java internal helper나 테스트 전용 adapter로
   메우지 않고 `feature-map.ko.md`에 gap으로 남긴다.
5. 포팅 중 버그가 발생하면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 원인을 public runtime,
   framework, stream connector, zlink http client, e2e harness 중 책임 계층까지 추적하고, 같은 문제가
   다시 생기지 않도록 회귀 테스트를 먼저 추가하거나 함께 추가한 뒤 수정한다.
6. 한 config의 build, `run_e2e.sh` 실행, feature-map 갱신, Codex 에이전트 리뷰가 모두 끝나기 전에는
   다음 config 작업을 시작하지 않는다.
7. Codex 에이전트 리뷰에서 이슈 없음이 나와야 해당 config를 완료로 본다.

## 기준

1. 공통 e2e 문서:
   - `framework/doc/framework/common/e2e/README.ko.md`
   - `framework/doc/framework/common/e2e/config-*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/e2e/<Config>/`
   - `framework/languages/dotnet/e2e/<Config>/feature-map.ko.md`
3. Kotlin/Java framework public surface:
   - `framework/doc/framework/kotlin/`
   - `framework/doc/framework/java/`
   - `framework/languages/java/zlink-framework-core/`
   - `framework/languages/java/zlink-framework-spring-boot-starter/`
4. Kotlin e2e 대상:
   - `framework/languages/java/e2e-kotlin/<Config>/`

공통 e2e 문서는 검증 기준이고, 새 public API 추가 근거가 아니다. Kotlin에 없는 기능이 `.NET`에 있더라도
spec 또는 공통 framework 계약 근거를 먼저 확인한다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 Kotlin에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, Kotlin에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 현재 Kotlin 작업물 처리 원칙

Kotlin e2e에는 이미 여러 config의 runner, Kotlin source, 일부 Java support source, feature-map이
있다. 이 작업물은 삭제 기준이 아니라 `.NET` e2e inventory와 공통 e2e 문서에 맞춰 분류할 입력이다.
작업자는 기존 Kotlin 파일이 어떤 `.NET` 파일과 scenario에 대응하는지 먼저 확인하고, 유지할 구현과
버릴 구조를 분리한다.

현재 상태:

- `framework/languages/java/e2e-kotlin`의 기존 파일은 검토 입력이다.
- 기존 Kotlin e2e는 Kotlin code path가 있는 config와 Java support source가 섞인 config가 있다. Kotlin
  사용자가 보는 설정, handler 등록, client scenario 흐름은 보존하거나 Kotlin으로 끌어올리고, 순수
  support 역할만 Java에 남긴다.
- 기존 `Monitoring` 이름은 `.NET` 기준 `RuntimeMonitoring`과 이름이 맞지 않는다. 완료 config로
  인정하기 전에 rename 또는 새 config 작성 여부를 inventory에서 결정한다.
- `YieldDispatch`는 Kotlin e2e에 없으므로 `.NET`과 공통 e2e 기준으로 새로 만든다.

판단:

- 기존 Kotlin e2e를 그대로 완료로 인정하면 `.NET` 기준 폴더 분류와 scenario 파일 대응을 놓칠 수 있다.
- 반대로 기존 구현을 버리면 이미 작성된 runner, Kotlin public API 호출, feature-map의 gap 분류를 잃는다.
- 따라서 Kotlin은 config별로 **기존 구현 보존을 기본값**으로 두고, `.NET` 기준 inventory에서 불일치가
  확인된 파일만 이동, Kotlin 재작성, 삭제한다.
- `src/main/java`를 둘 수는 있지만, public contract gap을 숨기는 우회로 쓰지 않는다. Kotlin 사용자 코드가
  보는 설정, handler 등록, client scenario 흐름은 `src/main/kotlin`에 드러나야 한다.
- config별 첫 산출물은 `porting-inventory.ko.md`다. 이 파일에서 기존 Kotlin/Java support 파일의 유지,
  이동, Kotlin 재작성, 삭제 판단을 먼저 끝낸 뒤 코드 변경을 시작한다.

1차 분류:

| config | 현재 판단 | 작업 원칙 |
|--------|-----------|-----------|
| `RegistryMessaging` | 리팩토링 대상 | Kotlin scenario code가 있고 runner가 살아 있다. `.NET`의 client scenario/support와 server role 분류에 맞춰 나눈다. |
| `SpotService` | Java 중심 구현을 Kotlin 호출 경로로 끌어올릴 리팩토링 대상 | 구현 범위와 gap 분류는 크지만, 현재 Kotlin source는 entrypoint 중심이고 client scenario와 server 설정은 Java source가 담당한다. 기존 scenario 구현은 보존하되, Kotlin 사용자가 보는 client scenario, handler 등록, server 설정 흐름을 Kotlin code path로 끌어올린 뒤 `.NET` 기준으로 분리한다. |
| `PubSub` | 리팩토링 대상 | Kotlin source로 공통 scenario를 구현했다. publisher, registry, subscriber 역할과 client scenario/support를 분리한다. |
| `RegistrationCodec` | 리팩토링 대상 | Kotlin source 중심으로 codec/registration scenario를 구현했다. 현재 한 project에 모인 handler/filter/server 역할을 `.NET` 분류에 맞춰 이동한다. |
| `DiscoveryRegistryHa` | 리팩토링 대상 | Java support와 Kotlin entrypoint가 섞여 있지만 HA scenario coverage가 넓다. 역할 분류를 맞추고 Kotlin에서 보이는 client 흐름을 분리한다. |
| `ResilienceLifecycle` | 리팩토링 대상 | Java support와 Kotlin entrypoint가 섞여 있지만 구현된 scenario와 gap 분류가 많다. client scenario/support와 server role 분류를 맞춘다. |
| `Monitoring` | RuntimeMonitoring 전환 대상 | `.NET` 기준 config 이름은 `RuntimeMonitoring`이다. 기존 Kotlin `Monitoring`은 구현이 있으므로 먼저 `RuntimeMonitoring` inventory에서 유지할 파일과 Kotlin으로 끌어올릴 파일을 결정한다. 전환이 끝나면 기존 `Monitoring` 디렉터리는 삭제한다. |
| `YieldDispatch` | 신규 작성 대상 | Kotlin e2e에 해당 config가 없다. `.NET`과 공통 e2e Config 8을 기준으로 새로 만든다. |

## 표준 Kotlin E2E 구조

```text
framework/languages/java/e2e-kotlin/<Config>/
|-- Shared/
|   `-- src/main/kotlin/.../shared/
|-- Server/
|   |-- <Role>/
|   |   |-- build.gradle.kts
|   |   |-- src/main/kotlin/.../<role>/Program.kt
|   |   |-- src/main/kotlin/.../<role>/Configuration/
|   |   |-- src/main/kotlin/.../<role>/Endpoints/
|   |   |-- src/main/kotlin/.../<role>/Handlers/
|   |   |-- src/main/kotlin/.../<role>/Infrastructure/
|   |   `-- src/main/kotlin/.../<role>/Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- build.gradle.kts
|   |-- src/main/kotlin/.../client/Program.kt
|   |-- src/main/kotlin/.../client/Scenarios/
|   `-- src/main/kotlin/.../client/Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- build.gradle.kts
|-- settings.gradle.kts
|-- feature-map.ko.md
`-- run_e2e.sh
```

Kotlin에서 일부 Java helper를 함께 컴파일해야 하면 `src/main/java`를 둘 수 있다. 다만 Kotlin e2e의
검증 흐름과 사용 예시는 Kotlin code path가 중심이어야 한다. Java helper가 public contract gap을
숨기는 우회가 되면 완료로 보지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared` | client와 server가 함께 쓰는 request, reply, event, evidence 타입 |
| `Client/.../Program.kt` | scenario 목록과 실행 순서 선언 |
| `Client/.../Scenarios/` | `.NET Client/Scenarios` 파일 하나에 대응하는 Kotlin scenario 파일 |
| `Client/.../Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/.../Program.kt` | role 실행 진입점 |
| `Server/<Role>/.../Configuration/` | role 실행 옵션과 환경 변수 해석 |
| `Server/<Role>/.../Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/.../Handlers/` | framework handler, observer, spot, actor handler |
| `Server/<Role>/.../Infrastructure/` | evidence store와 role 내부 상태 |
| `Server/<Role>/.../Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | Gradle build, 포트 할당, role process 시작과 종료, client 실행, 실패 로그 출력 |
| `feature-map.ko.md` | scenario ID별 구현 상태, gap, 검증 결과 |

`Program.kt`는 진입점만 담당한다. Kotlin DSL 설정과 endpoint 등록이 길어지면 role별 host factory와
configuration class로 나눈다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. `.NET` role root에 option, endpoint, handler,
evidence 파일이 남아 있으면 Kotlin에서는 책임별 package와 폴더로 재분류한다.

- option, argument, endpoint 주소 설정: `Server/<Role>/.../Configuration/`
- HTTP endpoint mapping, evidence wait, shutdown endpoint: `Server/<Role>/.../Endpoints/`
- framework handler, dispatch filter, observer, spot, actor handler: `Server/<Role>/.../Handlers/`
- evidence store, runtime state, in-memory repository: `Server/<Role>/.../Infrastructure/`
- 해당 role 내부에서만 쓰는 relay, wait, runtime helper: `Server/<Role>/.../Support/`
- 여러 scenario가 함께 쓰는 client-side context나 helper: `Client/.../Support/`
- scenario ID 하나를 실행하는 파일: `Client/.../Scenarios/`

재분류한 파일은 `porting-inventory.ko.md` 비고에 원본 위치와 목표 위치를 함께 적는다.

## Scenario ID 판정 규칙

`Client/Scenarios/` 아래에 있다는 이유만으로 모두 scenario 파일로 보지 않는다. scenario 파일은 공통
e2e 문서의 scenario ID 하나를 직접 실행하고 marker를 검증하는 파일이다. context, shared record, fixture,
helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 Kotlin에서는 `Client/.../Support/`나 `Shared`로
옮긴다.

`.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
`porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 Kotlin 대응 scenario 파일을 명시한다.

## Inventory 매핑 산출물

각 config는 `.NET` 기준 파일 하나하나가 Kotlin에서 어디로 옮겨졌는지 기록하는 매핑 문서를 반드시 둔다.

```text
framework/languages/java/e2e-kotlin/<Config>/porting-inventory.ko.md
```

형식:

| .NET 기준 파일 | Kotlin 대응 파일 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/src/main/kotlin/.../Scenarios/...Scenario.kt` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/src/main/kotlin/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/src/main/kotlin/.../shared/...` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/src/main/kotlin/.../Support/...` | support | done/gap | 공통 helper 책임 |

규칙:

- `.NET` 기준 파일 목록은 아래 명령으로 생성한다.

```bash
find framework/languages/dotnet/e2e/<Config> -type f \
  ! -path '*/bin/*' \
  ! -path '*/obj/*' \
  ! -path '*/logs/*' \
  | sed 's#^framework/languages/dotnet/e2e/<Config>/##' \
  | sort
```

- `.csproj`, `.gitignore`, `run_e2e.sh`, `feature-map.ko.md`, `README.ko.md`도 매핑에서 빠뜨리지 않는다.
- `.NET` 파일 하나가 여러 Kotlin 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- Kotlin에서 해당 파일이 필요 없다고 판단해도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`로
  두고 근거를 비고에 적는다.
- `pending` 상태가 하나라도 있으면 config 완료로 보지 않는다.

## 진행 순서

| 순서 | Config | 기준 문서 | 완료 조건 |
|------|--------|-----------|-----------|
| 1 | `RegistryMessaging` | `config-1-registry-messaging.ko.md` | `.NET`의 RM-* scenario, registry/provider/workflow/consumer role 전부 대응 |
| 2 | `PubSub` | `config-3-pubsub.ko.md` | publisher/subscriber/registry role과 pubsub scenario 전부 대응 |
| 3 | `RegistrationCodec` | `config-4-registration-codec.ko.md` | registration, codec variant, invalid registration scenario 전부 대응 |
| 4 | `DiscoveryRegistryHa` | `config-6-discovery-registry-ha.ko.md` | cluster, failover, embedded registry, direct endpoint scenario 전부 대응 |
| 5 | `ResilienceLifecycle` | `config-5-resilience-lifecycle.ko.md` | restart, remap, drain, crash, outage, observer failure scenario 전부 대응 |
| 6 | `RuntimeMonitoring` | `config-7-monitoring.ko.md` | monitoring event, filter, dispatch failure, recovery scenario 전부 대응 |
| 7 | `SpotService` | `config-2-spot-service.ko.md` | spot, actor, session, route, timer, multi-node scenario 전부 대응 |
| 8 | `YieldDispatch` | `config-8-yield-dispatch.ko.md` | YD-A/B/C/D/E 전체 scenario 대응. 특히 YD-D1 local topology, YD-E3 runtime shutdown, YD-E4 금지 표면 정적 검증, YD-E5 언어별 의미 동등성까지 확인 |

기존 Kotlin e2e의 `Monitoring` 이름은 `.NET`과 맞춰 `RuntimeMonitoring`으로 정렬한다. 기존에 없는
`YieldDispatch`도 공통 e2e와 `.NET`에 있으므로 별도 config로 다룬다.

## Config 단위 작업 절차

1. `.NET` inventory를 생성한다.
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
2. `.NET` scenario, role, shared message, support 목록을 `porting-inventory.ko.md`에 정리한다.
3. 공통 e2e 문서에서 scenario ID와 성공 조건을 확인한다.
4. `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
5. Kotlin public API와 문서에서 같은 동작을 제공할 수 있는지 확인한다.
6. public contract gap이 있으면 구현하지 말고 `feature-map.ko.md`에 남긴다.
7. Kotlin Gradle 프로젝트와 package를 `.NET` 역할 구조에 맞춰 만들되, stale `.NET` root 파일은 목표 폴더로 재분류한다.
8. Shared 타입, server role, client scenario 순서로 포팅한다.
9. scenario ID가 없는 helper/context 파일은 `Client/.../Scenarios/`가 아니라 `Client/.../Support/` 또는
   `Shared`로 옮긴다.
10. `run_e2e.sh`가 실제 role process를 띄우고 readiness, cleanup, 실패 로그 출력을 처리하게 한다.
11. `porting-inventory.ko.md`의 모든 행에 Kotlin 대응 파일, 분류, 상태를 채운다.
12. 해당 config의 `run_e2e.sh`를 실제 실행한다.
13. 실패하면 같은 config 안에서 수정하고 다시 실행한다. 이때 실패 원인을 모른 채 sleep, retry 횟수 증가,
    runner-only adapter, raw frame 조작으로 덮지 않는다. 원인을 좁혀 framework, stream connector,
    zlink http client, e2e 중 책임 위치를 수정하고 회귀 테스트를 추가한다.
14. 버그를 수정했다면 feature-map 또는 README에 원인, 수정 계층, 추가한 회귀 테스트를 함께 기록한다.
15. Codex 에이전트 리뷰를 요청한다.
16. 리뷰 이슈가 있으면 수정, 재실행, 재리뷰를 반복한다.
17. 리뷰가 이슈 없음이면 config 완료로 기록하고 다음 config로 이동한다.

## Codex 에이전트 리뷰 요청

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/java/e2e-kotlin/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 Kotlin에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 Kotlin에서 빠짐없이 대응되는가.
3. Shared message, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 Kotlin 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 Kotlin feature-map에서 과장 없이 반영했는가.
6. .NET role root에 있던 option/endpoint/handler/evidence/support 파일을 목표 분류로 재배치했는가.
7. Client/Scenarios 아래 helper/context 파일을 scenario로 오분류하지 않았는가.
8. public contract에 없는 기능을 private API, raw frame, test-only adapter로 우회하지 않았는가.
9. run_e2e.sh가 실제 프로세스 경계, readiness, cleanup, 실패 로그 출력을 제대로 처리하는가.
10. feature-map.ko.md가 구현 완료와 gap을 과장 없이 기록하는가.
11. 버그 수정이 임시 우회가 아니라 원인 계층을 고친 변경이며, 재발을 막는 회귀 테스트가 함께 있는가.
12. 실제 실행 결과가 문서와 코드의 완료 주장과 일치하는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```

## 누락 방지 체크리스트

- `.NET` inventory와 Kotlin 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `.NET Client/Scenarios` 아래 파일을 scenario ID 파일과 helper/context 파일로 구분했고, scenario ID 파일만
  Kotlin `Client/.../Scenarios/`에 대응된다.
- `.NET`의 모든 server role이 Kotlin `Server/<Role>/`에 대응된다.
- 공통 e2e 문서의 scenario ID가 `feature-map.ko.md`와 client scenario 파일에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/.../Scenarios/`에 두지 않았다.
- Kotlin 사용 표면 검증을 Java helper나 internal API로 대체하지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- Gradle build output, `.gradle`, `.kotlin`, `build`, 임시 로그는 커밋하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.
