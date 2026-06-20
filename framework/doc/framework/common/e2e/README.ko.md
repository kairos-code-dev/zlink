<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [공통 스펙](../README.ko.md) | [공통 샘플](../sample/README.ko.md)
<!-- framework-adapter-nav:end -->

# Framework Scenario E2E 테스트

이 문서는 ZLink Framework의 언어별 구현이 실제 사용 구조에서 같은 의미로 동작하는지
검증하기 위한 scenario E2E 테스트 묶음을 정의한다. 여기서 말하는 E2E 테스트는
샘플과 다르다. 샘플은 사용자가 따라 할 정상 흐름을 보여 주는 코드이고, E2E 테스트는
기능 조합과 실패 경로를 의도적으로 만든 검증 전용 코드다.

public contract 테스트는 개별 API 계약을 빠르게 고정한다. 하지만 실제 회귀는 channel,
Discovery, handler 등록, codec, Spot, actor, stream, logger가 함께 붙는 순간 자주
드러난다. 그래서 E2E 테스트는 가능한 한 실제 프로세스, 실제 connector, 실제 registry,
실제 logger를 사용한다.

## 1. 테스트 계층

| 계층 | 목적 | 실행 방식 |
|------|------|-----------|
| contract | public builder, handler interface, 오류 계약 같은 작은 단위를 빠르게 검증한다. | 단일 테스트 프로세스, fake runtime 허용 |
| integration | 하나의 기능 축을 실제 runtime에 붙여 검증한다. | 단일 프로세스 또는 제한된 multi-process |
| scenario E2E | 여러 기능 축이 함께 동작하는지 검증한다. | 언어별 표준 위치에 둔 별도 E2E 프로젝트의 실행 스크립트가 샘플처럼 서버와 클라이언트 프로세스를 띄우고, client scenario 파일을 실행한다 |
| sample smoke | 사용자가 읽을 수 있는 정상 샘플 흐름이 끝까지 동작하는지 확인한다. | `run_sample.*` 실행 |

scenario E2E는 sample smoke를 대체하지 않는다. sample smoke는 사용자 예제의 품질을
지키고, scenario E2E는 샘플에 넣기 어색한 실패 경로와 조합 회귀를 잡는다.
기존 unit, contract, integration 테스트에 scenario ID를 붙인 검증은 가까운 회귀 proof로
쓸 수 있지만, 이 문서에서 말하는 scenario E2E 완료로 보지 않는다.

## 1.1 표준 프로젝트 구조

각 언어는 아래 표의 표준 위치에 샘플과 분리된 E2E 프로젝트를 둔다. C++와 `.NET`처럼
`tests/` 아래에 둘 수 있고, Java와 Kotlin처럼 Gradle 하위 프로젝트로 둘 수도 있다.
프로젝트 이름과 빌드 단위는 언어 관례를 따르되, 역할은 같아야 한다.

| 언어 | 표준 위치 | 실행 단위 |
|------|-----------|-----------|
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/` | CMake target 또는 runner executable |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/` | 별도 `.csproj`와 test/runner command |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/` 또는 Gradle 하위 프로젝트 | Gradle task |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/` 또는 Gradle 하위 프로젝트 | Gradle task |
| Node.js | `framework/languages/node/test/scenario-e2e/` | Node test 또는 runner script |

각 프로젝트는 최소한 아래 구성을 가진다. `registry`는 Discovery나 scale-out처럼 scenario
file이 `registry.count > 0`을 선언할 때만 필수다.

- `registry` executable 또는 process role, 필요한 시나리오에서만 사용
- `server` executable 또는 process role
- `client` executable 또는 process role
- `scenarios/` 디렉토리
- `logs/`와 `evidence/` 출력 디렉토리
- 실행 결과를 요약하는 report 파일
- 서버와 클라이언트를 구동하는 실행 스크립트

실행은 샘플 smoke와 비슷한 방식이어야 한다. 테스트 프레임워크가 같은 프로세스 안에서
server host와 client host를 직접 생성해 검증하는 방식이 아니라, `run_*.sh`, `run_*.ps1`,
Gradle task, npm script 같은 실행 스크립트가 server/client 프로세스를 순서대로 띄운다.
scenario file이 `registry.count > 0`을 선언하면 registry 프로세스도 함께 띄운다. client는
`scenarios/*.json` 또는 언어별로 같은 의미를 갖는 시나리오 파일을 읽고 단계별로 요청을
보낸다. 서버는 테스트 전용 evidence endpoint나 evidence 파일을 남긴다. 스크립트는
시나리오가 선언한 프로세스를 띄우고 종료까지 관리한다.

client process의 시나리오 검증 코드는 테스트 전용 helper를 숨겨서 쓰지 않는다. 샘플처럼
읽히는 흐름으로 작성하고, 검증은 `ensure` 구문으로 직접 표현한다. client가 scenario를
검증할 때는 channel connector, stream connector, `zlink-http client`처럼 사용자가 실제로
호출하는 공개 client 표면만 사용한다. 서버 evidence나 관리 endpoint 조회도
`zlink-http client` 또는 공개 connector 경로로 수행한다. 언어별 public DI/container API로
공개 connector나 client instance를 얻는 것은 허용한다. 테스트를 쉽게 만들기 위해 framework
내부 helper, private API, reflection, server/test-only state에 직접 접근하는 service
provider 사용은 금지한다.

예시 구조:

```text
tests/Zlink.Framework.ScenarioE2E/
|-- scenarios/
|   |-- CH-001.request-response.json
|   |-- DSC-008.scaleout-scalein.json
|   `-- DERR-001.unregistered-request.json
|-- src/
|   |-- RegistryProcess.*
|   |-- ServerProcess.*
|   |-- ClientRunner.*
|   `-- ScenarioReport.*
|-- run_scenario_e2e.sh
|-- run_scenario_e2e.ps1
`-- README.ko.md
```

scenario file은 아래 정보를 표현해야 한다.

- scenario id
- registry count, server/client 개수
- 각 server의 channel, endpoint, routing id
- client가 보낼 request, send, publish 단계
- scale-out, scale-in, restart 같은 lifecycle 단계
- 기대 reply, public error, server evidence, log marker
- 실패 시 원인 레이어 분류에 필요한 evidence 수집 항목

## 1.2 우선순위

각 시나리오는 아래 우선순위를 가진다.

| 우선순위 | 의미 | 구현 기준 |
|----------|------|-----------|
| `P0` | 공통 framework 의미를 주장하려면 반드시 필요한 smoke보다 강한 검증 | 모든 언어에서 구현한다 |
| `P1` | 특정 기능을 지원한다고 문서화한 언어가 통과해야 하는 검증 | 지원 언어에서 구현한다 |
| `P2` | 운영 규모, 성능, rolling update처럼 비용이 큰 검증 | release gate에는 선택 적용하되, 미구현 이유를 남긴다 |

여기서 feature map은 언어별 guide의 기능 맵 문서를 뜻한다. `.NET`, Java, Kotlin,
Node.js는 `guide/10-feature-map.ko.md`, C++는 `guide/15-feature-map.ko.md`를 기준으로
한다. 새 언어가 기능 맵 문서를 아직 갖지 않았다면, 그 언어의 `README.ko.md`에 같은
역할의 표를 먼저 둔다.

## 2. 공통 실행 원칙

- 테스트는 독립된 임시 작업 디렉토리와 로그 디렉토리를 사용한다.
- 프로세스는 `server`, `client`를 분리해 띄운다. scenario file이 `registry.count > 0`이나
  `probe` 같은 추가 role을 선언하면 해당 role도 별도 프로세스로 띄운다.
- port, routing id, Redis key prefix, store path는 테스트 실행마다 격리한다.
- client는 framework public API, stream connector public API, `zlink-http client`만
  사용한다. scenario 검증용 helper로 framework 내부를 우회하지 않는다.
- client 검증은 샘플처럼 `ensure` 구문으로 직접 작성한다.
- 서버 준비는 sleep만으로 판단하지 않고 health endpoint 또는 readiness marker로 확인한다.
  scenario file이 `registry.count > 0`을 선언했다면 registry topology도 함께 확인한다.
- 성공 기준은 client 반환값, server evidence endpoint, 로그 marker를 조합해서 확인한다.
  scenario file이 `registry.count > 0`을 선언했다면 registry topology도 성공 기준에 포함한다.
- 실패 시에는 각 프로세스의 stdout/stderr, framework 로그, client 마지막 요청 정보를
  남긴다. scenario file이 `registry.count > 0`을 선언했다면 registry snapshot도 남긴다.
- 테스트가 실패하면 먼저 원인 레이어를 분리한다. C API 문제인지, 언어별 bindings 문제인지,
  framework 문제인지, 샘플 또는 테스트 하네스 문제인지 evidence로 판정한다.
- 버그를 고칠 때는 원인이 있는 레이어를 수정하고, 같은 레이어에 회귀 테스트를 추가한다.
  framework 테스트를 통과시키기 위해 C API나 bindings 버그를 framework에서 우회하지 않는다.
- core library 버그를 수정한 경우에는 bindings가 쓰는 native library를
  `bindings/dev_sync_local_core_libs.sh`로 다시 배포한 뒤 언어별 테스트를 재실행한다.
- 같은 시나리오는 언어별 public API 모양만 달라지고 의미와 marker는 같아야 한다.

## 3. 문서 목록

| 순서 | 문서 | 다루는 범위 |
|:----:|------|------------|
| 1 | [테스트 하네스와 evidence](01-harness-and-evidence.ko.md) | 프로세스 구성, 로그, readiness, evidence 수집, 실패 리포트 기준 |
| 2 | [Channel messaging](02-channel-messaging.ko.md) | client-server channel, route mesh, dealer mesh, round-robin, weighted routing, timeout |
| 3 | [Discovery와 scale-out](03-discovery-scaleout.ko.md) | registry 기반 자동 연결, provider 증감, topology 갱신, same routing id endpoint 교체, graceful drain |
| 4 | [Publish, subscribe, stream](04-pubsub-stream.ko.md) | fanout publish, subscriber 검증, stream session, reconnect, inbound observer |
| 5 | [Spot, actor, session](05-spot-actor-session.ko.md) | Entry Spot, user Spot, actor join, bound session push, timer, route resolver |
| 6 | [Handler 등록과 codec](06-handler-registration-codec.ko.md) | 자동 등록, annotation/attribute/decorator 등록, 수동 등록, JSON/Protobuf/MessagePack |
| 7 | [Dispatch 오류와 관측성](07-dispatch-error-observability.ko.md) | 미등록 packet, decode 실패, handler 예외, error reply, observer, 파일 로그 |
| 8 | [샘플 기반 업무 흐름](08-sample-derived-flows.ko.md) | Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall, GameQuest에서 가져온 검증 전용 흐름 |
| 9 | [복구와 lifecycle](09-resilience-lifecycle.ko.md) | 프로세스 재시작, Kubernetes식 failover, reconnect, cancellation, shutdown, resource cleanup |
| 10 | [언어별 E2E 구현 현황](10-language-coverage.ko.md) | 현재 checkout에서 확인한 구현 상태, 검증 명령, 남은 공통 E2E 작업 |

## 4. 시나리오 ID 규칙

시나리오 ID는 기능 축을 드러내는 접두사를 쓴다.

| 접두사 | 의미 |
|--------|------|
| `HAR` | harness, evidence, 로그 수집 |
| `CH` | channel messaging |
| `DSC` | Discovery, registry, scale-out |
| `PUB` | publish-subscribe |
| `STR` | stream connector, stream session |
| `SPOT` | Spot, actor, session actor dispatch |
| `REG` | handler 등록 |
| `CDC` | codec |
| `DERR` | dispatch error |
| `FLOW` | 샘플 기반 업무 흐름 |
| `RES` | resilience, lifecycle |

각 언어별 구현 문서는 이 ID를 그대로 참조해야 한다. 테스트 이름은 언어 관례에 맞게
바꿀 수 있지만, 리포트에는 이 ID가 드러나야 한다.

## 5. 완료 기준

새 언어 framework가 "공통 E2E 기준을 만족한다"고 말하려면 아래 조건을 채워야 한다.

- `P0` 시나리오는 모두 구현되어야 한다.
- `P1` 시나리오는 해당 기능을 지원한다고 문서화한 언어에서 구현되어야 한다.
- `P2` 시나리오는 선택 항목이다. 구현하지 않으면 해당 언어 feature map 또는
  `README.ko.md`에 이유를 남긴다.
- 지원하지 않는 기능은 테스트 skip이 아니라 feature map과 문서에 이유가 있어야 한다.
- 실패 경로는 client-visible 결과와 server-side 로그 또는 observer evidence를 함께 검증한다.
- 샘플 smoke와 scenario E2E가 같은 기능을 서로 다른 방식으로 중복 검증할 때, 둘 중 하나가
  깨지면 원인을 분석해 public contract, runtime, sample 중 어느 계층 문제인지 분리한다.
- 실패 원인 분류가 끝나지 않은 상태에서 workaround를 넣은 테스트는 완료로 보지 않는다.
  `core-capi`, `bindings`, `framework`, `sample`, `harness` 중 원인 레이어가 확인되어야
  하고, 수정한 레이어의 회귀 테스트가 함께 있어야 한다.
