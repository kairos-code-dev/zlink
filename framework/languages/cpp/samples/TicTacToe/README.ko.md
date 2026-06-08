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
- disconnect cleanup

샘플 이름에는 별도 접미사를 붙이지 않는다. 이 샘플은 수동 endpoint를 쓰는 직접 Play
연결 기준 샘플이다.

실행 파일은 아래 역할로 나뉜다.

- `sample_cpp_framework_tictactoe_api`: API channel과 authenticate handler
- `sample_cpp_framework_tictactoe_play`: play channel, Spot, actor factory, game flow
- `sample_cpp_framework_tictactoe_client`: HTTP client `POST /games` 시작 요청과 Stream Connector 기반 client flow

client smoke는 connector public API 사용 형태만 보지 않는다. 실행 중 실제 HTTP API server와
STREAM server를 띄우고, `zlink::http_client`가 `POST /games`로 room을 만든 뒤
Stream Connector가 TCP로 접속해 request reply와 push notification을 주고받는지 검증한다.
서버는 `tictactoe-server.log` 파일에 HTTP request, bind, monitoring event와
receive, reply, push 흐름을 남긴다.
