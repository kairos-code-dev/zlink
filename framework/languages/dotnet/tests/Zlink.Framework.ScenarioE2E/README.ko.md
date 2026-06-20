# .NET Scenario E2E

이 프로젝트는 framework 공통 E2E 문서의 시나리오형 테스트 구조를 .NET에서 검증한다.
기존 xUnit 기반 회귀 테스트와 달리 script가 server process와 client process를 따로
실행하고, client가 `scenarios/` 아래 scenario file을 읽어 실제 요청을 보낸다.

## 실행

```bash
./framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh
```

script는 임시 work directory를 만들고 server를 먼저 실행한 뒤 client를 실행한다. client는
framework public channel client 또는 route client와 `ZLinkHttpClient`만 사용해서 요청과
evidence 조회를 수행한다. 검증은 샘플 코드처럼 `ensure(...)` 구문으로 직접 작성한다.

## 현재 시나리오

| Scenario | 내용 |
|----------|------|
| `CH-001` | 별도 server process에 등록된 request handler로 channel request를 보내고 reply와 server evidence를 검증한다. |
| `CH-002` | server process 3개를 띄우고 client가 endpoint 3개를 직접 등록한 뒤 90개 검증 요청이 30/30/30으로 분산되는지 검증한다. |
| `CH-004` | route mesh peer 3개를 띄우고 client 역할 peer가 target routing id로 보낸 request가 target peer에만 도착하는지 검증한다. |
| `CH-006` | client가 one-way send를 보내고, server send handler가 command evidence를 남기는지 검증한다. |
| `CH-007` | 느린 request가 client timeout으로 끝난 뒤 같은 client의 다음 request와 겹친 정상 request가 정상 처리되는지 검증한다. |

실행 결과는 기본적으로 `bin/scenario-e2e-<SCENARIO_ID>/report.json`에 남고,
server/client stdout은 `bin/scenario-e2e-<SCENARIO_ID>/logs/` 아래에 저장된다.
