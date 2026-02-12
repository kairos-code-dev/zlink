# Router-Router ZMQ Parity 작업 계획

## 목적

PlayHouse에서 관측된 `Host unreachable`/연결 타이밍 편차를 줄이기 위해,
`tcp://` 기반 Router-Router 경로를 libzmq 4.3.5 동작에 최대한 가깝게 정렬한다.

핵심 목표:

- connect/ready 관련 monitor 이벤트 시맨틱 정렬
- reconnect/disconnect 처리의 예외 케이스 축소
- zlink-vs-zmq 비교 시나리오에서 차이 재현/감지 가능 상태 유지

## 범위

### 포함

- `core/src/transports/tcp/asio_tcp_connecter.cpp`
- `core/src/transports/ws/asio_ws_connecter.cpp`
- `core/src/transports/tls/asio_tls_connecter.cpp`
- `core/src/transports/ipc/asio_ipc_connecter.cpp`
- `core/src/transports/ws/asio_ws_engine.cpp`
- `core/tests/scenario/router_router/*` (검증 지표/문서/비교 스크립트)

### 제외

- API 시그니처 변경
- 신규 소켓 타입 추가
- 기존 시나리오 외 벤치 프레임워크 구조 변경

## 구현 단계

### 1) 문서/검증 기반 정리

- zlink/zmq 동일 러너에서 비교 가능한 지표를 통일:
  - `connected`
  - `connection_ready`
  - `connect_delayed` (`zero` / `nonzero`)
  - `connect_retried`, `disconnected`, `handshake_failed`
  - `ready_wait_ms`, `connect_to_ready_ms`
- `run_connect_diff_check.sh`로 반복 실행/요약 자동화.

완료 기준:

- `metrics.csv`, `SUMMARY.txt` 생성
- 지표 해석 문서화 완료

### 2) connecter 이벤트 시맨틱 정렬

대상:

- `asio_tcp_connecter.cpp`
- `asio_ws_connecter.cpp`
- `asio_tls_connecter.cpp`
- `asio_ipc_connecter.cpp`

작업:

- `event_connect_delayed(..., 0)` 고정값 제거
- 플랫폼별 delayed errno 성격값 사용:
  - POSIX: `EINPROGRESS`
  - Windows: `WSAEWOULDBLOCK` 또는 `WSAEINPROGRESS`
- reconnect timer/monitor 흐름은 기존 회귀 없이 유지

완료 기준:

- zlink monitor에서 `connect_delayed_nonzero` 관측 가능
- 기존 scenario pass 유지

### 3) WS 엔진 에러 경로 정렬

대상:

- `asio_ws_engine.cpp`

작업:

- `engine_error` 호출 시 handshake 완료 여부 전달 정렬 (`false` 고정 제거)
- disconnect monitor 이벤트 누락/중복 여부 점검 및 보강
- routing-id 전달 단계의 강한 assert 경로 점검(EAGAIN 허용 경로 유지)

완료 기준:

- reconnect 중 monitor `disconnected`/`handshake_failed` 집계 일관성 확보
- ws 경로에서 비정상 assert 미발생

### 4) 시나리오 기반 회귀 검증

필수 검증:

1. `ctest --test-dir core/build --output-on-failure -R "test_scenario_router_router|test_scenario_router_router_zlink_connect"`
2. `core/tests/scenario/router_router/run_connect_diff_check.sh`
   - 기본: `REPEATS=5`
   - 고강도: `REPEATS=10 SIZE=1024 CCU=10000 INFLIGHT=10`

판정:

- 시나리오 실패(`rc != 0`) 없음
- zlink `connect_retried/disconnected/handshake_failed` 합계가 0 또는 기존 대비 악화 없음
- `connect_to_ready_ms`가 zmq 대비 비정상적으로 커지지 않음
  - `RATIO_LIMIT`, `ABS_LIMIT_MS` 기준

## 리스크와 대응

- 리스크: connect 이벤트 값 변경으로 기존 로그 파서 영향 가능
  - 대응: `connect_delayed_zero/nonzero` 분리 문서화 및 CSV 컬럼 유지
- 리스크: ws 에러 처리 경로 수정 시 콜백 레이스
  - 대응: 기존 종료 순서 유지, 중복 이벤트 가드 추가
- 리스크: 환경별 libzmq 경로 차이
  - 대응: `LIBZMQ_INCLUDE`, `LIBZMQ_LIBDIR` 환경변수로 오버라이드

## 최종 산출물

- 코드 변경(diff)
- `router_router` 비교 결과 로그(`metrics.csv`, `SUMMARY.txt`)
- 최종 검증 명령 및 결과 요약

## 진행 현황 (2026-02-13)

완료:

- connecter `CONNECT_DELAYED` 값 parity 반영
  - `core/src/transports/tcp/asio_tcp_connecter.cpp`
  - `core/src/transports/ws/asio_ws_connecter.cpp`
  - `core/src/transports/tls/asio_tls_connecter.cpp`
  - `core/src/transports/ipc/asio_ipc_connecter.cpp`
- WS engine 에러/ready 경로 parity 보강
  - `core/src/transports/ws/asio_ws_engine.cpp`
- zlink/zmq 동일 시나리오 비교 러너 및 요약 스크립트 정비
  - `core/tests/scenario/router_router/run_connect_diff_check.sh`
  - `core/tests/scenario/router_router/zlink-connect/test_scenario_router_router_zlink_connect.cpp`
  - `core/tests/scenario/router_router/zmq/libzmq_native_router_router_bench.cpp`

검증 결과 요약:

- 반복 비교(`REPEATS=20`, `size=1024`, `ccu=10000`, `inflight=10`, `hwm=1000000`)
  - `overall=PASS`
  - 실패 run: `0`
  - `Host unreachable`: zlink/zmq 모두 `0`
  - `connect_retried/disconnected/handshake_failed`: zlink/zmq 모두 `0`
- 남은 관측 차이:
  - `self=0`에서 zlink `connect_to_ready_ms`가 `0/2ms`로 분포하고
    zmq는 `0ms` 비중이 더 높음.
  - 절대값 기준으로는 수 ms 범위이며, 기능 실패/붕괴 패턴은 재현되지 않음.
- 조기송신 검증(`WARMUP_MODE=none`, `REPEATS=10`) 결과:
  - zlink/zmq 모두 `Host unreachable` 발생(ready gate 제거 시 공통 재현)
  - zlink fail run: `9/20` (`self0=3`, `self1=6`)
  - zmq fail run: `14/20` (`self0=5`, `self1=9`)
  - reconnect/disconnect/handshake_failed 이벤트는 양쪽 모두 `0`

해석:

- 기존 `Host unreachable`의 핵심 원인은 연결 직후(ready 이전) 조기 송신일 가능성이 높음.
- 현재 시나리오 기준에서는 disconnect/reconnect 불안정이 직접 원인으로 보이지 않음.

## 남은 정밀 parity 후보

1. 계측 정밀도 개선
- monitor 시각을 ms에서 us로 확장해 `connect_to_ready` 양자화(0/2ms) 노이즈를 축소.

2. 워밍업 없음 조기송신 비교 시나리오 추가
- ready gate 없이 시작해 초기 N개 송신 실패율(`EHOSTUNREACH`)을 zlink/zmq 비교.
- 조기송신 민감도 차이를 정량화해 실제 통합 경로 재현성을 높임.

3. connecter delayed 이벤트 조건 정밀화 검토
- libzmq는 `connect()`가 즉시 성공하면 `CONNECT_DELAYED`를 내지 않음.
- ASIO 경로에서도 즉시완료 케이스에 대한 delayed 이벤트 과다 발행 여부를 확인.
