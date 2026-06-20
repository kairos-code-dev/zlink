# Node.js Scenario E2E

이 디렉토리는 framework 공통 E2E 문서의 시나리오형 테스트 구조를 Node.js에서 검증한다.
script가 server process와 client process를 따로 실행하고, client가 `scenarios/` 아래
scenario file을 읽어 실제 요청을 보낸다.

## 실행

```bash
./framework/languages/node/test/scenario-e2e/run_scenario_e2e.sh
```

client는 NestJS public module에서 받은 channel client 또는 route client와 `ZLinkHttpClient`만
사용한다. 검증은 샘플처럼 `ensure(...)` 구문으로 직접 작성한다.

## 현재 시나리오

| Scenario | 내용 |
|----------|------|
| `CH-001` | 별도 server process에 등록된 request handler로 channel request를 보내고 reply와 server evidence를 검증한다. |
| `CH-002` | server process 3개를 띄우고 client가 endpoint 3개를 직접 등록한 뒤 90개 검증 요청이 30/30/30으로 분산되는지 검증한다. |
| `CH-004` | route mesh peer 3개 중 client 역할 peer가 target routing id로 request를 보내고 target peer만 handler evidence를 남기는지 검증한다. |
| `CH-006` | client가 one-way send를 보내고, server send handler가 command evidence를 남기는지 검증한다. |
| `CH-007` | 짧은 timeout request가 public timeout error로 끝난 뒤 같은 client의 정상 request와 late reply 이후 request가 계속 성공하는지 검증한다. |
| `DERR-001` | client가 미등록 request packet을 보내고 public handler error와 server dispatch error evidence를 검증한다. |
| `DERR-002` | client가 미등록 send packet을 보내고 server dispatch observer와 로그가 drop을 보고하는지 검증한다. |
| `DERR-007` | request handler가 예외를 던질 때 public error reply와 server dispatch error evidence를 검증한다. |

실행 결과는 `.scenario-e2e/report.json`에 남고, server/client stdout은
`.scenario-e2e/logs/` 아래에 저장된다.
