# zlink MMORPG 듀얼 서버 샘플

MMORPG 스타일의 서버 클러스터를 zlink 메시징 라이브러리로 구축하는 종합 샘플입니다.
3명의 클라이언트가 2개의 프론트 서버(존 서버)에 접속하며, 프론트 서버끼리는 SPOT
pub/sub로 인접 존 동기화를 수행하고, 아웃게임 요청은 Gateway를 통해 로드밸런싱되는
2대의 API 서버로 전달됩니다. 모든 서비스 발견은 zlink Registry를 통해 자동으로
이루어집니다.

## 아키텍처

```text
Raw TCP 클라이언트 (Asio)
        |
        v
  [front-server A] <==== SPOT (인접 존 동기화) ====> [front-server B]
      |      ^                                        ^      |
      |      +---- Discovery(spot) + spot_node -------+      |
      |
      +---- Discovery(gateway) -> Gateway ----+
                                               |
                                    round-robin 로드밸런싱
                                      |                |
                                      v                v
                              [api-server A]    [api-server B]
                              (Receiver)        (Receiver)
                                      |                |
                                      +-------+--------+
                                              |
                                          [registry]
```

## 시연하는 zlink 기능

| 기능 | 역할 |
|------|------|
| **STREAM 소켓** | 프론트 서버의 Raw TCP 클라이언트 연결 수신 |
| **Gateway + Receiver** | 서버 간 RPC, round-robin 로드밸런싱 |
| **SPOT (pub/sub 메시)** | 인접 존 플레이어 위치 동기화 |
| **Registry + Discovery** | Gateway 및 SPOT 피어 자동 서비스 발견 |

## 사전 요구사항

- CMake 3.14 이상
- C++17 컴파일러 (GCC 7+, Clang 5+, MSVC 2017+)
- vcpkg (asio 및 zlink 의존성 설치용)
- zlink overlay port (리포지토리 루트 `vcpkg/ports/zlink/`에 포함)

## 빌드

```bash
cd samples/cpp/mmorpg_dual_server

# vcpkg로 구성 (VCPKG_ROOT를 본인의 vcpkg 설치 경로로 수정)
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=../../../vcpkg/ports

# 빌드
cmake --build build
```

빌드하면 `build/` 디렉토리에 4개 바이너리가 생성됩니다:

| 바이너리 | 설명 |
|----------|------|
| `sample-registry` | 서비스 디스커버리 허브 |
| `sample-api-server` | 아웃게임 API (Receiver) |
| `sample-front-server` | 존 서버 (STREAM + Gateway + SPOT) |
| `sample-raw-client` | 멀티 클라이언트 시나리오 러너 (Asio TCP) |

## 실행

### 빠른 시작 (올인원)

```bash
./scripts/run_local.sh all
```

5개 서버 프로세스를 모두 기동하고, 안정화를 기다린 후, 멀티 클라이언트 시나리오를
실행하고, 종료 시 모든 프로세스를 정리합니다.

### 수동 실행

```bash
# 터미널 1: 서버 기동
./scripts/run_local.sh start

# 터미널 2: 시나리오 실행
./scripts/run_local.sh scenario

# 완료 후 정리
./scripts/run_local.sh stop
```

### 스크립트 명령어

| 명령어 | 동작 |
|--------|------|
| `start` | Registry + API 서버 2대 + 프론트 서버 2대를 백그라운드로 기동 |
| `stop` | 모든 백그라운드 서버 종료 |
| `scenario` | 클라이언트 시나리오 실행 (서버가 이미 실행 중이어야 함) |
| `all` | 기동 -> 시나리오 실행 -> 종료 (인자 없이 실행 시 기본값) |

## 시나리오 출력

시나리오 러너는 4개 페이즈를 실행하며, 컬러 코드가 적용된 콘솔 출력을 보여줍니다.
성공 시 출력 예시:

```
  ============================================================
    MMORPG Dual-Server Sample  --  Multi-Client Scenario
  ============================================================

--- Phase 1: Zone Entry ---
  [Alice->FrontA] >>> REQ|A1|ENTER|Alice
  [Alice<-FrontA] <<< RES|A1|OK|zone(0,0) entered
  [Bob  ->FrontB] >>> REQ|B1|ENTER|Bob
  [Bob  <-FrontB] <<< RES|B1|OK|zone(1,0) entered
  [Carol->FrontA] >>> REQ|C1|ENTER|Carol
  [Carol<-FrontA] <<< RES|C1|OK|zone(0,0) entered
  [check 1-3] 3명 모두 입장 성공

--- Phase 2: Boundary Movement (SPOT 존 동기화) ---
  [Alice->FrontA] >>> REQ|A2|MOVE|85|50
  [Alice<-FrontA] <<< RES|A2|OK|85|50
  [Bob  <-FrontB] <<< EVT|PLAYER_NEAR|PLAYER|Alice|85|50|100|0   <- SPOT 동기화!
  [Carol<-FrontA]     (이벤트 없음 -- 같은 존 중앙에 있으므로 정상)
  [Bob  ->FrontB] >>> REQ|B2|MOVE|15|48
  [Alice<-FrontA] <<< EVT|PLAYER_NEAR|PLAYER|Bob|15|48|100|0     <- SPOT 동기화!
  [check 4-8] 경계 이동 + SPOT 전파 검증 통과

--- Phase 3: Gateway Load Balancing (OUTGAME) ---
  [Alice->FrontA] >>> REQ|A3|OUTGAME|PROFILE
  [Alice<-FrontA] <<< RES|A3|OK|Alice|lv10|hp100|api-1           <- api-1이 처리
  [Bob  ->FrontB] >>> REQ|B3|OUTGAME|PROFILE
  [Bob  <-FrontB] <<< RES|B3|OK|Bob|lv10|hp100|api-1             <- Gateway LB
  [check 9-11] OUTGAME 요청/응답 E2E 검증 통과

--- Phase 4: Zone Transfer ---
  [Alice->FrontA] >>> REQ|A4|LEAVE
  [Alice<-FrontA] <<< RES|A4|OK|left zone(0,0)
  Alice disconnecting from FrontA...
  Alice reconnecting to FrontB...
  [Alice->FrontB] >>> REQ|A1|ENTER|Alice
  [Alice<-FrontB] <<< RES|A1|OK|zone(1,0) entered
  [Carol<-FrontA]     (Alice 이벤트 없음 -- 존을 떠났으므로 정상)
  [check 12-15] 존 이전 + 이벤트 격리 검증 통과

  ============================================================
    Result: 15 / 15 checks passed
  ============================================================
```

## 프로세스 구성

| 바이너리 | 인스턴스 수 | 역할 |
|----------|------------|------|
| `sample-registry` | 1 | 서비스 디스커버리 허브 (PUB + ROUTER) |
| `sample-api-server` | 2 | 아웃게임 API, Receiver로 동작 (Gateway가 로드밸런싱) |
| `sample-front-server` | 2 | 존 서버: STREAM 수신, Gateway 클라이언트, SPOT 피어 |
| `sample-raw-client` | 1 | 멀티 클라이언트 시나리오 러너 (Asio raw TCP) |

### 포트 할당 (기본값)

| 프로세스 | 포트 | 프로토콜 |
|----------|------|----------|
| Registry PUB | 5550 | ZMQ PUB |
| Registry ROUTER | 5551 | ZMQ ROUTER |
| api-1 | 6001 | ZMQ ROUTER (Receiver) |
| api-2 | 6002 | ZMQ ROUTER (Receiver) |
| front-A | 7001 | TCP STREAM (클라이언트 대면) |
| front-B | 7002 | TCP STREAM (클라이언트 대면) |
| front-A SPOT | 9001 | ZMQ XPUB (SPOT 노드) |
| front-B SPOT | 9002 | ZMQ XPUB (SPOT 노드) |

## 메시지 프로토콜

클라이언트-서버 간 모든 메시지는 길이 프리픽스 TCP 프레이밍(`[4바이트 빅엔디안 길이][페이로드]`) 위에
파이프(`|`) 구분 텍스트 형식을 사용합니다.

### 요청 / 응답 / 이벤트

| 방향 | 형식 |
|------|------|
| 클라이언트 요청 | `REQ\|<req_id>\|<op>\|<arg1>\|...` |
| 서버 응답 | `RES\|<req_id>\|OK\|<body>` 또는 `RES\|<req_id>\|ERR\|<reason>` |
| 서버 푸시 이벤트 | `EVT\|<topic>\|<body>` |

### 클라이언트 커맨드

| 커맨드 | 처리 경로 | 설명 |
|--------|----------|------|
| `ENTER\|<name>` | 프론트 (인게임) | 현재 프론트 서버가 관리하는 존에 입장 |
| `MOVE\|<x>\|<y>` | 프론트 (인게임) | 좌표 이동. 경계 근처이면 SPOT으로 인접 존에 전파 |
| `LEAVE` | 프론트 (인게임) | 현재 존에서 퇴장 |
| `PING` | 프론트 (인게임) | Ping/Pong 왕복 확인 |
| `OUTGAME\|PROFILE` | 프론트 -> API | API 클러스터에 프로필 조회 요청 |
| `OUTGAME\|INVENTORY` | 프론트 -> API | API 클러스터에 인벤토리 조회 요청 |

### 프론트 -> API (Gateway 멀티파트)

| 프레임 | 내용 |
|--------|------|
| part 0 | `op` (예: `PROFILE`) |
| part 1 | `req_id` (내부 요청 ID) |
| part 2 | `session_id` (라우팅 ID hex) |
| part 3 | `payload` (플레이어 이름 등) |

### SPOT 토픽 / 페이로드

| 필드 | 예시 |
|------|------|
| 토픽 | `field:0:0:state` |
| 페이로드 | `PLAYER\|Alice\|85\|50\|100\|42` |

인접 존 기준: 맨해튼 거리 <= 1 (자기 자신 제외)

## 알려진 제약사항

- **gateway_t::recv() 버그**: zlink 코어 라이브러리의 `gateway.cpp`에서
  `zlink_msg_recv()` 반환값(성공 시 메시지 크기, 양수)을 `rc != 0`으로 검사하여
  정상 수신이 실패로 처리됩니다. 이 샘플에서는 `zlink_gateway_router()` C API로
  ROUTER 소켓에 직접 접근하는 방식으로 우회합니다.

- **Gateway 연결 풀 초기화**: Gateway의 서비스 풀은 첫 `send()` 또는
  `connection_count()` 호출 전까지 생성되지 않습니다. 프론트 서버의 메인 루프에서
  `connection_count()`를 주기적으로 호출하여 내부 리프레시 스레드가 API 서버에
  연결을 수립할 수 있도록 합니다.

## 트러블슈팅

- **포트 충돌**: CLI 인자로 포트를 조정할 수 있습니다 (예: `--port`, `--spot-port`,
  `--pub`, `--router`). 5개 프로세스 모두 커맨드라인 오버라이드를 지원합니다.
- **SPOT 구독 지연**: `run_local.sh` 스크립트는 서버 기동 후 SPOT 피어 디스커버리
  완료를 위해 대기 시간을 포함합니다.
- **vcpkg 문제**: overlay-ports 경로가 올바른지 확인하세요. `<repo-root>/vcpkg/ports`를
  가리켜야 `zlink` 포트를 찾을 수 있습니다.
- **zlink.hpp 빌드 오류**: CMake 파일은 `<repo-root>/bindings/cpp/include/zlink.hpp`
  경로를 기대합니다. 리포지토리 안에서 빌드해야 상대 경로가 올바르게 해석됩니다.
- **LD_LIBRARY_PATH**: 빌드된 바이너리 실행 시
  `bindings/cpp/native/linux-x86_64/`을 `LD_LIBRARY_PATH`에 추가해야 합니다.
  `run_local.sh`는 이를 자동으로 처리하지 않으므로, 환경 변수를 직접 설정하거나
  `LD_LIBRARY_PATH`를 export 한 후 실행하세요.

## 파일 구조

```
mmorpg_dual_server/
  CMakeLists.txt              빌드 설정
  vcpkg.json                  vcpkg 매니페스트 (asio + zlink)
  README.md                   이 파일
  common/
    app_protocol.hpp/.cpp     파이프 구분 메시지 파싱/생성
    raw_framing.hpp/.cpp      길이 프리픽스 TCP 프레이밍
    ids.hpp/.cpp              요청 ID 생성, 라우팅 ID hex 변환
    zone_math.hpp             존 좌표, 경계 판정, 맨해튼 거리
  registry/
    main.cpp                  Registry 프로세스 엔트리포인트
  api_server/
    main.cpp                  API 서버 엔트리포인트
    api_service.hpp/.cpp      더미 아웃게임 API 핸들러
  front_server/
    main.cpp                  프론트 서버 엔트리포인트
    front_server.hpp/.cpp     서버 오케스트레이션 및 폴 루프
    stream_ingress.hpp/.cpp   STREAM 소켓 + 세션 관리
    zone_service.hpp/.cpp     존 상태 (입장/이동/퇴장)
    outgame_gateway.hpp/.cpp  Gateway 클라이언트 (API 포워딩)
    spot_sync.hpp/.cpp        SPOT pub/sub (인접 존 동기화)
  raw_client/
    main.cpp                  클라이언트 엔트리포인트
    asio_client.hpp/.cpp      Asio TCP 커넥션 (프레이밍 포함)
    scenario_runner.hpp/.cpp  멀티 페이즈 시나리오 실행 엔진
    console_display.hpp/.cpp  클라이언트별 컬러/라벨 콘솔 출력
  scripts/
    run_local.sh              서버 기동/종료/시나리오 오케스트레이션
```
