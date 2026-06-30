# C++ Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/cpp/e2e`를 같은 폴더 형태, 역할 분리, 파일 분류, 실행 검증 수준으로 다시
정렬하는 절차를 정의한다.

C++ e2e는 기존 파일을 단순히 보존하면서 이름만 바꾸는 작업으로 보지 않는다. 목표는 `.NET` e2e의
config, role, shared contract, client scenario, support, run script 구조를 C++에 같은 의미로 포팅하는
것이다. 기존 C++ e2e에 일부 config가 없거나 통합 파일로 남아 있으면, `.NET` 기준 구조에 맞춰 다시
나눈다.

## 완료 기준

1. `framework/languages/cpp/e2e/<Config>/`가 대응하는 `.NET` config와 같은 의미의 구조를 가진다.
2. `.NET`의 `Client/`, `Server/`, `Shared/`, `Client/Scenarios/`, `Client/Support/`,
   `Server/<Role>/...` 분리가 C++에도 대응된다.
3. `.NET`에 있는 scenario, role, shared message, support 책임이 빠지지 않는다.
4. C++ public framework API로 구현할 수 없는 항목은 내부 helper, raw frame 조작, 테스트 전용 adapter로
   메우지 않고 `feature-map.ko.md`에 gap으로 남긴다.
5. 포팅 중 버그가 발생하면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 원인을 public runtime,
   framework, stream connector, zlink http client, e2e harness 중 책임 계층까지 추적하고, 같은 문제가
   다시 생기지 않도록 회귀 테스트를 먼저 추가하거나 함께 추가한 뒤 수정한다.
6. 한 config의 구현, 빌드, `run_e2e.sh` 실행, feature-map 갱신, Codex 에이전트 리뷰가 끝나기 전에는
   다음 config를 시작하지 않는다.
7. Codex 에이전트 리뷰에서 이슈 없음이 나오기 전에는 해당 config를 완료로 보지 않는다.

## 기준

작업할 때는 아래 순서로 확인한다.

1. 공통 e2e 문서:
   - `framework/doc/framework/common/e2e/README.ko.md`
   - `framework/doc/framework/common/e2e/config-*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/e2e/<Config>/`
   - `framework/languages/dotnet/e2e/<Config>/feature-map.ko.md`
3. C++ framework public surface:
   - `framework/languages/cpp/framework/include/`
   - `framework/doc/framework/cpp/`
   - `framework/languages/cpp/tests/`
4. C++ e2e 대상:
   - `framework/languages/cpp/e2e/<Config>/`

공통 e2e 문서는 검증 기준이고, 새 public API 추가 근거가 아니다. `.NET`에 기능이 있어도 C++ spec 또는
공통 framework 계약에 근거가 없으면 바로 public API를 추가하지 않는다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 C++에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, C++에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 현재 C++ 작업물 처리 원칙

C++ e2e에는 이미 상당 부분 진행된 config가 있다. 이 작업물은 무조건 삭제하거나 무조건 이어 쓰는
기준이 아니라, `.NET` e2e inventory와 공통 e2e 문서에 맞춰 config별로 검토할 대상이다. 작업자는 먼저
현재 C++ 파일이 어떤 `.NET` 파일과 scenario에 대응하는지 표로 확인한 뒤, 유지할 파일과 새로 작성할
파일을 나눈다.

현재 상태:

- `framework/languages/cpp/e2e`의 기존 파일은 삭제 기준이 아니라 검토 입력이다.
- 기존 C++ e2e가 `.NET`의 폴더 구조, 파일 분류, scenario 분류와 맞으면 유지하고 필요한 부분만 보강한다.
- 기존 C++ e2e가 `.NET` 기준과 다르면 해당 파일을 그대로 옮기지 말고 `porting-inventory.ko.md`에
  불일치 사유와 목표 위치를 먼저 기록한다.
- 새 작업은 항상 `framework/languages/dotnet/e2e/<Config>`와 공통 e2e 문서에서 inventory를 만든 뒤
  시작한다.

판단:

- 기존 C++ e2e를 그대로 완료로 인정하면 누락 scenario나 stale 분류를 놓칠 수 있다.
- 반대로 잘 진행된 C++ 작업물을 삭제하고 처음부터 다시 작성하면 이미 해결된 C++ 런타임 연결, build/run
  script, scenario 구현을 잃을 수 있다.
- 따라서 C++은 config별로 **기존 작업물 보존을 기본값**으로 두고, `.NET` 기준 inventory에서 불일치가
  확인된 파일만 이동, 재작성, 삭제한다.
- config별 첫 산출물은 `porting-inventory.ko.md`다. 이 파일에서 기존 C++ 파일의 유지, 이동, 재작성,
  삭제 판단을 먼저 끝낸 뒤 코드 변경을 시작한다.

1차 분류:

| config | 현재 판단 | 작업 원칙 |
|--------|-----------|-----------|
| `RegistryMessaging` | 리팩토링 대상 | 구현 범위가 넓고 runner가 살아 있으므로 보존한다. `.NET`의 `Client/Scenarios`, `Server/<Role>`, `Shared` 분류에 맞춰 파일을 이동하고 inventory로 scenario 대응을 고정한다. |
| `SpotService` | 리팩토링 대상 | scenario 구현이 가장 많이 진행되어 있으므로 삭제하지 않는다. 현재 세부 scenario header와 server role 코드를 `.NET` 분류에 맞춰 정리하고, 남은 gap은 feature-map에 유지한다. |
| `PubSub` | 리팩토링 대상 | runner와 client/server/shared 구현이 있으므로 보존한다. 단일 server 파일은 `.NET`의 publisher, registry, subscriber 역할 분류에 맞춰 나눈다. |
| `RegistrationCodec` | 리팩토링 대상 | 구현된 codec/registration scenario를 보존한다. server 역할과 handler/filter/support 파일을 `.NET` 분류에 맞춰 나눈다. |
| `DiscoveryRegistryHa` | 삭제 후 DeliveryDispatch 포팅으로 대체 | 기존 C++ 구현은 `RegistryMessaging` 계약과 binary를 재사용한 별도 HA harness라 `.NET DeliveryDispatch` 포팅 기준과 맞지 않는다. 잘못된 source와 runner는 보존하지 않고 제거한 뒤, `.NET DeliveryDispatch`의 registry, dispatch API, dispatch center, courier, tracking, session, probe, client 역할을 기준으로 새 C++ 포팅을 작성한다. |
| `ResilienceLifecycle` | runner 보존, source 신규 분리 대상 | 현재 C++에는 `run_e2e.sh`와 `feature-map.ko.md`만 있고 source는 `RegistryMessaging` binary를 재사용한다. runner의 scenario orchestration은 보존하되, `.NET` 기준 `Client/Scenarios`, `Client/Support`, `Server/<Role>` source는 새로 분리한다. |
| `Monitoring` | RuntimeMonitoring 전환 대상 | `.NET` 기준 config 이름은 `RuntimeMonitoring`이다. 현재 C++ `Monitoring`은 PubSub runner를 감싼 보조 검증 성격이고 자체 source가 없다. `RuntimeMonitoring` inventory를 만든 뒤 유지할 evidence 검증만 옮기고, 전환이 끝나면 기존 `Monitoring` 디렉터리는 삭제한다. |
| `YieldDispatch` | Track A/B/C/D 진행 중 | C++ e2e config와 runner가 생겼고 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2는 반복 runner 통과 증거가 있다. 구현된 client scenario header는 YD-A/B/C/D2까지 완료했고, Registry host factory, Delay host factory/handler/support, Play host factory/support/control/basic/timer/actor handler, spot type, `YieldProbeSpot` runtime, Session host factory와 `yield_session_t` support header도 분리했다. B2와 C3 일부는 같은 stream session 증거가 아니라 partial로 남긴다. 남은 YD-D3/D4/E scenario와 server handler 세부 파일 분리가 있어 config 완료 판정은 보류한다. |

## 표준 C++ E2E 구조

```text
framework/languages/cpp/e2e/<Config>/
|-- Shared/
|   `-- <config>_contracts.hpp
|-- Server/
|   |-- <Role>/
|   |   |-- main.cpp
|   |   |-- CMakeLists.txt
|   |   |-- Configuration/
|   |   |-- Endpoints/
|   |   |-- Handlers/
|   |   |-- Infrastructure/
|   |   `-- Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- main.cpp
|   |-- Scenarios/
|   `-- Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- feature-map.ko.md
`-- run_e2e.sh
```

언어 특성상 project 파일 이름은 CMake 구조에 맞춰 조정할 수 있다. 하지만 role을 하나의 executable에서
`--role` 옵션으로 바꿔 실행하는 방식은 `.NET`의 role 분리와 다르므로 완료로 보지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared/` | client와 server가 함께 쓰는 payload, evidence, marker 타입 |
| `Client/main.cpp` | scenario 목록과 실행 순서 선언 |
| `Client/Scenarios/` | `.NET Client/Scenarios` 파일 하나에 대응하는 C++ scenario 파일 |
| `Client/Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/main.cpp` | role 실행 진입점 |
| `Server/<Role>/Configuration/` | role 실행 옵션과 포트, endpoint 설정 |
| `Server/<Role>/Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/Handlers/` | framework handler, observer, spot, actor handler |
| `Server/<Role>/Infrastructure/` | evidence store와 role 내부 상태 |
| `Server/<Role>/Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | build, 포트 할당, role process 시작과 종료, client 실행, 실패 로그 출력 |
| `feature-map.ko.md` | scenario ID별 구현 상태, gap, 검증 결과 |

`main.cpp`에 endpoint, handler, framework 설정을 모두 넣지 않는다. 역할별 설정과 handler는 성격별
폴더로 나눈다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. `.NET` role root에 option, endpoint, handler,
evidence 파일이 남아 있으면 C++에서는 책임별 폴더로 재분류한다.

- option, argument, endpoint 주소 설정: `Server/<Role>/Configuration/`
- HTTP endpoint mapping, evidence wait, shutdown endpoint: `Server/<Role>/Endpoints/`
- framework handler, dispatch filter, observer, spot, actor handler: `Server/<Role>/Handlers/`
- evidence store, runtime state, in-memory repository: `Server/<Role>/Infrastructure/`
- 해당 role 내부에서만 쓰는 relay, wait, runtime helper: `Server/<Role>/Support/`
- 여러 scenario가 함께 쓰는 client-side context나 helper: `Client/Support/`
- scenario ID 하나를 실행하는 파일: `Client/Scenarios/`

재분류한 파일은 `porting-inventory.ko.md` 비고에 원본 위치와 목표 위치를 함께 적는다.

## Scenario ID 판정 규칙

`Client/Scenarios/` 아래에 있다는 이유만으로 모두 scenario 파일로 보지 않는다. scenario 파일은 공통
e2e 문서의 scenario ID 하나를 직접 실행하고 marker를 검증하는 파일이다. context, shared record, fixture,
helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 C++에서는 `Client/Support/`나 `Shared/`로 옮긴다.

`.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
`porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 C++ 대응 scenario 파일을 명시한다.

## Inventory 매핑 산출물

각 config는 `.NET` 기준 파일 하나하나가 C++에서 어디로 옮겨졌는지 기록하는 매핑 문서를 반드시 둔다.

```text
framework/languages/cpp/e2e/<Config>/porting-inventory.ko.md
```

형식:

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/Scenarios/..._scenario.hpp` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/<config>_contracts.hpp` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/Support/...` | support | done/gap | 공통 helper 책임 |

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
- `.NET` 파일 하나가 여러 C++ 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- C++에서 해당 파일이 필요 없다고 판단해도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`로 두고
  근거를 비고에 적는다.
- `pending` 상태가 하나라도 있으면 config 완료로 보지 않는다.

## 진행 순서

아래 순서를 고정한다. 한 행이 Codex 에이전트 리뷰까지 이슈 없음으로 끝나기 전에는 다음 행으로
넘어가지 않는다.

| 순서 | Config | 기준 문서 | 완료 조건 |
|------|--------|-----------|-----------|
| 1 | `RegistryMessaging` | `config-1-registry-messaging.ko.md` | `.NET`의 RM-* scenario, registry/provider/workflow/consumer role 전부 대응 |
| 2 | `PubSub` | `config-3-pubsub.ko.md` | publisher/subscriber/registry role과 pubsub scenario 전부 대응 |
| 3 | `RegistrationCodec` | `config-4-registration-codec.ko.md` | registration, codec variant, invalid registration scenario 전부 대응 |
| 4 | `DeliveryDispatch` | `.NET DeliveryDispatch` sample | registry, dispatch API, dispatch center, courier A/B, tracking, session, probe, client role을 C++ sample/e2e로 포팅하고 registry discovery readiness와 delivery reassignment flow까지 검증 |
| 5 | `ResilienceLifecycle` | `config-5-resilience-lifecycle.ko.md` | restart, remap, drain, crash, outage, observer failure scenario 전부 대응 |
| 6 | `RuntimeMonitoring` | `config-7-monitoring.ko.md` | monitoring event, filter, dispatch failure, recovery scenario 전부 대응 |
| 7 | `SpotService` | `config-2-spot-service.ko.md` | spot, actor, session, route, timer, multi-node scenario 전부 대응 |
| 8 | `YieldDispatch` | `config-8-yield-dispatch.ko.md` | YD-A/B/C/D/E 전체 scenario 대응. 특히 YD-D1 local topology, YD-E3 runtime shutdown, YD-E4 금지 표면 정적 검증, YD-E5 언어별 의미 동등성까지 확인 |

## Config 단위 작업 절차

1. `.NET` inventory를 생성한다.
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
2. `.NET`의 `Client/Scenarios`, `Server/<Role>`, `Shared`, `Client/Support` 목록을
   `porting-inventory.ko.md`에 표로 정리한다.
3. 공통 e2e config 문서에서 scenario ID, 성공 marker, 실패 조건을 대조한다.
4. `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
5. C++ public API와 문서에서 같은 동작을 제공할 수 있는지 확인한다.
6. public contract gap이 있으면 구현하지 말고 `feature-map.ko.md`에 남긴다.
7. `.NET`과 같은 의미의 C++ 파일 트리를 만들되, stale `.NET` root 파일은 목표 폴더로 재분류한다.
8. Shared contract를 먼저 옮기고, 그 다음 server role, client scenario 순서로 구현한다.
9. scenario ID가 없는 helper/context 파일은 `Client/Scenarios/`가 아니라 `Client/Support/` 또는
   `Shared/`로 옮긴다.
10. `run_e2e.sh`가 실제 role process를 띄우고 readiness, cleanup, 실패 로그 출력을 처리하게 한다.
11. `porting-inventory.ko.md`의 모든 행에 C++ 대응 파일, 분류, 상태를 채운다.
12. 해당 config의 `run_e2e.sh`를 실제 실행한다.
13. 실패하면 같은 config 안에서 고치고 다시 실행한다. 이때 실패 원인을 모른 채 sleep, retry 횟수 증가,
    runner-only adapter, raw frame 조작으로 덮지 않는다. 원인을 좁혀 framework, stream connector,
    zlink http client, e2e 중 책임 위치를 수정하고 회귀 테스트를 추가한다.
14. 버그를 수정했다면 feature-map 또는 README에 원인, 수정 계층, 추가한 회귀 테스트를 함께 기록한다.
15. Codex 에이전트 리뷰를 요청한다.
16. 리뷰 이슈가 있으면 수정, 재실행, 재리뷰를 반복한다.
17. 리뷰가 이슈 없음이면 config 완료로 기록하고 다음 config로 이동한다.

## Codex 에이전트 리뷰 요청

각 config 자체 검증 뒤 아래 요청을 사용한다.

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/cpp/e2e/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 C++에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 C++에서 빠짐없이 대응되는가.
3. Shared contract, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 C++ 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 C++ feature-map에서 과장 없이 반영했는가.
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

- `.NET` inventory와 C++ 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `.NET Client/Scenarios` 아래 파일을 scenario ID 파일과 helper/context 파일로 구분했고, scenario ID 파일만
  C++ `Client/Scenarios/`에 대응된다.
- `.NET`의 모든 server role이 C++ `Server/<Role>/`에 대응된다.
- 공통 e2e 문서의 scenario ID가 `feature-map.ko.md`와 client scenario 파일에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/Scenarios/`에 두지 않았다.
- C++ e2e가 internal runtime detail을 호출자 코드로 밀어내지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- 빌드 산출물, 임시 로그, generated file은 커밋하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.
