# Kotlin Scenario E2E

이 프로젝트는 framework 공통 E2E 문서의 시나리오형 테스트 구조를 Kotlin에서 검증한다.
script가 server process와 client process를 따로 실행하고, client가 `scenarios/` 아래
scenario file을 읽어 실제 요청을 보낸다.

## 실행

```bash
./framework/languages/java/zlink-framework-kotlin-scenario-e2e/run_scenario_e2e.sh
```

server는 Kotlin suspend annotation handler를 Spring Boot starter의 public lifecycle로
등록한다. client는 `ZLinkClient` 또는 `ZLinkRouteClient`의 Kotlin coroutine extension과
`ZLinkHttpClient` Kotlin extension만 사용한다. 검증은 샘플처럼 `ensure(...)` 구문으로
직접 작성한다.

## 현재 시나리오

| Scenario | 내용 |
|----------|------|
| `CH-001` | 별도 server process에 등록된 Kotlin suspend request handler로 channel request를 보내고 reply와 server evidence를 검증한다. |
| `CH-002` | server process 3개와 client process 1개를 띄우고, client가 manual endpoint 3개를 public builder에 등록해 90개 검증 요청이 30/30/30으로 분산되는지 검증한다. |
| `CH-004` | route mesh peer 3개를 띄우고 client 역할 peer가 target routing id로 보낸 request가 target peer에만 도착하는지 검증한다. |
| `CH-006` | client가 one-way send를 보내고, server suspend send handler가 command evidence를 남기는지 검증한다. |
| `CH-007` | 짧은 timeout request가 public `TimeoutException`으로 끝난 뒤 같은 client의 정상 request와 late reply 이후 request가 계속 성공하는지 검증한다. |

실행 결과는 scenario id별 `build/scenario-e2e-<ID>/report.json`에 남고, server/client stdout은
해당 작업 디렉터리의 `logs/` 아래에 저장된다.
