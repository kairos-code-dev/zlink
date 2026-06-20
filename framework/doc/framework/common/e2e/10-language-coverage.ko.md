<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [공통 스펙](../README.ko.md) | [공통 샘플](../sample/README.ko.md)
<!-- framework-adapter-nav:end -->

# 언어별 E2E 구현 현황

이 문서는 현재 checkout에서 확인한 scenario E2E 구현 상태를 정리한다. 공통 E2E 기준을
모두 만족했다는 완료 선언이 아니다. 특히 기존 unit, contract, integration 테스트에
scenario ID가 붙어 있더라도, 언어별 표준 위치의 별도 E2E 프로젝트가 서버와 클라이언트
프로세스를 띄우고 client scenario file을 실행하는 구조가 아니면 정식 scenario E2E
완료로 보지 않는다. 이 문서는 기존 proof와 남은 작업을 분리해서 다음 작업자가 같은
시나리오 ID와 같은 evidence 기준으로 이어서 작업할 수 있게 한다.

## 상태 표기

| 상태 | 의미 |
|------|------|
| 완료 | 별도 scenario E2E 프로젝트가 scenario file을 읽고 서버와 클라이언트 프로세스를 구동해 해당 시나리오를 검증한다. scenario file이 `registry.count > 0` 같은 추가 role을 선언할 때만 해당 추가 프로세스도 구동한다. |
| 부분 | 가까운 계약 테스트나 runtime proof는 있지만 공통 시나리오가 요구하는 별도 E2E 프로젝트 구조는 아직 아니다. |
| 미구현 | 해당 시나리오를 직접 검증하는 E2E 테스트가 아직 없다. |
| 계약 필요 | public API나 테스트 관측 표면이 부족해서 먼저 계약을 정해야 한다. |

## 정식 E2E 프로젝트 기준

현재 checkout에서 아래 구조가 갖춰져야 이 문서의 상태를 `완료`로 올릴 수 있다.

- 언어별 표준 위치에 샘플과 분리된 별도 scenario E2E 프로젝트가 있다. C++와 `.NET`은
  `tests/` 아래 project를 사용하고, Java와 Kotlin은 Gradle 하위 프로젝트를 사용할 수 있다.
- runner가 server/client를 별도 프로세스로 실행한다.
- scenario file이 `registry.count > 0` 같은 추가 role을 선언하면 runner가 그 추가 role도 별도
  프로세스로 실행한다. `registry.count = 0`이면 registry 프로세스를 시작하지 않는다.
- client process가 `scenarios/` 아래 scenario file을 읽고 단계별로 요청을 보낸다.
- 샘플처럼 직접 실행 가능한 script 또는 task가 server/client process를 구동한다.
- client 검증은 helper 없이 public connector와 `zlink-http client`만 사용하고, 샘플처럼
  `ensure` 구문으로 작성한다.
- server process는 테스트 전용 evidence endpoint 또는 evidence file을 남긴다.
- report에는 scenario id, language, process count, log path, evidence path, executed script path,
  per-role process id map, pass/fail, 실패 원인 레이어 분류가 남는다.

기존 `Zlink.Framework.E2ETests`, Gradle integration test, Node contract test, C++ unit test는
중요한 회귀 proof지만, 위 구조를 만족하기 전까지는 정식 scenario E2E 완료로 보지 않는다.

## 현재 적용 범위

| 언어 | 현재 상태 | 확인된 evidence | 남은 작업 |
|------|-----------|-----------------|-----------|
| C++ | 부분 | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/`가 추가되었고, `run_scenario_e2e.sh`가 server process와 client process를 띄운 뒤 client가 scenario file을 읽어 `CH-001` request-response, `CH-002` manual endpoint round-robin, `CH-004` route mesh targeted request, `CH-006` send one-way, `CH-007` request timeout과 late reply 복구, `DERR-001` 미등록 request error reply, `DERR-002` 미등록 send drop reporting, `DERR-007` handler exception reporting을 검증한다. `CH-002`는 server process 3개와 client process 1개를 띄우고, client가 endpoint 3개를 직접 등록해 90개 검증 요청이 30/30/30으로 분산되는지 `ensure` 구문으로 확인한다. `CH-004`는 route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 route evidence를 남기는지, 잘못된 routing id가 public error로 끝나는지 검증한다. `DERR-001`, `DERR-002`, `DERR-007`은 client가 C++ framework public channel client로 request 또는 send를 보내고, public error 또는 submit 결과와 server dispatch observer evidence, stderr 기본 로그를 `zlink::http_client`와 `ensure` 구문으로 확인한다. client 검증은 C++ framework public channel client, public route client와 `zlink::http_client` evidence 조회만 사용한다. 기존 `test_cpp_framework_channel_messaging`와 `test_cpp_framework_pubsub_e2e`도 여러 scenario ID의 회귀 proof를 제공하지만, unit/integration test process 안에서 runtime을 직접 구성하므로 정식 scenario E2E 완료 근거로 보지 않는다. | 아직 같은 project/script/scenario-file 구조로 옮겨야 할 P0 시나리오가 남아 있다. `registry.count > 0` 시나리오, multi-server lifecycle, 남은 dispatch error P0, discovery scale-out/in, same rid failover, resilience 시나리오가 아직 남아 있다. |
| Java | 부분 | `framework/languages/java/zlink-framework-scenario-e2e/`가 추가되었고, `run_scenario_e2e.sh`가 server process와 client process를 띄운 뒤 client가 scenario file을 읽어 `CH-001` request-response, `CH-002` manual endpoint round-robin, `CH-004` route mesh targeted request, `CH-006` send one-way, `CH-007` request timeout과 late reply 복구를 검증한다. `CH-002`는 server process 3개와 client process 1개를 띄우고, client가 endpoint 3개를 직접 등록해 90개 검증 요청이 30/30/30으로 분산되는지 `ensure` 구문으로 확인한다. `CH-004`는 route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 handler evidence를 남기는지, 잘못된 routing id가 public error로 끝나는지 검증한다. `CH-006`은 client가 one-way send를 보내고 server send handler의 command evidence를 `ZLinkHttpClient`로 조회한다. `CH-007`은 client가 짧은 timeout으로 request를 보낸 뒤 public `TimeoutException`을 받고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 reply를 받는지 확인한다. server evidence의 `completedRequestIds`는 timeout된 slow handler가 client timeout 이후 완료된 사실을 공개 HTTP evidence로 보여준다. client 검증은 Spring Boot starter가 공개한 `ZLinkClient`, `ZLinkRouteClient`, `ZLinkHttpClient` evidence 조회만 사용한다. 기존 `ChannelMessagingTest`와 `DiscoveryScaleoutTest`도 여러 scenario ID의 회귀 proof를 제공하지만, Gradle integration test 안에서 runtime을 직접 구성하므로 정식 scenario E2E 완료 근거로 보지 않는다. | `CH-001`, `CH-002`, `CH-004`, `CH-006`, `CH-007` 외 P0 시나리오를 같은 project/script/scenario-file 구조로 추가해야 한다. `registry.count > 0` 시나리오, multi-server lifecycle, dispatch error, discovery scale-out/in, same rid failover, resilience 시나리오가 아직 남아 있다. |
| Kotlin | 부분 | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/`가 추가되었고, `run_scenario_e2e.sh`가 server process와 client process를 띄운 뒤 client가 scenario file을 읽어 `CH-001` request-response, `CH-002` manual endpoint round-robin, `CH-004` route mesh targeted request, `CH-006` send one-way, `CH-007` request timeout과 late reply 복구를 검증한다. `CH-002`는 server process 3개와 client process 1개를 띄우고, client가 endpoint 3개를 직접 등록해 90개 검증 요청이 30/30/30으로 분산되는지 `ensure` 구문으로 확인한다. `CH-004`는 route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 handler evidence를 남기는지, 잘못된 routing id가 public error로 끝나는지 검증한다. `CH-006`은 client가 one-way send를 보내고 server suspend send handler의 command evidence를 `ZLinkHttpClient`로 조회한다. `CH-007`은 client가 짧은 timeout으로 request를 보낸 뒤 public `TimeoutException`을 받고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 reply를 받는지 확인한다. server evidence의 `completedRequestIds`는 timeout된 slow suspend handler가 client timeout 이후 완료된 사실을 공개 HTTP evidence로 보여준다. client 검증은 Kotlin coroutine public channel client, public route client와 `ZLinkHttpClient` evidence 조회만 사용한다. 기존 `KotlinFrameworkE2ETest`도 여러 scenario ID의 회귀 proof를 제공하지만, Gradle test process 안에서 runtime을 직접 구성하므로 정식 scenario E2E 완료 근거로 보지 않는다. | `CH-001`, `CH-002`, `CH-004`, `CH-006`, `CH-007` 외 P0 시나리오를 같은 project/script/scenario-file 구조로 추가해야 한다. `registry.count > 0` 시나리오, multi-server lifecycle, dispatch error, discovery scale-out/in, same rid failover, resilience 시나리오가 아직 남아 있다. |
| Node.js | 부분 | `framework/languages/node/test/scenario-e2e/`가 추가되었고, `run_scenario_e2e.sh`가 server process와 client process를 띄운 뒤 client가 scenario file을 읽어 `CH-001` request-response, `CH-002` manual endpoint round-robin, `CH-004` route mesh targeted request, `CH-006` send one-way, `CH-007` request timeout과 late reply 복구, `DERR-001` 미등록 request error reply, `DERR-002` 미등록 send drop reporting, `DERR-007` handler exception reporting을 검증한다. `CH-002`는 server process 3개와 client process 1개를 띄우고, client가 endpoint 3개를 직접 등록해 90개 검증 요청이 30/30/30으로 분산되는지 `ensure` 구문으로 확인한다. `CH-004`는 route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 handler evidence를 남기는지, 잘못된 routing id가 public error로 끝나는지 검증한다. `DERR-001`, `DERR-002`, `DERR-007`은 client가 NestJS public channel client로 request 또는 send를 보내고, public error 또는 submit 결과와 server dispatch observer evidence, server log marker를 `ZLinkHttpClient`와 `ensure` 구문으로 확인한다. server evidence의 `completedRequestIds`는 timeout된 slow handler가 client timeout 이후 완료된 사실을 공개 HTTP evidence로 보여준다. 이 시나리오를 추가하는 과정에서 Node framework channel dispatcher가 late reply submit 실패를 handler exception처럼 처리해 server process를 종료하던 버그를 확인했고, reply submit 실패를 dispatch error로 보고 drop하도록 framework 레이어를 수정했다. client 검증은 NestJS public channel client, public route client, `ZLinkHttpClient` evidence 조회만 사용한다. 기존 `channel-client.test.js`도 여러 scenario ID의 회귀 proof를 제공하지만, Node test process 안의 runtime harness 중심이므로 정식 scenario E2E 완료 근거로 보지 않는다. | 아직 같은 project/script/scenario-file 구조로 옮겨야 할 P0 시나리오가 남아 있다. `registry.count > 0` 시나리오, multi-server lifecycle, 남은 dispatch error P0, discovery scale-out/in, same rid failover, resilience 시나리오가 아직 남아 있다. |
| .NET | 부분 | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/`가 추가되었고, `run_scenario_e2e.sh`가 server process와 client process를 띄운 뒤 client가 scenario file을 읽어 `CH-001` request-response, `CH-002` manual endpoint round-robin, `CH-004` route mesh targeted request, `CH-006` send one-way, `CH-007` request timeout과 late reply 복구를 검증한다. `CH-002`는 server process 3개와 client process 1개를 띄우고, client가 endpoint 3개를 직접 등록해 90개 검증 요청이 30/30/30으로 분산되는지 `ensure` 구문으로 확인한다. `CH-004`는 route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 handler evidence를 남기는지, 잘못된 routing id가 public error로 끝나는지 검증한다. `CH-006`은 client가 one-way send를 보내고 server send handler의 command evidence를 `ZLinkHttpClient`로 조회한다. `CH-007`은 client가 짧은 timeout으로 request를 보낸 뒤 public `TimeoutException`을 받고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 reply를 받는지 확인한다. server evidence의 `completedRequestIds`는 timeout된 slow handler가 client timeout 이후 완료된 사실을 공개 HTTP evidence로 보여준다. client 검증은 framework public channel client, framework public route client와 `ZLinkHttpClient` evidence 조회만 사용한다. 기존 `Zlink.Framework.E2ETests`도 여러 scenario ID의 회귀 proof를 제공하지만, xUnit process 안에서 host를 직접 구성하므로 정식 scenario E2E 완료 근거로 보지 않는다. | `CH-001`, `CH-002`, `CH-004`, `CH-006`, `CH-007` 외 P0 시나리오를 같은 project/script/scenario-file 구조로 추가해야 한다. `registry.count > 0` 시나리오, multi-server lifecycle, dispatch error, discovery scale-out/in, same rid failover, resilience 시나리오가 아직 남아 있다. |

## P0 시나리오 추적

아래 표는 공통 기준에서 반드시 채워야 하는 P0 축을 요약한다. 특정 언어가 기능을 지원하지
않는다면 테스트 skip으로 숨기지 말고 feature map에 미지원 이유를 남긴다.

| 축 | P0 시나리오 | 현재 상태 |
|----|-------------|-----------|
| Harness | `HAR-001`, `HAR-002`, `HAR-003`, `HAR-004`, `HAR-005`, `HAR-007` | 문서 기준은 정의됨. 언어별 공통 runner 적용은 미구현. |
| Channel | `CH-001`, `CH-002`, `CH-004`, `CH-006`, `CH-007` | C++, `.NET`, Java, Kotlin, Node.js는 정식 scenario E2E project/script/client-scenario-file 구조로 `CH-001` 초기 proof가 있다. C++, `.NET`, Java, Kotlin, Node.js는 같은 구조로 `CH-002` manual endpoint round-robin proof도 추가했다. C++, `.NET`, Java, Kotlin, Node.js는 같은 구조로 `CH-004` route mesh targeted request proof를 추가했다. C++, `.NET`, Java, Kotlin, Node.js는 같은 구조로 `CH-006` send one-way proof도 추가했다. C++는 framework public `channel_client_t`에 `send_to_channel`/`send` surface를 추가해 scenario client가 public channel client만으로 one-way send를 검증한다. C++, `.NET`, Java, Kotlin, Node.js는 같은 구조로 `CH-007` request timeout과 late reply 복구 proof도 추가했다. 기존 언어별 unit/integration proof도 남아 있지만, 별도 process runner 구조로 옮기기 전에는 정식 완료가 아니다. |
| Discovery | `DSC-001`, `DSC-002`, `DSC-003`, `DSC-008`, `DSC-009` | `.NET`, Java, Kotlin, Node.js는 `DSC-008`, `DSC-009` proof가 있다. C++는 일반 client/server channel Discovery runtime attachment가 부족해서 framework 레이어 구현이 먼저 필요하다. |
| Publish/Stream | `PUB-001`, `PUB-002`, `STR-001`, `STR-002` | `PUB-001`은 .NET, Java, Kotlin, Node.js에 3-subscriber sequence evidence proof가 있다. C++는 public publisher call surface와 native subscriber 3개를 섞은 framework runtime harness proof이며, framework subscriber topic handler proof는 아직 남아 있다. `STR-001`은 .NET header stream session partial proof가 있지만 auth 절차까지 포함한 전체 시나리오는 아직 아니다. `PUB-002`, `STR-002`는 아직 미구현이다. |
| Spot/Actor/Session | `SPOT-001`, `SPOT-002`, `SPOT-003`, `SPOT-004`, `SPOT-006` | 미구현. 기존 샘플과 단위 테스트를 scenario E2E로 분리해야 한다. |
| Registration/Codec | `REG-001`, `REG-002`, `REG-003`, `CDC-001` | Java에 `REG-001` package scan과 `REG-002` annotation dispatch partial proof가 있다. `REG-003`은 .NET, Java, Node.js에 수동 request/send/publish handler 등록과 미등록 request/send/publish dispatch error proof가 있다. `CDC-001`의 JSON nested object, array, nullable field round-trip 변형은 .NET, Java, Kotlin, Node.js에 proof가 있고 decode 실패는 `DERR-006`이 덮는다. unknown-field 정책 변형은 아직 미구현이다. |
| Dispatch error | `DERR-001`, `DERR-002`, `DERR-004`, `DERR-006`, `DERR-007`, `DERR-009` | `DERR-001`, `DERR-002`, `DERR-007`은 C++와 Node.js에 정식 scenario E2E proof가 있고, .NET에 public-path proof, Kotlin에 coroutine public API proof가 있으며 Java에는 framework runtime harness proof가 있다. `DERR-004`는 .NET에 registry-backed Spot route public-path proof가 있고, C++/Java/Kotlin/Node.js는 공통 E2E가 더 필요하다. `DERR-006`은 .NET에 real host와 raw malformed packet proof, Kotlin에 coroutine public recovery proof가 있고 C++와 Node.js에는 dispatcher harness proof, Java에는 framework runtime harness proof가 있다. `DERR-009`는 모든 대상 언어에 proof가 있지만 C++와 Node.js는 dispatcher harness proof, Java는 reflection runtime harness proof, Kotlin과 .NET은 public lifecycle에 raw malformed packet을 섞은 proof다. |
| Sample flows | `FLOW-001`, `FLOW-003`, `FLOW-005` | 미구현. 샘플 smoke와 별도 E2E로 분리해야 한다. |
| Resilience | `RES-001`, `RES-001A`, `RES-004`, `RES-004A` | `DSC-009`가 same routing id failover 일부를 덮지만, lifecycle P0 전체는 미구현. |

## 실패 원인 분류 적용

테스트가 실패하면 먼저 원인 레이어를 분리한다. 같은 증상이 보여도 원인이 `core-capi`,
`bindings`, `framework`, `sample`, `harness` 중 어디인지 evidence로 남겨야 한다.

- C API 계약 또는 core runtime 버그면 core 쪽을 고치고 core 레이어 회귀 테스트를 추가한다.
- core library를 수정했다면 `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`를
  실행해 bindings가 쓰는 native library를 다시 배포한 뒤 언어별 E2E를 재실행한다.
- bindings wrapper 버그면 해당 언어 bindings에 회귀 테스트를 추가한다.
- framework 버그면 해당 언어 framework 테스트에 회귀 테스트를 추가한다.
- 샘플이나 하네스 버그면 제품 코드 우회 없이 샘플 또는 하네스만 수정한다.

## 이번 변경에서 실행한 검증

아래 검증 표는 dirty checkout 전체의 release 검증 기록이 아니다. 이 문서가 추적하는
framework E2E 변경 범위, 즉 `framework/doc/framework/common/e2e/`와 각 언어 framework
테스트/runtime 변경에 대한 검증 기록이다. 이 범위에서는 core C API나 core runtime을
수정하지 않았으므로, 이 표의 검증으로는 core 회귀 테스트와
`/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`가 실행 대상이
아니었다. 같은 checkout에 별도 core 또는 bindings 변경이 남아 있으면 이 문장이 그 변경의
검증을 면제하지 않는다. core 또는 bindings 변경을 커밋하거나 배포하려면 해당 레이어의
원인 분류, 회귀 테스트, native library 재배포 결과를 별도로 남겨야 한다.
`DERR-007` 작업 중 확인한 Java dispatch reporter의 log level/exception 보존 문제와 Node.js
dispatch reporter의 marker 보존 문제는 모두 framework 레이어 버그로 분류했다. `DERR-006`
작업 중 확인한 C++ error reply code mapping 누락, Java payload decode reason mapping 누락,
Node.js payload decode exception wrapping 누락도 framework 레이어 버그로 분류했다. C++
focused 테스트의 첫 실패와 Java/Kotlin DERR-006의 JSON codec 누락은 harness 문제로 분류하고
테스트만 수정했다. `DERR-009`는 기존 observer/logger 표면을 파일 sink에 연결한 테스트
harness 확장이다. `DERR-004` 작업 중 확인한 .NET Spot route handler missing 이벤트의
spot rid 누락은 framework 레이어 버그로 분류하고 .NET framework runtime과 E2E 회귀
테스트를 수정했다. `PUB-001` 작업 중 확인한 C++ `message_bus_t::publish()`가 event payload를
native publish 경로로 전달하지 않던 문제도 C++ framework 레이어 버그로 분류하고
`test_cpp_framework_pubsub_e2e`에 회귀 테스트를 추가했다. 앞으로 E2E 중 core-capi 원인이 확인되어 core library를 수정하면, core
회귀 테스트와 native library 재배포 결과를 아래 표에 함께 남겨야 한다.
`HAR-*` report marker 정리 중에는 C++, Java, Kotlin, Node.js, .NET scenario runner가 모두
실제 evidence endpoint URL 목록, 실행 script path, role별 process id map을 report에 남기고
runner가 해당 필드를 검증하도록 맞췄다. Java/Kotlin의 기존 `scriptPath` 필드는
`executedScriptPath`로 이름을 맞췄고, `CH-002`의 evidence path도 `servers:/evidence` 같은
scenario artifact 값이 아니라 실제 HTTP evidence endpoint 목록을 기록하도록 수정했다.

| 언어 | 명령 | 결과 |
|------|------|------|
| C++ | `cmake --build framework/languages/cpp/build --target test_cpp_framework_contract_headers test_cpp_framework_channel_messaging -j2` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(contract_headers)$' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_(channel_messaging)$' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'CH-001' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'CH-006' --output-on-failure` | 통과. C++ framework `message_bus_t::send()`가 payload를 native send 경로로 전달하지 않던 framework 레이어 버그의 회귀 테스트를 포함한다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-002' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-006' --output-on-failure` | 통과. C++ framework error reply가 payload decode 실패를 `request_failed`로 내보내던 framework 레이어 버그의 회귀 테스트를 포함한다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-007' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-009' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-00[1267]' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-00[12679]' --output-on-failure` | 통과 |
| C++ | `cmake -S framework/languages/cpp -B framework/languages/cpp/build` | 통과 |
| C++ | `cmake --build framework/languages/cpp/build --target test_cpp_framework_scenario_e2e -j2` | 통과 |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh` | 통과. `CH-001` request-response를 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-002.manual-endpoint-round-robin.json` | 통과. `CH-002` manual endpoint round-robin을 server process 3개와 client process 1개로 실행하고, warm-up 3회 뒤 검증 요청 90개가 30/30/30으로 분산되는지 public channel client와 `zlink::http_client` evidence 조회로 확인했다. 첫 실행에서 C++ framework가 manual endpoint 목록을 가진 client request마다 새 DEALER socket을 만들면서 첫 endpoint만 사용하던 framework 레이어 버그를 확인했다. 수정 후 channel client bundle이 manual endpoint 선택 상태를 보존하고, request 경로는 선택 endpoint부터 시작해 실패 시 남은 endpoint를 같은 호출 안에서 순회한다. 새 DEALER socket에는 `immediate`, `connect_timeout`, `request_timeout`을 설정해 연결되지 않은 endpoint로 요청이 오래 묶이지 않게 했다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-004.route-mesh-targeted-request.json` | 통과. `CH-004` route mesh targeted request를 peer 3개 구조로 실행했다. client 역할 peer가 C++ framework public route client로 target routing id `peer-b`에 request를 보내고, `peer-b`만 route request evidence와 source routing id를 남기며 `peer-c`에는 같은 request evidence가 없는지 `zlink::http_client`로 검증했다. 잘못된 routing id request가 `route_not_connected` 또는 `timeout` public error로 끝나는지도 확인했다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-006.send-one-way.json` | 통과. `CH-006` send one-way를 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. one-way send는 dispatch 완료와 evidence 기록 시점이 다를 수 있으므로 `assertEvidence` 단계에서 HTTP evidence를 짧게 polling한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-007.request-timeout-late-reply.json` | 통과. `CH-007` request timeout과 late reply 복구를 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. 첫 실행에서 native no-data timeout이 `request_failed`로 보고되고, timeout 실패가 전체 channel pending state를 비우며, 늦은 reply submit 예외가 server process를 종료시키는 C++ framework 레이어 버그를 확인했다. 수정 후 native timed-out/no-data request 예외는 framework `timeout`으로 매핑되고, 실패한 request reservation만 취소하며, late reply submit 실패는 server loop를 종료시키지 않고 stderr에 `zlink framework channel late reply ignored` marker를 남긴다. CH-007 scenario는 같은 client에서 timeout request와 정상 request를 겹쳐 실행해 pending isolation을 확인하고, late reply 지연 이후 추가 request와 log marker까지 확인한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/DERR-001.unregistered-request.json` | 통과. `DERR-001` 미등록 request를 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. client는 public `handler_not_found` error reply를 받고, server dispatch observer evidence와 stderr 기본 로그가 `handler_missing`과 `reply_error`를 남기는지 확인한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/DERR-002.unregistered-send.json` | 통과. `DERR-002` 미등록 send를 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. client submit은 완료되고, server dispatch observer evidence와 stderr 기본 로그가 `handler_missing`과 `drop`을 남기는지 확인한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/scenarios/DERR-007.handler-exception.json` | 통과. `DERR-007` handler exception을 별도 server/client process, client scenario file, C++ framework public channel client, `zlink::http_client` evidence 조회, `ensure` 검증 구조로 실행했다. client는 public `request_failed` error reply와 보존된 error message를 받고, server dispatch observer evidence와 stderr 기본 로그가 `handler_exception`과 `reply_error`를 남기는지 확인한다. |
| C++ | `cmake --build framework/languages/cpp/build --target test_cpp_framework_scenario_e2e test_cpp_framework_channel_messaging -j2` | 통과 |
| C++ | `framework/languages/cpp/build/test_cpp_framework_channel_messaging` | 통과. manual endpoint 선택 상태 수정 뒤 기존 channel messaging 회귀를 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'CH-007' --output-on-failure` | 통과. CTest label이 `test_cpp_framework_scenario_e2e_ch007`를 실행하는지 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'CH-002' --output-on-failure` | 통과. CTest label이 `test_cpp_framework_scenario_e2e_ch002`를 실행하는지 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'CH-004' --output-on-failure` | 통과. CTest label이 `test_cpp_framework_scenario_e2e_ch004`를 실행하는지 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'scenario-e2e' -j2 --output-on-failure` | 통과. `CH-001`, `CH-002`, `CH-004`, `CH-006`, `CH-007`, `DERR-001`, `DERR-002`, `DERR-007` CTest가 같은 binary build와 scenario work directory를 동시에 건드리지 않도록 CTest `RESOURCE_LOCK`과 scenario ID별 work directory를 적용한 뒤 병렬 실행에서 통과하는지 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-001' --output-on-failure` | 통과 |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-00[12]' --output-on-failure` | 통과. C++ scenario E2E의 `DERR-001`, `DERR-002`와 기존 channel messaging regression label이 함께 실행되는지 확인했다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'DERR-007' --output-on-failure` | 통과. C++ scenario E2E의 `DERR-007`과 기존 channel messaging regression label이 함께 실행되는지 확인했다. |
| C++ | `cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging -j2` | 통과 |
| C++ | `cmake --build framework/languages/cpp/build --target test_cpp_framework_pubsub_e2e -j2` | 통과 |
| C++ | `./framework/languages/cpp/build/test_cpp_framework_pubsub_e2e` | 통과. `PUB-001` framework publisher fanout envelope가 native subscriber 3개에 같은 sequence로 전달되는 회귀 테스트를 포함한다. |
| C++ | `ctest --test-dir framework/languages/cpp/build -L 'PUB-001' --output-on-failure` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~DiscoveryScaleoutTests"` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.DiscoveryScaleoutTest'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-scenario-e2e:installDist :zlink-framework-kotlin-scenario-e2e:installDist` | 통과 |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/run_scenario_e2e.sh` | 통과. `CH-001` request-response를 별도 server/client process, client scenario file, Java framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-scenario-e2e/scenarios/CH-002.manual-endpoint-round-robin.json` | 통과. `CH-002` manual endpoint round-robin을 server process 3개와 client process 1개로 실행하고, warm-up 3회 뒤 검증 요청 90개가 30/30/30으로 분산되는지 public channel client와 `ZLinkHttpClient` evidence 조회로 확인했다. |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-scenario-e2e/scenarios/CH-004.route-mesh-targeted-request.json` | 통과. `CH-004` route mesh targeted request를 peer 3개 구조로 실행했다. client 역할 peer가 Java framework public route client로 target routing id `peer-b`에 request를 보내고, `peer-b`만 route request evidence와 source routing id를 남기며 `peer-c`에는 같은 request evidence가 없는지 `ZLinkHttpClient`로 검증했다. 잘못된 routing id request가 caller-visible public error로 끝나는지도 확인했다. |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-scenario-e2e/scenarios/CH-006.send-one-way.json` | 통과. `CH-006` send one-way를 별도 server/client process, client scenario file, Java framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. one-way send는 dispatch 완료와 evidence 기록 시점이 다를 수 있으므로 `assertEvidence` 단계에서 HTTP evidence를 짧게 polling한다. |
| Java | `framework/languages/java/zlink-framework-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-scenario-e2e/scenarios/CH-007.request-timeout-late-reply.json` | 통과. `CH-007` request timeout과 late reply 복구를 별도 server/client process, client scenario file, Java framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. scenario client는 timeout request가 public `TimeoutException`으로 끝나는지 확인하고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 처리되는지 검증한다. `completedRequestIds` evidence로 timeout된 slow handler가 client timeout 이후 완료된 사실도 확인한다. |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin-scenario-e2e:installDist` | 통과 |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh` | 통과. `CH-001` request-response를 별도 server/client process, client scenario file, Kotlin coroutine public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-kotlin-scenario-e2e/scenarios/CH-002.manual-endpoint-round-robin.json` | 통과. `CH-002` manual endpoint round-robin을 server process 3개와 client process 1개로 실행하고, warm-up 3회 뒤 검증 요청 90개가 30/30/30으로 분산되는지 public channel client와 `ZLinkHttpClient` evidence 조회로 확인했다. |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-kotlin-scenario-e2e/scenarios/CH-004.route-mesh-targeted-request.json` | 통과. `CH-004` route mesh targeted request를 peer 3개 구조로 실행했다. client 역할 peer가 Kotlin coroutine public route client로 target routing id `peer-b`에 request를 보내고, `peer-b`만 route request evidence와 source routing id를 남기며 `peer-c`에는 같은 request evidence가 없는지 `ZLinkHttpClient`로 검증했다. 잘못된 routing id request가 caller-visible public error로 끝나는지도 확인했다. |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-kotlin-scenario-e2e/scenarios/CH-006.send-one-way.json` | 통과. `CH-006` send one-way를 별도 server/client process, client scenario file, Kotlin coroutine public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. one-way send는 dispatch 완료와 evidence 기록 시점이 다를 수 있으므로 `assertEvidence` 단계에서 HTTP evidence를 짧게 polling한다. |
| Kotlin | `framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh framework/languages/java/zlink-framework-kotlin-scenario-e2e/scenarios/CH-007.request-timeout-late-reply.json` | 통과. `CH-007` request timeout과 late reply 복구를 별도 server/client process, client scenario file, Kotlin coroutine public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. scenario client는 timeout request가 public `TimeoutException`으로 끝나는지 확인하고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 처리되는지 검증한다. `completedRequestIds` evidence로 timeout된 slow suspend handler가 client timeout 이후 완료된 사실도 확인한다. |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests 'systems.zlink.framework.kotlin.KotlinFrameworkE2ETest'` | 통과. `CH-001`, `CH-006`, `DERR-001`, `DSC-008`, `DSC-009`를 포함한다. |
| Node.js | `npm --prefix framework/languages/node run build` | 통과 |
| Node.js | `npm --prefix framework/languages/node run scenario:e2e` | 통과. `CH-001` request-response를 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/CH-002.manual-endpoint-round-robin.json` | 통과. `CH-002` manual endpoint round-robin을 server process 3개와 client process 1개로 실행하고, warm-up 3회 뒤 검증 요청 90개가 30/30/30으로 분산되는지 public channel client와 `ZLinkHttpClient` evidence 조회로 확인했다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/CH-004.route-mesh-targeted-request.json` | 통과. `CH-004` route mesh targeted request를 peer 3개 구조로 실행했다. client 역할 peer가 NestJS public route client로 target routing id `peer-b`에 request를 보내고, `peer-b`만 route request evidence와 source routing id를 남기며 `peer-c`에는 같은 request evidence가 없는지 `ZLinkHttpClient`로 검증했다. 잘못된 routing id request가 caller-visible public error로 끝나는지도 확인했다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/CH-006.send-one-way.json` | 통과. `CH-006` send one-way를 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. one-way send는 dispatch 완료와 evidence 기록 시점이 다를 수 있으므로 `assertEvidence` 단계에서 HTTP evidence를 짧게 polling한다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/CH-007.request-timeout-late-reply.json` | 통과. `CH-007` request timeout과 late reply 복구를 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. scenario client는 timeout request가 public timeout error로 끝나는지 확인하고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 처리되는지 검증한다. `completedRequestIds` evidence로 timeout된 slow handler가 client timeout 이후 완료된 사실도 확인한다. 첫 실행에서 Node framework channel dispatcher가 late reply submit 실패를 처리하지 못해 server process가 종료됐고, framework 레이어 수정 뒤 같은 시나리오가 회귀 proof가 되었다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/DERR-001.unregistered-request.json` | 통과. `DERR-001` 미등록 request를 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. client는 public handler error를 받고, server dispatch observer evidence와 server log marker가 `handlerMissing`과 `replyError`를 남기는지 확인한다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/DERR-002.unregistered-send.json` | 통과. `DERR-002` 미등록 send를 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. client submit은 완료되고, server dispatch observer evidence와 server log marker가 `handlerMissing`과 `drop`을 남기는지 확인한다. |
| Node.js | `framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh framework/languages/node/test/scenario-e2e/scenarios/DERR-007.handler-exception.json` | 통과. `DERR-007` handler exception을 별도 server/client process, client scenario file, NestJS public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. client는 보존된 error message를 받고, server dispatch observer evidence와 server log marker가 `handlerException`과 `replyError`를 남기는지 확인한다. |
| Node.js | `for scenario in framework/languages/node/test/scenario-e2e/scenarios/CH-001.request-response.json framework/languages/node/test/scenario-e2e/scenarios/CH-002.manual-endpoint-round-robin.json framework/languages/node/test/scenario-e2e/scenarios/CH-004.route-mesh-targeted-request.json framework/languages/node/test/scenario-e2e/scenarios/CH-006.send-one-way.json framework/languages/node/test/scenario-e2e/scenarios/CH-007.request-timeout-late-reply.json framework/languages/node/test/scenario-e2e/scenarios/DERR-001.unregistered-request.json framework/languages/node/test/scenario-e2e/scenarios/DERR-002.unregistered-send.json framework/languages/node/test/scenario-e2e/scenarios/DERR-007.handler-exception.json; do framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh "$scenario"; done` | 통과. Node.js scenario E2E 8개를 같은 runner로 순차 실행했다. runner는 stale ready file이 남아 readiness를 잘못 통과하지 않도록 scenario work directory를 매 실행마다 정리한다. |
| .NET | `dotnet build framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/Zlink.Framework.ScenarioE2E.csproj` | 통과 |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh` | 통과. `CH-001` request-response를 별도 server/client process, client scenario file, framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-002.manual-endpoint-round-robin.json` | 통과. `CH-002` manual endpoint round-robin을 server process 3개와 client process 1개로 실행하고, warm-up 3회 뒤 검증 요청 90개가 30/30/30으로 분산되는지 public channel client와 `ZLinkHttpClient` evidence 조회로 확인했다. |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-004.route-mesh-targeted-request.json` | 통과. `CH-004` route mesh targeted request를 peer 3개 구조로 실행했다. client 역할 peer가 framework public route client로 target routing id `peer-b`에 request를 보내고, `peer-b`만 route request evidence와 source routing id를 남기며 `peer-c`에는 같은 request evidence가 없는지 `ZLinkHttpClient`로 검증했다. 잘못된 routing id request가 caller-visible public error로 끝나는지도 확인했다. |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-006.send-one-way.json` | 통과. `CH-006` send one-way를 별도 server/client process, client scenario file, framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. one-way send는 dispatch 완료와 evidence 기록 시점이 다를 수 있으므로 `assertEvidence` 단계에서 HTTP evidence를 짧게 polling한다. |
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/scenarios/CH-007.request-timeout-late-reply.json` | 통과. `CH-007` request timeout과 late reply 복구를 별도 server/client process, client scenario file, framework public channel client, `ZLinkHttpClient` evidence 조회, `ensure` 검증 구조로 실행했다. scenario client는 timeout request가 public `TimeoutException`으로 끝나는지 확인하고, 같은 client에서 겹쳐 실행한 정상 request와 late reply 이후 request가 정상 처리되는지 검증한다. `completedRequestIds` evidence로 timeout된 slow handler가 client timeout 이후 완료된 사실도 확인한다. |
| Node.js | `timeout 60s node --test --test-name-pattern "DERR-001" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `timeout 120s node --test --test-name-pattern "CH-00[126]" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `timeout 25s node --test --test-name-pattern "DSC-00[89]" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~ClientServerTests"` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_requestReplySucceeds'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_missingRequestHandlerRepliesErrorAndReportsObserver'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_missingSendHandlerReportsObserverAndKeepsRequestPathAlive'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_payloadDecodeFailureRepliesErrorAndReportsObserver'` | 통과. Java framework가 payload decode 실패를 handler exception과 구분하지 못하던 framework 레이어 버그의 회귀 테스트를 포함한다. |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_handlerExceptionRepliesErrorAndReportsObserver'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_dispatchErrorsAreWrittenToFileLog'` | 통과 |
| Java | `cd framework/languages/java && timeout 90s ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_missingRequestHandlerRepliesErrorAndReportsObserver' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_missingSendHandlerReportsObserverAndKeepsRequestPathAlive' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_payloadDecodeFailureRepliesErrorAndReportsObserver' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_handlerExceptionRepliesErrorAndReportsObserver' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_dispatchErrorsAreWrittenToFileLog'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_requestReplySucceeds' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_sendDispatchesToHandler' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_missingRequestHandlerRepliesErrorAndReportsObserver'` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.scannedHandlerGroup_requestReplySucceeds' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.scannedMethodHandlerGroup_requestAndSendDispatch' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.scannedMethodHandlerGroup_publishDispatches' --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.publisherAndSubscriber_workAcrossHosts'` | 통과. `PUB-001` single-subscriber partial proof와 `REG-001`, `REG-002` partial proof를 기존 Java runtime 테스트에 연결한다. |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.fanout_deliversSameSequenceToThreeSubscribers'` | 통과. `PUB-001` 3-subscriber fanout sequence proof를 reflection runtime harness에서 검증한다. |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualChannelHandlers_dispatchRegisteredPacketsAndReportMissingPackets'` | 통과. `REG-003` 수동 request/send/publish handler 등록, 미등록 request error reply, 미등록 send/publish observer drop을 reflection runtime harness에서 검증한다. 첫 실행에서 client/server를 별도 runtime으로 나누면서 `inproc://` endpoint를 사용해 timeout이 났고, 이는 테스트 하네스 endpoint 선택 문제로 분류해 TCP endpoint로 수정했다. core C API와 bindings는 수정하지 않았다. |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.jsonCodec_roundTripsNestedArraysAndNullableFields'` | 통과. `CDC-001` JSON nested object, array, nullable field round trip을 framework channel request/reply 경로로 검증한다. |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests 'systems.zlink.framework.kotlin.KotlinFrameworkE2ETest'` | 통과 |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests '*PUB-001*' --tests '*CDC-001*'` | 통과. `PUB-001` 3-subscriber fanout sequence proof와 `CDC-001` JSON nested object, array, nullable field round trip을 coroutine public lifecycle 경로로 검증한다. 첫 실행에서 새 handler가 Kotlin 테스트의 명시 handler factory에 등록되지 않아 실패했고, 이는 테스트 하네스 등록 누락으로 분류해 handler factory만 수정했다. core C API와 bindings는 수정하지 않았다. |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests '*DERR-009*'` | 통과 |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests '*DERR-00*'` | 통과 |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests '*DSC-008*'` | 통과. 직전 전체 Kotlin E2E 실행에서 `DSC-008`이 한 번 request evidence 중복 확인 timeout을 냈지만, 단독 재실행은 통과했다. |
| Node.js | `node --test --test-name-pattern "CH-001" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~CH-006"` | 통과 |
| Java | `cd framework/languages/java && ./gradlew :zlink-framework-core:integrationTest --tests 'systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_sendDispatchesToHandler'` | 통과 |
| Kotlin | `cd framework/languages/java && ./gradlew :zlink-framework-kotlin:test --tests 'systems.zlink.framework.kotlin.KotlinFrameworkE2ETest'` | 통과 |
| Node.js | `node --test --test-name-pattern "CH-006" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `timeout 60s node --test --test-name-pattern "DERR-002" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `timeout 60s node --test --test-name-pattern "DERR-006" framework/languages/node/test/contract/channel-client.test.js` | 통과. Node.js framework가 payload decode 실패를 handler exception으로 보고하던 framework 레이어 버그의 회귀 테스트를 포함한다. |
| Node.js | `timeout 60s node --test --test-name-pattern "DERR-007" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `npm --prefix framework/languages/node run build && timeout 60s node --test --test-name-pattern "DERR-009" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `npm --prefix framework/languages/node run build && timeout 60s node --test --test-name-pattern "DERR-00[1267]" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `npm --prefix framework/languages/node run build && timeout 60s node --test --test-name-pattern "DERR-00[12679]" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~CH-002"` | 통과 |
| Node.js | `node --test --test-name-pattern "CH-002" framework/languages/node/test/contract/channel-client.test.js` | 통과 |
| Node.js | `timeout 60s node --test --test-name-pattern "PUB-001" framework/languages/node/test/contract/channel-client.test.js` | 통과. runtime host publisher와 3-subscriber fanout sequence proof, public fanout client publish socket proof 2개를 포함한다. |
| Node.js | `timeout 60s node --test --test-name-pattern "PUB-001|CDC-001" framework/languages/node/test/contract/channel-client.test.js` | 통과. `PUB-001` runtime host publisher와 3-subscriber fanout sequence proof, public fanout client publish socket proof, `CDC-001` JSON nested object, array, nullable field round-trip partial proof를 포함한다. |
| Node.js | `timeout 60s node --test --test-name-pattern "REG-003" framework/languages/node/test/contract/channel-client.test.js` | 통과. runtime host registration options로 수동 request/send/publish handler 등록, 미등록 request error reply, 미등록 send/publish observer drop을 검증한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~CH-007"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~PUB-001|DisplayName~STR-001"` | 통과. `PUB-001` 3-subscriber sequence proof, `PUB-001` single-subscriber partial proof, `STR-001` partial proof 3개를 포함한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName=PUB-001 fanout delivers the same sequence to three subscribers"` | 통과. public host/client 경로에서 publisher 1개와 subscriber 3개가 같은 sequence payload를 받는지 검증한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-001"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-002"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-004"` | 통과. .NET framework Spot route handler missing 이벤트와 로그가 target spot rid를 누락하던 framework 레이어 버그의 회귀 테스트를 포함한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-006"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-007"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-009"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~DERR-00"` | 통과. `DERR-001`, `DERR-002`, `DERR-004`, `DERR-006`, `DERR-007`, `DERR-009` 6개를 포함한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~CDC-001"` | 통과. public host/client 경로에서 JSON nested object, array, nullable field round trip 1개를 검증한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "DisplayName~REG-003"` | 통과. public host/client 경로에서 수동 request/send/publish handler 등록, 미등록 request error reply, 미등록 send/publish observer drop을 검증한다. |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~RegistryRemoteAddressesTests"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~RegistrySpotRemoteAddressesTests.RegistrySpotRemoteAddresses_RequestSend_By_Rid"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~DiscoveryTests.RegistrySpotRemoteAddresses_Enables_SpotOwnerSync"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~EntryMailboxExecutionTests.EntrySpot_NativeActorReadableBatch_Dispatches_Actors_Serially"` | 통과 |
| .NET | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --filter "FullyQualifiedName~RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence"` | 통과 |

`.NET` E2E 전체 실행도 시도했지만, full-suite 실행 중 `RemoteSessionRelayTests`가 한 번
timeout을 냈고 이후 전체 실행이 정체되어 수동 중단했다. 같은 테스트는 단독 실행에서 통과했기
때문에 이번 변경의 완료 근거로 full-suite 결과를 사용하지 않는다. 이 현상은 새로 추가한
`CH-007`, `DERR-001` focused regression과는 별개로 추적해야 한다.

## 다음 구현 순서

1. 각 언어의 E2E runner가 `HAR-001`부터 `HAR-007`까지 같은 report marker를 남기게 한다.
2. `.NET`, Java, Kotlin, Node.js에 들어간 `DSC-008`, `DSC-009` 구조를 C++에도 맞춘다.
3. Channel P0, dispatch error P0, registration/codec P0를 언어별 public API로 구현한다.
4. Spot, actor, session, pub/sub, stream 시나리오는 feature map의 지원 여부와 맞춰 구현한다.
5. P1은 지원 기능별로 추가하고, P2는 장기 실행 또는 운영 규모 runner로 분리한다.
6. 모든 변경 후 Codex 스타일 리뷰와 Claude 스타일 리뷰에서 `NO ISSUES`가 나올 때까지
   문서와 구현을 반복 수정한다.
