# Java Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/java/e2e`를 같은 폴더 형태, 역할 분리, 파일 분류, 실행 검증 수준으로 다시
정렬하는 절차를 정의한다.

기존 Java e2e가 `src/main/java` 아래 단일 package 중심으로 구현되어 있더라도, 이번 포팅의 완료 기준은
`.NET` e2e의 구조다. 각 config는 `.NET`처럼 `Shared`, `Server/<Role>`, `Client/Scenarios`,
`Client/Support`에 해당하는 책임을 분리해야 한다.

## 완료 기준

1. `framework/languages/java/e2e/<Config>/`가 대응하는 `.NET` config와 같은 의미의 구조를 가진다.
2. Gradle 파일과 Java package 배치는 Java 관례에 맞출 수 있지만, 역할과 파일 책임은 `.NET` 기준과
   대응된다.
3. `.NET`에 있는 scenario, server role, shared message, support 책임이 빠지지 않는다.
4. Java public framework API로 구현할 수 없는 항목은 내부 helper나 테스트 전용 adapter로 메우지 않고
   `feature-map.ko.md`에 gap으로 남긴다.
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
3. Java framework public surface:
   - `framework/doc/framework/java/`
   - `framework/languages/java/zlink-framework-core/`
   - `framework/languages/java/zlink-framework-spring-boot-starter/`
   - `framework/languages/java/zlink-framework-testkit/`
4. Java e2e 대상:
   - `framework/languages/java/e2e/<Config>/`

공통 e2e 문서는 구현 검증 기준이다. 새 public API가 필요해 보이면 먼저 spec/guide/draft 검토로
분리하고, e2e에서 private API로 우회하지 않는다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 Java에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, Java에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 현재 Java 작업물 처리 원칙

Java e2e에는 이미 여러 config의 runner, source, feature-map이 있다. 이 작업물은 삭제 기준이 아니라
`.NET` e2e inventory와 공통 e2e 문서에 맞춰 분류할 입력이다. 작업자는 먼저 기존 Java 파일이 어떤
`.NET` 파일과 scenario에 대응하는지 확인하고, 유지할 구현과 버릴 구조를 분리한다.

현재 상태:

- `framework/languages/java/e2e`의 기존 파일은 검토 입력이다.
- 기존 Java e2e는 단일 Gradle project와 단일 package 안에 역할이 섞인 config가 많다. 구현은 최대한
  보존하되, 목표 구조는 `.NET`의 `Shared`, `Server/<Role>`, `Client/Scenarios`, `Client/Support`
  분류에 맞춘다.
- 기존 `Monitoring` 이름은 `.NET` 기준 `RuntimeMonitoring`과 이름이 맞지 않는다. 완료 config로
  인정하기 전에 rename 또는 새 config 작성 여부를 inventory에서 결정한다.
- `YieldDispatch`는 Java e2e에 구현을 만들었다. 현재 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`를
  검증한다. `YD-B1`은 actor A와 actor B를 같은 target spot에 join한 뒤, actor A가 `yield`로 기다리는
  동안 actor B의 fast request가 먼저 완료되는 범위를 검증한다. `YD-B2`는 같은 target actor의 fast request가 yield continuation 뒤에
  처리되는지 검증한다. `YD-B3`는 Play role에서 만든 actor ref를 session에 bind하고, actor join call을
  `yield`로 기다리는 동안 다른 actor의 entry actor request가 먼저 완료되는지 검증한다. `YD-C1`은 같은
  target spot에서 yield 중인 timer가 기다리는 동안 빠른 timer tick이 먼저 완료되는지 검증한다. `YD-C2`는
  같은 timer의 다음 tick이 이전 tick의 yield continuation과 completion 뒤에 처리되는지 검증한다. `YD-C3`는
  actor와 timer가 서로 다른 mailbox로 진행되는지 검증한다. `YD-D2`는 `play-a` owner spot이 `play-b`
  target spot reply를 `yield`로 기다린 뒤 원래 owner spot에서 재개되는지 검증한다. `YD-D3`는 session
  gateway가 route mesh로 보낸 packet이 `play-b` target spot handler에서 yield하는 동안 probe가 먼저
  처리되는지 검증한다. `YD-D4`는 stream session relay로 bound actor handler에 들어간 request가
  `yield` 중일 때 bound session push를 원래 stream connector로 보내고, 다른 actor의 push wait는
  진행되지 않는지 검증한다. `YD-E1`은 timeout cleanup 뒤 같은 Spot mailbox가 probe를 처리하는지 검증한다.
  최신 재실행 로그는 `logs/20260630-135938-79287`이며 YD-A/B/C/D/E1, E4 정적 검증,
  E5 marker report 생성이 통과했다.
  `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1` 전체 gate는 `logs/20260630-134740-48684`에서 pending yield 중
  play-a SIGTERM shutdown, public framework error reply, 같은 endpoint 재시작 뒤 recovery request를
  통과했다. YD-E2 cancellation cleanup은 Java public yield terminator에 cancellation-aware 계약이
  없어 feature-map에 public contract gap으로 남겨 둔다. 공통 E2E만 근거로 새 public API를 추가하지
  않고, `framework/doc/framework/java/spec/draft/yield-cancellation.ko.md`에서 계약을 먼저 검토한 뒤
  구현한다.

판단:

- 기존 Java e2e를 그대로 완료로 인정하면 `.NET` 기준 폴더 분류와 scenario 파일 대응을 놓칠 수 있다.
- 반대로 기존 구현을 버리면 이미 작성된 runner, public API 호출, feature-map의 gap 분류를 잃는다.
- 따라서 Java는 config별로 **기존 구현 보존을 기본값**으로 두고, `.NET` 기준 inventory에서 불일치가
  확인된 파일만 이동, 재작성, 삭제한다.
- config별 첫 산출물은 `porting-inventory.ko.md`다. 이 파일에서 기존 Java 파일의 유지, 이동, 재작성,
  삭제 판단을 먼저 끝낸 뒤 코드 변경을 시작한다.

1차 분류:

| config | 현재 판단 | 작업 원칙 |
|--------|-----------|-----------|
| `RegistryMessaging` | 리팩토링 대상 | 공통 scenario 대부분을 구현했고 runner가 살아 있다. 단일 package 구조를 `.NET`의 `Client/Scenarios`, `Server/Provider`, `Server/Registry`, `Server/Workflow`, `Shared` 분류로 나눈다. |
| `SpotService` | 리팩토링 대상 | 구현 범위와 gap 분류가 이미 크다. 삭제하지 않고 `.NET` 기준 scenario 파일 분류로 client 흐름을 나누며, public contract gap은 유지한다. |
| `PubSub` | 리팩토링 대상 | 구현된 fanout/topic/late/reconnect/negative scenario를 보존한다. publisher, registry, subscriber 역할과 client scenario/support를 분리한다. |
| `RegistrationCodec` | 리팩토링 대상 | codec과 registration scenario 구현이 있으므로 보존한다. 현재 단일 package에 모인 handler/filter/invalid server 코드를 `.NET` 역할 분류에 맞춰 이동한다. |
| `DiscoveryRegistryHa` | 리팩토링 대상 | HA scenario coverage가 넓다. provider, registry, consumer, probe, embedded 역할을 `.NET` 구조에 맞게 분리한다. |
| `ResilienceLifecycle` | 리팩토링 대상 | 구현된 scenario와 gap 분류가 많다. client scenario/support와 server role 분류를 맞추고, 미완료 항목은 feature-map에 남긴다. |
| `Monitoring` | RuntimeMonitoring 전환 대상 | `.NET` 기준 config 이름은 `RuntimeMonitoring`이다. 기존 Java `Monitoring`은 구현이 있으므로 먼저 `RuntimeMonitoring` inventory에서 유지할 파일과 이동할 파일을 결정한다. 전환이 끝나면 기존 `Monitoring` 디렉터리는 삭제한다. |
| `YieldDispatch` | public contract gap | Java e2e에 config를 만들었다. `run_e2e.sh`는 registry, delay, play-a, play-b, session, client process를 띄워 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-B2`, `YD-B3`, `YD-C1`, `YD-C2`, `YD-C3`, `YD-D2`, `YD-D3`, `YD-D4`, `YD-E1`, E4 정적 검증, E5 marker report 생성을 수행한다. 최신 재실행 로그는 `logs/20260630-135938-79287`이며 현재 기본 runner 범위는 통과했다. `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1` 전체 gate는 `logs/20260630-134740-48684`에서 YD-E3 shutdown/restart recovery를 통과했다. 남은 범위는 YD-E2 cancellation cleanup이며, Java public yield terminator에 cancellation-aware 계약이 없어 public contract gap으로 둔다. 후보 계약은 `framework/doc/framework/java/spec/draft/yield-cancellation.ko.md`에 분리했다. |

## 표준 Java E2E 구조

Java build 도구와 package 구조는 Gradle 관례를 따른다. 다만 repository 파일 트리는 아래 의미를
유지한다.

```text
framework/languages/java/e2e/<Config>/
|-- Shared/
|   `-- src/main/java/.../shared/
|-- Server/
|   |-- <Role>/
|   |   |-- build.gradle.kts
|   |   |-- src/main/java/.../<role>/Program.java
|   |   |-- src/main/java/.../<role>/Configuration/
|   |   |-- src/main/java/.../<role>/Endpoints/
|   |   |-- src/main/java/.../<role>/Handlers/
|   |   |-- src/main/java/.../<role>/Infrastructure/
|   |   `-- src/main/java/.../<role>/Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- build.gradle.kts
|   |-- src/main/java/.../client/Program.java
|   |-- src/main/java/.../client/Scenarios/
|   `-- src/main/java/.../client/Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- build.gradle.kts
|-- settings.gradle.kts
|-- feature-map.ko.md
`-- run_e2e.sh
```

Gradle multi-project 구성이 더 단순하면 하나의 `settings.gradle.kts` 아래에 `:Client`, `:Shared`,
`:Server:<Role>` 형태로 둔다. 하지만 서로 다른 server role을 하나의 Java application에서 `--role`
옵션으로 바꾸는 방식은 완료로 보지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared` | client와 server가 함께 쓰는 request, reply, event, evidence 타입 |
| `Client/.../Program.java` | scenario 목록과 실행 순서 선언 |
| `Client/.../Scenarios/` | `.NET Client/Scenarios` 파일 하나에 대응하는 Java scenario 파일 |
| `Client/.../Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/.../Program.java` | role 실행 진입점 |
| `Server/<Role>/.../Configuration/` | role 실행 옵션과 환경 변수 해석 |
| `Server/<Role>/.../Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/.../Handlers/` | framework handler, observer, spot, actor handler |
| `Server/<Role>/.../Infrastructure/` | evidence store와 role 내부 상태 |
| `Server/<Role>/.../Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | Gradle build, 포트 할당, role process 시작과 종료, client 실행, 실패 로그 출력 |
| `feature-map.ko.md` | scenario ID별 구현 상태, gap, 검증 결과 |

`Program.java`는 진입점만 담당한다. framework 설정과 endpoint 등록이 길어지면 host factory 또는
configuration class로 나눈다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. `.NET` role root에 option, endpoint, handler,
evidence 파일이 남아 있으면 Java에서는 책임별 package와 폴더로 재분류한다.

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
helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 Java에서는 `Client/.../Support/`나 `Shared`로
옮긴다.

`.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
`porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 Java 대응 scenario 파일을 명시한다.

## Inventory 매핑 산출물

각 config는 `.NET` 기준 파일 하나하나가 Java에서 어디로 옮겨졌는지 기록하는 매핑 문서를 반드시 둔다.

```text
framework/languages/java/e2e/<Config>/porting-inventory.ko.md
```

형식:

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/src/main/java/.../Scenarios/...Scenario.java` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/src/main/java/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/src/main/java/.../shared/...` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/src/main/java/.../Support/...` | support | done/gap | 공통 helper 책임 |

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
- `.NET` 파일 하나가 여러 Java 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- Java에서 해당 파일이 필요 없다고 판단해도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`로 두고
  근거를 비고에 적는다.
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

기존 Java e2e의 `Monitoring` 이름은 `.NET`과 맞춰 `RuntimeMonitoring`으로 정렬한다. 기존에 없는
`YieldDispatch`도 공통 e2e와 `.NET`에 있으므로 별도 config로 다룬다.

## Config 단위 작업 절차

1. `.NET` inventory를 생성한다.
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
2. `.NET` scenario, role, shared message, support 목록을 `porting-inventory.ko.md`에 정리한다.
3. 공통 e2e 문서에서 scenario ID와 성공 조건을 확인한다.
4. `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
5. Java public API와 문서에서 같은 동작을 제공할 수 있는지 확인한다.
6. public contract gap이 있으면 구현하지 말고 `feature-map.ko.md`에 남긴다.
7. Java Gradle 프로젝트와 package를 `.NET` 역할 구조에 맞춰 만들되, stale `.NET` root 파일은 목표 폴더로 재분류한다.
8. Shared 타입, server role, client scenario 순서로 포팅한다.
9. scenario ID가 없는 helper/context 파일은 `Client/.../Scenarios/`가 아니라 `Client/.../Support/` 또는
   `Shared`로 옮긴다.
10. `run_e2e.sh`가 실제 role process를 띄우고 readiness, cleanup, 실패 로그 출력을 처리하게 한다.
11. `porting-inventory.ko.md`의 모든 행에 Java 대응 파일, 분류, 상태를 채운다.
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
대상: framework/languages/java/e2e/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 Java에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 Java에서 빠짐없이 대응되는가.
3. Shared message, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 Java 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 Java feature-map에서 과장 없이 반영했는가.
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

- `.NET` inventory와 Java 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `.NET Client/Scenarios` 아래 파일을 scenario ID 파일과 helper/context 파일로 구분했고, scenario ID 파일만
  Java `Client/.../Scenarios/`에 대응된다.
- `.NET`의 모든 server role이 Java `Server/<Role>/`에 대응된다.
- 공통 e2e 문서의 scenario ID가 `feature-map.ko.md`와 client scenario 파일에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/.../Scenarios/`에 두지 않았다.
- Java sample처럼 보이는 임시 우회 코드가 e2e에 들어가지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- Gradle build output, `.gradle`, `build`, 임시 로그는 커밋하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.
