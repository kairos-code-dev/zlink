# C++ 샘플 구현 계획 v2: Raw TCP Client + Front Zone + API Backend + Gateway + SPOT

> 상태: 계획 수립  
> 마지막 업데이트: 2026-02-10  
> 대상 디렉토리: `samples/cpp/mmorpg_dual_server/`  
> 핵심 목표: 배포된 `libzlink` + `bindings/cpp/include/zlink.hpp` 기반 통합 샘플 제공

## 1. 요청 반영 요약

- 반영: 클라이언트는 raw TCP 소켓(Asio) 사용
- 반영: 프론트 서버는 zlink `STREAM` 소켓으로 클라이언트 수신
- 반영: 서버 간 통신은 `Gateway -> Receiver` 구조 사용
- 반영: `SPOT`으로 MMORPG 인접 zone 동기화 시연
- 반영: 서버는 Front(ingame) + API(outgame) 2계층 역할로 구성

## 2. 목표와 비목표

### 2.1 목표

- zlink 서비스 기능(Registry, Discovery, Gateway, Receiver, SPOT, STREAM)을 하나의 완주형 시나리오로 시연한다.
- 클라이언트 요청이 raw TCP로 들어와 Front에서 처리되고, outgame 요청은 API 서버로 전달되는 E2E를 제공한다.
- Front 인스턴스 2개를 실행하여 zone 경계 인접 동기화를 SPOT으로 보여준다.
- `README` 기준 10분 내 로컬 실행이 가능해야 한다.

### 2.2 비목표

- 영속 DB, 인증/권한, 결제 등 상용 백엔드 완성도
- 대규모 클러스터 운영 자동화(오토스케일링/HA)
- 고급 LB 정책 검증(Weighted 튜닝)

## 3. 왜 이 구조가 적합한가

`STREAM + Gateway + SPOT`을 한 번에 보여주려면, 단일 서버보다 역할 분리가 명확한 구성이 필요하다.

- Front 서버: 외부(raw TCP) 진입점 + zone 처리
- API 서버: outgame 전용 Receiver
- Registry: Discovery/서비스 등록 허브
- Front 2인스턴스: SPOT 인접 zone 동기화의 최소 조건 충족

이 구조는 실제 게임 서버 아키텍처(게이트/존/백엔드)와 유사하고, zlink의 핵심 기능을 기능별로 명확하게 관찰할 수 있다.

## 4. zlink 서비스 기능 인벤토리 (샘플 반영 범위)

| 영역 | 핵심 API (C++) | 샘플에서의 용도 |
|---|---|---|
| Registry | `registry_t::set_endpoints`, `start` | 서비스 등록 허브 |
| Discovery | `discovery_t::connect_registry`, `subscribe`, `service_available` | Front가 API/Spot 서비스 발견 |
| Gateway | `gateway_t::send`, `recv`, `connection_count` | Front -> API outgame 요청/응답 |
| Receiver | `receiver_t::bind`, `connect_registry`, `register_service`, `router_handle` | API 서버 RPC 처리 |
| SPOT Node | `spot_node_t::bind`, `connect_registry`, `register_service`, `set_discovery` | Front 간 피어 자동 연결 |
| SPOT | `spot_t::publish`, `subscribe`, `subscribe_pattern`, `recv` | zone 상태/이벤트 동기화 |
| STREAM | `socket_t(ctx, socket_type::stream)` | Front의 raw TCP 클라이언트 수신 |

## 5. C++ 바인딩 확인 결과와 설계 반영

### 5.1 C++ 바인딩에서 바로 사용 가능한 항목

- `registry_t`, `discovery_t`, `gateway_t`, `receiver_t`, `spot_node_t`, `spot_t`, `socket_t`
- `poller_t`로 socket polling 가능
- `socket_t::wrap()`로 내부 핸들 wrapping 가능

### 5.2 C API fallback이 필요한 항목

| 필요 기능 | C++ wrapper 노출 | 대응 |
|---|---|---|
| `zlink_gateway_send_rid` | 없음 | `gateway.handle()` 기반 C API 호출 |
| `zlink_gateway_router` | 없음 | `zlink_gateway_router(gateway.handle())` 후 `socket_t::wrap()` |

MVP에서는 `gateway_t::recv(..., dontwait)` 폴링 루프로 시작하고, 필요 시 C API fallback을 2차 적용한다.

## 6. STREAM/raw TCP 프로토콜 기준

### 6.1 외부 클라이언트 wire format

- raw TCP 프레임: `[len_be32][payload]`
- `len_be32`는 4바이트 Big Endian
- 클라이언트(Asio)는 `read_exact(4)` -> `read_exact(len)` 패턴 사용

### 6.2 Front 내부 STREAM 처리 형식

- STREAM 수신은 항상 2프레임: `[routing_id(4)][payload]`
- 연결 이벤트: payload `0x01`
- 해제 이벤트: payload `0x00`
- 일반 메시지: payload N바이트

### 6.3 세션 처리 원칙

- `routing_id(4B)`를 session key로 사용
- connect 이벤트에서 session 생성
- disconnect 이벤트에서 session/pending/outstanding 정리

## 7. 샘플 아키텍처

```text
Raw TCP Client (Asio)
        |
        v
  [front-server A] <==== SPOT (adjacent zone sync) ====> [front-server B]
      |      ^                                           ^      |
      |      +---- Discovery(spot) + spot_node/spot -----+      |
      |
      +---- Discovery(gateway) -> Gateway ----+
                                               |
                                               v
                                       [api-server Receiver]
                                               |
                                               v
                                           [registry]
```

### 7.1 실행 단위

- `sample-registry`
- `sample-front-server` (동일 바이너리 2인스턴스 실행)
- `sample-api-server`
- `sample-raw-client` (Asio raw TCP 클라이언트)

### 7.2 서버 역할

- Front 서버:
  - STREAM ingress
  - 세션 관리
  - 간단 zone 상태 처리(이동/공격/입장/퇴장)
  - zone 이벤트를 SPOT publish
  - 인접 zone topic subscribe 후 로컬 캐시 반영
  - outgame 명령은 API 서버로 Gateway 전달
- API 서버:
  - Receiver 서비스 `outgame.api`
  - 프로필/인벤토리/메일 카운트 등 더미 데이터 응답

## 8. 시나리오 설계

### 8.1 클라이언트 명령

- `LOGIN <user>`
- `JOIN <zone>`
- `MOVE <x> <y>`
- `ATTACK <target>`
- `OUTGAME PROFILE`
- `OUTGAME INVENTORY`
- `PING`

### 8.2 흐름 1: ingame (Front 내부 처리)

1. raw TCP client -> Front(STREAM) payload 전송
2. Front가 zone state 처리
3. 결과를 동일 session(routing_id)로 응답

### 8.3 흐름 2: outgame (Front -> API)

1. client가 `OUTGAME *` 명령 전송
2. Front가 `request_id` 발급, pending map 저장
3. Front Gateway -> API Receiver 요청 전송
4. API Receiver 응답
5. Front가 `request_id -> routing_id` 매칭 후 client로 응답

### 8.4 흐름 3: zone 인접 동기화 (SPOT)

1. Front A/B가 각각 zone topic publish
2. 각 Front는 자신이 담당한 zone 기준 Manhattan 거리 1 이내 topic subscribe
3. 반대 노드에서 publish된 인접 zone 이벤트를 수신해 shadow state 갱신

인접 기준은 테스트 예제와 동일하게 `manhattan <= 1`을 사용한다.

## 9. 메시지 설계

### 9.1 클라이언트-Front payload (문자열 기반, 길이프리픽스 외부 framing)

- 요청: `REQ|<req_id>|<op>|<arg1>|...`
- 응답: `RES|<req_id>|OK|<body>`
- 에러: `RES|<req_id>|ERR|<reason>`
- 이벤트(push): `EVT|<topic>|<body>`

### 9.2 Front-API Gateway multipart

- part0: `op`
- part1: `req_id`
- part2: `session_id` (hex routing_id)
- part3: `payload`

### 9.3 SPOT topic/payload

- topic 예시: `field:<zone_x>:<zone_y>:state`
- payload 예시: `PLAYER|<name>|<x>|<y>|<hp>|<tick>`

## 10. 디렉토리/파일 구조 계획

```text
samples/
  cpp/
    mmorpg_dual_server/
      CMakeLists.txt
      README.md
      common/
        app_protocol.hpp
        app_protocol.cpp
        raw_framing.hpp
        raw_framing.cpp
        ids.hpp
        ids.cpp
        zone_math.hpp
      registry/
        main.cpp
      front_server/
        main.cpp
        front_server.hpp
        front_server.cpp
        stream_ingress.hpp
        stream_ingress.cpp
        zone_service.hpp
        zone_service.cpp
        outgame_gateway.hpp
        outgame_gateway.cpp
        spot_sync.hpp
        spot_sync.cpp
      api_server/
        main.cpp
        api_service.hpp
        api_service.cpp
      raw_client/
        main.cpp
        asio_client.hpp
        asio_client.cpp
      scripts/
        run_local.sh
```

## 11. 구현 단계

### 단계 0: 빌드 스캐폴딩

- 샘플 전용 `CMakeLists.txt` 작성
- `libzlink`, `zlink.hpp`, Asio 의존 연결
- 바이너리 4종 빌드 확인

완료 기준:

- 빈 `main.cpp` 기준 컴파일 통과

### 단계 1: Registry + API Receiver 최소 경로

- `sample-registry` 구현
- `sample-api-server` 구현
  - `receiver.bind`, `connect_registry`, `register_service("outgame.api")`
  - `register_result` 확인 로그

완료 기준:

- API 서비스가 registry에 정상 등록됨

### 단계 2: Front STREAM ingress + 세션 관리

- `STREAM` 소켓 bind (`tcp://127.0.0.1:7001`, `7002`)
- connect/disconnect 이벤트 처리
- `routing_id -> session` 테이블 구현
- 클라이언트 요청 파싱/기본 응답 처리(PING)

완료 기준:

- Asio client로 접속/해제/핑 왕복 확인

### 단계 3: Front -> API Gateway 연동

- Front에 `discovery_t(service_type::gateway)` + `gateway_t` 추가
- `outgame.api` subscribe 후 가용 대기
- `OUTGAME *` 명령을 API 서버로 전달
- pending map(`req_id -> routing_id`) 기반 응답 라우팅

완료 기준:

- OUTGAME 요청의 E2E 왕복 성공

### 단계 4: Front 간 SPOT 인접 zone 동기화

- Front별 `spot_node_t` bind/register
- `discovery_t(service_type::spot)` + `set_discovery` 설정
- zone ownership/adjacency 계산
- 인접 topic subscribe + publish/recv loop

완료 기준:

- Front A에서 발생한 zone 이벤트가 Front B 인접 zone에만 반영됨

### 단계 5: 통합 시나리오/운영 스크립트

- `scripts/run_local.sh`로 5개 프로세스 실행 순서 제공
- `README`에 시나리오/예상 로그/트러블슈팅 정리

완료 기준:

- 문서만 따라 10분 내 재현 가능

## 12. 테스트 계획

### 12.1 기능 테스트

- STREAM 세션 라이프사이클
  - connect(0x01) 시 session 생성
  - disconnect(0x00) 시 정리
- Gateway outgame
  - 정상 응답
  - 타임아웃/미매칭 req_id 처리
- SPOT 동기화
  - Manhattan 인접 zone만 수신
  - 비인접 zone 수신 금지

### 12.2 통합 시나리오

1. Front A/B, API, Registry 실행
2. Client 1 -> Front A에서 `JOIN`, `MOVE`
3. Client 2 -> Front B에서 인접 zone 이벤트 확인
4. Client 1이 `OUTGAME PROFILE` 요청
5. API 응답이 Front A를 통해 원 client로 반환되는지 확인

### 12.3 부하/안정성 (샘플 범위)

- 동시 20~50 클라이언트 `PING`/`OUTGAME` 반복
- pending map 누수 여부 확인
- SPOT 이벤트 처리 지연/유실 여부 확인

## 13. 리스크와 대응

### 리스크 1: Gateway 수신 폴링 복잡도

- 원인: C++ wrapper에 gateway router handle 직접 노출 없음
- 대응:
  - 1차: `gateway.recv(..., dontwait)` 주기 폴링
  - 2차: 필요 시 C API `zlink_gateway_router` + `socket_t::wrap` 전환

### 리스크 2: raw framing 구현 오류

- 원인: 길이프리픽스 파싱/부분 수신 처리 미흡
- 대응:
  - Asio `read`/`write` 정확한 바이트 수 보장 사용
  - `max_payload_size` 가드(예: 64KB) 추가

### 리스크 3: SPOT 구독 전파 지연

- 원인: 멀티노드에서 subscribe 전파 settle 시간 필요
- 대응:
  - publish 전 settle delay
  - 초기 warm-up 단계 별도 두기

### 리스크 4: 문서-코드 불일치

- 대응:
  - README 예시는 실제 실행 스크립트 출력에서 생성
  - PR 시 실행 로그를 같이 점검

## 14. 완료 기준 (DoD)

- `samples/cpp/mmorpg_dual_server/` 빌드 성공
- raw TCP Asio client <-> Front STREAM 요청/응답 성공
- Front -> API Gateway outgame 왕복 성공
- Front A/B 간 SPOT 인접 zone 동기화 성공
- README/스크립트 기반 재현 가능

## 15. 확장 계획 (후속)

- TLS/WSS STREAM 클라이언트 샘플 추가
- `gateway_send_rid` 기반 특정 API 인스턴스 라우팅 샘플
- Spot topic 모니터링/메트릭(throughput, lag) 추가

## 16. 독립 실행(Standalone) 보장 계획

이 샘플은 zlink 전체 저장소 빌드 결과에 종속되지 않고, 샘플 디렉토리 단독으로 빌드/실행 가능해야 한다.

### 16.1 독립 실행의 정의

- 샘플 소스(`samples/cpp/mmorpg_dual_server`)만 별도 디렉토리로 복사해도 동작
- 외부 의존성은 다음 3가지만 허용
  - C++ 컴파일러/CMake
  - OpenSSL 런타임(플랫폼 기본 또는 설치)
  - zlink 배포 산출물(`zlink.hpp`, `zlink.h`, `libzlink`)

### 16.2 zlink C++ 라이브러리 수급 경로 (우선순위)

1. 저장소 내 사전 배포 바이너리 사용 (개발/데모 최우선)
   - 헤더: `bindings/cpp/include/zlink.hpp`
   - 네이티브 라이브러리: `bindings/cpp/native/<platform>/`
     - `linux-x86_64/libzlink.so`
     - `linux-aarch64/libzlink.so`
     - `darwin-x86_64/libzlink.dylib`
     - `darwin-aarch64/libzlink.dylib`
     - `windows-x86_64/zlink.dll`
     - `windows-aarch64/zlink.dll`
   - 근거: `doc/bindings/cpp.md`

2. GitHub Release 자산 사용 (배포/CI 권장)
   - 릴리즈 자산에 플랫폼별 아카이브 + source tarball 제공
   - source tarball 예시 패턴:
     - `https://github.com/ulala-x/zlink/archive/refs/tags/<TAG>.tar.gz`
   - 근거:
     - `doc/building/packaging.md`
     - `core/packaging/conan/README.md`
     - `core/packaging/conan/conandata.yml`

3. Conan 패키지 경유 (패키지 매니저 통합 시)
   - 레시피 위치: `core/packaging/conan/conanfile.py`
   - 소스는 GitHub Release tarball 기준으로 가져오도록 설계됨

### 16.3 독립 샘플의 필수 파일 세트

샘플 배포 번들(`third_party/zlink/`)에 아래를 포함한다.

- `include/zlink.hpp` (C++ wrapper)
- `include/zlink.h` (C API 헤더)
- `lib/libzlink.so | libzlink.dylib | zlink.dll` (플랫폼별)
- Windows인 경우 import library(`zlink.lib`)도 함께 제공

### 16.4 CMake 독립 탐색 규칙

샘플 CMake에 다음 우선순위의 탐색 규칙을 둔다.

1. `-DZLINK_ROOT=/path/to/zlink-bundle` 명시 경로
2. 환경변수 `ZLINK_ROOT`
3. 샘플 내부 기본 경로 `third_party/zlink`

검증 실패 시 에러 메시지에 누락 파일(`zlink.hpp`, `zlink.h`, `libzlink`)을 구체적으로 출력한다.

### 16.5 문서/스크립트 추가 항목

- `samples/cpp/mmorpg_dual_server/README.md`에 다음 섹션을 추가
  - "빠른 시작: 저장소 내 native 바이너리 사용"
  - "GitHub Release에서 라이브러리 받기"
  - "Conan으로 라이브러리 준비"
- `scripts/`에 의존성 준비 스크립트 추가
  - 예: `scripts/prepare_zlink_dep.sh`
  - 역할: 플랫폼 감지 후 `third_party/zlink/` 구조 생성

### 16.6 범위 주의사항

- 문서(`doc/building/packaging.md`)에 vcpkg overlay 항목이 있으나, 현재 저장소에는 해당 디렉토리가 없다.
- 따라서 본 샘플의 1차 지원 수급 경로는 `native 번들`, `GitHub Release`, `Conan`으로 고정한다.

## 17. 참고 소스

- `bindings/cpp/include/zlink.hpp`
- `bindings/cpp/tests/test_cpp_discovery_gateway_spot.cpp`
- `core/include/zlink.h`
- `doc/internals/protocol-raw.md`
- `doc/guide/03-5-stream.md`
- `core/tests/spot/test_spot_pubsub_basic.cpp`
- `core/tests/spot/test_spot_pubsub_scenario.cpp`
