# C++ Scenario E2E

이 프로젝트는 framework 공통 E2E 문서의 시나리오형 테스트 구조를 C++에서 검증한다.
script가 server process와 client process를 따로 실행하고, client가 `scenarios/` 아래
scenario file을 읽어 실제 요청을 보낸다.

## 실행

```bash
./framework/languages/cpp/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh
```

server는 C++ framework `app_t`와 fluent builder 방식으로 channel handler, route handler,
HTTP evidence endpoint를 등록한다. client는 public `channel_client_t`, `route_client_t`,
`zlink::http_client`만 사용한다. 검증은 샘플처럼 `ensure(...)` 구문으로 직접 작성한다.

## 현재 시나리오

| Scenario | 내용 |
|----------|------|
| `CH-001` | 별도 server process에 등록된 request handler로 channel request를 보내고 reply와 server evidence를 검증한다. |
| `CH-002` | server process 3개를 띄우고 client가 endpoint 3개를 직접 등록한 뒤 90개 검증 요청이 30/30/30으로 분산되는지 검증한다. |
| `CH-004` | route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 route evidence를 남기는지 검증한다. |
| `CH-006` | client가 one-way send를 보내고, server send handler가 command evidence를 남기는지 검증한다. |
| `CH-007` | 느린 request가 client timeout으로 끝난 뒤 같은 client가 다음 request를 정상 처리하는지 검증한다. |
| `DERR-001` | client가 미등록 request packet을 보내고 public `handler_not_found` error reply와 server dispatch error evidence를 검증한다. |
| `DERR-002` | client가 미등록 send packet을 보내고 server dispatch observer와 stderr 로그가 drop을 보고하는지 검증한다. |
| `DERR-007` | request handler가 예외를 던질 때 public `request_failed` error reply와 server dispatch error evidence를 검증한다. |

실행 결과는 기본적으로 `build/scenario-e2e-<SCENARIO_ID>/report.json`에 남고,
server/client stdout은 `build/scenario-e2e-<SCENARIO_ID>/logs/` 아래에 저장된다.
