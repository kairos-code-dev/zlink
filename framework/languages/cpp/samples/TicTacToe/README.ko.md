# TicTacToe

이 샘플은 공통 TicTacToe 시나리오의 API 역할과 Play 역할만 C++ public API 위에서
보여 준다. 별도 Session 서버와 Registry 서버를 두지 않고, API 서버와 Play 서버가
수동 endpoint로 연결된다. client는 HTTP `POST /games` 응답으로 받은 Play stream
endpoint에 직접 연결한다.

포함 범위는 아래와 같다.

- Play STREAM endpoint 구성
- API channel server/client 구성
- play channel server 구성
- Entry Spot 등록
- actor factory 등록
- authenticate player flow
- handler logger 표면
- ensure player actor flow
- create game flow
- create room flow
- join game response
- place mark response
- Play session actor bind
- actor join, place mark, player joined, game state 흐름
- MessagePack stream/channel/actor/Spot payload
- disconnect cleanup

샘플 이름에는 별도 접미사를 붙이지 않는다. 이 샘플은 수동 endpoint를 쓰는 직접 Play
연결 기준 샘플이다.

실행 파일은 아래 역할로 나뉜다.

- `sample_cpp_framework_tictactoe_api`: API channel과 authenticate handler
- `sample_cpp_framework_tictactoe_play`: play channel, Spot, actor factory, game flow
- `sample_cpp_framework_tictactoe_client`: `TicTacToeClientScenario` 안에서 수행하는 HTTP `POST /games` 시작 요청과 Stream Connector 기반 client flow

client 실행 파일은 설정을 읽고 `tictactoe_client_scenario_t`를 실행한다. HTTP client `POST /games`
흐름은 `TicTacToeClientScenario` 안에 있다. client scenario는
`zlink::http_client`로 `POST /games`를 호출하고, 응답으로 받은 Play stream endpoint에
Stream Connector로 접속한다. API와 Play 실행
파일은 각각 실제 샘플 서버 역할을 보여 주며, 테스트 전용 fake 서버나 E2E 전용 sample
target은 샘플 트리에 두지 않는다.
script 실행 결과는 서버 role smoke 결과와 client 실행 파일 존재 여부를 표준 출력으로
보여 준다. 현재 C++ sample channel request 는 별도 process 사이의 full 실행이 아니라
local framework runtime 안에서 완료되는 구조라서, full client/server 검증은 제공하지
않는다. client scenario 는 public client 실행 파일 안에 남겨 두되, runner 가 성공한
full e2e처럼 표시하지 않는다.

## 실행과 설정

서버 실행 파일은 `--config`로 받은 설정 파일을 `app.config()`로 읽고 `sample.topology`를
`sample_topology_t`에 bind한 뒤 자기 role 만 실행한다. 서버 role 을 계속 실행하려면
설정 파일의 `sample.host.keepRunning` 값을 `true`로 둔다.

Client 실행 파일은 framework app을 만들지 않는다. Client는 API HTTP endpoint만 설정으로
받고, Play stream endpoint는 `POST /games` 응답에서 받아 connector를 만든다. 필요하면
`--api-http-endpoint` 또는 `ZLINK_CPP_CLIENT_API_HTTP_ENDPOINT`로 API HTTP endpoint를 받는다.

Linux 또는 WSL에서는 아래 script 를 실행한다.

```bash
./framework/languages/cpp/samples/TicTacToe/run_sample.sh
```

Windows PowerShell에서는 아래 script 를 실행한다.

```powershell
.\framework\languages\cpp\samples\TicTacToe\run_sample.ps1
```

script 는 Play 서버와 API 서버 실행 파일을 smoke 모드로 실행한다. full client/server
self-check 는 현재 실행 파일 조합으로 완료되지 않으므로 실행하지 않는다.
