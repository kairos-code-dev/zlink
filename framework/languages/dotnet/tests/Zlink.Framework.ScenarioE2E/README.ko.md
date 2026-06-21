# .NET Scenario E2E

이 프로젝트는 framework 공통 E2E 문서의 시나리오형 테스트를 .NET에서 검증한다. 구조는
샘플(Bingo·TicTacToe)을 그대로 따른다. 공유 server를 한 번 구성해 띄우고, client는
목차(chapter)별 파일에 담긴 시나리오를 하나씩 실행하면서 검증한다. 각 시나리오 코드는
샘플처럼 읽히도록, framework public client(channel/route)와 `ZLinkHttpClient`를 직접
호출하고 `Ensure(...)`로 단언한다. helper 뒤로 messaging을 숨기지 않는다.

## 실행

```bash
# 전체 시나리오
./framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh

# 단일 시나리오
./framework/languages/dotnet/tests/Zlink.Framework.ScenarioE2E/run_scenario_e2e.sh CH-002
```

`run_scenario_e2e.sh`는 시나리오마다 임시 work directory를 만들고, 시나리오가 선언한
topology(server 개수와 mode)에 맞춰 공유 server process를 띄운 뒤 client process를
실행한다. server zlink/HTTP 포트가 모두 열린 것을 `wait_port`로 확인하고 나서 client를
띄운다. client가 `scenario-e2e result=passed`를 출력하면 통과로 본다.

## 구조

| 파일 | 역할 |
|------|------|
| `Server/ScenarioServer.cs` | 모든 시나리오가 공유하는 server: handler 등록, evidence 저장, dispatch-error observer, `/health`·`/evidence` endpoint. channel/route-mesh 두 mode를 지원한다. |
| `Scenarios/ScenarioClient.cs` | client host 부트스트랩(샘플의 `CreateClient`/`HostFactory` 대응). 시나리오는 여기서 host를 받아 public client를 직접 호출한다. |
| `Scenarios/Ch02ChannelMessaging.cs` | E2E 문서 2장 — channel messaging 시나리오(`CH-001/002/004/006/007`). |
| `Scenarios/Ch07DispatchError.cs` | E2E 문서 7장 — dispatch 오류·관측성 시나리오(`DERR-001/002/007`). |
| `ScenarioCatalog.cs` | 시나리오 단일 출처: id별 topology와 client 흐름 delegate. 시나리오 추가 = chapter 파일에 method 하나 + 여기 한 줄. |
| `Program.cs` | 얇은 진입점. `server`/`client`/`topology`/`list` 역할만 분기한다. |

새 시나리오를 추가하려면 해당 chapter 파일에 `static Task` method를 작성하고
`ScenarioCatalog`에 `(id, mode, server 이름, method)` 한 줄을 더한다. 새 chapter는
`Scenarios/ChNN....cs` 파일을 추가한다.

## 현재 시나리오

| Scenario | 내용 |
|----------|------|
| `CH-001` | 등록된 request handler로 channel request를 보내고 reply와 server evidence를 검증한다. |
| `CH-002` | server 3개를 띄우고 client가 endpoint 3개를 직접 등록한 뒤 검증 요청 90개가 30/30/30으로 분산되는지 reply와 per-server evidence로 확인한다. |
| `CH-004` | route mesh에서 client peer가 target routing id로 보낸 request가 target peer에만 도착하고, 없는 routing id는 실패하며, non-target peer는 받지 않는지 검증한다. |
| `CH-006` | client one-way send가 server send handler에 command evidence를 남기는지 검증한다. |
| `CH-007` | client timeout으로 끝난 느린 request가 뒤따르는 정상 request·동시 request를 오염시키지 않는지 검증한다. |
| `DERR-001` | 미등록 packet request가 public 오류를 받고 server가 `handlerMissing`/`replyError`로 관측하는지 검증한다. |
| `DERR-002` | 미등록 packet send가 조용히 drop되고 server가 `handlerMissing`/`drop`으로 관측하는지 검증한다. |
| `DERR-007` | handler 예외가 public 오류를 받고 server가 `handlerException`/`replyError`로 관측하는지 검증한다. |

각 실행의 work directory는 `mktemp`로 격리되며, server/client stdout은 그 아래
`logs/`에, 요약은 `report.json`에 남는다.
