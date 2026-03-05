# C++ Binding Perf Benchmark Implementation Plan

> core/perf (C API) 벤치마크를 bindings/cpp/perf 로 1:1 포팅한다.
> **C++ binding API (`zlink::context_t`, `zlink::socket_t`, `zlink::service::*`) 만 사용하며, zlink C API (`zlink_*()`) 직접 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `current/` 분리 구조를 C++ binding 에서 그대로 반영한다.

```
bindings/cpp/
├── include/zlink/                           ← C++ binding 헤더 (수정 불가, 확장만 허용)
├── tests/
│   └── certs/gen/                           ← TLS 인증서 (바인딩 독립 관리)
│       ├── server.crt
│       ├── server.key
│       └── ca.crt
│
└── perf/
    ├── PORTING_PLAN.md                      ← 본 문서
    ├── README.md                            ← 사용법 안내
    ├── .gitignore                           ← build/, tmp/ 제외
    │
    ├── single/
    │   ├── common/                          ← ★ core/perf/single/common/ 대응
    │   │   ├── perf_single_common.cpp       ← 공통 유틸 구현 (★ core는 header-only, 아래 참고)
    │   │   ├── perf_single_common.hpp       ← 공통 유틸 헤더
    │   │   ├── perf_single_runner.cpp       ← run_standard_bench_main() 구현
    │   │   ├── perf_single_runner.hpp       ← runner 헤더
    │   │   ├── perf_single_tls.hpp          ← TLS 인증서 경로 리졸버 (single)
    │   │   └── perf_single_metric_header.hpp ← 페이로드 헤더 (core 1:1 유지)
    │   ├── current/                         ← ★ core/perf/single/current/ 대응
    │   │   ├── perf_pair.cpp
    │   │   ├── perf_pubsub.cpp
    │   │   ├── perf_dealer_dealer.cpp
    │   │   ├── perf_dealer_router.cpp
    │   │   ├── perf_router_router.cpp
    │   │   ├── perf_router_router_poll.cpp
    │   │   ├── perf_gateway.cpp
    │   │   └── perf_spot.cpp
    │   ├── build/                           ← 컴파일된 바이너리 (gitignore)
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   ├── run_comparison.py
    │   └── tests/
    │       └── test_run_comparison_policy.py
    │
    ├── multi/
    │   ├── common/                          ← ★ core/perf/multi/common/ 대응
    │   │   ├── perf_common.hpp              ← 공통 유틸 (multi 전용 RAII, monitor, settle)
    │   │   ├── perf_common_multi.hpp        ← Multi 설정 리졸버 (multi_bench_settings_t)
    │   │   ├── perf_multi_client_helpers.hpp ← 공통 client 루프 헬퍼
    │   │   ├── perf_multi_entry.hpp         ← set_perf_multi_pattern_env 헬퍼
    │   │   ├── perf_multi_tls.hpp           ← TLS 인증서 경로 리졸버 (multi)
    │   │   ├── perf_multi_metric_header.hpp ← 페이로드 헤더 (core 1:1 유지)
    │   │   ├── perf_multi_server_runner.cpp ← 서버 진입점 (RUN_MULTI_SERVER_FN 디스패치)
    │   │   └── perf_multi_client_runner.cpp ← 클라이언트 진입점 (RUN_MULTI_CLIENT_FN 디스패치)
    │   ├── current/                         ← ★ core/perf/multi/current/ 대응
    │   │   ├── perf_multi_dealer_dealer_server.cpp  ← ★ server/client 분리
    │   │   ├── perf_multi_dealer_dealer_client.cpp
    │   │   ├── perf_multi_dealer_router_server.cpp
    │   │   ├── perf_multi_dealer_router_client.cpp
    │   │   ├── perf_multi_router_router_server.cpp
    │   │   ├── perf_multi_router_router_client.cpp
    │   │   ├── perf_multi_pubsub_server.cpp
    │   │   ├── perf_multi_pubsub_client.cpp
    │   │   ├── perf_multi_gateway_server.cpp
    │   │   ├── perf_multi_gateway_client.cpp
    │   │   ├── perf_multi_spot_server.cpp
    │   │   ├── perf_multi_spot_client.cpp
    │   │   ├── perf_multi_stream_server.cpp       ← 서버 only (클라이언트=공통 stream client)
    │   │   ├── perf_multi_stream_callback_server.cpp
    │   │   └── perf_multi_stream_len32be_server.cpp
    │   ├── build/                           ← 컴파일된 바이너리 (gitignore)
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── results/
    │   ├── single/
    │   │   ├── baseline/.gitkeep
    │   │   ├── report/.gitkeep
    │   │   └── tmp/.gitkeep
    │   └── multi/
    │       ├── baseline/.gitkeep
    │       ├── report/.gitkeep
    │       └── tmp/.gitkeep
    │
    ├── run_benchmarks.sh                    ← 루트 single 래퍼
    ├── run_benchmarks.ps1
    ├── run_benchmarks_multi.sh              ← 루트 multi 래퍼
    ├── run_benchmarks_multi.ps1
    └── run_comparison.py                    ← 루트 오케스트레이터
```

> **core/perf 와의 common/ 구조 차이:**
> core/perf 의 `common/` 은 **header-only** (`.hpp` 인라인 함수만) 이다.
> C++ binding 포팅에서는 C++ API 치환으로 인해 함수 본체가 커지므로,
> `perf_single_common.cpp` 와 `perf_single_runner.cpp` 를 별도 소스로 분리한다.
> 이는 **빌드 단위 분리** (컴파일 시간 단축, ODR 위반 방지) 를 위한 의도적 설계이다.
> metric header (`.hpp`) 와 TLS 리졸버 (`.hpp`) 는 core 와 동일하게 header-only 로 유지한다.

### core/perf 대비 구조 매핑

| core/perf | cpp/perf | 비고 |
|-----------|----------|------|
| `single/common/bench_common.hpp` | `single/common/perf_single_common.hpp` + `.cpp` | C++ API 치환, 헤더/소스 분리 |
| `single/common/perf_single_metric_header.hpp` | `single/common/perf_single_metric_header.hpp` | 구조/상수 1:1 유지 |
| `single/current/perf_pair.cpp` | `single/current/perf_pair.cpp` | 1:1 매핑, C++ API 사용 |
| `single/current/perf_dealer_dealer.cpp` | `single/current/perf_dealer_dealer.cpp` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/common/perf_common.hpp` | C++ API 치환 |
| `multi/common/perf_common_multi.hpp` | `multi/common/perf_common_multi.hpp` | 1:1 유지 |
| `multi/common/perf_multi_client_helpers.hpp` | `multi/common/perf_multi_client_helpers.hpp` | C++ API 치환 |
| `multi/common/perf_multi_entry.hpp` | `multi/common/perf_multi_entry.hpp` | 1:1 유지 |
| `multi/current/perf_multi_dealer_dealer_server.cpp` | `multi/current/perf_multi_dealer_dealer_server.cpp` | ★ server/client 분리 유지 |
| `multi/current/perf_multi_stream_server.cpp` | `multi/current/perf_multi_stream_server.cpp` | 서버 only |

---

## 2. 빌드 시스템

### 2.1 빌드 방식 (run_policy_bench.py 경유 컴파일)

> **사용자 진입점**: `run_benchmarks.sh` → `run_comparison.py` (core/perf 동일 구조)
> **내부 빌드 엔진**: `run_policy_bench.py --binding cpp` 가 컴파일러를 직접 호출

`run_policy_bench.py` 는 바인딩 전용 내부 빌드/실행 엔진이다.
사용자는 `run_benchmarks.sh` 를 통해 실행하며, 스크립트가 내부적으로 `run_policy_bench.py` 를 호출한다.
CMake 를 거치지 않고, 소스 파일을 직접 컴파일한다.

```bash
CXX=${CXX:-c++}
$CXX -O3 -std=c++17 -pthread \
  -I${CORE_INCLUDE} \
  -I${BINDING_INCLUDE} \
  -I${SINGLE_COMMON} \
  ${SINGLE_COMMON}/*.cpp ${SINGLE_CURRENT}/perf_pair.cpp \
  -DRUN_PATTERN_FN=run_pattern_pair \
  -L${CPP_NATIVE} -lzlink -Wl,-rpath,${CPP_NATIVE} \
  -o ${PERF_DIR}/single/build/perf_pair
```

### 2.2 네이티브 라이브러리 경로

```
bindings/cpp/native/<os>-<arch>/libzlink.so
```

| 플랫폼 | 경로 |
|--------|------|
| Linux x86_64 | `bindings/cpp/native/linux-x86_64/libzlink.so` |
| Linux aarch64 | `bindings/cpp/native/linux-aarch64/libzlink.so` |
| macOS x86_64 | `bindings/cpp/native/darwin-x86_64/libzlink.dylib` |
| macOS aarch64 | `bindings/cpp/native/darwin-aarch64/libzlink.dylib` |

### 2.3 CMakeLists.txt (선택적 CMake 빌드)

`bindings/cpp/CMakeLists.txt` 의 `ZLINK_CPP_BUILD_BENCHMARKS=ON` 옵션으로도 빌드 가능하다.
`add_subdirectory(perf)` 로 활성화되며, 타겟 구조는 core/perf 와 동일하게 구성한다.

**Single 타겟:**

| 타겟 | 소스 |
|------|------|
| `perf_pair` | `single/common/*.cpp` + `single/current/perf_pair.cpp` |
| `perf_pubsub` | `single/common/*.cpp` + `single/current/perf_pubsub.cpp` |
| `perf_dealer_dealer` | `single/common/*.cpp` + `single/current/perf_dealer_dealer.cpp` |
| `perf_dealer_router` | `single/common/*.cpp` + `single/current/perf_dealer_router.cpp` |
| `perf_router_router` | `single/common/*.cpp` + `single/current/perf_router_router.cpp` |
| `perf_router_router_poll` | `single/common/*.cpp` + `single/current/perf_router_router_poll.cpp` |
| `perf_gateway` | `single/common/*.cpp` + `single/current/perf_gateway.cpp` |
| `perf_spot` | `single/common/*.cpp` + `single/current/perf_spot.cpp` |

**Multi 타겟:**

| 타겟 | 소스 |
|------|------|
| `perf_multi_dealer_dealer_server` | `multi/common/perf_multi_server_runner.cpp` + `multi/current/perf_multi_dealer_dealer_server.cpp` |
| `perf_multi_dealer_dealer_client` | `multi/common/perf_multi_client_runner.cpp` + `multi/current/perf_multi_dealer_dealer_client.cpp` |
| `perf_multi_dealer_router_server` | 동일 패턴 |
| `perf_multi_dealer_router_client` | 동일 패턴 |
| `perf_multi_router_router_server` | 동일 패턴 |
| `perf_multi_router_router_client` | 동일 패턴 |
| `perf_multi_pubsub_server` | 동일 패턴 |
| `perf_multi_pubsub_client` | 동일 패턴 |
| `perf_multi_gateway_server` | 동일 패턴 |
| `perf_multi_gateway_client` | 동일 패턴 |
| `perf_multi_spot_server` | 동일 패턴 |
| `perf_multi_spot_client` | 동일 패턴 |
| `perf_multi_stream_server` | 서버 only |
| `perf_multi_stream_callback_server` | 서버 only |
| `perf_multi_stream_len32be_server` | 서버 only |

**STREAM client:** `core/perf/common/streamclient/perf_stream_client` 공용 바이너리 사용 (별도 빌드 불필요).

**컴파일 설정:**
- 표준: `cxx_std_17`
- 최적화: `-O3` (Linux/macOS), `/O2` (MSVC)
- 링크: `libzlink`, `Threads::Threads`
- Include: `${CORE_INCLUDE}`, `bindings/cpp/include`

### 2.4 빌드 명령

```bash
# 사용자 진입점 (권장)
./run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64

# run_policy_bench.py 직접 호출 (내부 엔진)
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite single --reuse-build

# CMake 경유 (선택적)
cmake -B build -DZLINK_CPP_BUILD_BENCHMARKS=ON
cmake --build build
```

---

## 3. CLI 인터페이스 (run_policy_bench.py 호환)

### 3.1 Single 실행

```bash
bindings/cpp/perf/single/build/perf_pair <TRANSPORT> <SIZE>
```

- **TRANSPORT**: `tcp | tls | ws | wss | inproc | ipc`
- **SIZE**: 양의 정수 (바이트)
- 종료코드: 0=성공, 비-0=오류

> **참고**: 러너(`run_policy_bench.py`)는 C++ single 바이너리를 `<binary> <transport> <size>` 두 인자로 호출한다. 패턴명은 바이너리 파일명에 내재되어 있으므로 별도 인자가 없다.

### 3.2 Multi 실행

**서버:**
```bash
bindings/cpp/perf/multi/build/perf_multi_dealer_dealer_server <TRANSPORT> <SIZE>
```

**클라이언트:**
```bash
bindings/cpp/perf/multi/build/perf_multi_dealer_dealer_client <TRANSPORT> <SIZE> --endpoint <ENDPOINT>
```

### 3.3 STREAM 패턴 클라이언트

STREAM, STREAM_CALLBACK, STREAM_LEN32BE 패턴은:
- **서버**: C++ binding 벤치마크가 직접 구현 (`zlink::socket_t::stream_attach()` API)
- **클라이언트**: `core/perf/common/streamclient/build/perf_stream_client` (C++ 공통 바이너리) 사용
- `run_policy_bench.py` 가 자동으로 공통 stream client 를 호출한다

### 3.4 run_policy_bench.py 패턴 스펙 (기존 등록 완료)

```python
CPP_SINGLE_PATTERN_SPECS = {
    "PAIR":               ("perf_pair.cpp",              "run_pattern_pair",              "perf_pair"),
    "PUBSUB":             ("perf_pubsub.cpp",            "run_pattern_pubsub",            "perf_pubsub"),
    "DEALER_DEALER":      ("perf_dealer_dealer.cpp",     "run_pattern_dealer_dealer",     "perf_dealer_dealer"),
    "DEALER_ROUTER":      ("perf_dealer_router.cpp",     "run_pattern_dealer_router",     "perf_dealer_router"),
    "ROUTER_ROUTER":      ("perf_router_router.cpp",     "run_pattern_router_router",     "perf_router_router"),
    "ROUTER_ROUTER_POLL": ("perf_router_router_poll.cpp","run_pattern_router_router_poll","perf_router_router_poll"),
    "GATEWAY":            ("perf_gateway.cpp",           "run_pattern_gateway",           "perf_gateway"),
    "SPOT":               ("perf_spot.cpp",              "run_pattern_spot",              "perf_spot"),
}

CPP_MULTI_SERVER_PATTERN_SPECS = {
    "MULTI_DEALER_DEALER":   ("perf_multi_dealer_dealer_server.cpp",   ...),
    "MULTI_DEALER_ROUTER":   ("perf_multi_dealer_router_server.cpp",   ...),
    "MULTI_ROUTER_ROUTER":   ("perf_multi_router_router_server.cpp",   ...),
    "MULTI_PUBSUB":          ("perf_multi_pubsub_server.cpp",          ...),
    "MULTI_GATEWAY":         ("perf_multi_gateway_server.cpp",         ...),
    "MULTI_SPOT":            ("perf_multi_spot_server.cpp",            ...),
    "MULTI_STREAM":          ("perf_multi_stream_server.cpp",          ...),
    "MULTI_STREAM_CALLBACK": ("perf_multi_stream_callback_server.cpp", ...),
    "MULTI_STREAM_LEN32BE":  ("perf_multi_stream_len32be_server.cpp",  ...),
}

CPP_MULTI_CLIENT_PATTERN_SPECS = {
    # 6개 비-STREAM 패턴: dealer_dealer, dealer_router, router_router, pubsub, gateway, spot
}
```

---

## 4. RESULT 출력 형식 (core/perf 동일)

**러너 파서 인식 메트릭 (completion 계산 대상: throughput, bandwidth, latency):**
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,throughput,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,bandwidth,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency,<value>
```

**러너 파서 인식 메트릭 (리소스, completion 미포함):**
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_mem_mb,<value>
```

**바이너리 자체 출력 전용 (러너 파서 미인식, 로그/디버그용):**
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p95,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p99,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_snd_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_end,<value>
```

> **주의**: `latency_p95`, `latency_p99`, `server_*_pending_*` 메트릭은 `run_policy_bench.py` 파서가 인식하지 않으므로 결과 테이블/리포트에 포함되지 않는다. 바이너리가 stdout 으로 출력은 가능하나 러너 집계 대상이 아님. completion 판정은 `throughput + bandwidth + latency` 3개 기준이다.

### Bandwidth 계산 규칙

```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```

| 구분 | 승수 | 비고 |
|------|------|------|
| **Single 전체** | **1.0** | run_policy_bench.py 기준 모든 single 은 one-way 방향 |
| Multi echo (DEALER_ROUTER, ROUTER_ROUTER, STREAM*) | 2.0 | 요청+응답 양방향 |
| Multi one-way (DEALER_DEALER, PUBSUB, GATEWAY, SPOT) | 1.0 | 단방향 (러너 `MULTI_ONE_WAY_PATTERNS` 기준) |

---

## 5. 환경 변수 (core/perf 동일)

### 5.1 Single

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_IO_THREADS` | 0 (기본) | Context IO 스레드 |
| `PERF_WARMUP_COUNT` | 1000 | 웜업 메시지 횟수 (count 기반, 시간 기반 아님) |
| `PERF_SINGLE_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 200000 | 레이턴시 reservoir sampling 캡 |
| `PERF_SINGLE_HWM` | 1000 | 소켓 HWM (send+recv) |
| `PERF_SINGLE_SNDHWM` | 1000 | 송신 HWM |
| `PERF_SINGLE_RCVHWM` | 1000 | 수신 HWM |
| `PERF_SINGLE_SNDTIMEO_MS` | 200 | 송신 타임아웃 |
| `PERF_SINGLE_RCVTIMEO_MS` | 200 | 수신 타임아웃 |
| `PERF_MSG_SIZES` | size별 자동 주입 | 메시지 크기 목록 (CSV). 러너가 size별로 별도 실행하며, 바이너리 1회 실행은 1 size 기준 |
| `PERF_MAX_SOCKETS` | 자동 | 최대 소켓 수 |

### 5.2 Multi

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_MULTI_CLIENTS` | **1000** | 동시 클라이언트 수 (러너 CLI 기본: `--multi-clients 0` → env fallback 1000) |
| `PERF_MULTI_WARMUP_SECONDS` | **3** | 웜업 기간 (러너 CLI: `--multi-warmup-seconds 3`) |
| `PERF_MULTI_SETTLE_MS` | 500 | 측정 전 안정화 |
| `PERF_MULTI_DURATION_SECONDS` | 5 | 활성 측정 기간 (러너 CLI: `--multi-duration-seconds 5`) |
| `PERF_MULTI_ACTIVE_WARMUP` | 0 | 0=sleep, 1=active |
| `PERF_MULTI_HWM` | **100000** | 소켓 HWM (러너 CLI: `--multi-hwm 100000`) |
| `PERF_MULTI_SNDHWM` | 0 (HWM fallback) | 송신 HWM |
| `PERF_MULTI_RCVHWM` | 0 (HWM fallback) | 수신 HWM |
| `PERF_MULTI_SNDTIMEO_MS` | **5000** | 송신 타임아웃 (러너 CLI: `--multi-sndtimeo-ms 5000`) |
| `PERF_MULTI_RCVTIMEO_MS` | **5000** | 수신 타임아웃 (러너 CLI: `--multi-rcvtimeo-ms 5000`) |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | 5000 | 연결 대기 |
| `PERF_MULTI_MONITOR_HWM` | **200000** | 모니터 소켓 HWM (러너 CLI: `--multi-monitor-hwm 200000`) |
| `PERF_MULTI_SERVER_BIND_PORT` | 0 (자동) | 서버 포트 고정 |
| `PERF_IO_THREADS` | 0 | IO 스레드 |
| `PERF_MULTI_SERVER_IO_THREADS` | 0 | 서버 전용 IO 스레드 |
| `PERF_MULTI_CLIENT_IO_THREADS` | 0 | 클라이언트 전용 IO 스레드 |
| `PERF_MULTI_CLIENT_POLL_TIMEOUT_MS` | 0 | 클라이언트 poll 타임아웃 |
| `PERF_CTX_BLOCKY` | 미설정 | Context blocky 모드 (설정 시 적용) |
| `PERF_CTX_TERM` | 1 (STREAM: 0) | Context terminate 호출 여부 |

---

## 6. 벤치마크 페이즈 (core/perf 동일)

### 6.1 Single 페이즈

```
[Warmup(count)] → [Active(duration) — throughput + latency 동시 측정]
```

1. **Warmup** (`PERF_WARMUP_COUNT`, 기본 1000): 고정 횟수 send/recv 반복으로 워밍업 (시간 기반이 아님)
2. **Active** (`PERF_SINGLE_DURATION_SECONDS`, 5초): duration 기반 throughput 측정 + reservoir sampling 으로 latency/p95/p99 동시 수집

> **core 구현 참고**: single 은 별도 Settle/Drain/Latency 페이즈가 없다.
> Active 페이즈에서 메시지 헤더의 `sent_ts_us` 를 기반으로 throughput 과 latency 를 동시에 측정한다.
> 수신 측에서 `latency_stats_builder_t` (reservoir sampling) 으로 p95/p99 를 수집한다.

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle] → [Active(duration)] → [Drain]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_MULTI_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_MULTI_WARMUP_SECONDS`, 3초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_MULTI_SETTLE_MS`, 500ms): 안정화 sleep (one-way: phase_drain 라벨, echo: phase_warmup 라벨)
4. **Active** (`PERF_MULTI_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)
5. **Drain** (하드코딩, echo: 300ms, one-way: 0ms): 인플라이트 메시지 대기 (phase_drain)

---

## 7. 패턴별 상세 구현 계획

### 7.1 Single 패턴

> **참고**: run_policy_bench.py 기준 모든 single 패턴은 one-way 방향(`bandwidth 승수 = 1.0`).
> "소켓 동작" 열은 실제 send/recv 패턴(echo=양방향, one-way=단방향)을 나타낸다.

| # | 파일 | 패턴 | 소켓 타입 | 소켓 동작 | 트랜스포트 |
|---|------|------|-----------|----------|-----------|
| 1 | perf_pair.cpp | PAIR | `socket_type::pair` ×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 2 | perf_pubsub.cpp | PUBSUB | `socket_type::pub` + `socket_type::sub` | one-way | tcp,tls,ws,wss,inproc,ipc |
| 3 | perf_dealer_dealer.cpp | DEALER_DEALER | `socket_type::dealer` ×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 4 | perf_dealer_router.cpp | DEALER_ROUTER | `socket_type::dealer` + `socket_type::router` | echo | tcp,tls,ws,wss,inproc,ipc |
| 5 | perf_router_router.cpp | ROUTER_ROUTER | `socket_type::router` ×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 6 | perf_router_router_poll.cpp | ROUTER_ROUTER_POLL | `socket_type::router` ×2 + Poller | echo | tcp,tls,ws,wss,inproc,ipc |
| 7 | perf_gateway.cpp | GATEWAY | `service::gateway_t` + `service::receiver_t` | echo | tcp,tls,ws,wss |
| 8 | perf_spot.cpp | SPOT | `service::spot_node_t` (pub/sub) | one-way | tcp,tls,ws,wss |

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 별도 파일**로 분리한다.

| # | 서버 파일 | 클라이언트 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|----------|---------------|------|-----------|----------------|
| 1 | perf_multi_dealer_dealer_server | perf_multi_dealer_dealer_client | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | perf_multi_dealer_router_server | perf_multi_dealer_router_client | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | perf_multi_router_router_server | perf_multi_router_router_client | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | perf_multi_pubsub_server | perf_multi_pubsub_client | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | perf_multi_gateway_server | perf_multi_gateway_client | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | perf_multi_spot_server | perf_multi_spot_client | SPOT | Spot publish | Spot subscribe |
| 7 | perf_multi_stream_server | (공통 stream client) | STREAM | STREAM bind, raw echo | C++ stream client |
| 8 | perf_multi_stream_callback_server | (공통 stream client) | STREAM_CALLBACK | stream_attach callback | C++ stream client |
| 9 | perf_multi_stream_len32be_server | (공통 stream client) | STREAM_LEN32BE | stream_attach_len32be | C++ stream client |

**Multi 서버 통신 프로토콜:**
- 서버 stdout 에 `READY,<endpoint>` 출력 → 스크립트가 클라이언트 시작
- 클라이언트 종료 시 서버에 stop-token 전송
- 서버 graceful shutdown 후 RESULT 메트릭 출력

---

## 8. 공통 유틸리티

### 8.1 Single common/ 모듈

**`perf_single_common.hpp` + `.cpp`** — (core/perf single/common/bench_common.hpp 대응)

```cpp
namespace perf::single {

// ── RAII 래퍼 ──
class ctx_guard_t {
    zlink::context_t ctx_;
public:
    explicit ctx_guard_t();          // apply_ctx_options() 호출
    zlink::context_t& ctx();
    ~ctx_guard_t();                  // shutdown + term
};

class socket_guard_t {
    zlink::socket_t sock_;
public:
    socket_guard_t(ctx_guard_t& ctx, zlink::socket_type type);
    zlink::socket_t& sock();
    ~socket_guard_t();               // close
};

// ── 환경변수 파싱 ──
int parse_positive_env(const char* name, int default_value);
int resolve_single_duration_seconds();        // PERF_SINGLE_DURATION_SECONDS (5)
int resolve_single_send_timeout_ms();         // PERF_SINGLE_SNDTIMEO_MS (200)
int resolve_single_recv_timeout_ms();         // PERF_SINGLE_RCVTIMEO_MS (200)
int resolve_single_socket_hwm(bool is_send);  // PERF_SINGLE_HWM/SNDHWM/RCVHWM (1000)

// ── 소켓 옵션 적용 ──
void apply_ctx_options(zlink::context_t& ctx);
void apply_single_hwm(zlink::socket_t& socket);
void apply_single_benchmark_socket_options(zlink::socket_t& socket, const std::string& transport);

// ── Send/Receive 헬퍼 ──
bool send_exact(zlink::socket_t& socket, const void* data, size_t size, zlink::send_flag flags);
bool recv_exact(zlink::socket_t& socket, void* data, size_t size, zlink::recv_flag flags);
int recv_single_part_msg_flags(zlink::socket_t& socket, size_t expected_size, zlink::recv_flag flags);
int recv_two_part_msg_flags(zlink::socket_t& socket, size_t payload_size,
                            std::string& routing_id_out, zlink::recv_flag flags);

// ── 엔드포인트 생성 ──
std::string make_endpoint(const std::string& transport, const std::string& id);
std::string bind_and_resolve_endpoint(zlink::socket_t& socket, const std::string& transport,
                                       const std::string& id);

// ── 커넥션 설정 ──
void setup_connected_pair(zlink::socket_t& bind_sock, zlink::socket_t& conn_sock,
                           const std::string& transport, const std::string& id);
bool connect_checked(zlink::socket_t& socket, const std::string& endpoint);
void settle();  // 100ms sleep

// ── Transport 지원 확인 ──
bool transport_available(const std::string& transport);

// ── RESULT 출력 (bandwidth 승수 = 1.0 for all single) ──
void print_result(const std::string& lib, const std::string& pattern, const std::string& transport,
                  size_t size, double throughput, double latency_us, double p95_us, double p99_us);

// ── 큐 프로빙 ──
class queue_probe_t { /* ... */ };

// ── 레이턴시 수집 ──
struct latency_stats_t { double mean_us, p95_us, p99_us; };
class latency_stats_builder_t { /* reservoir sampling, cap=200k */ };

// ── 벤치마크 진입점 ──
using run_fn_t = void(*)(const std::string& transport, size_t size, const std::string& lib_name);
int run_standard_bench_main(int argc, char** argv, run_fn_t fn);

} // namespace perf::single
```

**`perf_single_tls.hpp`** — single 전용 TLS 인증서 리졸버

```cpp
namespace perf::single {

// bindings/cpp/tests/certs/gen/ 에서 server.crt, server.key, ca.crt 탐색
bool try_resolve_perf_tls_paths(std::string& cert_out, std::string& key_out, std::string& ca_out);

void setup_tls_server(zlink::socket_t& socket, const std::string& transport);
void setup_tls_client(zlink::socket_t& socket, const std::string& transport);

} // namespace perf::single
```

**`perf_single_runner.hpp` + `.cpp`** — single 진입점

```cpp
// run_standard_bench_main 구현
// argv[1]=transport argv[2]=size 파싱 후 RunFn 호출
// -DRUN_PATTERN_FN=run_pattern_pair 로 패턴 함수 주입
```

**`perf_single_metric_header.hpp`** — 페이로드 헤더 (core `perf_single_metric_header.hpp` 1:1 유지)

```cpp
namespace perf_single_metric {

static const uint32_t k_magic = 0x53504631U;  // "SPF1"

enum phase_t {
    phase_unknown = 0,
    phase_warmup  = 1,
    phase_active  = 2
};

struct header_t {
    uint32_t magic;       // offset 0:  SPF1
    uint32_t run_id;      // offset 4:  실행 ID
    uint32_t phase;       // offset 8:  phase_t
    uint32_t msg_size;    // offset 12: 메시지 크기
    uint64_t seq;         // offset 16: 시퀀스 번호
    uint64_t sent_ts_us;  // offset 24: 송신 타임스탬프 (μs)
};
// 총 32바이트 (uint32×4 + uint64×2)

inline size_t header_size();
inline uint64_t now_us();
inline void init_header(header_t* out, uint32_t run_id, phase_t phase,
                        size_t msg_size, uint64_t seq, uint64_t sent_ts_us);
inline bool encode_header(void* dst, size_t dst_size, const header_t& h);
inline bool decode_header(const void* src, size_t src_size, header_t* out);
inline bool stamp_payload(void* payload, size_t payload_size, uint32_t run_id,
                           phase_t phase, size_t msg_size, uint64_t seq, uint64_t sent_ts_us);
inline bool decode_payload_header(const void* payload, size_t payload_size, header_t* out);
inline bool is_expected(const header_t& h, uint32_t run_id, phase_t phase, size_t msg_size);

} // namespace perf_single_metric
```

**`perf_multi_metric_header.hpp`** — 멀티 페이로드 헤더 (core `perf_multi_metric_header.hpp` 1:1 유지)

single 과 동일 구조, magic 과 phase enum 만 다름:
- magic: `0x4D504631U` ("MPF1")
- phase_t: `phase_unknown=0, phase_warmup=1, phase_active=2, phase_drain=3`
- 나머지 header_t 구조, 함수 시그니처 동일 (namespace: `perf_multi_metric`)

### 8.2 Multi common/ 모듈

**`perf_common.hpp`** — (core/perf multi/common/perf_common.hpp 대응)

single 의 ctx_guard_t / socket_guard_t 와 유사하되 multi 전용 기능 추가:

```cpp
namespace perf::multi {

class ctx_guard_t {
    zlink::context_t ctx_;
    bool skip_term_;  // STREAM 벤치마크는 term 생략
public:
    explicit ctx_guard_t();
    void force_term();
    zlink::context_t& ctx();
    ~ctx_guard_t();
};

class socket_guard_t { /* single과 동일 */ };

// ── 소켓 옵션 ──
void apply_benchmark_hwm(zlink::socket_t& socket, int hwm_value);
void apply_benchmark_socket_options(zlink::socket_t& socket, int hwm, const std::string& transport);
void apply_debug_timeouts(zlink::socket_t& socket, const std::string& transport);

// ── 모니터 ──
struct connect_monitor_t {
    zlink::socket_t* owner;
    zlink::socket_t  monitor;
};
void open_connect_monitor(zlink::socket_t& socket, connect_monitor_t& out);
bool wait_connect_ready(zlink::socket_t& monitor, int timeout_ms);
bool wait_connect_ready_count(zlink::socket_t& monitor, size_t expected_ready, int timeout_ms);
void close_connect_monitor(connect_monitor_t& mon);

// ── 커넥션 설정 ──
void setup_connected_pair(zlink::socket_t& bind_sock, zlink::socket_t& conn_sock,
                           const std::string& transport, const std::string& id);
void settle();  // 300ms sleep (multi 기본)

// ── RESULT 출력 ──
void print_result(const std::string& lib, const std::string& pattern, const std::string& transport,
                  size_t size, double throughput, double bandwidth, double latency_us,
                  double p95_us, double p99_us);
void print_server_queue_metrics(const std::string& lib, const std::string& pattern,
                                 const std::string& transport, size_t size,
                                 /* queue stats */);

// ── 레이턴시 / stopwatch ──
struct bench_latency_stats_t { double mean_us, p95_us, p99_us; };
class bench_latency_sampler_t { /* reservoir sampling */ };

// ── 리소스 메트릭 ──
void print_cpu_mem_metrics(const std::string& lib, const std::string& pattern,
                            const std::string& transport, size_t size,
                            const std::string& role, double elapsed_ns);

} // namespace perf::multi
```

**`perf_common_multi.hpp`** — (core/perf multi/common/perf_common_multi.hpp 대응)

```cpp
namespace perf::multi {

struct multi_bench_settings_t {
    size_t clients;
    int    hwm;
    int    warmup_seconds;
    int    active_warmup;
    int    duration_seconds;
    int    settle_ms;
    int    client_poll_timeout_ms;
    int    connect_ready_timeout_ms;
};

multi_bench_settings_t resolve_multi_bench_settings();
size_t resolve_multi_default_clients(const std::string& pattern);   // 비-STREAM: 100, STREAM: 10000
int    resolve_multi_default_hwm(const std::string& pattern, size_t clients);  // 비-STREAM: 100, STREAM: 10

} // namespace perf::multi
```

**`perf_multi_client_helpers.hpp`** — (core/perf multi/common/perf_multi_client_helpers.hpp 대응)

```cpp
namespace perf::multi {

bool is_supported_transport(const std::string& transport);
std::string parse_endpoint_arg(int argc, char** argv);
void wait_all_client_connect_ready(/* monitors, timeout */);
void run_multi_echo_client_benchmark(/* sockets, settings, ... */);
void run_multi_oneway_client_benchmark(/* sockets, settings, ... */);

} // namespace perf::multi
```

**`perf_multi_entry.hpp`** — (core/perf multi/common/perf_multi_entry.hpp 대응)

```cpp
namespace perf::multi {

inline void set_perf_multi_pattern_env(const char* pattern) {
    setenv("PERF_MULTI_PATTERN", pattern, 1);
}

} // namespace perf::multi
```

**`perf_multi_tls.hpp`** — TLS 인증서 리졸버

```cpp
namespace perf::multi {

// bindings/cpp/tests/certs/gen/ 에서 탐색
// 상위 디렉토리 순회: bindings/cpp/tests/certs/gen → tests/certs/gen
bool try_resolve_perf_tls_paths(std::string& cert_out, std::string& key_out, std::string& ca_out);

void setup_tls_server(zlink::socket_t& socket, const std::string& transport);
void setup_tls_client(zlink::socket_t& socket, const std::string& transport);

} // namespace perf::multi
```

---

## 9. TLS 인증서 관리

### 9.1 독립 인증서 디렉토리

각 바인딩은 `bindings/<lang>/tests/certs/` 에서 인증서를 독립 관리한다.

```
bindings/cpp/tests/certs/gen/
├── server.crt          ← 서버 인증서 (localhost SAN 포함)
├── server.key          ← 서버 개인키
└── ca.crt              ← CA 인증서
```

> dotnet 선례: `bindings/dotnet/tests/certs/` 에 동일 3개 파일 관리 중.
> **core/perf 방식** (embedded cert → `/tmp/bench_*.pem` 임시파일) 은 사용하지 않고, 파일 경로 기반으로 전환한다.

### 9.2 인증서 경로 탐색 로직

상위 디렉토리 순회 방식:

```cpp
// 탐색 순서:
// 1. <cwd>/bindings/cpp/tests/certs/gen/
// 2. <cwd>/../bindings/cpp/tests/certs/gen/ (반복 순회)
// 3. 바이너리 경로 기준 상위 순회
// server.crt, server.key, ca.crt 3개 파일이 모두 존재하는지 확인
```

### 9.3 소켓 옵션 설정

```cpp
// C++ API 를 통한 TLS 설정
socket.set(zlink::socket_option::tls_cert, cert_path);
socket.set(zlink::socket_option::tls_key, key_path);
socket.set(zlink::socket_option::tls_ca, ca_path);

// Gateway/Receiver 서비스 TLS 설정
receiver.set_tls_server(cert_path, key_path);
gateway.set_tls_client(ca_path, hostname, trust);
```

### 9.4 누락 시 동작

- 인증서 파일 자동 생성/복사 로직은 추가하지 않는다
- 실행 전 파일 존재 여부를 체크하고, 누락 시 해당 조합을 즉시 `fail` 처리한다

---

## 10. C++ API 매핑

### 10.1 Core → C++ Binding 치환 대상

| Core C API | C++ Binding API | 비고 |
|-----------|----------------|------|
| `zlink_ctx_new()` | `zlink::context_t ctx` | RAII 생성자 |
| `zlink_ctx_shutdown(ctx)` | `ctx.shutdown()` | |
| `zlink_ctx_term(ctx)` | 소멸자 자동 | |
| `zlink_ctx_set(ctx, opt, val)` | `ctx.set(zlink::context_option::xxx, val)` | |
| `zlink_socket(ctx, type)` | `zlink::socket_t sock(ctx, zlink::socket_type::xxx)` | RAII |
| `zlink_close(s)` | 소멸자 자동 | |
| `zlink_bind(s, ep)` | `sock.bind(ep)` | |
| `zlink_connect(s, ep)` | `sock.connect(ep)` | |
| `zlink_send(s, buf, len, flags)` | `sock.send(buf, len, zlink::send_flag::xxx)` | |
| `zlink_recv(s, buf, len, flags)` | `sock.recv(buf, len, zlink::recv_flag::xxx)` | |
| `zlink_setsockopt(s, opt, val, len)` | `sock.set(zlink::socket_option::xxx, val)` | typed overload 사용 |
| `zlink_getsockopt(s, opt, val, len)` | `sock.get(zlink::socket_option::xxx, &val)` | |
| `zlink_has(feat)` | `zlink::has(feat)` | runtime capability |
| `zlink_socket_monitor(s, ev)` | `sock.monitor_open(zlink::monitor_event::xxx)` | 반환: `socket_t` |
| `zlink_socket_peers(s, ...)` | `sock.peers(peers, &count)` | |
| `zlink_peer_info_t` | `zlink_peer_info_t` (동일 구조체) | |
| `zlink_stream_attach(s, cb, fl)` | `sock.stream_attach(cb, flags)` | |
| `zlink_stream_attach_len32be(s, cb)` | `sock.stream_attach_len32be(cb)` | |
| `zlink_stream_send(s, rid, buf, len, fl)` | `sock.stream_send(rid, buf, len, flags)` | |

### 10.2 서비스 API 매핑

| Core C API | C++ Binding API |
|-----------|----------------|
| `zlink_receiver_new(ctx)` | `zlink::service::receiver_t recv(ctx)` |
| `zlink_receiver_bind(r, ep)` | `recv.bind(ep)` |
| `zlink_receiver_register(r, svc, adv, w)` | `recv.register_service(svc, adv, w)` |
| `zlink_receiver_set_tls_server(r, c, k)` | `recv.set_tls_server(cert, key)` |
| `zlink_gateway_new(ctx, disc)` | `zlink::service::gateway_t gw(ctx, disc)` |
| `zlink_gateway_send(g, svc, buf, len, fl)` | `gw.send(svc, buf, len, flags)` |
| `zlink_gateway_recv(g, parts, svc, fl)` | `gw.recv(parts, svc, flags)` |
| `zlink_gateway_set_tls_client(g, ca, h, t)` | `gw.set_tls_client(ca, hostname, trust)` |
| `zlink_spot_node_new(...)` | `zlink::service::spot_node_t spot(...)` |

---

## 11. 리소스 메트릭 수집 (C++)

### 11.1 CPU 사용률

```cpp
// Linux: /proc/self/stat 파싱 (utime + stime)
// clock_gettime(CLOCK_PROCESS_CPUTIME_ID) 로 프로세스 CPU 시간
struct timespec cpu_before, cpu_after;
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_before);
// ... 측정 ...
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_after);
double cpu_pct = (cpu_after - cpu_before) / (elapsed_ns * nCores) * 100.0;
```

### 11.2 메모리 사용량

```cpp
// Linux: /proc/self/status → VmRSS 파싱
// 또는 getrusage(RUSAGE_SELF, &usage) → ru_maxrss (KB)
struct rusage usage;
getrusage(RUSAGE_SELF, &usage);
double mem_mb = usage.ru_maxrss / 1024.0;  // Linux: KB → MB
```

---

## 12. 스크립트

### 12.1 run_benchmarks.sh (루트)

core/perf/run_benchmarks.sh 와 동일한 인터페이스:

```bash
./run_benchmarks.sh [options]

Options:
  --pattern NAME              패턴 (쉼표 구분) 또는 ALL
  --runs N                    반복 횟수 (기본: 1)
  --duration N                활성 측정 초 (기본: 5, 내부: single=환경변수 PERF_SINGLE_DURATION_SECONDS, multi=--multi-duration-seconds)
  --reuse-build               빌드 재사용
  --clean-build               클린 빌드
  --build-dir PATH            빌드 디렉토리
  --output PATH               콘솔 tee
  --results-dir PATH          결과 저장 경로
  --results-tag NAME          파일명 태그
  --pin-cpu                   CPU 고정 (taskset)
  --io-threads N              IO 스레드
  --msg-sizes LIST            메시지 크기 목록 (러너가 size별로 별도 실행; 바이너리 1회 실행은 1 size)
  --transports LIST           트랜스포트
  --hwm N                     소켓 HWM
  --send-hwm N                송신 HWM
  --recv-hwm N                수신 HWM
  --sndtimeo N                송신 타임아웃 (ms)
  --rcvtimeo N                수신 타임아웃 (ms)
```

사용자 진입점. 내부적으로 `bindings/perf/run_policy_bench.py --binding cpp --suite single` 을 호출한다.

### 12.2 run_benchmarks_multi.sh (루트)

```bash
./run_benchmarks_multi.sh [options]

추가 Options:
  --clients N                 클라이언트 수 (기본: 1000, 내부: --multi-clients)
  --warmup N                  웜업 초 (기본: 3, 내부: --multi-warmup-seconds)
  --server-io-threads N       서버 IO 스레드 (기본: non-stream=2, stream=4)
  --client-io-threads N       클라이언트 IO 스레드 (기본: non-stream=2, stream=4)
  --connect-concurrency N     동시 연결 수 (기본: 128)
  --transport-transition-ms N 트랜스포트 전환 대기 (기본: 3000)
  --pattern-transition-ms N   패턴 전환 대기 (기본: 3000)
  --server-ready-timeout-ms N 서버 준비 대기 (기본: 10000)
  --connect-ready-timeout-ms N 연결 준비 대기 (기본: 5000)
  --monitor-hwm N             모니터 HWM (기본: 200000, 내부: --multi-monitor-hwm)
  --server-shutdown-timeout-ms N 서버 종료 대기 (기본: 5000)
  --server-bind-port N        서버 포트 고정 (기본: 0=자동)
```

사용자 진입점. 내부: `bindings/perf/run_policy_bench.py --binding cpp --suite multi`

### 12.3 run_benchmarks.ps1 / run_benchmarks_multi.ps1

동일 인터페이스, PowerShell 구현.

### 12.4 Preflight 검증 (multi)

```bash
# nofile check: ulimit -n >= (clients * 3 + 4096)
ensure_nofile_limit $CLIENTS
# 환경변수 PERF_SKIP_NOFILE_CHECK=1 로 건너뛸 수 있음

# memory check: 가용 RAM >= base_mb + clients * per_client_kb / 1024
ensure_memory_budget $CLIENTS
# 환경변수 PERF_SKIP_MEMORY_CHECK=1 로 건너뛸 수 있음
```

---

## 13. 산출물

### 13.1 결과 파일

```
bindings/cpp/perf/results/single/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
bindings/cpp/perf/results/multi/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
```

**report 파일** (테이블만, `--result` + `status=complete` 시 생성):
```
## PATTERN: PAIR
### tcp
| Size | Throughput | Bandwidth | Latency |
|------|-----------|-----------|---------|
| 64   | 523401.23 | 33.50     | 12.35   |
...
```

**tmp/baseline 파일** (메타 + RESULT + 테이블):
```
META,os,linux
META,cpu,x86_64
META,cores,8
META,build,Release
META,commit,abc1234
META,timestamp,2026-03-05T12:00:00+09:00
META,load_avg,1.23
META,mode,observe
META,runs,1
META,clients,1000
META,status,complete
META,expected,936
META,actual,936
RESULT,current,PAIR,tcp,64,throughput,523401.23
RESULT,current,PAIR,tcp,64,bandwidth,33.50
RESULT,current,PAIR,tcp,64,latency,12.35
...
TABLE
## PATTERN: PAIR
### tcp
| Size | Throughput | Bandwidth | Latency |
|------|-----------|-----------|---------|
| 64   | 523401.23 | 33.50     | 12.35   |
...
```

> **포맷 구분 요약:**
> - **report 파일**: 사람이 읽기 좋은 테이블만 저장. META/RESULT 라인 없음.
> - **tmp/baseline 파일**: `META,key,value` + `RESULT,...` + `TABLE` 구분자 + 테이블. 완료 상태는 `META,status,complete|partial` 로 기록.

### 13.2 결과 보존 정책

- 최대 100개 파일 per report/ 디렉토리
- 초과 시 파일명 정렬 기준 oldest 삭제 (FIFO)

---

## 14. 구현 순서

### Phase 0: 인증서 및 인프라 준비

1. `bindings/cpp/tests/certs/gen/` 인증서 파일 3개 존재 확인 (server.crt, server.key, ca.crt)
2. `.gitignore`, `.gitkeep` 파일 생성
3. `run_policy_bench.py` 의 cpp 패턴 스펙 등록 확인

### Phase 1: 인프라 (공통 유틸, 진입점)

4. `single/common/perf_single_common.hpp` + `.cpp` — ctx/socket RAII, 환경변수 파싱, EAGAIN/EINTR 루프 헬퍼, RESULT 출력
5. `single/common/perf_single_tls.hpp` — single TLS 인증서 리졸버 (파일 경로 기반)
6. `single/common/perf_single_metric_header.hpp` — 페이로드 헤더 (core 1:1)
7. `single/common/perf_single_runner.hpp` + `.cpp` — `run_standard_bench_main()` 진입점
8. `multi/common/perf_common.hpp` — multi 공통 유틸 (ctx_guard_t, socket_guard_t, monitor)
9. `multi/common/perf_common_multi.hpp` — multi 설정 리졸버 (multi_bench_settings_t)
10. `multi/common/perf_multi_client_helpers.hpp` — 공통 client 루프 헬퍼
11. `multi/common/perf_multi_entry.hpp` — set_perf_multi_pattern_env
12. `multi/common/perf_multi_tls.hpp` — TLS 인증서 리졸버
13. `multi/common/perf_multi_metric_header.hpp` — 멀티 페이로드 헤더 (core 1:1)
14. `multi/common/perf_multi_server_runner.cpp` — 서버 진입점 디스패치
15. `multi/common/perf_multi_client_runner.cpp` — 클라이언트 진입점 디스패치

### Phase 2: Single 소켓 패턴 (6개)

16. `single/current/perf_pair.cpp`
17. `single/current/perf_pubsub.cpp`
18. `single/current/perf_dealer_dealer.cpp`
19. `single/current/perf_dealer_router.cpp`
20. `single/current/perf_router_router.cpp`
21. `single/current/perf_router_router_poll.cpp`

### Phase 3: Single 서비스 패턴 (2개)

22. `single/current/perf_gateway.cpp`
23. `single/current/perf_spot.cpp`

### Phase 4: Multi 패턴 — server/client 분리 (6×2 + 3 서버 only)

24. `multi/current/perf_multi_dealer_dealer_server.cpp` + `perf_multi_dealer_dealer_client.cpp`
25. `multi/current/perf_multi_dealer_router_server.cpp` + `perf_multi_dealer_router_client.cpp`
26. `multi/current/perf_multi_router_router_server.cpp` + `perf_multi_router_router_client.cpp`
27. `multi/current/perf_multi_pubsub_server.cpp` + `perf_multi_pubsub_client.cpp`
28. `multi/current/perf_multi_gateway_server.cpp` + `perf_multi_gateway_client.cpp`
29. `multi/current/perf_multi_spot_server.cpp` + `perf_multi_spot_client.cpp`
30. `multi/current/perf_multi_stream_server.cpp` (서버 only)
31. `multi/current/perf_multi_stream_callback_server.cpp` (서버 only)
32. `multi/current/perf_multi_stream_len32be_server.cpp` (서버 only)

### Phase 5: 스크립트 및 마무리

33. `run_benchmarks.sh` / `.ps1` (루트 + single/ + multi/)
34. `run_benchmarks_multi.sh` / `.ps1`
35. `run_comparison.py`
36. `README.md`
37. 빌드 검증 (`run_policy_bench.py --binding cpp --suite single --reuse-build`)
38. `run_policy_bench.py` 통합 검증

### Phase 6: 코드 리뷰 및 리팩토링

> 성능 벤치마크 코드이므로 측정 오차를 최소화하기 위해 불필요한 오버헤드를 제거한다.
> 모든 패턴 구현 완료 후, 아래 항목을 체계적으로 리뷰하고 수정한다.

#### 6.1 데드코드 / 미사용 코드 제거

39. 모든 소스 파일(`*.cpp`, `*.hpp`)에서 미사용 `#include`, 미사용 변수, 미사용 함수, 주석 처리된 코드 블록을 검출하고 삭제한다.
40. 빌드 스크립트/CMakeLists.txt 에서 참조되지 않는 타겟, 불필요한 정의를 삭제한다.

```bash
# 미사용 include 검출 (clang-tidy 또는 수동)
# 미사용 변수 검출
$CXX -Wall -Wunused-variable -Wunused-function -Wunused-parameter \
  -std=c++17 -fsyntax-only bindings/cpp/perf/**/*.cpp
```

#### 6.2 불필요한 할당/복사 제거 (성능 크리티컬)

41. **핫 루프 내 동적 할당 검사**: send/recv 루프 내에서 `new`, `malloc`, `std::vector<>` 재할당, `std::string` 임시 생성이 발생하지 않는지 확인한다.
    - 페이로드 버퍼는 루프 진입 전 한 번만 할당하고 재사용해야 한다.
    - `std::string endpoint` 등 반복 생성은 루프 밖으로 호이스팅한다.

42. **불필요한 복사 검사**: 큰 버퍼(`payload`, `routing_id`)가 값으로 전달되는 곳을 찾아 `const&` 또는 포인터로 전환한다.
    - `std::string` 반환값이 NRVO/move 로 최적화되는지 확인한다.
    - `message_t` 가 불필요하게 복사되지 않는지 확인한다 (move semantics 활용).

```bash
# 핫 루프 내 할당 패턴 검출
rg -n "std::vector|std::string|new |malloc|make_unique|make_shared" \
  bindings/cpp/perf --glob '*.cpp' --glob '*.hpp'
# 각 발견 건에 대해 루프 내부 여부를 수동 검증
```

#### 6.3 불필요한 대기/동기화 제거

43. **측정 구간 내 불필요 sleep 검사**: Active 페이즈 내에 `sleep`, `usleep`, `std::this_thread::sleep_for` 호출이 없는지 확인한다.
    - Warmup/Settle/Drain 페이즈의 sleep 은 정상 (core 동일).
    - 측정 구간 내 sleep 은 throughput 왜곡을 유발하므로 금지.

44. **불필요 mutex/lock 검사**: 단일 스레드 경로에 불필요한 동기화 프리미티브가 없는지 확인한다.
    - multi server 의 메인 스레드 send/recv 루프에 lock 이 있으면 제거.
    - latency sampler 가 단일 스레드에서만 접근되면 atomic 불필요.

#### 6.4 API 호출 최적화

45. **소켓 옵션 중복 설정 검사**: 동일 옵션이 여러 번 설정되는 패턴을 찾아 한 번으로 통합한다.
    - `apply_single_hwm()` + 수동 `sock.set(sndhwm, ...)` 중복 등.

46. **DONTWAIT vs blocking 모드 검사**: recv 루프에서 불필요하게 `recv_flag::dontwait` + busy-wait 하는 대신, 타임아웃 기반 blocking recv 가 적절한 곳을 식별한다.
    - 단, core 와 동일한 패턴을 유지해야 하므로, core 에서 dontwait 을 쓰는 곳은 그대로 유지.

#### 6.5 주석 정리 및 추가

47. 각 패턴 파일의 상단에 **패턴 설명, 소켓 토폴로지, 측정 방식**을 요약하는 블록 주석을 추가한다.

```cpp
// ─── perf_pair.cpp ───
// PAIR 패턴 벤치마크: 2개의 PAIR 소켓 간 양방향(echo) 통신.
// 토폴로지: s_bind(PAIR) <──bind/connect──> s_conn(PAIR)
// 측정: s_conn→s_bind 방향 one-way throughput + round-trip latency
// 페이즈: Warmup(count) → Active(duration, throughput + latency 동시 측정)
```

48. 공통 유틸 함수에 **파라미터 의미와 반환값** 주석을 추가한다 (단, 자명한 함수에는 생략).

49. 핫 루프 내 비자명 로직에 **인라인 주석**을 추가한다.
    - reservoir sampling 로직, queue probe 주기, phase transition 조건 등.

50. 불필요한 기존 주석(TODO, FIXME, 임시 메모, 주석 처리된 코드)을 삭제한다.

---

## 15. 정책 준수 사항

### 15.1 금지 사항

- **C API 직접 호출 금지**: `zlink_*()` 함수 직접 호출 일체 금지
- **정책적 Retry 금지**: send/recv 실패 시 재시도 없음. 단, EAGAIN/EINTR 은 정상 흐름으로 루프 허용 (이는 retry 가 아닌 비동기 I/O 패턴)
- **Inflight/Outstanding 옵션 금지**: 백프레셔 한도 = 소켓 HWM 만
- **stub 파일 금지**: `#include` 한 줄 위임 파일 방식 분리 금지
- **core 바이너리 프록시 호출 금지**: 자체 포팅 구현으로만 동작

### 15.2 필수 사항

- 각 벤치마크 소스에 **소켓 생성, bind/connect, send/recv 루프, 페이즈 컨트롤** 인라인
- `RESULT,current,...` 형식의 stdout 출력
- STREAM 서버는 stop-token `__zlink_perf_stop__` 수신 시 정상 종료
- Multi 서버는 `READY,<endpoint>` stdout 출력 후 클라이언트 대기
- TLS 인증서는 `bindings/cpp/tests/certs/gen/` 경로 사용

### 15.3 코드 인라이닝 정책

- 각 패턴 파일에 메인 루프 로직 인라인 (core/perf 동일)
- `common/perf_single_common` 으로 추출 허용: 환경변수 파싱, EAGAIN/EINTR 루프, printResult, 엔드포인트 생성
- Multi 클라이언트는 `common/perf_multi_client_helpers` 의 공통 루프 위임 허용
- STREAM 서버 인프라는 모듈화 허용

### 15.4 C++ API 강제 원칙

- `bindings/cpp/perf/**/*.cpp,*.hpp` 내부에서 `zlink_*` 함수 직접 호출 금지
- 허용 API:
  - `zlink::context_t`, `zlink::socket_t`, `zlink::message_t`, `zlink::poller_t`
  - `zlink::service::gateway_t`, `receiver_t`, `spot_node_t`, `discovery_t`
  - `zlink::has` 등 runtime API
- 포팅 중 필요한 기능이 C++ API에 없으면 `bindings/cpp/include/zlink` 에 먼저 메서드를 확장하고, perf 에서는 확장된 C++ 메서드만 사용한다

---

## 16. 검증 계획

### 16.1 정적 검증

- C API 직접 호출 금지 검사:
  ```bash
  # C++ binding perf 소스에서 zlink_ 직접 호출이 없어야 함
  rg -n "\\bzlink_[a-zA-Z0-9_]+\\(" bindings/cpp/perf --glob '*.{cpp,hpp}'
  # 기대 결과: 0건
  ```
- stream client 공유 경로 확인:
  ```bash
  rg -n "core/perf/common/streamclient" bindings/cpp/perf/run_comparison.py
  # 기대 결과: 공용 경로만 참조
  ```
- TLS 인증서 경로 확인:
  ```bash
  rg -n "tests/certs" bindings/cpp/perf --glob '*.{cpp,hpp}'
  # 기대 결과: bindings/cpp/tests/certs/gen 경로만 사용, core/tests/certs 참조 없음
  ```

### 16.2 빌드 검증

- [ ] `run_policy_bench.py --binding cpp --suite single --reuse-build` 빌드 성공 (종료코드 0)
- [ ] `run_policy_bench.py --binding cpp --suite multi --reuse-build` 빌드 성공 (종료코드 0)
- [ ] `bindings/cpp/tests/certs/gen/` 인증서 파일 3개 존재 (server.crt, server.key, ca.crt)
- [ ] 컴파일 경고 0건 (`-Wall -Wextra` 기준)

### 16.3 기능 smoke 테스트

- single smoke:
  ```bash
  PERF_SINGLE_DURATION_SECONDS=1 \
  python3 bindings/perf/run_policy_bench.py \
    --binding cpp --suite single \
    --pattern PAIR --transports tcp --msg-sizes 64 \
    --runs 1 --reuse-build
  ```
- multi smoke:
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding cpp --suite multi \
    --pattern MULTI_DEALER_DEALER --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 10 --reuse-build
  ```
- stream smoke:
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding cpp --suite multi \
    --pattern MULTI_STREAM --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 100 --reuse-build
  ```
- TLS smoke:
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding cpp --suite single \
    --pattern PAIR --transports tls --msg-sizes 64 \
    --runs 1 --reuse-build
  ```
  > **참고**: single duration 은 CLI 옵션이 아니라 환경변수 `PERF_SINGLE_DURATION_SECONDS` 로 제어한다.

### 16.4 메트릭 정확성 검증

각 RESULT 라인의 메트릭 값이 논리적으로 정확한지 검증한다.

**러너 집계 대상 메트릭 (조합별):**
- single: `throughput`, `bandwidth`, `latency` — 3개 메트릭이 모든 pattern/transport/size 조합에 존재 (completion 기준)
- single 리소스: `cpu_pct`, `mem_mb` (러너가 프로세스 모니터링으로 수집)
- multi: `throughput`, `bandwidth`, `latency` + `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb`

**바이너리 자체 검증용 메트릭 (러너 미집계):**
- `latency_p95`, `latency_p99` — 바이너리가 stdout 출력하나 러너 파서 미인식

**대역폭 계산식 검증:**
```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```
- single 전체 (one-way 방향): `bandwidth ≈ throughput × size / 1,000,000`
- multi echo 패턴 (DEALER_ROUTER, ROUTER_ROUTER, STREAM*): `bandwidth ≈ throughput × size × 2 / 1,000,000`
- multi one-way 패턴 (DEALER_DEALER, PUBSUB, GATEWAY, SPOT): `bandwidth ≈ throughput × size / 1,000,000`
- 허용 오차: ±1%

**Percentile 일관성 검증 (바이너리 자체 stdout 확인, 러너 미집계):**
- `latency_p95 >= latency` (mean)
- `latency_p99 >= latency_p95`

**메트릭 값 범위 검증:**
- `throughput > 0` (유효한 처리량)
- `bandwidth > 0`
- `latency > 0` (유효한 레이턴시)
- `cpu_pct >= 0 && cpu_pct <= 100 × nCores`
- `mem_mb > 0`

**검증 방법:**
```bash
# single smoke 결과 파일에서 RESULT 라인 추출 후 검증
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024 \
  --runs 1 --reuse-build --result

# 결과 파일에서 bandwidth 계산 검증
# RESULT,current,PAIR,tcp,64,throughput,X
# RESULT,current,PAIR,tcp,64,bandwidth,Y
# 검증: abs(Y - X * 64 / 1000000) / Y < 0.01
```

**multi 메트릭 검증 예시:**
```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite multi \
  --pattern MULTI_DEALER_ROUTER --transports tcp --msg-sizes 64,1024 \
  --runs 1 --multi-duration-seconds 2 --multi-clients 10 --reuse-build --result

# echo 패턴이므로 bandwidth = throughput × size × 2 / 1,000,000
```

### 16.5 사이즈별 순차 실행 및 결과 수집 검증

**요구 동작:**
- `pattern/transport` 실행 중 각 `size` 테스트가 순차적으로 완료된다.
- 러너는 각 size 바이너리 실행 완료 후 stdout 을 캡처하여 RESULT 라인을 파싱한다 (실시간 중계가 아닌 프로세스 종료 후 일괄 수집).
- 모든 size 실행이 완료된 후 러너가 파싱된 결과를 테이블로 출력하고, 다음 transport 또는 pattern 으로 전환된다.

> **참고**: 러너는 `subprocess.Popen(stdout=PIPE)` + `proc.communicate()` 로 child stdout 을 캡처하므로, 바이너리의 RESULT 출력이 사용자 콘솔에 실시간으로 보이지 않는다. 진행 상황은 러너 레벨의 로그를 통해 확인한다.

**검증 방법:**
```bash
# 3개 사이즈로 실행 (--results-tag 로 파일명 고정, tmp 파일은 항상 생성됨)
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --reuse-build \
  --results-tag size_order_check
```

> **주의**: `--save` 를 사용하면 baseline 이 갱신되고, partial 상태일 경우 에러 종료가 발생할 수 있다. 순서 검증 목적에는 `--save` 없이 tmp 파일만 사용한다.

**tmp 파일 기반 검증 기준:**
1. tmp 파일에서 `RESULT,current,PAIR,tcp,64,...` 라인이 `RESULT,current,PAIR,tcp,1024,...` 보다 먼저 나옴
2. `RESULT,current,PAIR,tcp,1024,...` 라인이 `RESULT,current,PAIR,tcp,65536,...` 보다 먼저 나옴
3. 각 사이즈의 러너 인식 메트릭 (throughput, bandwidth, latency) 이 연속으로 기록됨
4. 모든 RESULT 라인이 `TABLE` 구분자보다 앞에 위치함

> **참고**: 러너는 child stdout 을 `subprocess.Popen(stdout=PIPE)` + `proc.communicate()` 로 캡처하여 파싱하므로, `--output` 콘솔 로그에는 RESULT 원본 라인이 아닌 러너의 테이블/진행 로그가 출력된다. RESULT 라인 순서 검증은 tmp 파일에서 수행한다.

**자동 검증 스크립트 (선택):**
```python
# --results-tag 로 고정된 tmp 파일에서 RESULT 라인의 size 값 순서 검증
import re, glob
sizes_seen = []
target = glob.glob("bindings/cpp/perf/results/single/tmp/perf_*_size_order_check.txt")
assert target, "tmp file not found — --results-tag size_order_check 실행 필요"
with open(target[0]) as fh:
    for line in fh:
        m = re.match(r"RESULT,current,PAIR,tcp,(\d+),throughput,", line)
        if m:
            sizes_seen.append(int(m.group(1)))
assert sizes_seen == [64, 1024, 65536], f"size order mismatch: {sizes_seen}"
```

### 16.6 기본 설정 전체 실행 무실패 검증

기본 옵션(옵션 미지정)으로 single/multi 전체를 실행하여 모든 패턴이 실패 없이 완료되는지 확인한다.

**single 기본 실행:**
```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite single --result
```

**multi 기본 실행:**
```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite multi --result
```

**합격 기준:**
- [ ] 두 실행 모두 프로세스 종료코드 `0`
- [ ] tmp/baseline 파일에서 `META,status,complete` 확인 (report 파일은 테이블만 포함, 메타 정보 없음)
- [ ] tmp/baseline 파일에서 `META,expected,N` 과 `META,actual,N` 이 동일 (누락 없음)
- [ ] 콘솔 로그에 fail 조합 0건
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)

> **참고**: `run_policy_bench.py` 결과 파일 구조:
> - **tmp/baseline 파일**: `META,key,value` + `RESULT,...` + `TABLE` + 테이블 라인 (메타 포함)
> - **report 파일**: 테이블 라인만 저장 (META/RESULT 없음, `--result` + `status=complete` 시에만 생성)

**기본 조합 수 예상 (single):**
- socket 패턴 (6종: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL) × 6 transport (tcp,tls,ws,wss,inproc,ipc) × 6 size = 216 조합
- GATEWAY, SPOT (2종) × 4 transport (tcp,tls,ws,wss) × 6 size = 48 조합
- STREAM, STREAM_CALLBACK, STREAM_LEN32BE (3종) × 4 transport (tcp,tls,ws,wss) × 4 size = 48 조합
- 총: 312 조합 → UNSUPPORTED 제외한 나머지 전부 success

**기본 조합 수 예상 (multi):**
- 비-STREAM 패턴 (6종) × 4 transport × 6 size = 144 조합
- STREAM 패턴 (3종) × 4 transport × 4 size = 48 조합
- 총: 192 조합 → UNSUPPORTED 제외한 나머지 전부 success

**결과 파일 확인:**
```bash
# report 파일이 생성되었는지 확인 (테이블만 포함)
ls -la bindings/cpp/perf/results/single/report/perf_*.txt
ls -la bindings/cpp/perf/results/multi/report/perf_*.txt

# tmp 파일에서 completion 메타 확인 (report 에는 메타 없음)
grep "^META,status," bindings/cpp/perf/results/single/tmp/perf_*.txt
# 기대 출력: META,status,complete

grep "^META,expected\|^META,actual" bindings/cpp/perf/results/single/tmp/perf_*.txt
# 기대 출력: META,expected,N 과 META,actual,N 이 동일
```

### 16.7 코드 리뷰 검증 (Phase 6 결과)

Phase 6 리팩토링 완료 후 다음 항목을 자동/수동으로 재검증한다.

**데드코드 제거 확인:**
```bash
# 주석 처리된 코드 블록 (연속 2줄 이상 주석) 검출
rg -n "^\\s*//" bindings/cpp/perf --glob '*.{cpp,hpp}' | \
  awk -F: '{if(prev_file==$1 && prev_line+1==$2) count++; else count=1; prev_file=$1; prev_line=$2; if(count>=3) print}'
# 기대 결과: 정당한 블록 주석만 존재
```

**핫 루프 할당 검출:**
```bash
# Active 페이즈 루프 내 동적 할당 검출
rg -n "new |malloc|std::vector<|std::string " bindings/cpp/perf --glob '*.{cpp,hpp}'
# 각 건에 대해 루프 내부 여부를 수동 검증
# 기대: 루프 밖 할당만 존재, 루프 내 할당 0건
```

**불필요 복사 검출:**
```bash
# 값 전달 패턴 검출 (std::string, std::vector 값 파라미터)
rg -n "\\(std::string [^&*]|\\(std::vector<" bindings/cpp/perf --glob '*.{cpp,hpp}'
# 기대: 모든 큰 타입은 const& 또는 && 로 전달
```

**빌드 경고 제로 확인:**
```bash
CXX=${CXX:-c++}; $CXX -Wall -Wextra -Wpedantic -Wunused -std=c++17 -fsyntax-only \
  bindings/cpp/perf/single/current/*.cpp \
  -Ibindings/cpp/include -Icore/include -Ibindings/cpp/perf/single/common
# 기대 결과: 경고 0건
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `current/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 별도 파일로 분리.
- runner 옵션/기본값/결과 형식이 core 와 동등하게 동작.
- single/multi 모든 패턴 바이너리가 `bindings/cpp/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 C API (`zlink_*`) 직접 호출 0건 — C++ binding API 만 사용.
- TLS 인증서는 `bindings/cpp/tests/certs/gen/` 에서 독립 관리.
- 정책적 retry 로직/우회 wrapper/비정책 실행 경로가 없음 (EAGAIN/EINTR 루프는 허용).
- **메트릭 헤더**: single 은 SPF1 (0x53504631), multi 는 MPF1 (0x4D504631) 페이로드 헤더를 stamp/decode 하여 phase 필터링 및 latency 측정에 사용 (core 1:1).
- **메트릭 정확성**(필수 메트릭 존재/bandwidth 계산식/percentile 일관성/값 범위)이 검증됨.
- **사이즈별 순차 실행 및 결과 수집**이 검증됨 (러너가 각 size 바이너리 실행 후 stdout 캡처·파싱하여 tmp 파일에 RESULT 순서대로 기록).
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- **코드 리뷰 완료**: 데드코드 0건, 핫 루프 내 불필요 할당/복사/대기 0건, 컴파일 경고 0건.
- **주석 정리 완료**: 각 패턴 파일 상단 블록 주석, 공통 유틸 함수 파라미터 주석, 핫 루프 인라인 주석이 적절한 수준으로 추가됨.
