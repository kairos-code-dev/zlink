# C++ Binding Perf Benchmark Implementation Plan

> core/perf (C API) 벤치마크를 bindings/cpp/perf 로 1:1 포팅한다.
> **C++ binding API (`zlink::context_t`, `zlink::socket_t`, `zlink::service::*`) 만 사용하며, zlink C API (`zlink_*()`) 직접 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `src/` 분리 구조를 C++ binding 에서 그대로 반영한다.

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
    ├── CPP_PORTING_PLAN.md                  ← 본 문서
    ├── README.md                            ← 사용법 안내
    ├── .gitignore                           ← build/ 제외
    │
    ├── single/
    │   ├── common/                          ← ★ core/perf/single/common/ 대응
    │   │   ├── perf_single_common.cpp       ← 공통 유틸 구현 (★ core는 header-only, 아래 참고)
    │   │   ├── perf_single_common.hpp       ← 공통 유틸 헤더
    │   │   ├── perf_single_runner.cpp       ← run_standard_bench_main() 구현
    │   │   ├── perf_single_runner.hpp       ← runner 헤더
    │   │   ├── perf_single_tls.hpp          ← TLS 인증서 경로 리졸버 (single)
    │   │   └── perf_single_metric_header.hpp ← 페이로드 헤더 (core 1:1 유지)
    │   ├── src/                             ← ★ core/perf/single/src/ 대응
    │   │   ├── perf_pair.cpp
    │   │   ├── perf_pubsub.cpp
    │   │   ├── perf_dealer_dealer.cpp
    │   │   ├── perf_dealer_router.cpp
    │   │   ├── perf_router_router.cpp
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
    │   │   ├── perf_client_helpers.hpp       ← 공통 client 루프 헬퍼
    │   │   ├── perf_entry.hpp               ← set_perf_pattern_env 헬퍼
    │   │   ├── perf_tls.hpp                 ← TLS 인증서 경로 리졸버 (multi)
    │   │   ├── perf_metric_header.hpp       ← 페이로드 헤더 (core 1:1 유지)
    │   │   └── (runner 소스 없음: 각 패턴 파일이 main() 직접 소유)
    │   ├── src/                             ← ★ core/perf/multi/src/ 대응
    │   │   ├── perf_dealer_dealer_server.cpp       ← ★ server/client 분리
    │   │   ├── perf_dealer_dealer_client.cpp
    │   │   ├── perf_dealer_router_server.cpp
    │   │   ├── perf_dealer_router_client.cpp
    │   │   ├── perf_router_router_server.cpp
    │   │   ├── perf_router_router_client.cpp
    │   │   ├── perf_pubsub_server.cpp
    │   │   ├── perf_pubsub_client.cpp
    │   │   ├── perf_gateway_server.cpp
    │   │   ├── perf_gateway_client.cpp
    │   │   ├── perf_spot_server.cpp
    │   │   ├── perf_spot_client.cpp
    │   │   ├── perf_stream_server.cpp             ← 서버 only (클라이언트=공통 stream client)
    │   │   ├── perf_stream_callback_server.cpp
    │   │   └── perf_stream_len32be_server.cpp
    │   ├── build/                           ← 컴파일된 바이너리 (gitignore)
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── results/
    │   ├── single/
    │   │   └── report/.gitkeep
    │   └── multi/
    │       └── report/.gitkeep
    │
    ├── run_benchmarks.sh                    ← 루트 single 래퍼
    ├── run_benchmarks.ps1
    ├── run_benchmarks_multi.sh              ← 루트 multi 래퍼
    ├── run_benchmarks_multi.ps1
    └── run_comparison.py                    ← 루트 오케스트레이터
```

> **core/perf 와의 common/ 구조 차이:**
> core/perf 의 single/common, multi/common 은 **header-only** (`.hpp` 인라인 함수만) 이다.
> 단, `core/perf/common/streamclient/` 에는 `perf_stream_client.cpp` 가 포함되어 있어
> common 전체가 header-only 인 것은 아니다 (streamclient 는 독립 바이너리로 빌드됨).
> C++ binding 포팅에서는 C++ API 치환으로 인해 함수 본체가 커지므로,
> `perf_single_common.cpp` 와 `perf_single_runner.cpp` 를 별도 소스로 분리한다.
> 이는 **빌드 단위 분리** (컴파일 시간 단축, ODR 위반 방지) 를 위한 의도적 설계이다.
> metric header (`.hpp`) 와 TLS 리졸버 (`.hpp`) 는 core 와 동일하게 header-only 로 유지한다.

### core/perf 대비 구조 매핑

| core/perf | cpp/perf | 비고 |
|-----------|----------|------|
| `single/common/bench_common.hpp` | `single/common/perf_single_common.hpp` + `.cpp` | C++ API 치환, 헤더/소스 분리 |
| `single/common/perf_single_metric_header.hpp` | `single/common/perf_single_metric_header.hpp` | 구조/상수 1:1 유지 |
| `single/src/perf_pair.cpp` | `single/src/perf_pair.cpp` | 1:1 매핑, C++ API 사용 |
| `single/src/perf_dealer_dealer.cpp` | `single/src/perf_dealer_dealer.cpp` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/common/perf_common.hpp` | C++ API 치환 |
| `multi/common/perf_common_multi.hpp` | `multi/common/perf_common_multi.hpp` | 1:1 유지 |
| `multi/common/perf_client_helpers.hpp` | `multi/common/perf_client_helpers.hpp` | C++ API 치환 |
| `multi/common/perf_entry.hpp` | `multi/common/perf_entry.hpp` | 1:1 유지 |
| `multi/src/perf_dealer_dealer_server.cpp` | `multi/src/perf_dealer_dealer_server.cpp` | ★ server/client 분리 유지 |
| `multi/src/perf_stream_server.cpp` | `multi/src/perf_stream_server.cpp` | 서버 only |

---

## 2. 빌드 시스템

### 2.1 빌드 방식 (run_policy_bench.py 경유 컴파일)

> **사용자 진입점**: `run_benchmarks.sh` → `run_policy_bench.py --binding cpp`
> **내부 빌드 엔진**: `run_policy_bench.py --binding cpp` 가 컴파일러를 직접 호출
>
> **core/perf 엔트리 체인과의 차이**: core/perf 는 `run_benchmarks.sh` → `single/run_comparison.py` (기본),
> `PERF_ALLOW_MULTI=1` 시 루트 `run_comparison.py` 를 사용한다. cpp binding 은 `run_policy_bench.py` 경유.

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
`add_subdirectory(perf)` 로 활성화되며, 타겟 구조는 core/perf 의 패턴별 바이너리 분리 원칙을 따른다.
> 단, core 는 `src/*.cpp` 단일 소스 빌드인 반면, cpp binding 은
> single 은 `common/*.cpp + src/*.cpp`, multi 는 `src/*.cpp` 단독(main 포함)으로 빌드한다.

**Single 타겟:**

| 타겟 | 소스 |
|------|------|
| `perf_pair` | `single/common/*.cpp` + `single/src/perf_pair.cpp` |
| `perf_pubsub` | `single/common/*.cpp` + `single/src/perf_pubsub.cpp` |
| `perf_dealer_dealer` | `single/common/*.cpp` + `single/src/perf_dealer_dealer.cpp` |
| `perf_dealer_router` | `single/common/*.cpp` + `single/src/perf_dealer_router.cpp` |
| `perf_router_router` | `single/common/*.cpp` + `single/src/perf_router_router.cpp` |
| `perf_gateway` | `single/common/*.cpp` + `single/src/perf_gateway.cpp` |
| `perf_spot` | `single/common/*.cpp` + `single/src/perf_spot.cpp` |

**Multi 타겟:**

| 타겟 | 소스 |
|------|------|
| `comp_src_dealer_dealer_server` | `multi/src/perf_dealer_dealer_server.cpp` |
| `comp_src_dealer_dealer_client` | `multi/src/perf_dealer_dealer_client.cpp` |
| `comp_src_dealer_router_server` | 동일 패턴 |
| `comp_src_dealer_router_client` | 동일 패턴 |
| `comp_src_router_router_server` | 동일 패턴 |
| `comp_src_router_router_client` | 동일 패턴 |
| `comp_src_pubsub_server` | 동일 패턴 |
| `comp_src_pubsub_client` | 동일 패턴 |
| `comp_src_gateway_server` | 동일 패턴 |
| `comp_src_gateway_client` | 동일 패턴 |
| `comp_src_spot_server` | 동일 패턴 |
| `comp_src_spot_client` | 동일 패턴 |
| `comp_src_stream_server` | 서버 only |
| `comp_src_stream_callback_server` | 서버 only |
| `comp_src_stream_len32be_server` | 서버 only |

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

> **Single 패턴 구분:**
> - **러너 기준 전체 11개**: 위 8개 + STREAM, STREAM_CALLBACK, STREAM_LEN32BE (3개는 multi stream server + core 공통 stream client 조합으로 위임, `supports_split_multi(cpp)=true`)

### 3.2 Multi 실행

**서버:**
```bash
bindings/cpp/perf/multi/build/comp_src_dealer_dealer_server <TRANSPORT> <SIZE>
```

**클라이언트:**
```bash
bindings/cpp/perf/multi/build/comp_src_dealer_dealer_client <TRANSPORT> <SIZE> --endpoint <ENDPOINT>
```

### 3.3 STREAM 패턴 클라이언트

STREAM, STREAM_CALLBACK, STREAM_LEN32BE 패턴은:
- **서버**: C++ binding 벤치마크가 직접 구현 (`zlink::socket_t::stream_attach()` API)
- **클라이언트**: `core/perf/common/streamclient/build/perf_stream_client` (core 공통 바이너리, 바인딩 무관) 사용
- `run_policy_bench.py` 가 자동으로 공통 stream client 를 호출한다

### 3.4 run_policy_bench.py 패턴 스펙 (기존 등록 완료)

```python
CPP_SINGLE_PATTERN_SPECS = {
    "PAIR":               ("perf_pair.cpp",              "run_pattern_pair",              "perf_pair"),
    "PUBSUB":             ("perf_pubsub.cpp",            "run_pattern_pubsub",            "perf_pubsub"),
    "DEALER_DEALER":      ("perf_dealer_dealer.cpp",     "run_pattern_dealer_dealer",     "perf_dealer_dealer"),
    "DEALER_ROUTER":      ("perf_dealer_router.cpp",     "run_pattern_dealer_router",     "perf_dealer_router"),
    "ROUTER_ROUTER":      ("perf_router_router.cpp",     "run_pattern_router_router",     "perf_router_router"),
    "GATEWAY":            ("perf_gateway.cpp",           "run_pattern_gateway",           "perf_gateway"),
    "SPOT":               ("perf_spot.cpp",              "run_pattern_spot",              "perf_spot"),
}

CPP_MULTI_SERVER_PATTERN_SPECS = {
    "MULTI_DEALER_DEALER":   ("perf_dealer_dealer_server.cpp",   ...),
    "MULTI_DEALER_ROUTER":   ("perf_dealer_router_server.cpp",   ...),
    "MULTI_ROUTER_ROUTER":   ("perf_router_router_server.cpp",   ...),
    "MULTI_PUBSUB":          ("perf_pubsub_server.cpp",          ...),
    "MULTI_GATEWAY":         ("perf_gateway_server.cpp",         ...),
    "MULTI_SPOT":            ("perf_spot_server.cpp",            ...),
    "MULTI_STREAM_CALLBACK": ("perf_stream_callback_server.cpp", ...),
    "MULTI_STREAM_LEN32BE":  ("perf_stream_len32be_server.cpp",  ...),
}

CPP_MULTI_CLIENT_PATTERN_SPECS = {
    # 6개 비-STREAM 패턴: dealer_dealer, dealer_router, router_router, pubsub, gateway, spot
}
```

---

## 4. RESULT 출력 형식 (core/perf 동일)

**Latency 단위 규칙:**
- `RESULT ... latency`, `latency_p95`, `latency_p99` 값은 **ms 단위**로 출력한다.
- 내부 헤더 타임스탬프(`sent_ts_us`)와 샘플러 누적은 `us` 기반으로 유지하되, RESULT 출력 시 `us / 1000.0`으로 변환한다.

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

**러너 파서 인식 (결과 테이블 포함):**
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p95,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p99,<value>
```

**바이너리 자체 출력 전용 (러너 파서 미인식, 로그/디버그용):**
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_snd_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_end,<value>
```

> **참고**: `run_policy_bench.py` 파서는 필수 5개 메트릭(`throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`)을 인식하며 결과 테이블에 포함한다. `server_*_pending_*` 메트릭은 러너 집계 대상이 아님. completion 판정 기준 메트릭 수는 cpp binding 에서 3개(`throughput + bandwidth + latency`), core/perf `run_comparison.py` 에서 5개이다.

### Bandwidth 계산 규칙

```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```

| 구분 | 승수 | 비고 |
|------|------|------|
| **Single 전체** | **1.0** | run_policy_bench.py 기준 모든 single 은 one-way 방향 |
| Multi echo (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*) | 2.0 | 요청+응답 양방향 (러너 `MULTI_ECHO_PATTERNS` 기준) |
| Multi one-way (DEALER_DEALER, PUBSUB, SPOT) | 1.0 | 단방향 (러너 `MULTI_ONE_WAY_PATTERNS` 기준) |

---

## 5. 환경 변수 (core/perf 동일)

### 5.1 Single

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_IO_THREADS` | 0 (기본) | Context IO 스레드 |
| `PERF_WARMUP_COUNT` | 패턴별 가변 (소켓 패턴: 1000, GATEWAY/SPOT: 200) | 웜업 메시지 횟수 (count 기반, 시간 기반 아님). SPOT 은 size≥65536 일 때 20 으로 클램프 |
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

| 변수 | 바이너리 기본값 | 러너 CLI 오버라이드 | 용도 |
|------|----------------|-------------------|------|
| `PERF_CLIENTS` | 패턴별 (비-STREAM: 100, STREAM: 10000) | `--multi-clients 0` (env fallback) | 동시 클라이언트 수 |
| `PERF_WARMUP_SECONDS` | **2** | `--multi-warmup-seconds 3` | 웜업 기간 |
| `PERF_SETTLE_MS` | 500 | — | 측정 전 안정화 + Drain 루프 시간 |
| `PERF_DURATION_SECONDS` | 5 | `--multi-duration-seconds` | 활성 측정 기간 |
| `PERF_ACTIVE_WARMUP` | 0 | — | 0=sleep, 1=active |
| `PERF_HWM` | 패턴별 (비-STREAM: 100, STREAM: 10) | `--multi-hwm 100000` | 소켓 HWM |
| `PERF_SNDHWM` | 미설정 (PERF_HWM 상속) | — | 송신 HWM |
| `PERF_RCVHWM` | 미설정 (PERF_HWM 상속) | — | 수신 HWM |
| `PERF_SNDTIMEO_MS` | **200** | `--multi-sndtimeo-ms 5000` | 송신 타임아웃 |
| `PERF_RCVTIMEO_MS` | **200** | `--multi-rcvtimeo-ms 5000` | 수신 타임아웃 |
| `PERF_CONNECT_READY_TIMEOUT_MS` | 5000 | — | 연결 대기 |
| `PERF_MONITOR_HWM` | **1000** | `--multi-monitor-hwm` | 모니터 소켓 HWM |
| `PERF_SERVER_BIND_PORT` | 0 (자동) | — | 서버 포트 고정 |
| `PERF_IO_THREADS` | 0 | — | IO 스레드 |
| `PERF_CLIENT_POLL_TIMEOUT_MS` | 0 | — | 클라이언트 poll 타임아웃 |
| `PERF_CTX_BLOCKY` | 미설정 | — | Context blocky 모드 (설정 시 적용) |
| `PERF_CTX_TERM` | 미설정 (기본 shutdown만) | — | Context terminate (`1` 시 full term) |

> **참고**: core/perf 에서는 `PERF_MULTI_` 접두어를 사용하지 않는다.
> 러너(`run_policy_bench.py`)는 CLI 옵션을 `PERF_MULTI_*` 이름으로 환경변수에 주입하지만,
> 바이너리가 읽는 환경변수 키는 위 표의 접두어 없는 이름이다.
> C++ binding 구현 시에도 core/perf 와 동일하게 접두어 없는 이름을 사용한다.

---

## 6. 벤치마크 페이즈 (core/perf 동일)

### 6.1 Single 페이즈

```
[Warmup(count)] → [Active(duration) — throughput + latency 동시 측정]
```

1. **Warmup** (`PERF_WARMUP_COUNT`, 소켓 패턴 기본 1000, GATEWAY/SPOT 기본 200): 고정 횟수 send/recv 반복으로 워밍업 (시간 기반이 아님)
2. **Active** (`PERF_SINGLE_DURATION_SECONDS`, 5초): duration 기반 throughput 측정 + reservoir sampling 으로 latency/p95/p99 동시 수집

> **core 구현 참고**: single 은 별도 Settle/Drain/Latency 페이즈가 없다.
> Active 페이즈에서 메시지 헤더의 `sent_ts_us` 를 기반으로 throughput 과 latency 를 동시에 측정한다.
> 수신 측에서 `latency_stats_builder_t` (reservoir sampling) 으로 p95/p99 를 수집한다.

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle(=Drain)] → [Active(duration)]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_WARMUP_SECONDS`, 바이너리 기본 2초, 러너 CLI 기본 3초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_SETTLE_MS`, 500ms): Warmup 후 안정화 루프. Active 이전에 인플라이트 메시지를 drain 하는 역할을 겸한다.
   - one-way 패턴: `phase_drain` 라벨로 recv-only 루프 (큐 비우기)
   - echo 패턴: `phase_warmup` 라벨로 send/recv 루프 (`allow_send=false`, 수신만)
4. **Active** (`PERF_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)

> **주의**: Active 이후 별도 Drain 단계는 **없다**. Settle 단계가 Warmup→Active 전환 시
> 인플라이트 메시지를 drain 하는 역할을 수행한다. `phase_drain` 라벨은 이 Settle 구간에서만 사용된다.

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
| 7 | perf_gateway.cpp | GATEWAY | `service::gateway_t` + `service::receiver_t` | echo | tcp,tls,ws,wss |
| 8 | perf_spot.cpp | SPOT | `service::spot_node_t` (pub/sub) | one-way | tcp,tls,ws,wss |

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 별도 파일**로 분리한다.

| # | 서버 파일 | 클라이언트 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|----------|---------------|------|-----------|----------------|
| 1 | perf_dealer_dealer_server | perf_dealer_dealer_client | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | perf_dealer_router_server | perf_dealer_router_client | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | perf_router_router_server | perf_router_router_client | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | perf_pubsub_server | perf_pubsub_client | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | perf_gateway_server | perf_gateway_client | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | perf_spot_server | perf_spot_client | SPOT | Spot publish | Spot subscribe |
| 7 | perf_stream_server | (core 공통 stream client) | STREAM | STREAM bind, raw echo | core/perf/common/streamclient |
| 8 | perf_stream_callback_server | (core 공통 stream client) | STREAM_CALLBACK | stream_attach callback | core/perf/common/streamclient |
| 9 | perf_stream_len32be_server | (core 공통 stream client) | STREAM_LEN32BE | stream_attach_len32be | core/perf/common/streamclient |

**Multi 서버 통신 프로토콜:**
- 서버 stdout 에 `READY,<payload>` 출력 → 스크립트가 클라이언트 시작
  - 기본 소켓 패턴: `READY,<endpoint>`
  - `GATEWAY`: `READY,<server_endpoint>|<registry_pub_endpoint>|<registry_router_endpoint>`
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
// RESULT 출력값: latency/p95/p99 는 ms 단위 (내부 us -> 출력 시 변환)

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
// 각 single 패턴 파일의 main() 에서 직접 호출
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

**`perf_metric_header.hpp`** — 멀티 페이로드 헤더 (core `perf_metric_header.hpp` 1:1 유지)

single 과 동일 구조, magic 과 phase enum 만 다름:
- magic: `0x4D504631U` ("MPF1")
- phase_t: `phase_unknown=0, phase_warmup=1, phase_active=2, phase_drain=3`
- 나머지 header_t 구조, 함수 시그니처 동일 (namespace: `perf_metric`)

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
// RESULT 출력값: latency/p95/p99 는 ms 단위 (내부 us -> 출력 시 변환)
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

**`perf_client_helpers.hpp`** — (core/perf multi/common/perf_client_helpers.hpp 대응)

```cpp
namespace perf::multi {

bool is_supported_transport(const std::string& transport);
std::string parse_endpoint_arg(int argc, char** argv);
void wait_all_client_connect_ready(/* monitors, timeout */);
void run_multi_echo_client_benchmark(/* sockets, settings, ... */);
void run_multi_oneway_client_benchmark(/* sockets, settings, ... */);

} // namespace perf::multi
```

**`perf_entry.hpp`** — (core/perf multi/common/perf_entry.hpp 대응)

```cpp
namespace perf::multi {

inline void set_perf_pattern_env(const char* pattern) {
    setenv("PERF_PATTERN", pattern, 1);
}

} // namespace perf::multi
```

**`perf_tls.hpp`** — TLS 인증서 리졸버

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

> dotnet 선례: `bindings/dotnet/tests/certs/` 에 동일 3개 파일 관리 중 (dotnet 은 `/gen/` 서브디렉토리 없음).
> **core/perf 방식** (소스 코드 내 embedded cert → `/tmp/bench_*.pem` 임시파일 기록) 은 사용하지 않고, 파일 경로 기반으로 전환한다.

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

### 12.4 Preflight 검증 (multi only)

> **참고**: single 스크립트에는 preflight 검증이 없다 (소켓/메모리 소모가 적으므로 불필요).

core/perf 와 동일 기준:

```bash
# nofile check: ulimit -n >= (clients * 3 + 4096)
ensure_nofile_limit $CLIENTS
# 환경변수 PERF_SKIP_NOFILE_CHECK=1 로 건너뛸 수 있음

# memory check: 퍼센트 기반 메모리 예산
# 가용 RAM × (PERF_MEMORY_BUDGET_PCT / 100) >= base_mb + clients × per_client_kb / 1024
# (기본: PERF_MEMORY_BUDGET_PCT=70, PERF_MEMORY_BASE_MB=512, PERF_MEMORY_PER_CLIENT_KB=1024)
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

> **포맷 요약:**
> - **report 파일**: 사람이 읽기 좋은 테이블만 저장. META/RESULT 라인 없음. `results/{single,multi}/report/` 에 생성.
> - core/perf 와 동일하게 `report/` 디렉토리만 사용한다.

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
10. `multi/common/perf_client_helpers.hpp` — 공통 client 루프 헬퍼
11. `multi/common/perf_entry.hpp` — set_perf_pattern_env
12. `multi/common/perf_tls.hpp` — TLS 인증서 리졸버
13. `multi/common/perf_metric_header.hpp` — 멀티 페이로드 헤더 (core 1:1)
14. `multi/src/*.cpp` 각 파일에 `main()` 직접 구현 (server/client 개별 파싱)

### Phase 2: Single 소켓 패턴 (6개)

16. `single/src/perf_pair.cpp`
17. `single/src/perf_pubsub.cpp`
18. `single/src/perf_dealer_dealer.cpp`
19. `single/src/perf_dealer_router.cpp`
20. `single/src/perf_router_router.cpp`

### Phase 3: Single 서비스 패턴 (2개)

22. `single/src/perf_gateway.cpp`
23. `single/src/perf_spot.cpp`

### Phase 4: Multi 패턴 — server/client 분리 (6×2 + 3 서버 only)

24. `multi/src/perf_dealer_dealer_server.cpp` + `perf_dealer_dealer_client.cpp`
25. `multi/src/perf_dealer_router_server.cpp` + `perf_dealer_router_client.cpp`
26. `multi/src/perf_router_router_server.cpp` + `perf_router_router_client.cpp`
27. `multi/src/perf_pubsub_server.cpp` + `perf_pubsub_client.cpp`
28. `multi/src/perf_gateway_server.cpp` + `perf_gateway_client.cpp`
29. `multi/src/perf_spot_server.cpp` + `perf_spot_client.cpp`
30. `multi/src/perf_stream_server.cpp` (서버 only)
31. `multi/src/perf_stream_callback_server.cpp` (서버 only)
32. `multi/src/perf_stream_len32be_server.cpp` (서버 only)

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
- Multi 서버는 `READY,<payload>` stdout 출력 후 클라이언트 대기
  - `GATEWAY` 는 registry pub/router endpoint 까지 함께 전달한다.
- TLS 인증서는 `bindings/cpp/tests/certs/gen/` 경로 사용

### 15.3 recv 루프 정책 (강제)

- **Single 패턴**: `blocking recv` 로 최초 수신 후, 같은 루프에서 `recv_flag::dontwait` 기반으로 큐를 **비어 있을 때까지 drain** 해야 한다.
- **Multi 패턴**: `zlink::poller_t` 로 readiness 대기 후, ready 소켓에서 `recv_flag::dontwait` 로 **무제한 drain** 해야 한다.
- **Drain cap 금지**: `N개까지만 drain` 같은 상한값(`cap`, `budget`)을 두지 않는다.
- **Settle 전용 예외 금지**: drain 로직은 settle 단계 전용이 아니라, recv 핫루프(active 포함)에서 동일 정책으로 유지한다.

### 15.4 I/O 정책 (강제)

- **적용 기준은 역할(role)** 이다. `server/client` 구분이 아니라, 해당 소켓이 `send 역할` 인지 `recv 역할` 인지에 따라 정책을 적용한다.
- **실제 오류는 즉시 fail** 한다. 실패를 성공처럼 보이게 만드는 fallback, retry budget, sleep/yield 기반 우회는 금지한다.
- **`EAGAIN`은 flow-control 상태** 로만 취급한다. 오류 은닉 수단으로 사용하지 않는다.
- **핫 루프 내 금지**:
  - heap 할당
  - 문자열 생성/로그 출력
  - retry budget / batch cap / drain cap
  - `sleep`, `yield`, busy-wait fallback

**Single 공통 정책**
- `send`: `blocking send` 1회만 호출한다. 실패 시 즉시 fail 한다.
- `recv`: 필요한 첫 메시지/프레임은 `blocking recv` 로 받고, 같은 iteration 안에서 `recv_flag::dontwait` 로 `EAGAIN` 까지 drain 한다.

**Multi 공통 정책**
- `recv`: 모든 recv 는 **callback-only** 다. 소켓 생성 시 recv handler 를 등록하면 I/O thread 에서 콜백이 호출된다. poller(`PollIn`) 는 사용하지 않는다.
- `send`: recv callback 안에서 직접 `send(..., dontwait)` 를 호출한다 (callback 중 same-handle send 허용).
- `set_send_ready_handler()` 로 writable transition 콜백을 등록한다.
- 동일 소켓의 recv callback 과 send-ready callback 은 직렬화되므로 concurrent queue 가 필요 없다.
- `while(send 실패)` 같은 즉시 재시도는 금지한다.
- perf 환경(HWM 100, inflight 1/peer)에서 EAGAIN 은 사실상 발생하지 않으며, backpressure 자료구조는 safety net 이다.

**역할별 backpressure**
- `echo 서버` (소켓 1개 × 클라이언트 N개): `EAGAIN` 시 per-socket `std::deque` 에 저장. pending 이 있는 동안 recv callback 의 새 send 는 deque 에 추가만 한다. send-ready callback 에서 deque 를 `EAGAIN` 까지 drain 한다.
- `echo 클라이언트` (per-socket, inflight 1): `EAGAIN` 시 `bool send_pending` 플래그만 설정. send-ready callback 에서 재전송. 응답 수신 → 다음 전송의 1:1 대응이므로 deque 불필요.
- `one-way sender`: `EAGAIN` 시 `bool send_pending` 플래그만 설정. send-ready callback 에서 재전송.
- `one-way receiver`: send 없음. recv callback 에서 카운트만 증가시킨다.
- `pub/sub`, `spot`: 발행/송신 쪽은 one-way sender, 구독/수신 쪽은 one-way receiver 정책을 따른다.

### 15.5 코드 인라이닝 정책

> **core/perf 와의 핵심 차이**: core/perf 는 `perf_client_helpers.hpp` 에 `run_one_way_duration()`,
> `run_echo_duration()` 등 공통 send/recv 루프를 두고 각 패턴이 호출하는 구조이다.
> **cpp binding 은 이와 다르게, 각 패턴 파일 내부에 send/recv 핵심 로직을 직접 인라인한다.**
> 패턴 파일 하나만 열면 소켓 생성 → bind/connect → send/recv 루프 → 메트릭 수집까지
> 전체 흐름을 샘플 코드처럼 읽을 수 있어야 한다.

**인라인 대상 (각 패턴 파일 내부에 직접 작성):**
- 소켓 생성, bind/connect
- send/recv 메인 루프 (warmup / active phase 포함)
- 페이즈 전환 로직 (phase stamp/decode)
- 메트릭 계산 및 RESULT 출력

**common 으로 추출 허용 (인프라/유틸만):**
- `common/perf_single_common`: 환경변수 파싱, EAGAIN/EINTR 루프 매크로, printResult 포맷터, 엔드포인트 생성
- `common/perf_common.hpp` (multi): ctx_guard_t, socket_guard_t, monitor, settle, HWM/timeout 적용
- STREAM 서버 인프라 (stream_attach 콜백 등)는 모듈화 허용

**추출 금지 (반드시 패턴 파일 내부):**
- send/recv 호출 코드 자체 (공통 함수로 위임 금지)
- warmup/active 루프 본문
- 메트릭 stamp/decode 호출부

### 15.6 엔트리포인트 정책

- `single/src/*.cpp`, `multi/src/*.cpp` **각 패턴 파일에 `main()`을 직접 둔다.**
- 공용 runner 파일에서 `#define RUN_*` 방식으로 패턴을 주입하는 구조는 사용하지 않는다.
- 단일 패턴 파일만 열어도 `main` → 패턴 함수 → 핵심 루프 흐름이 보이도록 유지한다.

```cpp
// 예시: perf_dealer_router_client.cpp — send/recv 루프가 파일 내에 직접 존재
while (active) {
    // send
    zlink::message_t msg(payload, size);
    perf_metric::stamp_header(msg.data(), phase_active);
    sock.send(msg, zlink::send_flags::none);

    // recv (echo)
    zlink::message_t reply;
    sock.recv(reply, zlink::recv_flags::none);
    auto hdr = perf_metric::decode_header(reply.data());
    if (hdr.phase == phase_active) {
        ++count;
        latency_tracker.record(hdr.timestamp);
    }
}
```

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
    --pattern MULTI_STREAM_CALLBACK --transports tcp --msg-sizes 64 \
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
- single: `throughput`, `bandwidth`, `latency` — 3개 메트릭이 모든 pattern/transport/size 조합에 존재 (completion 기준, `latency` 단위는 ms)
- single 리소스: `cpu_pct`, `mem_mb` (러너가 프로세스 모니터링으로 수집)
- multi: `throughput`, `bandwidth`, `latency` + `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb` (`latency` 단위는 ms)

**러너 파서 인식 (결과 테이블 포함, completion 판정 대상 아님):**
- `latency_p95`, `latency_p99` — 바이너리가 stdout 출력, 러너 파서가 인식하여 테이블에 포함 (단위 ms)

**대역폭 계산식 검증:**
```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```
- single 전체 (one-way 방향): `bandwidth ≈ throughput × size / 1,000,000`
- multi echo 패턴 (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*): `bandwidth ≈ throughput × size × 2 / 1,000,000`
- multi one-way 패턴 (DEALER_DEALER, PUBSUB, SPOT): `bandwidth ≈ throughput × size / 1,000,000`
- 허용 오차: ±1%

**Percentile 일관성 검증 (러너 파서 인식, 결과 테이블 포함):**
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
- **Single**: 러너는 각 size 바이너리 실행 완료 후 `proc.communicate()` 로 stdout 을 일괄 캡처하여 RESULT 라인을 파싱한다.
- **Multi**: 서버 프로세스는 `READY,<payload>` 감지를 위해 **스레딩 기반 실시간 stdout 펌핑** (`Thread` + `stdout_queue`) 으로 캡처한다. 클라이언트는 single 과 동일하게 `proc.communicate()` 로 종료 후 일괄 수집한다.
- `GATEWAY` payload 는 `server|registry_pub|registry_router` 3필드 문자열을 사용한다.
- 모든 size 실행이 완료된 후 러너가 파싱된 결과를 테이블로 출력하고, 다음 transport 또는 pattern 으로 전환된다.

> **참고**: 바이너리의 RESULT 출력은 사용자 콘솔에 실시간으로 보이지 않는다. 진행 상황은 러너 레벨의 로그를 통해 확인한다.

**검증 방법:**
```bash
# 3개 사이즈로 실행 (--results-tag 로 파일명 고정)
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --reuse-build \
  --result --results-tag size_order_check
```

**report 파일 기반 검증 기준:**
1. report 파일에서 테이블 행 순서가 size 64 → 1024 → 65536 순으로 나열됨
2. 각 사이즈의 메트릭 (throughput, bandwidth, latency) 이 테이블에 모두 포함됨

> **참고**: 러너는 child stdout 을 캡처하여 파싱하므로 (single: `proc.communicate()`, multi server: 스레딩 실시간 펌핑), 콘솔 로그에는 러너의 테이블/진행 로그가 출력된다. 최종 결과는 report 파일에서 확인한다.

**자동 검증 스크립트 (선택):**
```python
# --results-tag 로 고정된 report 파일에서 테이블 내 size 값 순서 검증
import re, glob
sizes_seen = []
target = glob.glob("bindings/cpp/perf/results/single/report/perf_*_size_order_check.txt")
assert target, "report file not found — --results-tag size_order_check 실행 필요"
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
- [ ] 콘솔 로그에 fail 조합 0건
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)
- [ ] report 파일이 `results/{single,multi}/report/` 에 생성됨

**기본 조합 수 예상 (single):**
- GATEWAY, SPOT (2종) × 4 transport (tcp,tls,ws,wss) × 6 size = 48 조합
- STREAM, STREAM_CALLBACK, STREAM_LEN32BE (3종) × 4 transport (tcp,tls,ws,wss) × 4 size = 48 조합
- 총: 312 조합 → UNSUPPORTED 제외한 나머지 전부 success
> **참고**: STREAM 패턴은 single suite 에서 multi stream server + core 공통 stream client 조합으로
> 실행된다 (`supports_split_multi(cpp)=true`). 별도 single 바이너리는 없으며 multi server 를 재사용한다.

**기본 조합 수 예상 (multi):**
- 비-STREAM 패턴 (6종) × 4 transport × 6 size = 144 조합
- STREAM 패턴 (3종) × 4 transport × 4 size = 48 조합
- 총: 192 조합 → UNSUPPORTED 제외한 나머지 전부 success

**결과 파일 확인:**
```bash
# report 파일이 생성되었는지 확인 (테이블만 포함)
ls -la bindings/cpp/perf/results/single/report/perf_*.txt
ls -la bindings/cpp/perf/results/multi/report/perf_*.txt
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
  bindings/cpp/perf/single/src/*.cpp \
  -Ibindings/cpp/include -Icore/include -Ibindings/cpp/perf/single/common
# 기대 결과: 경고 0건
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `src/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 별도 파일로 분리.
- runner 옵션/기본값/결과 형식이 core 와 동등하게 동작.
- single/multi 모든 패턴 바이너리가 `bindings/cpp/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 C API (`zlink_*`) 직접 호출 0건 — C++ binding API 만 사용.
- TLS 인증서는 `bindings/cpp/tests/certs/gen/` 에서 독립 관리.
- 정책적 retry 로직/우회 wrapper/비정책 실행 경로가 없음 (EAGAIN/EINTR 루프는 허용).
- **메트릭 헤더**: single 은 SPF1 (0x53504631), multi 는 MPF1 (0x4D504631) 페이로드 헤더를 stamp/decode 하여 phase 필터링 및 latency 측정에 사용 (core 1:1).
- **메트릭 정확성**(필수 메트릭 존재/bandwidth 계산식/percentile 일관성/값 범위)이 검증됨.
- **사이즈별 순차 실행 및 결과 수집**이 검증됨 (러너가 각 size 바이너리 실행 후 stdout 캡처·파싱하여 report 파일에 테이블로 기록).
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- **코드 리뷰 완료**: 데드코드 0건, 핫 루프 내 불필요 할당/복사/대기 0건, 컴파일 경고 0건.
- **주석 정리 완료**: 각 패턴 파일 상단 블록 주석, 공통 유틸 함수 파라미터 주석, 핫 루프 인라인 주석이 적절한 수준으로 추가됨.

---

## 18. 구현 진행 체크리스트

> 각 항목을 순서대로 확인하며 진행한다. 완료 시 `[x]` 로 표시한다.

### Phase 0: 인증서 및 인프라 준비

- [x] `bindings/cpp/tests/certs/gen/server.crt` 파일 존재 확인
- [x] `bindings/cpp/tests/certs/gen/server.key` 파일 존재 확인
- [x] `bindings/cpp/tests/certs/gen/ca.crt` 파일 존재 확인
- [x] `bindings/cpp/perf/.gitignore` 생성 (build/ 제외)
- [x] `bindings/cpp/perf/results/single/report/.gitkeep` 생성
- [x] `bindings/cpp/perf/results/multi/report/.gitkeep` 생성
- [x] `run_policy_bench.py` 에 `CPP_SINGLE_PATTERN_SPECS` 8개 패턴 등록 확인
- [x] `run_policy_bench.py` 에 `CPP_MULTI_SERVER_PATTERN_SPECS` 9개 패턴 등록 확인
- [x] `run_policy_bench.py` 에 `CPP_MULTI_CLIENT_PATTERN_SPECS` 6개 패턴 등록 확인

### Phase 1: 인프라 — Single common/

- [x] `single/common/perf_single_metric_header.hpp` 작성 (core 1:1, magic=SPF1)
- [x] `single/common/perf_single_common.hpp` 작성 (ctx_guard_t, socket_guard_t, 환경변수 파싱, 헬퍼 함수 선언)
- [x] `single/common/perf_single_common.cpp` 작성 (위 선언의 구현체)
- [x] `single/common/perf_single_tls.hpp` 작성 (TLS 인증서 경로 리졸버, `bindings/cpp/tests/certs/gen/` 탐색)
- [x] `single/common/perf_single_runner.hpp` 작성 (run_standard_bench_main 선언)
- [x] `single/common/perf_single_runner.cpp` 작성 (argv 파싱 helper)
- [x] C API 직접 호출 없음 확인: `rg "\\bzlink_[a-zA-Z0-9_]+\\(" single/common/`

### Phase 1: 인프라 — Multi common/

- [x] `multi/common/perf_metric_header.hpp` 작성 (core 1:1, magic=MPF1)
- [x] `multi/common/perf_common.hpp` 작성 (ctx_guard_t, socket_guard_t, monitor, settle, RESULT 출력)
- [x] `multi/common/perf_common_multi.hpp` 작성 (multi_bench_settings_t, resolve 함수)
- [x] `multi/common/perf_client_helpers.hpp` 작성 (공통 client 루프 헬퍼)
- [x] `multi/common/perf_entry.hpp` 작성 (set_perf_pattern_env)
- [x] `multi/common/perf_tls.hpp` 작성 (TLS 인증서 경로 리졸버)
- [x] `multi/src/*.cpp` 각 파일에 `main()` 직접 구현 (server/client 분리)
- [x] C API 직접 호출 없음 확인: `rg "\\bzlink_[a-zA-Z0-9_]+\\(" multi/common/`

### Phase 2: Single 소켓 패턴 (6개)

- [x] `single/src/perf_pair.cpp` — PAIR echo 벤치마크
  - [x] 빌드 성공 (`run_policy_bench.py --binding cpp --suite single --pattern PAIR --reuse-build`)
  - [x] smoke 테스트 통과 (tcp, size=64)
  - [x] TLS smoke 통과 (tls, size=64)
- [x] `single/src/perf_pubsub.cpp` — PUBSUB one-way 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
- [x] `single/src/perf_dealer_dealer.cpp` — DEALER-DEALER echo 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
- [x] `single/src/perf_dealer_router.cpp` — DEALER-ROUTER echo 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
- [x] `single/src/perf_router_router.cpp` — ROUTER-ROUTER echo 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)

### Phase 3: Single 서비스 패턴 (2개)

- [x] `single/src/perf_gateway.cpp` — GATEWAY echo 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
  - [x] TLS smoke 통과 (tls, size=64)
  - [x] inproc/ipc 에서 UNSUPPORTED 정상 처리 확인
- [x] `single/src/perf_spot.cpp` — SPOT one-way 벤치마크
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64)
  - [x] inproc/ipc 에서 UNSUPPORTED 정상 처리 확인

### Phase 2-3 통합 검증: Single 전체

- [x] 전 패턴 빌드 성공 (8개 바이너리)
- [x] 컴파일 경고 0건 (`-Wall -Wextra`)
- [x] C API 직접 호출 0건: `rg "\\bzlink_[a-zA-Z0-9_]+\\(" single/`
- [x] RESULT 출력 형식 확인: `RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,<metric>,<value>`
- [x] bandwidth 계산식 검증: `bandwidth ~= throughput * size / 1,000,000` (single 전체 승수 1.0)
- [x] latency percentile 일관성: `p99 >= p95 >= mean`
- [x] 사이즈별 순차 실행 검증 (64, 1024, 65536 순서 확인)

### Phase 4: Multi 비-STREAM 패턴 (6 x server/client)

- [x] `multi/src/perf_dealer_dealer_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과 (tcp, size=64, clients=10)
  - [x] 서버 `READY,<endpoint>` stdout 출력 확인
- [x] `multi/src/perf_dealer_router_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과
  - [x] bandwidth 승수 2.0 검증 (echo 패턴)
- [x] `multi/src/perf_router_router_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과
- [x] `multi/src/perf_pubsub_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과
- [x] `multi/src/perf_gateway_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과
- [x] `multi/src/perf_spot_server.cpp` + `_client.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과

### Phase 4: Multi STREAM 패턴 (3 x server only)

- [x] `multi/src/perf_stream_server.cpp`
  - [x] 빌드 성공
  - [x] `core/perf/common/streamclient/build/perf_stream_client` 바이너리 존재 확인
  - [x] smoke 테스트 통과 (tcp, size=64, clients=100)
  - [x] stop-token `__zlink_perf_stop__` 수신 시 정상 종료 확인
- [x] `multi/src/perf_stream_callback_server.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과
- [x] `multi/src/perf_stream_len32be_server.cpp`
  - [x] 빌드 성공
  - [x] smoke 테스트 통과

### Phase 4 통합 검증: Multi 전체

- [x] 전 패턴 빌드 성공 (15개 바이너리: 6 server + 6 client + 3 stream server)
- [x] 컴파일 경고 0건
- [x] C API 직접 호출 0건: `rg "\\bzlink_[a-zA-Z0-9_]+\\(" multi/`
- [x] STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용 (자체 빌드 없음)
- [x] multi echo 패턴 bandwidth 승수 2.0 검증 (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*)
- [x] multi one-way 패턴 bandwidth 승수 1.0 검증 (DEALER_DEALER, PUBSUB, SPOT)
- [x] 서버 READY 프로토콜 정상 동작 확인

### Phase 5: 스크립트 및 마무리

- [x] `bindings/cpp/perf/run_benchmarks.sh` 작성 (루트 single 래퍼)
- [x] `bindings/cpp/perf/run_benchmarks.ps1` 작성
- [x] `bindings/cpp/perf/run_benchmarks_multi.sh` 작성 (루트 multi 래퍼)
- [x] `bindings/cpp/perf/run_benchmarks_multi.ps1` 작성
- [x] `bindings/cpp/perf/run_comparison.py` 작성 (루트 오케스트레이터)
- [x] `bindings/cpp/perf/single/run_benchmarks.sh` 작성
- [x] `bindings/cpp/perf/single/run_benchmarks.ps1` 작성
- [x] `bindings/cpp/perf/single/run_comparison.py` 작성
- [x] `bindings/cpp/perf/multi/run_benchmarks.sh` 작성
- [x] `bindings/cpp/perf/multi/run_benchmarks.ps1` 작성
- [x] `bindings/cpp/perf/multi/run_comparison.py` 작성
- [x] `bindings/cpp/perf/README.md` 작성
- [x] `bindings/cpp/perf/CMakeLists.txt` 타겟 목록과 실제 소스 파일 일치 확인

### Phase 5 통합 검증: 전체 실행

- [x] single 기본 전체 실행 성공:
  ```
  python3 bindings/perf/run_policy_bench.py --binding cpp --suite single --result
  ```
  - [x] 종료코드 0
  - [x] `META,status,complete` 확인
  - [x] `META,expected` == `META,actual` (312 조합 기준, UNSUPPORTED 제외)
  - [x] fail 조합 0건
- [x] multi 기본 전체 실행 성공:
  ```
  python3 bindings/perf/run_policy_bench.py --binding cpp --suite multi --result
  ```
  - [x] 종료코드 0
  - [x] `META,status,complete` 확인
  - [x] `META,expected` == `META,actual` (576 메트릭 기준, default runs=3)
  - [x] fail 조합 0건
- [x] report 파일 생성 확인 (single + multi)

### Phase 6: 코드 리뷰 및 리팩토링

#### 6.1 데드코드 / 미사용 코드 제거

- [x] 미사용 `#include` 0건
- [x] 미사용 변수/함수 0건
- [x] 주석 처리된 코드 블록 0건 (정당한 블록 주석 제외)
- [x] CMakeLists.txt 에서 참조되지 않는 타겟 0건 (run_policy_bench.py 타겟 매핑 기준)

#### 6.2 불필요한 할당/복사 제거

- [x] 핫 루프 내 동적 할당 (`new`, `malloc`, `std::vector` 재할당, `std::string` 임시 생성) 0건
- [x] 페이로드 버퍼 루프 밖 사전 할당 확인
- [x] 큰 타입 (`std::string`, `std::vector`) 값 전달 0건 (모두 `const&` 또는 `&&`)

#### 6.3 불필요한 대기/동기화 제거

- [x] Active 페이즈 내 `sleep`/`usleep`/`sleep_for` 호출 0건
- [x] 단일 스레드 경로에 불필요한 mutex/lock 0건

#### 6.4 API 호출 최적화

- [x] 소켓 옵션 중복 설정 0건
- [x] dontwait vs blocking 모드 core 대비 일관성 확인

#### 6.4.1 C++ 객체지향 리팩토링 규칙 (2026-03-06)

- 대상 경로: `bindings/cpp/perf/multi/src/*.cpp` (client 패턴 우선)
- 대상 패턴: `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`, `MULTI_PUBSUB`, `MULTI_GATEWAY`, `MULTI_SPOT`
- 각 패턴 파일 내부에 `phase_config_t`, `bench_result_t` 타입을 둔다.
- 긴 함수는 phase 단위 메서드로 분리한다:
  - `run_warmup()`
  - `run_settle()` 또는 `run_settle_drain()`
  - `run_active()`
- 소켓/컨텍스트/버퍼 생명주기는 패턴 파일 내 class 멤버로 명시한다.
- send/recv 핵심 루프는 패턴 파일 내에서 직접 보이도록 유지한다.
- 패턴 간 공통화는 금지하고, 파일 내부 private helper까지만 허용한다.
- fallback/retry budget/cap 으로 실패를 성공처럼 보이게 만드는 동작은 금지한다.
- 적용 상태:
  - [x] `perf_dealer_dealer_client.cpp`
  - [x] `perf_dealer_router_client.cpp`
  - [x] `perf_router_router_client.cpp`
  - [x] `perf_pubsub_client.cpp`
  - [x] `perf_gateway_client.cpp`
  - [x] `perf_spot_client.cpp`

#### 6.5 주석 정리

- [x] 각 패턴 파일 상단 블록 주석 추가 (패턴 설명, 토폴로지, 측정 방식)
- [x] 공통 유틸 함수 파라미터/반환값 주석 추가 (자명한 함수 제외)
- [x] 핫 루프 내 비자명 로직 인라인 주석 추가
- [x] 불필요한 주석 (TODO, FIXME, 임시 메모) 삭제

#### 6.6 최종 빌드 검증

- [x] `-Wall -Wextra -Wpedantic -Wunused` 경고 0건
- [x] C API 직접 호출 최종 확인: `rg "\\bzlink_[a-zA-Z0-9_]+\\(" bindings/cpp/perf --glob '*.{cpp,hpp}'` → 0건
- [x] TLS 인증서 경로 최종 확인: core/tests/certs 참조 0건
- [x] STREAM client 자체 빌드 없음 최종 확인

### Phase 6 통합 검증: 리팩토링 후 회귀 테스트

- [x] single 전체 실행 재검증 (Phase 5 통합 검증 반복)
  - [x] 종료코드 0, `META,status,complete`, fail 0건
- [x] multi 전체 실행 재검증
  - [x] 종료코드 0, `META,status,complete`, fail 0건
- [x] 메트릭 정확성 재검증 (bandwidth 계산식, percentile 일관성, 값 범위)

### 완료 판정 (Definition of Done)

- [x] 디렉토리/파일 구조: core/perf 의 `common/` + `src/` 분리 구조 일치
- [x] multi server/client 별도 파일 분리 확인
- [x] runner 옵션/기본값/결과 형식이 core 동등
- [x] single 8개 + multi 15개 바이너리 빌드 확인
- [x] STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용
- [x] C API 직접 호출 0건
- [x] TLS 인증서 `bindings/cpp/tests/certs/gen/` 독립 관리
- [x] 정책적 retry/우회 wrapper 없음
- [x] 메트릭 헤더 stamp/decode 정상 (single=SPF1, multi=MPF1)
- [x] 메트릭 정확성 검증 완료
- [x] 사이즈별 순차 실행/수집 검증 완료
- [x] 기본 설정 전체 실행 `status: complete` 확인
- [x] 코드 리뷰 완료 (데드코드/할당/복사/대기/경고 0건)
- [x] 주석 정리 완료
