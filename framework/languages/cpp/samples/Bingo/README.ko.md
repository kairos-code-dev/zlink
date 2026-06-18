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
- framework Protobuf codec extension으로 등록한 stream/channel/actor/Spot payload
- channel request/reply handler
- handler logger와 callback log sink
- STREAM packet relay
- publish/subscribe 구성과 일반 event publish
- callback submit
- coroutine submit
- user Spot 생성과 room spot node 구성
- `onCreateActor`, room join, room leave callback 흐름
- room user Spot의 `leaveActor` 호출과 Entry Spot 복귀
- Entry Spot의 `destroyActor` 호출
- destroy는 `onLeaveActor`를 호출하지 않는다
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

client scenario 실행 파일은 Stream Connector public API로 request reply와 push notification을
검증하는 시나리오를 담고 있다. 서버 실행 파일들은 Registry, API, Play, Session 역할을
각각 보여 주며, 테스트 전용 fake 서버나 E2E 전용 sample target은 샘플 트리에 두지 않는다.
script 실행 결과는 full client/server self-check 결과와 actor lifecycle sample gate 결과를
표준 출력으로 보여 준다. actor lifecycle sample gate는 sample source가
Entry Spot에서만 `destroyActor`를 호출하는지 확인하고, runtime test로 `leaveActor` 후
Entry Spot destroy와 destroy가 `onLeaveActor`를 호출하지 않는다라는 callback isolation을
검증한다. 같은 gate는 destroy 뒤 actor lookup에서 사라지는지와 같은 actor id 재생성이
가능한지도 확인한다. runner는 Registry, API, Play, Session 서버를 별도 process로 계속
실행한 뒤 public client 실행 파일로 authenticate, match, card submit, server draw, winner
판단 흐름을 검증한다.

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

script 는 CTest sample parity와 actor lifecycle runtime gate를 먼저 실행한다. 그 다음
Registry, API, Play, Session 서버 실행 파일을 계속 실행 모드로 띄우고 public client
실행 파일로 full client/server self-check 를 수행한다.
