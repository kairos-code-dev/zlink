<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [다음: Channel messaging](02-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

# 테스트 하네스와 Evidence

scenario E2E는 실제 사용 구조를 검증하므로 테스트 하네스 자체가 중요하다. 하네스가
준비 상태, 로그, 실패 원인을 안정적으로 모으지 못하면 기능 회귀와 테스트 불안정을
구분할 수 없다.

## HAR-001 프로세스 역할 분리

우선순위: `P0`

구성:

- `server` 프로세스 1개 이상
- `client` 프로세스 1개 이상
- scenario file이 `registry.count > 0`을 선언하면 `registry` 프로세스 1개 이상
- 필요하면 `probe` 프로세스 1개

언어별 scenario E2E는 기존 unit test process 안에서 host 객체를 직접 만들고 검증하는
방식이 아니다. 언어별 표준 위치의 별도 E2E 프로젝트 실행 스크립트가 server/client를 독립
프로세스로 시작하고, scenario file이 `registry.count > 0`을 선언하면 registry도 독립 프로세스로
시작한다. 같은 프로세스 안에서 `IHost`, Spring context, Node runtime, C++ runtime 객체를
여러 개 만들어 검증하는 방식은 integration proof로만 취급한다.

client 프로세스는 `scenarios/` 아래의 scenario file을 읽어 단계별 명령을 수행한다. 서버
프로세스는 샘플 서버가 아니라 E2E 검증 전용 서버이며, 샘플과 같은 public framework 사용
패턴을 쓰되 evidence 수집 endpoint나 evidence 파일을 추가로 제공한다.

절차:

1. 테스트별 임시 작업 디렉토리를 만든다.
2. scenario file을 읽고 실행 계획을 만든다.
3. scenario file이 `registry.count > 0`을 선언하면 실행 스크립트가 registry process를 시작한다.
4. 실행 스크립트가 server process를 필요한 개수만큼 시작한다.
5. 실행 스크립트가 readiness를 확인한 뒤 client process를 시작한다.
6. 모든 프로세스 stdout/stderr를 `logs/<role>.log`에 저장한다.
7. server는 자신의 role, endpoint, routing id, channel name을 시작 로그에 남긴다.
8. client는 요청별 correlation id와 기대 결과를 로그에 남긴다.
9. 테스트 종료 시 모든 프로세스를 정상 종료하고 남은 프로세스가 없는지 확인한다.

검증:

- 모든 프로세스 로그 파일이 존재한다.
- client가 실행한 scenario file 경로와 scenario id가 report에 남는다.
- report에는 사용한 실행 스크립트 경로와 각 process id가 남는다.
- readiness marker 전에 client 요청을 시작하지 않는다.
- 실패 시 로그 tail이 리포트에 포함된다. scenario file이 `registry.count > 0`을 선언했다면
  registry snapshot도 포함된다.

## HAR-001A 실행 스크립트 계약

우선순위: `P0`

각 언어별 scenario E2E 프로젝트는 샘플처럼 사람이 직접 실행할 수 있는 스크립트를 제공한다.
스크립트 이름은 언어 관례에 맞추되, README와 CI 문서에서 같은 의미로 참조할 수 있어야 한다.

권장 이름:

- C++: `run_scenario_e2e.sh`
- .NET: `run_scenario_e2e.sh`와 필요하면 `run_scenario_e2e.ps1`
- Java/Kotlin: `./gradlew :<scenario-project>:scenarioE2E`
- Node.js: `npm run scenario:e2e`

스크립트 책임:

1. build가 필요하면 먼저 수행한다.
2. 임시 작업 디렉토리를 만든다.
3. server/client process를 시작하고, scenario file이 `registry.count > 0`을 선언하면 registry
   process도 시작한다.
4. readiness를 확인한다.
5. client process에 scenario file 경로를 넘겨 실행한다.
6. client exit code와 report를 확인한다.
7. 실패 여부와 관계없이 모든 process를 종료한다.
8. logs, evidence, report 경로를 stdout에 출력한다.

검증:

- 테스트 통과 여부는 스크립트 exit code로 판단할 수 있어야 한다.
- server process가 죽으면 스크립트가 실패해야 한다.
- client process가 scenario failure를 report하면 스크립트가 실패해야 한다.
- 스크립트는 샘플 실행처럼 로컬 개발자가 바로 실행할 수 있어야 한다.

## HAR-002 Readiness 판정

우선순위: `P0`

절차:

1. server는 health endpoint 또는 ready marker를 제공한다.
2. registry를 쓰는 시나리오는 topology snapshot에서 필요한 channel과 endpoint가
   `Ready` 상태인지 확인한다.
3. client는 readiness가 끝난 뒤에만 request를 시작한다.

검증:

- 단순 sleep만으로 readiness를 판정하지 않는다.
- registry 기반 시나리오는 원하는 provider 수가 채워졌는지 확인한다.
- 수동 endpoint 시나리오는 최소 1회 handshake 또는 ping request로 연결을 확인한다.

## HAR-003 Evidence endpoint

우선순위: `P0`

server는 테스트 전용 evidence endpoint 또는 query API를 제공할 수 있다. 이 endpoint는
업무 API가 아니라 테스트 검증용이므로 샘플 public 흐름에는 넣지 않는다.

수집 값:

- 받은 request 수
- handler별 호출 수
- routing id별 호출 수
- publish 수신 수
- actor join/leave 기록
- dispatch error 기록
- 마지막 payload 요약

검증:

- client 로그만으로 성공을 판단하지 않는다.
- server evidence가 client 기대값과 일치해야 한다.
- evidence는 테스트 실행별 임시 저장소를 사용한다.

## HAR-004 Deterministic input

우선순위: `P0`

절차:

1. 요청 payload에는 deterministic id를 넣는다.
2. round-robin이나 weighted routing 검증에는 충분한 요청 수를 사용한다.
3. timer와 retry가 있는 시나리오는 timeout 범위를 명시한다.

검증:

- 실패 시 어느 요청이 어느 server로 갔는지 재구성할 수 있다.
- test flake를 줄이기 위해 random seed를 로그에 남긴다.

## HAR-004A Scenario file 계약

우선순위: `P0`

scenario file은 사람이 읽을 수 있고 source control에 보관되는 입력이어야 한다. 파일
형식은 언어별 runner가 쉽게 읽을 수 있는 JSON 또는 YAML을 우선한다. 언어별 구현이 다른
형식을 쓰더라도 아래 의미는 같아야 한다.

필수 필드:

- `id`: `CH-001`, `DSC-008` 같은 scenario id
- `name`: 사람이 읽는 시나리오 이름
- `roles`: registry, server, client process 목록
- `steps`: start, wait-ready, request, send, publish, stop, restart, assert 단계
- `expect`: reply, public error, server evidence, registry topology, log marker 조건
- `artifacts`: logs, evidence, report 출력 위치

예:

```json
{
  "id": "CH-001",
  "name": "request-response across server process",
  "roles": {
    "registry": { "count": 1 },
    "server": [{ "name": "api-a", "channel": "api" }],
    "client": [{ "name": "client-a" }]
  },
  "steps": [
    { "start": "registry" },
    { "start": "server:api-a" },
    { "waitReady": "server:api-a" },
    {
      "clientRequest": {
        "client": "client-a",
        "channel": "api",
        "requestId": "ch-001-1",
        "expectReply": "ok"
      }
    }
  ]
}
```

검증:

- 테스트 이름만으로 scenario를 하드코딩하지 않는다.
- client 프로세스는 scenario file의 단계 순서를 따라 실행한다.
- server, client, 그리고 scenario file이 선언한 추가 role의 evidence가 scenario file의
  `expect`와 일치해야 통과한다.
- scenario file에 없는 hidden step으로 성공을 만들지 않는다.

## HAR-004B Client 검증 코드 계약

우선순위: `P0`

client process는 시나리오 파일을 실행하는 검증 주체다. client 코드는 샘플처럼 읽을 수
있어야 하며, 검증을 숨기는 테스트 helper를 사용하지 않는다. client를 작성할 때는
시나리오 검증 로직을 별도 helper 뒤에 감추지 않고, 단계별 connector 호출과 evidence
조회, `ensure` 검증이 코드 흐름에 직접 드러나야 한다.

규칙:

- client는 channel connector, stream connector, `zlink-http client`처럼 사용자가 실제로
  호출하는 공개 client 표면만 사용한다. 시나리오 검증은 connector와 `zlink-http client`
  조합으로 작성하고, 테스트 전용 helper로 framework 내부 상태를 대신 읽지 않는다.
- server evidence 조회는 `zlink-http client` 또는 공개 connector 경로로 수행한다.
- 언어별 public DI/container API로 공개 connector나 client instance를 얻는 것은 허용한다.
- framework 내부 runtime 객체, private API, reflection, server/test-only state에 직접
  접근하는 service provider 사용은 금지한다.
- 검증은 샘플 코드처럼 `ensure(condition, message)` 형태로 직접 작성한다.
- scenario 단계마다 어떤 값을 보냈고 어떤 값을 기대하는지 client 코드에서 드러나야 한다.
- 반복 대기나 readiness 확인도 `ensure`와 공개 client 호출의 조합으로 작성한다.

예:

```text
ensure(reply.requestId == step.requestId, "reply request id must match");
ensure(reply.providerId == "api-a", "request must reach provider api-a");
ensure(evidence.requests.contains(step.requestId), "server evidence must include request");
```

검증:

- client 코드만 읽어도 scenario의 성공 조건을 알 수 있어야 한다.
- helper 이름만 보고 내부 동작을 추측해야 하는 검증 코드는 쓰지 않는다.
- evidence 조회가 필요하면 별도 test-only in-process 객체를 참조하지 않고, server process가
  공개한 HTTP endpoint나 connector endpoint를 통해 조회한다.

## HAR-005 로그 marker 표준

우선순위: `P0`

각 시나리오는 성공 marker와 실패 marker를 정의한다.

예:

```text
scenario=CH-001 step=request index=17 target=server-b result=ok
scenario=DERR-001 dispatch-error reason=HandlerMissing action=ReplyError
scenario=SPOT-004 actor-bound actor=player-1 session=session-a
```

검증:

- marker는 grep 가능한 plain text로 남긴다.
- structured logging을 쓰더라도 핵심 marker 문자열은 유지한다.
- 로그에 민감한 payload 전문을 남기지 않는다.

## HAR-006 Cross-language report

우선순위: `P1`

언어별 E2E runner는 같은 summary 형식을 출력한다.

필수 필드:

- scenario id
- language
- runtime version
- transport backend
- codec
- process count
- passed/failed/skipped
- evidence path
- log directory
- executed script path
- per-role process id map

검증:

- CI에서 언어별 결과를 같은 표로 모을 수 있다.
- skip은 언어별 feature map의 미지원 항목과 연결되어야 한다. feature map 위치는
  [E2E 목차](README.ko.md)의 우선순위 절을 따른다.

## HAR-007 실패 원인 레이어 분류

우선순위: `P0`

E2E 실패는 여러 레이어를 지나서 보이기 때문에 증상만 보고 framework 코드를 고치면 안 된다.
테스트는 실패 시 어느 레이어가 원인인지 분리할 수 있는 evidence를 남겨야 한다.
작업 중 새 버그를 발견하면 먼저 `core-capi`, `bindings`, `framework`, `sample`, `harness`
중 실제 원인 레이어를 판정한다. 예를 들어 C API 계약이 깨졌는지, 언어별 bindings가 C API
결과를 잘못 감쌌는지, framework dispatch나 lifecycle 코드가 잘못 처리했는지 분리해서 확인한다.
원인이 있는 레이어를 고친 뒤에는 같은 레이어에 회귀 테스트를 추가한다.

분류 기준:

- `core-capi`: `core/include/zlink.h` 공개 C API나 core runtime 동작이 계약과 다르다.
  C API 호출 결과, errno, ownership, timeout, routing id 처리 중 하나라도 공개 계약과 다르면
  framework 쪽에서 우회하지 않고 이 레이어의 버그로 분리한다.
- `bindings`: C API 결과를 언어별 bindings가 잘못 감싸거나 오류, ownership, timeout,
  routing id 변환을 다르게 해석한다.
- `framework`: bindings public API는 정상인데 framework registration, dispatch, codec,
  discovery, lifecycle 처리가 잘못된다.
- `sample`: framework public API는 정상인데 샘플 wiring, 설정, process orchestration이
  잘못된다.
- `harness`: 제품 코드가 아니라 테스트 준비, readiness, evidence 수집, timeout 설정이
  잘못된다.

검증:

- 실패 리포트에는 위 분류 중 하나와 판단 근거를 남긴다.
- 원인 레이어를 수정하면 같은 레이어에 회귀 테스트를 추가한다.
- `core-capi`, `bindings`, `framework` 중 어느 레이어 문제인지 확인하기 전에는 상위
  레이어에서 workaround를 넣지 않는다. 상위 레이어 테스트가 실패했더라도 원인이 C API나
  bindings이면 그 레이어를 먼저 고친다.
- core library를 수정한 경우에는 `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`를
  실행해 bindings native library를 갱신한 뒤 framework E2E를 다시 실행한다.
- 원인 레이어가 확인되지 않은 임시 우회는 완료로 보지 않는다.
