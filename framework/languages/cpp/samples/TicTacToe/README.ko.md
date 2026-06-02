# TicTacToe

이 샘플은 `.NET` TicTacToe 샘플의 session relay 구조를 C++ public API 위에서 검토하기
위한 리뷰 샘플이다. C++ 샘플도 `.NET` 샘플처럼 `Shared`, `Client`, `Server/Registry`,
`Shared/Actors`, `Shared/Configuration`, `Shared/Contracts`, `Client`,
`Server/Registry`, `Server/Api/Handlers`, `Server/Play/EntrySpot`,
`Server/Play/GameSpots`, `Server/Play/Handlers`, `Server/Session` 파일 단위로 나누어
둔다. 그래서 STREAM과 ActorGateway 기반 흐름이 framework 안에서 처리되는 모습을
역할별로 비교할 수 있다.

포함 범위는 아래와 같다.

- STREAM endpoint 구성
- API channel server/client 구성
- play channel server 구성
- ActorGateway attach
- Entry Spot 등록
- actor factory 등록
- authenticate actor flow
- handler logger 표면
- ensure player actor flow
- create match flow
- create match room flow
- join match response
- place mark response
- session actor bind
- STREAM packet relay
- bound session push
- actor join, place mark, turn changed, game ended 흐름
- disconnect cleanup

샘플 이름에는 별도 접미사를 붙이지 않는다. 이 샘플이 STREAM과 ActorGateway 기반
actor/session relay의 기준 샘플이다.

실행 파일은 아래 역할로 나뉜다.

- `sample_cpp_framework_tictactoe_registry`: registry host 구성
- `sample_cpp_framework_tictactoe_api`: API channel과 authenticate handler
- `sample_cpp_framework_tictactoe_play`: play channel, Spot, actor factory, game flow
- `sample_cpp_framework_tictactoe_session`: STREAM endpoint와 ActorGateway relay
- `sample_cpp_framework_tictactoe_client`: Stream Connector 기반 client flow

client smoke는 connector public API 사용 형태만 보지 않는다. 실행 중 실제 STREAM server를
띄우고 Stream Connector가 TCP로 접속해 request reply와 push notification을 주고받는지
검증한다. 서버는 `tictactoe-server.log` 파일에 bind, receive, reply, push 흐름을 남긴다.
