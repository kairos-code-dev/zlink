# Bingo

이 샘플은 공통 Bingo 시나리오의 Session, Api, Play, Registry 역할 구분을 C++ public
API 위에서 보여 준다. client는 Session stream 하나에 연결하고, Play 서버는
`Domain`, `Application`, `Adapters/ZLink` 구조로 게임 규칙과 framework 연결 코드를
나누어 둔다.

포함 범위는 아래와 같다.

- sample topology와 endpoint 이름
- registry host 구성
- API channel server/client 구성
- play channel server 구성
- session stream endpoint 구성
- authenticate player/session handler
- ensure player actor handler
- match API handler와 actor match handler
- allocate room, join room, card submit handler
- bingo room state, 3 x 3 card, server draw, winner 판단
- Stream Connector public wait helper 기반 client push 검증
- Protobuf stream/channel/actor/Spot payload
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

이 샘플은 Registry/Discovery 자동 연결과 session gateway 흐름을 맡는다. 수동 endpoint로
Play stream에 직접 연결하는 흐름은 TicTacToe 샘플이 맡는다.

실행 파일은 아래 역할로 나뉜다.

- `sample_cpp_framework_bingo_registry`: registry host 구성
- `sample_cpp_framework_bingo_api`: API channel과 authenticate handler
- `sample_cpp_framework_bingo_play`: play channel, room domain, room handlers, publish, Spot timer
- `sample_cpp_framework_bingo_session`: STREAM endpoint와 session packet dispatch
- `sample_cpp_framework_bingo_client`: Stream Connector 기반 client flow와 push payload self-check

client 실행 파일은 Stream Connector public API로 request reply와 push notification을
검증하는 시나리오를 담고 있다. 서버 실행 파일들은 Registry, API, Play, Session 역할을
각각 보여 주며, 테스트 전용 fake 서버나 E2E 전용 sample target은 샘플 트리에 두지 않는다.
script 실행 결과는 서버 role smoke 결과와 client 실행 파일 존재 여부를 표준 출력으로
보여 준다. 현재 C++ sample channel request 는 별도 process 사이의 full 실행이 아니라
local framework runtime 안에서 완료되는 구조라서, full client/server 검증은 제공하지
않는다. client scenario 는 public client 실행 파일 안에 남겨 두되, runner 가 성공한
full e2e처럼 표시하지 않는다.

## 실행과 설정

서버 실행 파일은 `--config`로 받은 설정 파일을 `app.config()`로 읽고 `sample.topology`를
`sample_topology_t`에 bind한 뒤 자기 role 만 실행한다. Registry, API, Play, Session 서버는
모두 별도 process 로 실행한다. 서버 role 을 계속 실행하려면 설정 파일의
`sample.host.keepRunning` 값을 `true`로 둔다.

Client 실행 파일은 framework app을 만들지 않는다. Client는 stream connector만 사용하며,
필요하면 `--stream-endpoint` 또는 `ZLINK_CPP_CLIENT_STREAM_ENDPOINT`로 접속 endpoint를
받는다.

Linux 또는 WSL에서는 아래 script 를 실행한다.

```bash
./framework/languages/cpp/samples/Bingo/run_sample.sh
```

Windows PowerShell에서는 아래 script 를 실행한다.

```powershell
.\framework\languages\cpp\samples\Bingo\run_sample.ps1
```

script 는 Registry, API, Play, Session 서버 실행 파일을 smoke 모드로 실행한다. full
client/server self-check 는 현재 실행 파일 조합으로 완료되지 않으므로 실행하지 않는다.
