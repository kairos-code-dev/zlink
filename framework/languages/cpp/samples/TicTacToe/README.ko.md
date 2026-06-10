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
- `sample_cpp_framework_tictactoe_client`: HTTP client `POST /games` 시작 요청과 Stream Connector 기반 client flow

client 실행 파일은 `zlink::http_client`로 `POST /games`를 호출하고, 응답으로 받은 Play
stream endpoint에 Stream Connector로 접속하는 시나리오를 담고 있다. API와 Play 실행
파일은 각각 실제 샘플 서버 역할을 보여 주며, 테스트 전용 fake 서버나 E2E 전용 sample
target은 샘플 트리에 두지 않는다. client scenario는 `tictactoe-client.log`에 완료 여부를
남긴다.
