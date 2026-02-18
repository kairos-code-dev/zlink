# Codex STREAM Socket 성능 개선 실행 계획서

- 작성일: 2026-02-17
- 대상 모듈:
  - `core/src/sockets/stream.*`
  - `core/src/engine/asio/*`
  - `core/src/transports/*/asio_*`
  - `core/src/transports/tcp/tcp.*`
  - `core/src/api/zlink.cpp`
  - `core/include/zlink.h`
  - `core/src/core/options.*`
  - `core/tests/test_stream_*`
  - `core/tests/scenario/stream/*`
- 입력 근거 문서:
  - `doc/study/cgdk10-architecture-analysis.md`
  - `doc/study/cppserver-architecture-analysis.md`

## 1) 목적과 범위

### 목적
`ZLINK_STREAM`의 소형 메시지(64B/1KB) 구간 throughput과 tail latency를 개선한다.
핵심 방향은 다음 조합이다.

1. CppServer형 송신 경로 최적화(`main/flush` 배치, 불필요한 큐잉/할당 제거)
2. CGDK10형 복사/할당 최소화(가능한 경로는 zero-copy/gather 우선)
3. STREAM fastpath를 C API와 socket option으로 노출

### 범위(In Scope)

1. TCP/IPC/TLS/WS STREAM 엔진 경로 정렬 및 A/B 게이트
2. STREAM 송수신 fastpath 노출(C API + sockopt)
3. backpressure 구간 복사/메모리 churn 완화
4. 성능 측정 환경 고정 및 CI 성능 게이트 정착

### 범위(Out of Scope)

1. ZMP 라우팅 프로토콜 자체 변경
2. 신규 전송 프로토콜 추가(io_uring 등)
3. 대규모 아키텍처 교체(완전 신규 엔진 도입)

## 2) 현재 상태 진단 (코드 근거)

### 핵심 진단 요약

1. TCP `STREAM` listener는 `asio_stream_engine_t`가 아니라 `asio_raw_engine_t`를 사용 중이다.
2. TCP connect 측 `STREAM` 경로는 현재 비활성화되어 있다.
3. `stream_t::xsend()` fastpath는 "단일 프레임 + `routing_id != 0` + multipart 아님"일 때만 진입한다.
4. `stream_t::xrecv()` single-frame fastpath는 현재 env(`ZLINK_STREAM_SINGLE_FRAME_RECV`) 기반이다.
5. `TCP_NODELAY`는 현재 내부에서 하드코딩된 ON(1)이며 공개 옵션으로 제어할 수 없다.

### 근거 표

| 항목 | 근거 | 영향 |
|---|---|---|
| TCP STREAM이 raw 엔진 사용 | `core/src/transports/tcp/asio_tcp_listener.cpp:351` | TCP에서 stream 엔진 최적화 경로 미사용 |
| TCP STREAM connect 비활성 | `core/src/transports/tcp/asio_tcp_connecter.cpp:395` | client 측 STREAM 경로/비교 불가 |
| `xsend` fastpath 조건 | `core/src/sockets/stream.cpp:89` | C API 연동 시 조건 만족 설계 필요 |
| recv fastpath env 의존 | `core/src/sockets/stream.cpp:193` | 운영 제어성/가시성 낮음 |
| `TCP_NODELAY` 기본 ON | `core/src/transports/tcp/tcp.cpp:30` | 벤치 스크립트 값이 실제 적용되지 않음 |

## 3) 성능 목표(KPI)

### 정량 KPI (Phase 0 baseline 이후 확정)

1. 신뢰성 게이트:
   - `parse_error`, `protocol_error`, `send_error` = 0
   - 기능 테스트 100% 통과
2. 성능 게이트:
   - 기본 목표: 소형 메시지 워크로드에서 아래 중 최소 1개 달성
   - `throughput +10%` 이상 또는 `p99 latency -15%` 이상 개선
3. 비회귀 게이트:
   - 어떤 워크로드에서도 `throughput -3%` 초과 하락 금지
   - 어떤 워크로드에서도 `p99 latency +5%` 초과 악화 금지
4. 통계 기준:
   - 반복 측정 중앙값 + p50/p99/p99.9 비교
   - 노이즈 밴드(반복 변동폭) 밖의 개선만 "유의미"로 판정

### 측정 기준

1. 측정 스크립트: `core/tests/scenario/stream/run_stream_compare.sh`
2. 워크로드 축:
   - 메시지 크기: `64`, `1024`, `65536`
   - 혼합: `64B 80% + 1KB 15% + 64KB 5%`
   - 동시성: `CCU=1000`, `CCU=10000` (환경 미충족 시 대체값 명시)
3. 반복/수집:
   - warm-up 15s + 측정 60s, 최소 7회 반복
   - 중앙값 기준 판정
4. 비교군 사용 방식:
   - 성능 게이트 판정은 `zlink` A/B 중심
   - `asio/cppserver/cgdk10/dotnet`은 참고 비교(게이트 판정 제외)
5. 환경 고정 항목:
   - CPU governor, 코어 pinning, `ulimit -n`, `sysctl`, 실행 호스트 정보 기록

## 4) 구현 전략 (Phase 0~5)

## Phase 0. Baseline 고정 + 측정 인프라 잠금

### 작업

1. 현재 HEAD에서 STREAM baseline 확보.
2. 결과를 `doc/plan/baseline` 하위 산출물(csv/json/명령행)로 저장.
3. 벤치 환경 스냅샷(하드웨어/커널/튜닝/제한값) 문서화.
4. PR에서 실행 가능한 경량 성능 게이트 뼈대(`zlink`, 핵심 size) 먼저 추가.

### 완료 기준

1. baseline 재현 커맨드/파라미터가 고정됨.
2. 성능 비교 자동화가 PR에서 동작함.
3. CCU=10000 가능/불가 사유가 기록되고 대체 기준이 정의됨.

## Phase 0.5. 병목 프로파일링

### 작업

1. `perf stat`/`perf record` 기반 CPU 병목 추적.
2. 할당/복사 hotspot(송신/수신/백프레셔 경로) 식별.
3. 엔진 전환 가설(raw↔stream)이 타당한지 코드+측정으로 검증.

### 완료 기준

1. 상위 병목 3개 이상이 함수 단위로 식별됨.
2. Phase 1/2의 우선순위가 프로파일 결과 기반으로 조정됨.

## Phase 1. TCP STREAM 엔진 경로 A/B 게이트 구축

### 결정

1. TCP STREAM 기본 엔진은 즉시 전환하지 않는다.
2. `asio_raw_engine_t`와 `asio_stream_engine_t`를 TCP에서 모두 선택 가능하게 만든다.
3. TCP connect STREAM 비활성 분기를 제거해 listener/connecter 모두 비교 가능하게 만든다.
4. 엔진 전환 전, raw 엔진 선택 이력(commit/blame)을 확인해 의도/제약을 문서화한다.

### 변경 파일

1. `core/src/transports/tcp/asio_tcp_listener.cpp`
2. `core/src/transports/tcp/asio_tcp_connecter.cpp`
3. 필요 시 `core/src/core/options.*` (엔진 선택 옵션)

### 구현 상세

1. listener path:
   - `options.type == ZLINK_STREAM`일 때 엔진 선택값(`raw|stream`)에 따라 분기.
2. connecter path:
   - `options.type == ZLINK_STREAM` 즉시 종료 분기 제거.
   - connect 성공 후 listener와 동일한 엔진 선택 분기 적용.
3. interop 검증:
   - `raw listener ↔ stream connecter`
   - `stream listener ↔ raw connecter`
4. 선택 정책:
   - 기본값 `raw` 유지.
   - 게이트 통과 후 default 전환.
   - 문제 시 즉시 `raw` 롤백 가능한 flag 유지.
5. 회귀 범위:
   - WS/IPC/TLS 기존 경로 회귀 테스트를 Phase 1 완료 조건에 포함.

### 완료 기준

1. TCP bind/connect 모두에서 STREAM 연결이 정상 동작.
2. `raw`/`stream` 엔진을 동일 시나리오로 비교 가능.
3. interop 매트릭스 통과.
4. A/B 결과 보고서(워크로드별 throughput/latency/error + 환경 정보) 확보.

## Phase 2. 엔진별 최적화 (A/B + 워크로드 기반)

### 결정

1. 엔진 우열은 단일 점수가 아니라 워크로드별로 판단한다.
2. 배포 타깃 워크로드에서 효과가 확인된 경로를 우선 최적화한다.
3. 기존 튜닝 이력(예: gather threshold 8192)을 먼저 검증하고 중복 작업을 피한다.

### 변경 파일

1. raw 경로 우선 시:
   - `core/src/engine/asio/asio_engine.cpp`
   - `core/src/transports/tcp/tcp_transport.cpp`
2. stream 경로 우선 시:
   - `core/src/engine/asio/asio_stream_engine.cpp`
   - `core/src/engine/asio/asio_stream_engine.hpp`

### 구현 상세

1. 공통:
   - 핫 경로 복사/할당 제거
   - 변경 즉시 벤치마크/회귀 검증
2. raw 경로:
   - backpressure pending buffer 복사/재할당 빈도 완화
   - 기존 `handler_allocator.hpp` 재사용 적용 검토
3. stream 경로:
   - `process_input_buffer()` memmove 빈도 절감(오프셋 기반)
   - `push_one_frame()` 복사 경로 개선(안전성 우선)
   - 미사용 멤버 정리(`_tx_msg_valid` 등)
4. 메모리/동시성 안정성:
   - HWM 도달 시 RSS 상한 관측 테스트 추가
   - ThreadSanitizer 또는 동등 스트레스 검증 추가
5. gather threshold:
   - 기본 8192 유지, 재조정은 프로파일 근거가 있을 때만 수행

### 완료 기준

1. KPI 목표 또는 비회귀 게이트 충족.
2. EAGAIN/partial write/backpressure 안정성 유지.
3. 메모리 상한 및 동시성 안정성 검증 통과.

## Phase 3. STREAM fastpath C API 노출 (ABI 고려)

### 결정

1. `msg_t` routing id 설정 API를 C API에 추가한다.
2. 기존 multipart(`routing_id` 프레임 + payload) 동작은 그대로 유지한다.
3. ABI 호환 정책(버전/심볼)과 함께 배포한다.

### 공개 API 추가안

1. `core/include/zlink.h`:
   - `ZLINK_EXPORT int zlink_msg_set_routing_id (zlink_msg_t *msg_, uint32_t routing_id_);`
   - `ZLINK_EXPORT uint32_t zlink_msg_get_routing_id (const zlink_msg_t *msg_);`
2. `core/src/api/zlink.cpp`:
   - 위 API 구현(`msg_t::set_routing_id`, `msg_t::get_routing_id` 연결)

### 구현 상세

1. fastpath 진입 조건을 문서/테스트에 명시:
   - `!(msg->flags & more)`
   - `routing_id != 0`
2. 구버전/신버전 상호 운용 테스트 매트릭스 추가:
   - old-send/new-recv
   - new-send/old-recv
   - old-send/old-recv
   - new-send/new-recv
3. 심볼/버전 정책:
   - API 추가 시 버전 정책(최소 minor 증가)과 export 심볼 목록 반영.

### 완료 기준

1. 신규 API 단위 테스트 통과.
2. 기존 multipart 코드 회귀 없음.
3. ABI 정책/릴리스 노트 반영 완료.

## Phase 4. 운영 제어/API 보강 (성능 부가 항목)

### 결정

1. `TCP_NODELAY`를 공개 소켓 옵션으로 실제 동작하게 구현한다.
2. 기본값은 현행과 동일하게 `ON(1)` 유지.
3. single-frame recv 기본값은 바꾸지 않고 opt-in 제어로 노출.
4. 런타임 경합 방지를 위해 관련 옵션은 연결 전 설정을 원칙으로 한다.

### 공개 인터페이스 변경안

1. `core/include/zlink.h`:
   - `ZLINK_TCP_NODELAY` 상수 추가
   - 값 의미: `0` = Nagle on, `1` = Nagle off
   - `ZLINK_STREAM_SINGLE_FRAME_RECV` 상수 추가
2. `core/src/core/options.*`:
   - 옵션 필드 추가(예: `bool stream_single_frame_recv`, `int tcp_nodelay`)
   - 기본값: `tcp_nodelay = 1`
   - `setsockopt/getsockopt` 구현
3. `core/src/sockets/stream.cpp`:
   - recv fastpath 조건을 env + socket option 조합으로 변경
4. `core/src/transports/tcp/tcp.cpp`:
   - 하드코딩 `TCP_NODELAY=1`을 `options.tcp_nodelay` 기반으로 변경

### 완료 기준

1. `ZLINK_TCP_NODELAY` `0/1` 설정 시 실제 소켓 옵션 반영 확인.
2. `ZLINK_STREAM_SINGLE_FRAME_RECV` on/off 동작 검증.
3. env fallback deprecation 정책 문서화:
   - N 릴리스: env/sockopt 병행 + 경고
   - N+1 릴리스: env 제거

## Phase 5. 안정화 및 성능 게이트 고도화

### 작업

1. PR 게이트: `zlink` 타깃 핵심 워크로드(빠른 모드).
2. Nightly 게이트: `--stack all --size all` + 혼합/고CCU(확장 모드).
3. 성능 판정 표준화:
   - 3회 이상 반복, 중앙값 비교
   - 허용 편차(노이즈 밴드) 초과 시만 회귀로 판정
4. 실패 원인 분류 로그(연결/파싱/프로토콜/송신) 표준화.

### 완료 기준

1. PR 단계에서 성능 회귀 조기 탐지 가능.
2. Nightly 보고서로 stack 간 장기 추세 관리 가능.

## 5) 테스트 계획

### 기능 테스트

1. 기존:
   - `core/tests/test_stream_socket.cpp`
   - `core/tests/test_stream_fastpath.cpp`
   - `core/tests/routing-id/test_stream_routing_id_size.cpp`
2. 신규:
   - `core/tests/test_stream_msg_routing_id_api.cpp`
   - `core/tests/test_stream_single_frame_recv_sockopt.cpp`
   - `core/tests/test_tcp_nodelay_sockopt.cpp`
   - `core/tests/test_stream_invalid_sockopt.cpp`
   - `core/tests/test_stream_option_change_after_connect.cpp`
3. 호환성 매트릭스:
   - old/new 조합 4가지 송수신 시나리오 고정

### 시나리오/성능 테스트

1. 스크립트: `core/tests/scenario/stream/run_stream_compare.sh`
2. 타깃 모드(PR):
   - `--stack zlink --size 64 --connections 1000`
   - `--stack zlink --size 1024 --connections 1000`
3. 확장 모드(Nightly):
   - `--stack zlink --size 65536 --connections 100`
   - `--stack zlink --size mixed --connections 1000`
   - `--stack zlink --size 64 --connections 10000`
   - `--stack all --size all`

### 수용 기준(최종)

1. 기능 테스트 전부 통과.
2. 기존 API 동작 회귀 없음.
3. KPI 충족 또는 미달 사유/추가 액션 문서화.

## 6) 롤아웃 전략

### 1단계 (opt-in)

1. feature flag 기본 off.
2. 테스트/벤치 환경에서만 on.

### 2단계 (default-on 후보)

1. 전환 조건:
   - 연속 N회 CI 통과
   - 성능 회귀 0건
   - 스테이징 무장애 운영 시간 충족
2. 필요 시 즉시 opt-out/롤백 가능 상태 유지.

### 3단계 (정리)

1. 임시 env 토글 deprecation 절차에 따라 제거.
2. 문서/코드 default 동작 일치 보장.

## 7) 리스크 및 대응

| 리스크 | 설명 | 대응 |
|---|---|---|
| 엔진 전환 회귀 | TCP STREAM 경로 변경으로 예기치 않은 동작 | feature flag + interop 매트릭스 + 롤백 경로 |
| ABI/배포 리스크 | 신규 C API 추가 시 심볼/버전 불일치 가능 | 버전 정책 + 심볼 목록 + 릴리스 노트 동시 반영 |
| 메모리 급증 | backpressure 변경 시 pending 큐 증가/OOM 가능 | HWM 조건 메모리 상한 테스트 + RSS 관측 |
| 동시성 경합 | 옵션/핫경로 변경 시 race 가능 | 연결 전 옵션 설정 원칙 + TSan/스트레스 테스트 |
| CI 노이즈 | 공유 러너 변동으로 오탐 가능 | 반복 중앙값 + 노이즈 허용폭 + Nightly 분리 |
| 운영 제어성 부족 | env 의존 토글 관리 어려움 | socket option 우선 + env deprecation 일정 |
| 옵션 오설정 | `TCP_NODELAY=0`에서 연속 소형 전송 latency 악화 가능 | 기본값 1 유지 + 조건부 영향 문서화 |

## 8) 일정(권장)

1. Week 1: Phase 0 + Phase 0.5 + PR 게이트 뼈대
2. Week 2: Phase 1 구현/안정화 + API 시그니처 확정
3. Week 3: Phase 2 최적화 + Phase 3 API/ABI 적용
4. Week 4: Phase 4 옵션화 + Phase 5 게이트 고도화

## 9) Done 정의 (DoD)

1. 각 Phase 산출물이 코드/테스트/벤치 결과로 추적 가능.
2. STREAM 회귀 테스트가 CI 필수 게이트로 고정.
3. 성능 결과가 baseline 대비 정량 비교 형태로 보관.
4. 신규 API/옵션 사용법이 `doc`에 반영됨.

## 10) 구현 시 고정 원칙

1. binding-stack 검증은 각 stack 바이너리를 직접 실행한다.
2. 비등가 shortcut benchmark는 허용하지 않는다.
3. 성능 개선 PR은 재현 가능한 수치와 명령을 포함한다.

## 11) Claude 리뷰 반영 내역 (비판적 수용)

### 수용

1. 프로파일링 단계 부재 지적 수용: `Phase 0.5` 추가.
2. KPI 모호성 지적 수용: 정량 임계값/통계 기준 명시.
3. interop/롤백/회귀 범위 지적 수용: Phase 1 완료 기준 강화.
4. ABI/버전 관리 누락 지적 수용: Phase 3에 정책 반영.
5. 동시성/메모리 리스크 누락 지적 수용: TSan/HWM/RSS 검증 추가.
6. CI 노이즈/게이트 시점 지적 수용: PR/Nightly 이원화 및 Week 1 선반영.
7. 테스트 축 부족 지적 수용: mixed/connections 축 및 에러 경로 테스트 추가.
8. `TCP_NODELAY` 과일반화 지적 수용: 조건부 영향으로 문구 수정.

### 부분 수용

1. "승자/패자 이분법" 지적: 단일 승자 고정은 제거했지만, 구현 비용 제약상 우선순위 경로는 유지.
2. "dotnet 비교군 제외" 지적: 게이트 판정에서는 제외, 참고 비교군으로는 유지.

### 기각

1. 없음. (나머지 지적은 문서 범위 내에서 모두 반영 가능)
