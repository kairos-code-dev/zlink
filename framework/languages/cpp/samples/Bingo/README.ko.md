# Bingo

이 샘플은 `.NET` Bingo 샘플과 같은 역할 구분을 C++ public API 위에서 검토하기 위한
리뷰 샘플이다. C++ 샘플도 `.NET` 샘플처럼 `Shared`, `Client`, `Server/Registry`,
`Shared/Configuration`, `Shared/Contracts`, `Client`, `Server/Registry`,
`Server/Api/Handlers`, `Server/Play/Handlers`, `Server/Play/BingoRoomSpots`,
`Server/Play/EntrySpot`, `Server/Session` 파일 단위로 나누어 둔다. 그래서 사용자는 각
프로세스 역할과 handler 구현이 framework API 위에서 어떻게 보이는지 그대로 비교할 수
있다.

포함 범위는 아래와 같다.

- sample topology와 endpoint 이름
- registry host 구성
- API channel server/client 구성
- play channel server 구성
- session stream endpoint 구성
- authenticate player/session handler
- ensure player actor handler
- match API handler와 actor match handler
- allocate room, join room, start game, leave room handler
- bingo room state, card, marks, join, start, draw, leave
- client notification inbox
- channel request/reply handler
- handler logger와 callback log sink
- STREAM packet relay
- publish/subscribe 구성과 일반 event publish
- callback submit
- coroutine submit
- user Spot 생성과 room spot node 구성
- SPOT timer 등록
- monitoring source 등록
- offload handler option

이 샘플은 `.NET` Bingo와 같은 session stream 역할을 포함한다. 다만 TicTacToe가
STREAM과 ActorGateway 기반 actor/session relay의 기준 샘플이다.

실행 파일은 아래 역할로 나뉜다.

- `sample_cpp_framework_bingo_registry`: registry host 구성
- `sample_cpp_framework_bingo_api`: API channel과 authenticate handler
- `sample_cpp_framework_bingo_play`: play channel, room domain, room handlers, publish, Spot timer
- `sample_cpp_framework_bingo_session`: STREAM endpoint와 session packet dispatch
- `sample_cpp_framework_bingo_client`: Stream Connector 기반 client flow와 notification inbox

client smoke는 connector public API 사용 형태만 보지 않는다. 실행 중 실제 STREAM server를
띄우고 Stream Connector가 TCP로 접속해 request reply와 push notification을 주고받는지
검증한다. 서버는 `bingo-server.log` 파일에 bind, monitoring event, receive, reply, push
흐름을 남긴다.
