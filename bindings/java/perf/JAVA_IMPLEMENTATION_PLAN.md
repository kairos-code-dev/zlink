# Java Perf Benchmark Implementation Plan

> core/perf (C++) 벤치마크를 bindings/java/perf 로 1:1 포팅한다.
> **Java binding API (`dev.kairoscode.zlink.*`) 만 사용하며, zlink C API / wrapper 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `src/` 분리 구조를 Java 패키지 컨벤션으로 그대로 반영한다.

```
bindings/java/
├── tests/
│   └── certs/                              ← TLS 인증서 (바인딩 독립 관리)
│       ├── server.crt
│       ├── server.key
│       └── ca.crt
│
└── perf/
    ├── JAVA_IMPLEMENTATION_PLAN.md         ← 본 문서
    ├── README.md                           ← 사용법 안내
    ├── .gitignore                          ← build/ 제외
    │
    ├── single/
    │   ├── Zlink.PerfBench/
    │   │   ├── build.gradle                ← Gradle 서브프로젝트 (perf-single)
    │   │   └── src/main/java/dev/kairoscode/zlink/integration/bench/
    │   │       ├── PerfMain.java           ← 진입점 (pattern, transport, size)
    │   │       ├── common/                 ← ★ core/perf/single/common/ 대응
    │   │       │   ├── PerfCommon.java     ← 공통 유틸 (retry, PrintResult, 엔드포인트 등)
    │   │       │   ├── PerfSingleMetricHeader.java ← SPF1 페이로드 헤더 stamp/decode
    │   │       │   └── PerfTls.java        ← TLS 인증서 경로 리졸버 (single)
    │   │       └── src/                    ← ★ core/perf/single/src/ 대응
    │   │           ├── PerfPair.java
    │   │           ├── PerfPubSub.java
    │   │           ├── PerfDealerDealer.java
    │   │           ├── PerfDealerRouter.java
    │   │           ├── PerfRouterRouter.java
    │   │           ├── PerfRouterRouterPoll.java
    │   │           ├── PerfGateway.java
    │   │           └── PerfSpot.java
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── multi/
    │   ├── Zlink.PerfBench/
    │   │   ├── build.gradle
    │   │   └── src/main/java/dev/kairoscode/zlink/integration/bench/
    │   │       ├── PerfMultiMain.java      ← 진입점 (--multi-server / --multi-client)
    │   │       ├── common/                 ← ★ core/perf/multi/common/ 대응
    │   │       │   ├── PerfCommon.java     ← 공통 유틸 (multi 전용)
    │   │       │   ├── PerfMultiCommon.java    ← Multi 설정 리졸버
    │   │       │   ├── PerfMultiServerEntry.java  ← 서버 디스패처 + CPU/MEM
    │   │       │   ├── PerfMultiClientEntry.java  ← 클라이언트 디스패처 + CPU/MEM
    │   │       │   ├── PerfMultiClientHelpers.java ← client 유틸 (transport 판별, 연결 대기)
    │   │       │   ├── PerfMultiMetricHeader.java ← MPF1 페이로드 헤더 stamp/decode
    │   │       │   └── PerfMultiTls.java          ← TLS 인증서 경로 리졸버
    │   │       └── src/                    ← ★ core/perf/multi/src/ 대응
    │   │           ├── PerfMultiDealerDealerServer.java  ← ★ server/client 분리
    │   │           ├── PerfMultiDealerDealerClient.java
    │   │           ├── PerfMultiDealerRouterServer.java
    │   │           ├── PerfMultiDealerRouterClient.java
    │   │           ├── PerfMultiRouterRouterServer.java
    │   │           ├── PerfMultiRouterRouterClient.java
    │   │           ├── PerfMultiPubSubServer.java
    │   │           ├── PerfMultiPubSubClient.java
    │   │           ├── PerfMultiGatewayServer.java
    │   │           ├── PerfMultiGatewayClient.java
    │   │           ├── PerfMultiSpotServer.java
    │   │           ├── PerfMultiSpotClient.java
    │   │           ├── PerfMultiStreamServer.java       ← 서버 only (클라이언트=공통 stream client)
    │   │           ├── PerfMultiStreamCallbackServer.java
    │   │           └── PerfMultiStreamLen32BeServer.java
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
    ├── run_benchmarks.sh                   ← 루트 single 래퍼
    ├── run_benchmarks.ps1
    ├── run_benchmarks_multi.sh             ← 루트 multi 래퍼
    ├── run_benchmarks_multi.ps1
    └── run_comparison.py                   ← 루트 오케스트레이터
```

### core/perf 대비 구조 매핑

| core/perf | java/perf | 비고 |
|-----------|-----------|------|
| `single/common/bench_common.hpp` | `single/.../common/PerfCommon.java` | 패키지 분리 |
| `single/common/perf_single_metric_header.hpp` | `single/.../common/PerfSingleMetricHeader.java` | SPF1 (0x53504631) 페이로드 헤더 stamp/decode |
| `single/src/perf_pair.cpp` | `single/.../src/PerfPair.java` | 1:1 매핑 |
| `single/src/perf_dealer_dealer.cpp` | `single/.../src/PerfDealerDealer.java` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/.../common/PerfCommon.java` | 패키지 분리 |
| `multi/common/perf_common_multi.hpp` | `multi/.../common/PerfMultiCommon.java` | |
| `multi/common/perf_metric_header.hpp` | `multi/.../common/PerfMultiMetricHeader.java` | MPF1 (0x4D504631) 페이로드 헤더 stamp/decode |
| `multi/common/perf_client_helpers.hpp` | `multi/.../common/PerfMultiClientHelpers.java` | |
| `multi/common/perf_entry.hpp` | (해당 없음 — env setter 1개, Java 에서는 PerfMultiMain 내부에서 직접 처리) | env 설정 helper 만 포함 |
| (core 각 server/client main() 의 디스패치+메트릭 로직) | `multi/.../common/PerfMultiServerEntry.java` + `PerfMultiClientEntry.java` | Java 전용 디스패치+CPU/MEM 집약 |
| `multi/src/perf_dealer_dealer_server.cpp` | `multi/.../src/PerfMultiDealerDealerServer.java` | ★ server/client 분리 유지 |
| `multi/src/perf_dealer_dealer_client.cpp` | `multi/.../src/PerfMultiDealerDealerClient.java` | |
| `multi/src/perf_stream_server.cpp` | `multi/.../src/PerfMultiStreamServer.java` | 서버 only |
| `common/streamclient/` (C++ 독립 바이너리) | (Java 대응 없음 — C++ 공용 바이너리 그대로 사용) | `run_policy_bench.py` 가 자동 호출 |

---

## 2. 빌드 시스템

### 2.1 settings.gradle (기존 — 변경 없음)

```groovy
include ':perf-single'
project(':perf-single').projectDir = file('perf/single/Zlink.PerfBench')

include ':perf-multi'
project(':perf-multi').projectDir = file('perf/multi/Zlink.PerfBench')
```

### 2.2 single build.gradle (`perf/single/Zlink.PerfBench/build.gradle`)

```groovy
plugins { id 'java' }

java {
    toolchain { languageVersion = JavaLanguageVersion.of(22) }
}

dependencies {
    implementation project(':')   // zlink-java 메인 모듈 (dev.kairoscode.zlink)
}

jar { enabled = false }  // 클래스파일만 빌드; JAR 배포 불필요
```

### 2.3 multi build.gradle (`perf/multi/Zlink.PerfBench/build.gradle`)

```groovy
plugins { id 'java' }

java {
    toolchain { languageVersion = JavaLanguageVersion.of(22) }
}

dependencies {
    implementation project(':')
}

jar { enabled = false }
```

### 2.4 빌드 명령

```bash
cd bindings/java
./gradlew -q :perf-single:classes :perf-multi:classes
```

- `run_policy_bench.py` 가 동일한 명령으로 빌드를 호출한다.
- 산출물: `perf/single/Zlink.PerfBench/build/classes/java/main/` 및 `perf/multi/Zlink.PerfBench/build/classes/java/main/`

---

## 3. CLI 인터페이스 (run_policy_bench.py 호환)

### 3.1 Single 실행

```bash
java --enable-native-access=ALL-UNNAMED \
  -cp <classpath> \
  dev.kairoscode.zlink.integration.bench.PerfMain \
  <PATTERN> <TRANSPORT> <SIZE>
```

- **PATTERN** (Java 바이너리 직접 실행 8종): `PAIR | PUBSUB | DEALER_DEALER | DEALER_ROUTER | ROUTER_ROUTER | ROUTER_ROUTER_POLL | GATEWAY | SPOT`
- **TRANSPORT**: `tcp | tls | ws | wss | inproc | ipc`
- **SIZE**: 양의 정수 (바이트)
- 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류

> **core/perf 기준 single suite 패턴은 8종**이다 (`STANDARD_PATTERNS` = PAIR~SPOT).
> STREAM 3종은 single suite 에 포함되지 않으며, multi suite 에서만 실행된다.
> `run_policy_bench.py` 는 `supports_split_multi()=true` 인 바인딩에서 single STREAM 을
> multi 서버 + `core/perf/common/streamclient` C++ 클라이언트로 위임 실행하지만,
> 이는 런너의 편의 기능이며 core/perf single suite 자체에는 STREAM 이 없다.

### 3.2 Multi 실행

**서버:**
```bash
java --enable-native-access=ALL-UNNAMED \
  -cp <classpath> \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server <PATTERN> <TRANSPORT> <SIZE>
```

**클라이언트:**
```bash
java --enable-native-access=ALL-UNNAMED \
  -cp <classpath> \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client <PATTERN> <TRANSPORT> <SIZE> --endpoint <endpoint>
```

### 3.3 STREAM 패턴 클라이언트

STREAM, STREAM_CALLBACK, STREAM_LEN32BE 패턴은:
- **서버**: Java 벤치마크가 직접 구현 (zlink Socket/attachStream API)
- **클라이언트**: `core/perf/common/streamclient/build/perf_stream_client` (C++ 공통 바이너리) 사용
- `run_policy_bench.py` 가 자동으로 공통 stream client 를 호출한다.

### 3.4 run_policy_bench.py 수정 사항

`run_policy_bench.py` 에 Java 바인딩 코드가 존재하나 perf 구조 개편 전 상태이다. 아래 항목을 반영해야 한다:

| 함수 | 위치 | 현재 상태 | 필수 수정 |
|------|------|----------|----------|
| `build_binding_if_needed()` | L590 | artifact 경로가 `java/build/classes/java/test/` | `java/perf/<suite>/Zlink.PerfBench/build/classes/java/main/` |
| `binding_cmd_prefix()` | L854 | `--enable-native-access` 미포함, cp 에 test 경로 | `--enable-native-access=ALL-UNNAMED` 추가, cp=`java/perf/single/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main` |
| `binding_multi_role_command()` | L1035 | 동일 (test 경로, native-access 미포함) | `--enable-native-access=ALL-UNNAMED` 추가, cp=`java/perf/multi/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main` |

---

## 4. RESULT 출력 형식 (core/perf 동일)

```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,throughput,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,bandwidth,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p95,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p99,<value>
```

벤치마크는 core/perf 와 동일하게 조합당 **5개 RESULT 라인** (throughput, bandwidth, latency, latency_p95, latency_p99) 을 stdout 에 출력해야 한다.

> 단위 규칙 (Java perf, 2026-03-06): `latency`, `latency_p95`, `latency_p99` 값은 **ms** 단위로 출력한다.
> 내부 측정/샘플링은 기존처럼 `sent_ts_us` 기반(μs)으로 유지하고, RESULT 출력 시 `us / 1000.0` 변환만 적용한다.

> **완료 판정**: `run_policy_bench.py` 는 조합당 `required_metric_count` 기준으로 판정한다.
> Java 는 **3** (throughput, bandwidth, latency) 기준이다 (`expected = (total - unsupported - skipped) × required_metric_count`).

Multi 추가 메트릭 (PerfMultiServerEntry / PerfMultiClientEntry 에서):
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_mem_mb,<value>
```

### Bandwidth 계산 규칙

```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```

| 구분 | 승수 | 비고 |
|------|------|------|
| **Single 전체** | **1.0** | run_policy_bench.py 기준 모든 single 은 one-way 방향 |
| Multi echo (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*) | 2.0 | 요청+응답 양방향 |
| Multi one-way (DEALER_DEALER, PUBSUB, SPOT) | 1.0 | 단방향 |

---

## 5. 환경 변수 (core/perf 동일)

### 5.1 Single

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_IO_THREADS` | 0 (기본) | Context IO 스레드 |
| `PERF_WARMUP_COUNT` | 일반 **1000**, GATEWAY/SPOT **200** | 웜업 메시지 횟수 (count 기반). SPOT 은 `msg_size ≥ 65536` 시 최대 20 으로 clamp |
| `PERF_SINGLE_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 200000 | 레이턴시 reservoir sampling 캡 |
| `PERF_SINGLE_HWM` | **1000** | 소켓 HWM (send+recv 기본) |
| `PERF_SINGLE_SNDHWM` | **1000** | 송신 HWM (HWM 오버라이드) |
| `PERF_SINGLE_RCVHWM` | **1000** | 수신 HWM (HWM 오버라이드) |
| `PERF_SINGLE_SNDTIMEO_MS` | **200** | 송신 타임아웃 |
| `PERF_SINGLE_RCVTIMEO_MS` | **200** | 수신 타임아웃 |
| `PERF_MAX_SOCKETS` | 자동 | 최대 소켓 수 |

### 5.2 Multi

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_CLIENTS` | **100** (STREAM: 10000) | 동시 클라이언트 수 |
| `PERF_WARMUP_SECONDS` | **2** | 웜업 기간 |
| `PERF_SETTLE_MS` | 500 | Warmup→Active 사이 안정화 (인플라이트 소진 포함) |
| `PERF_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_ACTIVE_WARMUP` | 0 | 0=sleep, 1=active |
| `PERF_HWM` | **100** (STREAM: 10) | 소켓 HWM |
| `PERF_SNDHWM` | `PERF_HWM` 값 (미설정 시 HWM 과 동일) | 송신 HWM 오버라이드 |
| `PERF_RCVHWM` | `PERF_HWM` 값 (미설정 시 HWM 과 동일) | 수신 HWM 오버라이드 |
| `PERF_SNDTIMEO_MS` | **200** | 송신 타임아웃 |
| `PERF_RCVTIMEO_MS` | **200** | 수신 타임아웃 |
| `PERF_CONNECT_READY_TIMEOUT_MS` | 5000 | 연결 대기 |
| `PERF_MONITOR_HWM` | **1000** | 모니터 소켓 HWM |
| `PERF_SERVER_BIND_PORT` | 0 (자동) | 서버 포트 고정 |
| `PERF_IO_THREADS` | 0 | IO 스레드 |
| `PERF_CLIENT_POLL_TIMEOUT_MS` | 0 | 클라이언트 poll 타임아웃 |
| `PERF_CTX_BLOCKY` | 미설정 | Context blocky 모드 (설정 시 적용) |
| `PERF_CTX_TERM` | 미설정 | Context termination 모드 (1=full term) |

---

## 6. 벤치마크 페이즈 (core/perf 동일)

### 6.1 Single 페이즈

```
[Setup+Settle] → [Warmup(count)] → [Active(duration) — throughput + latency 동시 측정 + implicit drain]
```

1. **Setup + Settle**: `setup_connected_pair()` 내부에서 소켓 연결 후 `settle()` 호출 (`SETTLE_TIME_MS` = 100ms sleep)
2. **Warmup** (`PERF_WARMUP_COUNT`): 고정 횟수 send/recv 반복 (시간 기반이 아님). 기본값: 일반 1000, GATEWAY/SPOT 200. SPOT 은 `msg_size ≥ 65536` 시 최대 20 으로 clamp
3. **Active** (`PERF_SINGLE_DURATION_SECONDS`, 5초): duration 기반 throughput 측정 + reservoir sampling 으로 latency/p95/p99 동시 수집

> **core 구현 참고**: single 은 settle 과 drain 이 별도 named 페이즈로 노출되지 않는다.
> - **Settle**: `setup_connected_pair()` 내부에서 100ms sleep (네트워크 안정화)
> - **Drain**: Active 페이즈 종료 시 sender 완료 후 receiver 가 `drain_idle_limit` (기본 200ms) 동안 잔여 메시지 수신 대기
> - Active 페이즈에서 메시지 헤더의 `sent_ts_us` 를 기반으로 throughput 과 latency 를 동시에 측정한다.
> - 수신 측에서 reservoir sampling (`latency_stats_builder_t`) 으로 p95/p99 를 수집한다.

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle(settle_ms)] → [Active(duration)]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_WARMUP_SECONDS`, 2초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_SETTLE_MS`, 500ms): 인플라이트 메시지 소진 + 안정화 (one-way: phase_drain 라벨로 recv-only, echo: phase_warmup+allow_send=false)
4. **Active** (`PERF_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)

> **참고**: Active 이후 별도 Drain 페이즈는 없다. Drain 성격의 처리는 Settle 구간에서 수행된다.

---

## 7. 패턴별 상세 구현 계획

### 7.1 Single 패턴

> **참고**: run_policy_bench.py 기준 모든 single 패턴은 one-way 방향(`bandwidth 승수 = 1.0`).
> "소켓 동작" 열은 실제 send/recv 패턴(echo=양방향, one-way=단방향)을 나타낸다.

> core/perf 기준 single suite 는 **8개** 패턴이다 (`STANDARD_PATTERNS`).
> `run_policy_bench.py` 는 STREAM 3종을 추가로 포함하여 11개를 `SINGLE_PATTERNS` 로 정의하지만,
> 이는 런너가 multi 서버로 위임 실행하는 편의 기능이다.
> Java 바이너리가 직접 실행하는 single 패턴은 #1~#8 의 8개이다.

| # | 파일 | 패턴 | 소켓 타입 | 소켓 동작 | 트랜스포트 |
|---|------|------|-----------|----------|-----------|
| 1 | PerfPair.java | PAIR | PAIR×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 2 | PerfPubSub.java | PUBSUB | PUB+SUB | one-way | tcp,tls,ws,wss,inproc,ipc |
| 3 | PerfDealerDealer.java | DEALER_DEALER | DEALER×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 4 | PerfDealerRouter.java | DEALER_ROUTER | DEALER+ROUTER | echo | tcp,tls,ws,wss,inproc,ipc |
| 5 | PerfRouterRouter.java | ROUTER_ROUTER | ROUTER×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 6 | PerfRouterRouterPoll.java | ROUTER_ROUTER_POLL | ROUTER×2+Poller | echo | tcp,tls,ws,wss,inproc,ipc |
| 7 | PerfGateway.java | GATEWAY | Gateway+Receiver | echo | tcp,tls,ws,wss |
| 8 | PerfSpot.java | SPOT | Spot (pub/sub) | one-way | tcp,tls,ws,wss |
| 9 | *(multi 위임)* | STREAM | STREAM socket | echo | tcp,tls,ws,wss |
| 10 | *(multi 위임)* | STREAM_CALLBACK | STREAM+callback | echo | tcp,tls,ws,wss |
| 11 | *(multi 위임)* | STREAM_LEN32BE | STREAM+len32be | echo | tcp,tls,ws,wss |

> **STREAM 3종 구현**: Java single 파일은 없으며, multi suite 의 PerfMultiStream*Server.java 를 런너가
> `SINGLE_TO_MULTI_STREAM_PATTERN` 매핑으로 위임 실행한다. 클라이언트는 `core/perf/common/streamclient` C++ 바이너리.

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 별도 파일**로 분리한다.

| # | 서버 파일 | 클라이언트 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|----------|---------------|------|-----------|----------------|
| 1 | PerfMultiDealerDealerServer | PerfMultiDealerDealerClient | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | PerfMultiDealerRouterServer | PerfMultiDealerRouterClient | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | PerfMultiRouterRouterServer | PerfMultiRouterRouterClient | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | PerfMultiPubSubServer | PerfMultiPubSubClient | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | PerfMultiGatewayServer | PerfMultiGatewayClient | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | PerfMultiSpotServer | PerfMultiSpotClient | SPOT | Spot service instance publish | Spot service instance recv |
| 7 | PerfMultiStreamCallbackServer | (공통 stream client) | STREAM_CALLBACK | attachStream callback | C++ stream client |
| 9 | PerfMultiStreamLen32BeServer | (공통 stream client) | STREAM_LEN32BE | attachStreamLen32be | C++ stream client |

> 기준 릴리스는 `core/v4.0.0` 이고, native/runtime 동기화 커밋은 `da1d308a`
> (`chore(bindings): sync runtimes for core v4.0.0`) 이다.
> callback-only recv 모델 전환에 따라 모든 recv 는 콜백으로 처리하고, poller 는 사용하지 않는다.
> MULTI_STREAM(기존 sync recv 기반)은 삭제되었다. MULTI_STREAM_CALLBACK 이 STREAM 수신의 기본 패턴이다.

**Multi 서버 통신 프로토콜:**
- 서버 stdout 에 `READY,<endpoint>` 출력 → 스크립트가 클라이언트 시작
- 클라이언트 종료 시 서버에 stop-token 전송
- 서버 graceful shutdown 후 RESULT 메트릭 출력

---

## 8. 공통 유틸리티

### 8.1 Single common/ 패키지

**`PerfCommon.java`** — `single/.../common/` (core/perf single/common/bench_common.hpp 대응)

```java
public final class PerfCommon {
    // 환경변수 파싱
    static int parseEnv(String name, int defaultValue);
    static int parseEnvNonNegative(String name, int defaultValue);

    // 소켓 옵션 적용
    static void applySingleContextOptions(Context ctx);
    static void applySingleSocketOptions(Socket socket);

    // Send/Receive with retry (EAGAIN/EINTR only)
    static int receiveRetry(Socket socket, byte[] buffer);
    static int sendRetry(Socket socket, byte[] buffer);

    // 폴링
    static boolean waitForInput(Socket socket, int timeoutMs);
    static boolean waitUntil(BooleanSupplier check, int timeoutMs);

    // 엔드포인트 생성
    static String endpointFor(String transport, String name);

    // RESULT 출력 (bandwidth 승수 = 1.0 for all single)
    // core/perf 와 동일하게 5개 RESULT 라인 출력:
    //   throughput, bandwidth, latency, latency_p95, latency_p99
    // 런너 파서는 5종 모두 수집, 완료 판정은 3종 (throughput/bandwidth/latency) 기준
    static void printResult(String pattern, String transport, int size,
                           double throughput, double latencyUs,
                           double latencyP95Us, double latencyP99Us);

    // Gateway / Spot 헬퍼
    static void gatewayReceiveProviderMessage(Socket router, byte[] routingIdBuf, byte[] payloadBuf);
    static int spotReceivePayloadWithTimeout(Spot spot, byte[] payloadBuf, int timeoutMs);
}
```

**`PerfTls.java`** — single 전용 TLS 인증서 리졸버

```java
public final class PerfTls {
    static void configureTlsServerIfNeeded(Socket socket, String transport);
    static void configureTlsClientIfNeeded(Socket socket, String transport);
    // bindings/java/tests/certs/ 에서 server.crt, server.key, ca.crt 탐색
    static boolean tryResolvePerfTlsPaths(String[] outCert, String[] outKey, String[] outCa);
}
```

### 8.2 Multi common/ 패키지

**`PerfCommon.java`** — `multi/.../common/` (core/perf multi/common/perf_common.hpp 대응)

single 의 PerfCommon 유틸 중 필요한 것을 포함하고 multi 전용 기능 추가.

**`PerfMultiCommon.java`** — (core/perf multi/common/perf_common_multi.hpp 대응)

```java
public final class PerfMultiCommon {
    static int resolveClients(String pattern);          // PERF_CLIENTS — 비-STREAM: 100, STREAM: 10000
    static int resolveHwm(String pattern);              // PERF_HWM — 비-STREAM: 100, STREAM: 10
    static int resolveWarmupSeconds();                   // PERF_WARMUP_SECONDS — 기본 2
    static int resolveDurationSeconds();                 // PERF_DURATION_SECONDS — 기본 5
    static int resolveSettleMs();                        // PERF_SETTLE_MS — 기본 500
    // ... 기타 환경변수 리졸버 (SNDTIMEO, RCVTIMEO, CONNECT_READY_TIMEOUT 등)
}
```

**`PerfMultiClientHelpers.java`** — (core/perf multi/common/perf_client_helpers.hpp 대응)

```java
public final class PerfMultiClientHelpers {
    static boolean isSupportedTransport(String transport);
    static String parseEndpointArg(String[] args);
    static void waitAllClientConnectReady(List<MonitorSocket> monitors, int timeoutMs);
    // NOTE: send/recv 루프는 여기에 두지 않는다.
    // 각 패턴 클라이언트 파일이 자체적으로 인라인한다 (§15.3).
}
```

**`PerfMultiServerEntry.java`** — (core 각 server main() 의 디스패치+메트릭 로직을 집약한 Java 전용 클래스)

> **참고**: core 의 `perf_entry.hpp` 는 `set_perf_pattern_env()` env setter 1개만 포함하는 헬퍼이다.
> Java 에서는 이 env 설정을 `PerfMultiMain` 에서 직접 처리하며, 아래 Entry 클래스는
> core 의 각 server/client `main()` 에 분산된 디스패치+CPU/MEM 메트릭 수집 로직을 공통 클래스로 집약한 것이다.

```java
// 1. 패턴 디스패치 → runServer(transport, size)
// 2. CPU/MEM 메트릭 수집
// 3. RESULT 출력: server_cpu_pct, server_mem_mb
```

**`PerfMultiClientEntry.java`** — (core 각 client main() 의 디스패치+메트릭 로직을 집약한 Java 전용 클래스)

```java
// 1. 패턴 디스패치 → runClient(transport, size, endpoint)
// 2. CPU/MEM 메트릭 수집
// 3. RESULT 출력: client_cpu_pct, client_mem_mb
```

**`PerfMultiTls.java`** — TLS 인증서 리졸버 (dotnet PerfMultiTls.cs 대응)

```java
// bindings/java/tests/certs/ 에서 탐색
// 상위 디렉토리 순회: bindings/java/tests/certs → tests/certs
```

### 8.3 메트릭 페이로드 헤더 (SPF1 / MPF1)

sender 가 페이로드 첫 32바이트에 헤더를 stamp 하고, receiver 가 decode 하여 phase 필터링 및 latency 측정에 사용한다.

**헤더 구조** (core `perf_single_metric_header.hpp` / `perf_metric_header.hpp` 동일):

| 오프셋 | 크기 | 필드 | 설명 |
|--------|------|------|------|
| 0 | 4 | `magic` | SPF1=`0x53504631`, MPF1=`0x4D504631` |
| 4 | 4 | `run_id` | 실행 ID (타임스탬프 기반) |
| 8 | 4 | `phase` | 0=unknown, 1=warmup, 2=active (multi: 3=drain) |
| 12 | 4 | `msg_size` | 메시지 크기 |
| 16 | 8 | `seq` | 시퀀스 번호 |
| 24 | 8 | `sent_ts_us` | 송신 타임스탬프 (μs, epoch) |

총 **32바이트** (`uint32×4 + uint64×2`).

**`PerfSingleMetricHeader.java`** — `single/.../common/`

```java
public final class PerfSingleMetricHeader {
    static final int HEADER_SIZE = 32;
    static final int MAGIC = 0x53504631;  // "SPF1"

    // phase 상수
    static final int PHASE_UNKNOWN = 0;
    static final int PHASE_WARMUP = 1;
    static final int PHASE_ACTIVE = 2;

    // 페이로드 첫 32바이트에 헤더 stamp (little-endian)
    static void stampPayload(byte[] payload, int runId, int phase,
                             int msgSize, long seq, long sentTsUs);

    // 페이로드 첫 32바이트에서 헤더 decode
    static boolean decodePayloadHeader(byte[] payload, int[] out);
    // out: [magic, runId, phase, msgSize] + long[]: [seq, sentTsUs]

    // 현재 시각 (μs)
    static long nowUs();
}
```

**`PerfMultiMetricHeader.java`** — `multi/.../common/`

```java
public final class PerfMultiMetricHeader {
    static final int HEADER_SIZE = 32;
    static final int MAGIC = 0x4D504631;  // "MPF1"

    // phase 상수 (single + drain)
    static final int PHASE_UNKNOWN = 0;
    static final int PHASE_WARMUP = 1;
    static final int PHASE_ACTIVE = 2;
    static final int PHASE_DRAIN = 3;

    static void stampPayload(byte[] payload, int runId, int phase,
                             int msgSize, long seq, long sentTsUs);
    static boolean decodePayloadHeader(byte[] payload, int[] out);
    static long nowUs();
}
```

---

## 9. TLS 인증서 관리

### 9.1 독립 인증서 디렉토리

각 바인딩은 `bindings/<lang>/tests/certs/` 에서 인증서를 독립 관리한다.

```
bindings/java/tests/certs/
├── server.crt          ← 서버 인증서 (localhost SAN 포함)
├── server.key          ← 서버 개인키
└── ca.crt              ← CA 인증서
```

> dotnet 선례: `bindings/dotnet/tests/certs/` 에 동일 3개 파일 관리 중.

### 9.2 인증서 생성

> **참고**: core/perf 벤치마크는 인증서를 **소스 코드에 임베디드** (`bench_common.hpp` 의 `test_certs` namespace)
> 하고, 런타임에 `/tmp/bench_*.pem` 임시 파일로 기록하여 사용한다.
> Java 는 파일 기반으로 관리하므로, dotnet 선례와 동일하게 `bindings/java/tests/certs/` 에
> 인증서 파일을 독립 배치한다.

인증서 파일은 기존 바인딩(dotnet)에서 복사하거나, OpenSSL 로 재생성:

```bash
# dotnet 인증서에서 복사 (동일 인증서)
cp bindings/dotnet/tests/certs/server.crt bindings/java/tests/certs/
cp bindings/dotnet/tests/certs/server.key bindings/java/tests/certs/
cp bindings/dotnet/tests/certs/ca.crt     bindings/java/tests/certs/
```

### 9.3 인증서 경로 탐색 로직

dotnet `PerfMultiTls.cs` 와 동일한 상위 디렉토리 순회 방식:

```java
// 탐색 순서:
// 1. bindings/java/tests/certs/
// 2. tests/certs/ (상위 순회)
// AppContext 또는 user.dir 기준으로 상위 디렉토리를 순회하며
// "bindings/java/tests/certs" 또는 "tests/certs" 하위에
// server.crt, server.key, ca.crt 3개 파일이 모두 존재하는지 확인
```

### 9.4 소켓 옵션 설정

```java
socket.setSockOpt(SocketOption.TLS_CERT, certPath);
socket.setSockOpt(SocketOption.TLS_KEY, keyPath);
socket.setSockOpt(SocketOption.TLS_CA, caPath);
```

---

## 10. Java API 매핑

| Core C API (perf 벤치마크 사용) | Java API |
|-------------|----------|
| `zlink_ctx_new()` | `new Context()` |
| `zlink_socket(ctx, type)` | `new Socket(ctx, SocketType.XXX)` |
| `zlink_bind(s, endpoint)` | `socket.bind(endpoint)` |
| `zlink_connect(s, endpoint)` | `socket.connect(endpoint)` |
| `zlink_send(s, buf, len, flags)` | `socket.send(byte[], SendFlag.XXX)` |
| `zlink_recv(s, buf, len, flags)` | `socket.recv(byte[], ReceiveFlag.XXX)` |
| `zlink_setsockopt(s, opt, val)` | `socket.setSockOpt(SocketOption.XXX, val)` |
| `zlink_getsockopt(s, opt)` | `socket.getSockOptInt(SocketOption.XXX)` |
| `zlink_poll(items, n, timeout)` | `Poller.poll(timeoutMs)` / `Poller.pollCount(timeoutMs)` |
| STREAM attach | `socket.attachStream(handler, mode)` |
| STREAM send | `socket.streamSend(routingId, payload, flags)` |
| `zlink_ctx_set(ctx, IO_THREADS)` | `ctx.setOption(ContextOption.IO_THREADS, n)` |
| Gateway / Receiver | `new Gateway(...)` / `new Receiver(...)` |
| Spot facade mode | `new Spot(node)` + `spot.publish()/spot.recv()` |
| Spot pollable mode | `new Spot(node)` + `Poller.addSpotPub()/addSpotSub()` + facade send/recv |
| Gateway pollable mode | `new Gateway(...)` + `Poller.addGateway()` |
| Receiver pollable mode | `new Receiver(...)` + `Poller.addReceiver()` |
| MonitorSocket | `socket.monitorOpen(events)` |

**주의: `Socket.send(MemorySegment, ...)` 등 Panama 네이티브 메모리 API 도 사용 가능하나, 벤치마크에서는 `byte[]` 기반이 간결.**

---

## 11. 리소스 메트릭 수집 (Java)

### 11.1 CPU 사용률

```java
// Linux: /proc/self/stat 파싱 (utime + stime)
// 대안: ManagementFactory.getOperatingSystemMXBean() (com.sun.management)
long cpuTimeBefore = ((com.sun.management.OperatingSystemMXBean) mxBean).getProcessCpuTime();
// ... 측정 ...
long cpuTimeAfter = ((com.sun.management.OperatingSystemMXBean) mxBean).getProcessCpuTime();
double cpuPct = (cpuTimeAfter - cpuTimeBefore) / (elapsedNanos * nCores) * 100.0;
```

### 11.2 메모리 사용량

```java
// Linux: /proc/self/status → VmRSS
// 대안: Runtime.getRuntime().totalMemory() - freeMemory() (힙만)
// 정확도를 위해 /proc/self/status 직접 파싱 권장
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
  --duration N                활성 측정 초 (기본: 5)
  --reuse-build               빌드 재사용
  --clean-build               클린 빌드
  --output PATH               콘솔 tee
  --results-dir PATH          결과 저장 경로
  --results-tag NAME          파일명 태그
  --pin-cpu                   CPU 고정
  --io-threads N              IO 스레드
  --msg-sizes LIST            메시지 크기 목록 (러너가 size별로 별도 실행; 바이너리 1회 실행은 1 size)
  --transports LIST           트랜스포트
  --hwm N                     소켓 HWM
  --sndtimeo N                송신 타임아웃 (ms)
  --rcvtimeo N                수신 타임아웃 (ms)
```

내부적으로 `bindings/perf/run_policy_bench.py --binding java --suite single` 을 호출한다.

### 12.2 run_benchmarks_multi.sh (루트)

```bash
./run_benchmarks_multi.sh [options]

추가 Options:
  --clients N                 클라이언트 수 (기본: 100, STREAM: 10000)
  --warmup N                  웜업 초 (기본: 2)
  --transport-transition-ms N 트랜스포트 전환 대기 (기본: 3000)
  --pattern-transition-ms N   패턴 전환 대기 (기본: 3000)
```

내부: `bindings/perf/run_policy_bench.py --binding java --suite multi`

#### nofile preflight (multi 전용)

multi 스크립트는 실행 전 파일 디스크립터 한도를 확인한다 (core/perf/run_benchmarks_multi.sh 동일):

```bash
ensure_nofile_limit() {
  local clients=$1
  local required=$(( clients * 3 + 4096 ))
  local soft=$(ulimit -Sn)
  local hard=$(ulimit -Hn)
  if (( required > soft )); then
    ulimit -Sn "$required" 2>/dev/null || return 1
  fi
}
```

- 계산식: `clients × 3 + 4096`
- `PERF_SKIP_NOFILE_CHECK=1` 로 비활성화 가능
- 한도 부족 시 `SKIP,current,<PATTERN>,all,nofile:...` 출력 후 해당 패턴 건너뜀

### 12.3 run_benchmarks.ps1 / run_benchmarks_multi.ps1

동일 인터페이스, PowerShell 구현.

---

## 13. 산출물

### 13.1 결과 파일

```
bindings/java/perf/results/single/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
bindings/java/perf/results/multi/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
```

#### report 파일 형식

`write_report_file()` 가 생성한다. **테이블만 저장** (META/RESULT/Completion 섹션 없음):

```
## PATTERN: PAIR
### tcp
| Size | Throughput | Bandwidth | Latency |
|------|-----------|-----------|---------|
| 64   | 523401.23 | 33.50     | 12.35   |
```

### 13.2 결과 보존 정책

- 최대 100개 파일 per report/ 디렉토리
- 초과 시 파일명 정렬 기준 oldest 삭제 (FIFO)

---

## 14. 구현 순서

### Phase 0: 인증서 및 스크립트 준비

1. `bindings/java/tests/certs/` 디렉토리 생성 + 인증서 복사 (server.crt, server.key, ca.crt)
2. `run_policy_bench.py` 수정: `--enable-native-access=ALL-UNNAMED` 추가, artifact 경로 갱신
3. `.gitignore`, `.gitkeep` 파일

### Phase 1: 인프라 (빌드, 공통, 진입점)

4. `perf/single/Zlink.PerfBench/build.gradle` 생성
5. `perf/multi/Zlink.PerfBench/build.gradle` 생성
6. `single/.../common/PerfCommon.java` — 환경변수 파싱, retry 헬퍼, RESULT 출력
7. `single/.../common/PerfSingleMetricHeader.java` — SPF1 페이로드 헤더 stamp/decode
8. `single/.../common/PerfTls.java` — single TLS 인증서 리졸버
9. `single/.../PerfMain.java` — 패턴 디스패치 진입점
10. `multi/.../common/PerfCommon.java` — multi 공통 유틸
11. `multi/.../common/PerfMultiMetricHeader.java` — MPF1 페이로드 헤더 stamp/decode
12. `multi/.../common/PerfMultiCommon.java` — multi 설정 리졸버
13. `multi/.../common/PerfMultiClientHelpers.java` — client 유틸 (transport 판별, endpoint 파싱, 연결 대기)
14. `multi/.../common/PerfMultiServerEntry.java` — 서버 디스패치 + 메트릭
15. `multi/.../common/PerfMultiClientEntry.java` — 클라이언트 디스패치 + 메트릭
16. `multi/.../common/PerfMultiTls.java` — TLS 인증서 리졸버
17. `multi/.../PerfMultiMain.java` — --multi-server / --multi-client 디스패치

### Phase 2: Single 소켓 패턴 (6개)

18. `single/.../src/PerfPair.java`
19. `single/.../src/PerfPubSub.java`
20. `single/.../src/PerfDealerDealer.java`
21. `single/.../src/PerfDealerRouter.java`
22. `single/.../src/PerfRouterRouter.java`
23. `single/.../src/PerfRouterRouterPoll.java`

### Phase 3: Single 서비스 패턴 (2개)

24. `single/.../src/PerfGateway.java`
25. `single/.../src/PerfSpot.java`

### Phase 4: Multi 패턴 — server/client 분리 (6×2 + 3 서버 only)

26. `multi/.../src/PerfMultiDealerDealerServer.java` + `PerfMultiDealerDealerClient.java`
27. `multi/.../src/PerfMultiDealerRouterServer.java` + `PerfMultiDealerRouterClient.java`
28. `multi/.../src/PerfMultiRouterRouterServer.java` + `PerfMultiRouterRouterClient.java`
29. `multi/.../src/PerfMultiPubSubServer.java` + `PerfMultiPubSubClient.java`
30. `multi/.../src/PerfMultiGatewayServer.java` + `PerfMultiGatewayClient.java`
31. `multi/.../src/PerfMultiSpotServer.java` + `PerfMultiSpotClient.java`
32. `multi/.../src/PerfMultiStreamServer.java` (서버 only)
33. `multi/.../src/PerfMultiStreamCallbackServer.java` (서버 only)
34. `multi/.../src/PerfMultiStreamLen32BeServer.java` (서버 only)

### Phase 5: 스크립트 및 마무리

35. `run_benchmarks.sh` / `.ps1` (루트 + single/ + multi/)
36. `run_benchmarks_multi.sh` / `.ps1`
37. `run_comparison.py`
38. `README.md`
39. 빌드 검증 (`./gradlew :perf-single:classes :perf-multi:classes`)
40. `run_policy_bench.py` 통합 검증

### Phase 6: 코드 품질 리뷰 및 리팩토링

> 모든 패턴 구현과 스크립트 완성 후, 코드 전체에 대한 품질 리뷰와 개선을 수행한다.
> 성능 벤치마크 코드이므로 불필요한 오버헤드에 특히 엄격히 대응한다.

41. **Dead Code / 미사용 파일 정리**
    - 사용되지 않는 import, 변수, 메서드, 클래스 전부 삭제
    - 의미 없는 주석 (TODO 잔재, 복사 흔적, 주석 처리된 코드) 전부 삭제
    - 빈 파일, 미사용 설정 파일 삭제
42. **가독성 리팩토링**
    - 메서드/변수 네이밍 일관성 검토 (core/perf 와 대응 관계 명확화)
    - 과도한 중첩 / 긴 메서드 분리 (단, 벤치마크 인라인 정책 범위 내)
    - 매직 넘버 → 상수 추출 (타임아웃, 버퍼 크기, 재시도 한도 등)
    - 패턴 파일 간 구조 일관성 확보 (동일 페이즈 순서, 동일 변수명 컨벤션)
43. **성능 리뷰 (벤치마크 오버헤드 제거)**
    - **불필요한 할당**: 측정 루프 내 `new byte[]`, `new String()`, 박싱 (`Integer`, `Long`) 등
    - **불필요한 복사**: `Arrays.copyOf`, `System.arraycopy` 가 회피 가능한 경우
    - **불필요한 대기**: 측정 루프 내 `Thread.sleep`, busy-wait 이 과도한 경우
    - **GC 압박**: 측정 구간에서 단명 객체 반복 생성 여부
    - **I/O 플러시**: `System.out.println` 이 측정 루프 내에 포함되지 않는지 확인
    - **버퍼 재사용**: send/recv 버퍼가 루프 밖에서 1회 할당 후 재사용되는지 확인
    - 리뷰 체크리스트 (파일별):
      ```
      [ ] 측정 루프 내 힙 할당 0건
      [ ] 측정 루프 내 불필요한 복사 0건
      [ ] 측정 루프 내 Thread.sleep / 과도한 busy-wait 없음
      [ ] 측정 루프 밖에서 I/O 출력
      [ ] send/recv 버퍼 루프 밖 할당 + 재사용
      [ ] 박싱/언박싱 없음 (primitive 직접 사용)
      ```
44. **개선 사항 적용 후 주석 추가**
    - 리팩토링/성능 개선 완료 후, 코드 이해를 돕는 적절한 수준의 주석 추가
    - 주석 대상:
      - 각 패턴 파일 상단: 패턴 설명, 소켓 구성, 측정 방식 요약 (1-3줄)
      - 페이즈 전환 지점: `// --- Warmup ---`, `// --- Active measurement ---` 등
      - 비자명 로직: stop-token 처리, routing-id 관리, echo 루프 구조 등
      - 성능 관련 설계 결정: 버퍼 사전 할당 이유, DontWait 플래그 사용 이유 등
    - 주석 금지 대상:
      - 자명한 코드 (`i++`, `socket.close()` 등)
      - API 호출 단순 설명 (JavaDoc 참조로 충분한 경우)
      - 이력/변경 로그 스타일 주석

---

## 15. 정책 준수 사항

### 15.1 금지 사항

- **C API 직접 호출 금지**: `zlink_*()` 함수, JNI, Panama FFI raw call 일체 금지
- **래퍼 호출 금지**: `Native.java`, `NativeMsg.java` 등 internal 패키지 접근 금지
- **Retry 금지** (정책): send/recv 실패 시 재시도 없음 (EAGAIN/EINTR 은 예외적으로 루프)
- **Inflight/Outstanding 옵션 금지**: 백프레셔 한도 = 소켓 HWM 만

### 15.2 필수 사항

- 각 벤치마크 소스에 **소켓 생성, bind/connect, send/recv 루프, 페이즈 컨트롤** 인라인
- `RESULT,current,...` 형식의 stdout 출력
- STREAM 서버는 stop-token `__zlink_perf_stop__` 수신 시 정상 종료
- Multi 서버는 `READY,<endpoint>` stdout 출력 후 클라이언트 대기
- TLS 인증서는 `bindings/java/tests/certs/` 경로 사용

### 15.3 코드 인라이닝 정책 — core/perf 와의 차이점

> **core/perf (C++)** 는 `perf_client_helpers.hpp` 에 `run_echo_window_round_robin()`,
> `run_one_way_window_loop()` 등 공통 send/recv 루프를 두고, 각 패턴 파일이 이를 호출한다.
>
> **Java 포팅은 이 구조를 따르지 않는다.** 각 패턴 파일이 send/recv 핵심 루프를
> 자체적으로 포함하여, 파일 하나만 열면 해당 패턴의 전체 벤치마크 흐름을
> 샘플 코드처럼 읽을 수 있도록 한다.
>
> **명시 정책:** send/recv 코드는 **패턴 파일 내부에서만 공통화**한다.
> `core/perf` 처럼 패턴 간 공용 helper(`common/`)로 send/recv 루프를 공유하지 않는다.
> 즉, 공통화가 필요하면 해당 패턴 파일의 `private` 메서드로만 추출한다.

**패턴 파일 내 인라인 (각 파일에 직접 작성):**
- 소켓 생성, bind/connect
- 페이로드 헤더 stamp (SPF1/MPF1)
- warmup / settle / active 페이즈 루프
- send/recv 호출 및 에러 처리 (EAGAIN/EINTR 루프)
- 메트릭 수집 (throughput, latency reservoir sampling)
- RESULT 출력 호출

**Single 패턴 예시 (PerfPair.java 핵심 구조):**
```java
// --- Warmup ---
for (int i = 0; i < warmupCount; i++) {
    stampHeader(payload, SPF1_MAGIC, runId, PHASE_WARMUP, msgSize, seq++);
    sender.send(payload, 0, msgSize, Socket.DONTWAIT);
    receiver.receive(recvBuf, 0, msgSize, 0);
}

// --- Active (duration-based, throughput + latency 동시 측정) ---
long activeStart = System.nanoTime();
long deadline = activeStart + durationNs;
long received = 0;
while (System.nanoTime() < deadline) {
    stampHeader(payload, SPF1_MAGIC, runId, PHASE_ACTIVE, msgSize, seq++);
    sender.send(payload, 0, msgSize, Socket.DONTWAIT);
    int rc = receiver.receive(recvBuf, 0, msgSize, 0);
    if (rc > 0 && decodePhase(recvBuf) == PHASE_ACTIVE) {
        received++;
        double latencyUs = (System.nanoTime() - decodeSentTsUs(recvBuf)) / 1000.0;
        latencyStats.add(latencyUs);
    }
}
double elapsedSec = (System.nanoTime() - activeStart) / 1e9;
double throughput = received / elapsedSec;

PerfCommon.printResult("current", pattern, transport, msgSize,
    throughput, latencyStats.mean(), latencyStats.p95(), latencyStats.p99());
```

**Multi 패턴 예시 (PerfMultiDealerDealerClient.java 핵심 구조):**
```java
// --- Warmup (duration-based) ---
long warmupDeadline = System.nanoTime() + warmupSeconds * 1_000_000_000L;
while (System.nanoTime() < warmupDeadline) {
    stampHeader(payload, MPF1_MAGIC, runId, PHASE_WARMUP, msgSize, seq++);
    sockets.get((int)(seq % N)).send(payload, 0, msgSize, Socket.DONTWAIT);
}

// --- Settle (인플라이트 소진) ---
Thread.sleep(settleMs);

// --- Active (duration-based, 라운드로빈) ---
long activeDeadline = System.nanoTime() + durationSeconds * 1_000_000_000L;
long sent = 0;
while (System.nanoTime() < activeDeadline) {
    stampHeader(payload, MPF1_MAGIC, runId, PHASE_ACTIVE, msgSize, seq++);
    sockets.get((int)(sent++ % N)).send(payload, 0, msgSize, Socket.DONTWAIT);
}
```

**`common/` 으로 추출 허용 (유틸리티만):**
- 환경변수 파싱, 소켓 옵션 적용, TLS 설정
- `printResult()` — RESULT 라인 포맷팅
- `endpointFor()` — 트랜스포트별 엔드포인트 생성
- `stampHeader()` / `decodeHeader()` — 메트릭 헤더 encode/decode
- `isSupportedTransport()`, `parseEndpointArg()`, `waitAllClientConnectReady()`

**`common/` 에 두지 않는 것:**
- send/recv 루프, 페이즈 전환 로직, 메트릭 수집 루프 — 반드시 각 패턴 파일 내에 인라인
- STREAM 클라이언트는 Java 에서 구현하지 않음 (`core/perf/common/streamclient` C++ 공용 바이너리 사용)

---

## 16. 검증 계획

### 16.1 정적 검증

- C API / internal 패키지 직접 호출 금지 검사:
  ```bash
  # Java 소스에서 Native, NativeMsg, zlink_ 직접 호출이 없어야 함
  rg -n "\\bNative\\." bindings/java/perf --glob '*.java'
  rg -n "\\bNativeMsg\\." bindings/java/perf --glob '*.java'
  rg -n "\\bdev\\.kairoscode\\.zlink\\.internal\\b" bindings/java/perf --glob '*.java'
  # 기대 결과: 각각 0건
  ```
- Java 측 stream client 구현 없음 확인:
  ```bash
  rg -n "StreamClient" bindings/java/perf --glob '*.java'
  # 기대 결과: 0건 (STREAM client = core/perf/common/streamclient C++ 공용 바이너리)
  ```
- TLS 인증서 경로 확인:
  ```bash
  rg -n "tests/certs" bindings/java/perf --glob '*.java'
  # 기대 결과: bindings/java/tests/certs 경로만 사용, core/tests/certs 참조 없음
  ```

### 16.2 빌드 검증

- [ ] `./gradlew :perf-single:classes` 빌드 성공 (종료코드 0)
- [ ] `./gradlew :perf-multi:classes` 빌드 성공 (종료코드 0)
- [ ] `bindings/java/tests/certs/` 인증서 파일 3개 존재 (server.crt, server.key, ca.crt)

### 16.3 기능 smoke 테스트

- single smoke:
  ```bash
  PERF_SINGLE_DURATION_SECONDS=1 \
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite single \
    --pattern PAIR --transports tcp --msg-sizes 64 \
    --runs 1 --reuse-build
  ```
- multi smoke:
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite multi \
    --pattern MULTI_DEALER_DEALER --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 10 --reuse-build
  ```
- stream smoke:
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite multi \
    --pattern MULTI_STREAM_CALLBACK --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 100 --reuse-build
  ```
- TLS smoke:
  ```bash
  PERF_SINGLE_DURATION_SECONDS=1 \
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite single \
    --pattern PAIR --transports tls --msg-sizes 64 \
    --runs 1 --reuse-build
  ```

### 16.4 메트릭 정확성 검증

각 RESULT 라인의 메트릭 값이 논리적으로 정확한지 검증한다.

**메트릭 존재 검증 (조합별):**
- single: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99` — 5개 메트릭이 모든 pattern/transport/size 조합에 존재
- multi: 위 5개 + `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb`
- 런너 파서는 11종 메트릭을 모두 수집하지만, **완료 판정**은 Java 기준 조합당 3개 (throughput/bandwidth/latency) 이므로 p95/p99 누락이 완료 판정에 영향 없음
- 단위: `latency*` 계열은 RESULT에서 **ms**

**대역폭 계산식 검증:**
```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```
- single 전체 (one-way 방향): `bandwidth ≈ throughput × size / 1,000,000`
- multi echo 패턴 (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*): `bandwidth ≈ throughput × size × 2 / 1,000,000`
- multi one-way 패턴 (DEALER_DEALER, PUBSUB, SPOT): `bandwidth ≈ throughput × size / 1,000,000`
- 허용 오차: ±1%

**Percentile 일관성 검증:**
- `latency_p95 >= latency` (mean)
- `latency_p99 >= latency_p95`

**메트릭 값 범위 검증:**
- `throughput > 0` (유효한 처리량)
- `bandwidth > 0`
- `latency > 0` (유효한 레이턴시, ms)
- `cpu_pct >= 0 && cpu_pct <= 100 × nCores`
- `mem_mb > 0`

**검증 방법:**
```bash
# single smoke 결과 파일에서 RESULT 라인 추출 후 검증
PERF_SINGLE_DURATION_SECONDS=2 \
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite single \
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
  --binding java --suite multi \
  --pattern MULTI_DEALER_ROUTER --transports tcp --msg-sizes 64,1024 \
  --runs 1 --multi-duration-seconds 2 --multi-clients 10 --reuse-build --result

# echo 패턴이므로 bandwidth = throughput × size × 2 / 1,000,000
```

### 16.5 사이즈별 순차 실행 및 테이블 출력 검증

**런너 동작 이해:**
- `run_policy_bench.py` 는 자식 프로세스(벤치마크)의 stdout 을 `subprocess.PIPE` 로 캡처한다.
- 벤치마크가 출력하는 `RESULT,...` 라인은 자식 프로세스 종료 후 파싱되며, 실시간 중계되지 않는다.
- 런너가 콘솔에 출력하는 것은 진행 상황 (`Testing tcp | 64B: 1 `) 과 최종 테이블이다.
- RESULT 라인은 report 파일에 포함되지 않는다 (report 는 테이블만 저장).

**검증 대상: 벤치마크 자체의 RESULT 출력 정확성**
- 벤치마크 프로세스가 stdout 에 `RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,<metric>,<value>` 형식의 라인을 올바르게 출력하는지 확인한다.
- 각 조합의 5개 메트릭 (throughput, bandwidth, latency, latency_p95, latency_p99) 이 모두 출력되는지 확인한다.

**검증 방법:**
```bash
# 3개 사이즈로 실행하고 콘솔 로그를 파일로 수집
PERF_SINGLE_DURATION_SECONDS=1 \
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --reuse-build \
  --output bindings/java/perf/results/single/report/size_progress.log
```

**로그 검증 기준:**
1. 콘솔에 `Testing tcp | 64B:`, `Testing tcp | 1024B:`, `Testing tcp | 65536B:` 가 순서대로 출력됨
2. 최종 테이블에 3개 사이즈 행이 모두 포함됨
3. report 파일에 각 사이즈의 테이블 행이 존재함

**자동 검증 스크립트 (선택):**
```python
# report 파일에서 RESULT 라인의 size 값 순서 및 메트릭 완전성 검증
import glob, re
results = {}
for path in sorted(glob.glob("bindings/java/perf/results/single/report/perf_*.txt")):
    with open(path) as f:
        for line in f:
            m = re.match(r"RESULT,current,PAIR,tcp,(\d+),(\w+),", line)
            if m:
                size, metric = int(m.group(1)), m.group(2)
                results.setdefault(size, set()).add(metric)
for size in [64, 1024, 65536]:
    assert size in results, f"missing size: {size}"
    assert results[size] >= {"throughput", "bandwidth", "latency", "latency_p95", "latency_p99"}, f"missing metrics for {size}"
```

### 16.6 기본 설정 전체 실행 무실패 검증

기본 옵션(옵션 미지정)으로 single/multi 전체를 실행하여 모든 패턴이 실패 없이 완료되는지 확인한다.

**single 기본 실행:**
```bash
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite single --result
```

**multi 기본 실행:**
```bash
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite multi --result
```

**합격 기준:**
- [ ] 두 실행 모두 프로세스 종료코드 `0`
- [ ] 콘솔에 `status: complete` 출력 확인
- [ ] 콘솔에 `warning: status=partial` 경고가 없어야 함
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)
- [ ] `--result` 사용 시 report 파일 생성 확인 (테이블만 포함)

**기본 조합 수 예상 (single):**

> core/perf 기준 single suite 는 8종이지만, `run_policy_bench.py` 의 `SINGLE_PATTERNS` 에는
> STREAM 3종이 추가 포함되어 있다 (런너 기준 11종).
> Java 는 `supports_split_multi()=true` 이므로, 런너가 single STREAM 패턴을
> multi server 경로로 위임 실행한다 (`SINGLE_TO_MULTI_STREAM_PATTERN` 매핑 사용).
> 즉, multi stream 서버 구현이 완료되면 런너 실행 시 STREAM 48 조합도 성공 대상이 된다.

- socket 패턴 (6종: PAIR~ROUTER_ROUTER_POLL) × 6 transport (Linux) × 6 size = 216 조합
- STREAM 패턴 (3종) × 4 transport × 4 size = 48 조합 → multi server 위임 실행
- gateway/spot (2종) × 4 transport × 6 size = 48 조합
- 총: **312** 조합 → UNSUPPORTED 제외한 나머지 전부 success

**기본 조합 수 예상 (multi):**
- 비-STREAM 패턴 (6종) × 4 transport × 6 size = 144 조합
- STREAM 패턴 (3종) × 4 transport × 4 size = 48 조합
- 총: 192 조합 → UNSUPPORTED 제외한 나머지 전부 success

**결과 파일 확인:**
```bash
# report 파일 확인 (--result 사용 시 생성, 테이블만 포함)
ls -la bindings/java/perf/results/single/report/perf_*.txt
ls -la bindings/java/perf/results/multi/report/perf_*.txt
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `src/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 별도 파일로 분리.
- runner 옵션/기본값/결과 형식이 `run_policy_bench.py` 정책 기준으로 동등하게 동작 (5개 메트릭 출력, 완료 판정은 3개 기준, report=table-only).
- single/multi 모든 패턴 클래스가 `bindings/java/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 C API / internal 패키지 직접 호출 0건.
- TLS 인증서는 `bindings/java/tests/certs/` 에서 독립 관리.
- retry 로직/우회 wrapper/비정책 실행 경로가 없음.
- send/recv 공통화는 패턴 파일 내부(private helper)로만 제한되고, 패턴 간 공용 helper(`common/`)에 루프가 없다.
- **메트릭 헤더**: single 은 SPF1 (0x53504631), multi 는 MPF1 (0x4D504631) 페이로드 헤더를 stamp/decode 하여 phase 필터링 및 latency 측정에 사용.
- **메트릭 정확성**(조합당 5개 메트릭 출력: throughput, bandwidth, latency, latency_p95, latency_p99 / bandwidth 계산식 / 값 범위)이 검증됨. 완료 판정은 필수 3개(throughput, bandwidth, latency) 기준.
- **벤치마크 RESULT 출력 정확성**: 각 조합의 5개 메트릭이 stdout 에 올바르게 출력되고 report 파일에 테이블로 저장됨이 검증됨.
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- `run_policy_bench.py` 수정 반영: `--enable-native-access=ALL-UNNAMED`, artifact 경로 갱신.
- **코드 품질 리뷰 완료**: dead code/미사용 주석 0건, 측정 루프 내 불필요한 할당/복사/대기 0건.
- **주석 정리 완료**: 패턴 설명, 페이즈 전환, 비자명 로직에 적절한 수준의 주석 추가.

---

## 18. 구현 진행 체크리스트

> 각 항목을 순서대로 확인하며 진행한다. `[x]` 로 완료를 표시한다.
> 괄호 안 `§N` 은 본 문서의 해당 섹션 참조이다.

---

### Phase 0: 인증서 및 스크립트 준비

**0-1. TLS 인증서 (§9)**
- [ ] `bindings/java/tests/certs/` 디렉토리 존재
- [ ] `server.crt` 파일 존재 (localhost SAN 포함)
- [ ] `server.key` 파일 존재
- [ ] `ca.crt` 파일 존재
- [ ] 3개 파일이 `bindings/dotnet/tests/certs/` 와 동일 내용이거나 동등한 OpenSSL 인증서

**0-2. run_policy_bench.py 수정 (§3.4)**
- [ ] `binding_cmd_prefix()` — `--enable-native-access=ALL-UNNAMED` 추가됨
- [ ] `binding_cmd_prefix()` — cp 경로가 `java/perf/single/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main`
- [ ] `binding_multi_role_command()` — `--enable-native-access=ALL-UNNAMED` 추가됨
- [ ] `binding_multi_role_command()` — cp 경로가 `java/perf/multi/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main`
- [ ] `build_binding_if_needed()` — artifact 경로가 `java/perf/<suite_dir>/Zlink.PerfBench/build/classes/java/main/`

**0-3. 프로젝트 메타 파일 (§1)**
- [ ] `bindings/java/perf/.gitignore` 생성 (build/ 제외)
- [ ] `bindings/java/perf/results/single/report/.gitkeep` 존재
- [ ] `bindings/java/perf/results/multi/report/.gitkeep` 존재

---

### Phase 1: 인프라 (빌드, 공통, 진입점)

**1-1. 빌드 시스템 (§2)**
- [ ] `settings.gradle` 에 `:perf-single`, `:perf-multi` 프로젝트 등록 확인
- [ ] `perf/single/Zlink.PerfBench/build.gradle` 생성됨
  - [ ] `java.toolchain.languageVersion = 22`
  - [ ] `implementation project(':')` 의존성
  - [ ] `jar { enabled = false }`
- [ ] `perf/multi/Zlink.PerfBench/build.gradle` 생성됨
  - [ ] `java.toolchain.languageVersion = 22`
  - [ ] `implementation project(':')` 의존성
  - [ ] `jar { enabled = false }`
- [ ] `./gradlew -q :perf-single:classes` 빌드 성공 (종료코드 0)
- [ ] `./gradlew -q :perf-multi:classes` 빌드 성공 (종료코드 0)
- [ ] single 산출물 경로 존재: `perf/single/Zlink.PerfBench/build/classes/java/main/`
- [ ] multi 산출물 경로 존재: `perf/multi/Zlink.PerfBench/build/classes/java/main/`

**1-2. Single common/ 패키지 (§8.1)**
- [ ] `PerfCommon.java` 생성됨
  - [ ] `parseEnv()` / `parseEnvNonNegative()` — 환경변수 파싱
  - [ ] `applySingleContextOptions()` — IO_THREADS, MAX_SOCKETS (§5.1)
  - [ ] `applySingleSocketOptions()` — HWM, SNDHWM, RCVHWM, SNDTIMEO, RCVTIMEO (§5.1)
  - [ ] `receiveRetry()` / `sendRetry()` — EAGAIN/EINTR only 재시도
  - [ ] `waitForInput()` / `waitUntil()` — 폴링 헬퍼
  - [ ] `endpointFor()` — 트랜스포트별 엔드포인트 생성
  - [ ] `printResult()` — `RESULT,current,...` 형식, bandwidth 승수=1.0, 5개 RESULT 라인 (throughput/bandwidth/latency/p95/p99)
  - [ ] `gatewayReceiveProviderMessage()` — Gateway 수신 헬퍼
  - [ ] `spotReceivePayloadWithTimeout()` — Spot 수신 헬퍼
- [ ] `PerfSingleMetricHeader.java` 생성됨 (§8.3)
  - [ ] `HEADER_SIZE = 32`
  - [ ] `MAGIC = 0x53504631` (SPF1)
  - [ ] `PHASE_UNKNOWN=0, PHASE_WARMUP=1, PHASE_ACTIVE=2`
  - [ ] `stampPayload()` — little-endian 32바이트 stamp
  - [ ] `decodePayloadHeader()` — 32바이트 decode
  - [ ] `nowUs()` — μs 타임스탬프
- [ ] `PerfTls.java` 생성됨 (§9.3, §9.4)
  - [ ] `configureTlsServerIfNeeded()` — tls/wss 일 때 서버 인증서 설정
  - [ ] `configureTlsClientIfNeeded()` — tls/wss 일 때 CA 설정
  - [ ] `tryResolvePerfTlsPaths()` — `bindings/java/tests/certs/` 상위 순회 탐색

**1-3. Single 진입점 (§3.1)**
- [ ] `PerfMain.java` 생성됨
  - [ ] CLI: `<PATTERN> <TRANSPORT> <SIZE>` 3개 위치 인자
  - [ ] 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류
  - [ ] 8개 패턴 디스패치: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT

**1-4. Multi common/ 패키지 (§8.2)**
- [ ] `PerfCommon.java` (multi) 생성됨
  - [ ] single PerfCommon 유틸 + multi 전용 기능
- [ ] `PerfMultiMetricHeader.java` 생성됨 (§8.3)
  - [ ] `MAGIC = 0x4D504631` (MPF1)
  - [ ] `PHASE_DRAIN=3` 추가
  - [ ] `stampPayload()` / `decodePayloadHeader()` / `nowUs()`
- [ ] `PerfMultiCommon.java` 생성됨 (§5.2)
  - [ ] `resolveClients()` — PERF_CLIENTS, 비-STREAM:100, STREAM:10000
  - [ ] `resolveHwm()` — PERF_HWM, 비-STREAM:100, STREAM:10
  - [ ] `resolveWarmupSeconds()` — PERF_WARMUP_SECONDS, 기본 2
  - [ ] `resolveDurationSeconds()` — PERF_DURATION_SECONDS, 기본 5
  - [ ] `resolveSettleMs()` — PERF_SETTLE_MS, 기본 500
  - [ ] Settle 구간이 인플라이트 소진(drain 역할) 겸용 (별도 drain 페이즈/환경변수 없음)
  - [ ] 기타 §5.2 환경변수 리졸버 (SNDTIMEO, RCVTIMEO, CONNECT_READY_TIMEOUT 등)
- [ ] `PerfMultiClientHelpers.java` 생성됨
  - [ ] `isSupportedTransport()` — 트랜스포트 지원 판별
  - [ ] `parseEndpointArg()` — `--endpoint` 인자 파싱
  - [ ] `waitAllClientConnectReady()` — MonitorSocket N개 연결 대기
  - [ ] send/recv 루프는 포함하지 않음 — 각 패턴 파일에서 인라인 (§15.3)
- [ ] `PerfMultiServerEntry.java` 생성됨
  - [ ] 패턴 디스패치 → `runServer()`
  - [ ] CPU/MEM 메트릭 수집 (§11)
  - [ ] `RESULT` 출력: `server_cpu_pct`, `server_mem_mb`
- [ ] `PerfMultiClientEntry.java` 생성됨
  - [ ] 패턴 디스패치 → `runClient()`
  - [ ] CPU/MEM 메트릭 수집 (§11)
  - [ ] `RESULT` 출력: `client_cpu_pct`, `client_mem_mb`
- [ ] `PerfMultiTls.java` 생성됨 (§9.3)
  - [ ] `bindings/java/tests/certs/` 상위 순회 탐색

**1-5. Multi 진입점 (§3.2)**
- [ ] `PerfMultiMain.java` 생성됨
  - [ ] `--multi-server <PATTERN> <TRANSPORT> <SIZE>` 모드
  - [ ] `--multi-client <PATTERN> <TRANSPORT> <SIZE> --endpoint <endpoint>` 모드
  - [ ] STREAM 패턴은 서버 모드만 디스패치 (클라이언트 = C++ 공용 바이너리)

---

### Phase 2: Single 소켓 패턴 (6개)

> 각 파일: 소켓 생성 → bind/connect → Warmup(count) → Active(duration) → RESULT 출력 (§6.1, §7.1)

- [ ] `PerfPair.java` — PAIR×2, echo, tcp/tls/ws/wss/inproc/ipc
  - [ ] SPF1 헤더 stamp/decode 사용
  - [ ] Warmup: `PERF_WARMUP_COUNT` 횟수 기반
  - [ ] Active: `PERF_SINGLE_DURATION_SECONDS` duration 기반, throughput+latency 동시 측정
  - [ ] Reservoir sampling 으로 p95/p99 수집
  - [ ] `RESULT,current,PAIR,...` 5개 RESULT 라인 출력 (throughput/bandwidth/latency/p95/p99)
- [ ] `PerfPubSub.java` — PUB+SUB, one-way, tcp/tls/ws/wss/inproc/ipc
  - [ ] 동일 페이즈/메트릭 구조
- [ ] `PerfDealerDealer.java` — DEALER×2, echo, tcp/tls/ws/wss/inproc/ipc
  - [ ] 동일 페이즈/메트릭 구조
- [ ] `PerfDealerRouter.java` — DEALER+ROUTER, echo, tcp/tls/ws/wss/inproc/ipc
  - [ ] 동일 페이즈/메트릭 구조
- [ ] `PerfRouterRouter.java` — ROUTER×2, echo, tcp/tls/ws/wss/inproc/ipc
  - [ ] 동일 페이즈/메트릭 구조
- [ ] `PerfRouterRouterPoll.java` — ROUTER×2+Poller, echo, tcp/tls/ws/wss/inproc/ipc
  - [ ] `Poller.poll()` / `Poller.pollCount()` 사용
  - [ ] 동일 페이즈/메트릭 구조

---

### Phase 3: Single 서비스 패턴 (2개)

- [ ] `PerfGateway.java` — Gateway+Receiver, echo, tcp/tls/ws/wss (inproc/ipc 미지원)
  - [ ] `new Gateway(...)` / `new Receiver(...)` API 사용
  - [ ] `gatewayReceiveProviderMessage()` 헬퍼 사용
  - [ ] warmup 기본값 200 (`PERF_WARMUP_COUNT`)
  - [ ] 동일 페이즈/메트릭 구조
- [ ] `PerfSpot.java` — Spot (pub/sub), one-way, tcp/tls/ws/wss (inproc/ipc 미지원)
  - [ ] `new Spot(...)` API 사용
  - [ ] `spotReceivePayloadWithTimeout()` 헬퍼 사용
  - [ ] warmup 기본값 200, `msg_size ≥ 65536` 시 최대 20 clamp
  - [ ] 동일 페이즈/메트릭 구조

---

### Phase 4: Multi 패턴 — server/client 분리 (6×2 + 3 서버 only)

> 각 서버: `READY,<endpoint>` stdout → 4-phase (§6.2: Connect → Warmup → Settle → Active) → RESULT 출력
> 각 클라이언트: `--endpoint` 수신 → Connect → 4-phase → stop-token 전송
> STREAM 서버: stop-token `__zlink_perf_stop__` 수신 시 정상 종료 (§15.2)

**4-1. DEALER_DEALER (one-way, bandwidth 승수=1.0)**
- [ ] `PerfMultiDealerDealerServer.java` — DEALER bind, relay
  - [ ] MPF1 헤더 stamp/decode
  - [ ] 4-phase: Connect → Warmup → Settle → Active
  - [ ] `READY,<endpoint>` stdout 출력
  - [ ] `RESULT,current,DEALER_DEALER,...` throughput/bandwidth/latency 출력
- [ ] `PerfMultiDealerDealerClient.java` — DEALER connect, send one-way
  - [ ] send/recv 루프 자체 인라인 (§15.3)
  - [ ] MonitorSocket 연결 확인

**4-2. DEALER_ROUTER (echo, bandwidth 승수=2.0)**
- [ ] `PerfMultiDealerRouterServer.java` — ROUTER bind, echo
- [ ] `PerfMultiDealerRouterClient.java` — DEALER connect, send+recv

**4-3. ROUTER_ROUTER (echo, bandwidth 승수=2.0)**
- [ ] `PerfMultiRouterRouterServer.java` — ROUTER bind, echo
- [ ] `PerfMultiRouterRouterClient.java` — ROUTER connect, send+recv

**4-4. PUBSUB (one-way, bandwidth 승수=1.0)**
- [ ] `PerfMultiPubSubServer.java` — PUB bind, publish
- [ ] `PerfMultiPubSubClient.java` — SUB connect, recv

**4-5. GATEWAY (echo, bandwidth 승수=2.0)**
- [ ] `PerfMultiGatewayServer.java` — Receiver bind, echo
- [ ] `PerfMultiGatewayClient.java` — Gateway connect, send+recv

**4-6. SPOT (one-way, bandwidth 승수=1.0)**
- [ ] `PerfMultiSpotServer.java` — Spot publish
- [ ] `PerfMultiSpotClient.java` — Spot subscribe

**4-7. STREAM 3종 (서버 only, echo, bandwidth 승수=2.0)**
- [ ] `PerfMultiStreamServer.java` — STREAM bind, raw echo
  - [ ] stop-token `__zlink_perf_stop__` 처리
  - [ ] 클라이언트 = `core/perf/common/streamclient` C++ 공용 바이너리
- [ ] `PerfMultiStreamCallbackServer.java` — `attachStream` callback 모드
  - [ ] stop-token 처리
- [ ] `PerfMultiStreamLen32BeServer.java` — `attachStreamLen32be` 모드
  - [ ] stop-token 처리

---

### Phase 5: 스크립트 및 마무리

**5-1. 셸 스크립트 (§12)**
- [ ] `perf/run_benchmarks.sh` (루트 single 래퍼)
  - [ ] `bindings/perf/run_policy_bench.py --binding java --suite single` 호출
  - [ ] §12.1 옵션 인터페이스 준수
- [ ] `perf/run_benchmarks.ps1` (루트 single 래퍼, PowerShell)
- [ ] `perf/run_benchmarks_multi.sh` (루트 multi 래퍼)
  - [ ] `bindings/perf/run_policy_bench.py --binding java --suite multi` 호출
  - [ ] nofile preflight: `clients × 3 + 4096` 계산, `PERF_SKIP_NOFILE_CHECK` 지원
  - [ ] §12.2 추가 옵션 인터페이스 준수
- [ ] `perf/run_benchmarks_multi.ps1` (루트 multi 래퍼, PowerShell)
- [ ] `perf/single/run_benchmarks.sh` + `.ps1` (single 내부)
- [ ] `perf/multi/run_benchmarks.sh` + `.ps1` (multi 내부)

**5-2. 비교 스크립트 (§12)**
- [ ] `perf/run_comparison.py` (루트 오케스트레이터)
- [ ] `perf/single/run_comparison.py`
- [ ] `perf/multi/run_comparison.py`

**5-3. 문서**
- [ ] `perf/README.md` — 사용법, 빌드, 실행 예시

**5-4. 빌드 통합 검증 (§2.4)**
- [ ] `./gradlew :perf-single:classes :perf-multi:classes` 전체 빌드 성공
- [ ] `run_policy_bench.py` 에서 Java 바인딩 빌드 자동 호출 동작 확인

---

### Phase 6: 코드 품질 리뷰 및 리팩토링

**6-1. Dead Code / 미사용 파일 정리**
- [ ] 미사용 import 0건
- [ ] 미사용 변수/메서드/클래스 0건
- [ ] 주석 처리된 코드 / TODO 잔재 0건
- [ ] 빈 파일, 미사용 설정 파일 0건

**6-2. 가독성 리팩토링**
- [ ] 메서드/변수 네이밍 — core/perf 대응 관계 명확
- [ ] 매직 넘버 → 상수 추출 완료
- [ ] 패턴 파일 간 구조 일관성 (페이즈 순서, 변수명 컨벤션)

**6-3. 성능 리뷰 — 파일별 체크**

> 모든 패턴 파일 (single 8개 + multi server 9개 + multi client 6개 = 23개):

- [ ] 측정 루프 내 힙 할당 0건 (`new byte[]`, `new String()`, 박싱 등)
- [ ] 측정 루프 내 불필요한 복사 0건 (`Arrays.copyOf`, `System.arraycopy`)
- [ ] 측정 루프 내 `Thread.sleep` / 과도한 busy-wait 없음
- [ ] 측정 루프 밖에서 I/O 출력 (`System.out.println`)
- [ ] send/recv 버퍼 루프 밖 1회 할당 + 재사용
- [ ] 박싱/언박싱 없음 (primitive 직접 사용)

**6-4. 주석 정리**
- [ ] 각 패턴 파일 상단: 패턴 설명, 소켓 구성, 측정 방식 (1-3줄)
- [ ] 페이즈 전환 지점에 구분 주석
- [ ] 비자명 로직 (stop-token, routing-id, echo 루프)에 설명 주석
- [ ] 자명한 코드에 불필요한 주석 없음

**6-5. 진행 현황 (2026-03-05, multi 우선)**
- 2026-03-07 기준 `MULTI_GATEWAY`, `MULTI_SPOT`, `MULTI_STREAM_LEN32BE` blocker 는 Java 쪽 수정으로 해소했다.
  현재 미완료 항목은 `runs=3 all transport/size` 전수 검증과 DoD 문서 체크 정리다.
- [x] `multi/src` 패턴 파일(서버 9 + 클라이언트 6)에서 retry/phase/상수 구조를 정리하고 `try/catch` 복잡도를 축소
- [x] `core/perf`와 다르게 send/recv 공통화는 각 패턴 파일 `private` 메서드 내부로만 유지 (`common/` 공유 루프 미사용)
- [x] 매직 넘버 상수화 적용 (`32`, `256`, `1024`, retry backoff, ns 단위 상수 등)
- [x] 비자명 로직 주석 보강 (routing-id echo, STREAM control frame, stop-token)
- [x] `MULTI_PUBSUB` ws `server_shutdown_timeout` 수정 (`LINGER=0`, shutdown 경로 안정화)
- [x] `MULTI_GATEWAY` tcp `server_shutdown_timeout` 수정 (receiver linger + inactivity-based shutdown)
- [x] `MULTI_SPOT` high-HWM backlog 수정
  - `PERF_HWM=100000` runner 기본 환경에서 warmup backlog 때문에 `no_active_frames`가 나던 문제를,
    pre-active backlog drain 후 첫 active frame 시작 기준으로 수정
  - 재검증:
    `run_policy_bench.py --binding java --suite multi --pattern MULTI_SPOT --transports tcp --msg-sizes 64 --runs 1 --result`
- [x] `MULTI_STREAM_LEN32BE` callback ownership 수정
  - LEN32BE callback message vector 를 callback-owned 경로로 분리하고
    perf server 는 `streamSend(..., Message)` 로 consume 하도록 정렬
  - 재검증:
    `TestStreamSocketPortedTest` backlog echo,
    `run_policy_bench.py --binding java --suite multi --pattern MULTI_STREAM_LEN32BE --transports tcp --msg-sizes 64 --runs 1 --result`
- [x] 멀티 스모크 검증 통과 (tcp/64B): `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`, `MULTI_PUBSUB`, `MULTI_GATEWAY`, `MULTI_SPOT`, `MULTI_STREAM_CALLBACK`, `MULTI_STREAM_LEN32BE`
- [ ] 기본설정 `runs=3` 부분 검증 통과: `MULTI_PUBSUB`(all transport/size), `MULTI_GATEWAY`(all), `MULTI_SPOT`(all)
- [ ] `MULTI_STREAM_LEN32BE` all transport/size `runs=1` 분할 검증 통과
- [ ] 기본설정 `runs=3` 전체 완주 검증(`MULTI_STREAM_CALLBACK`, `MULTI_STREAM_LEN32BE`) 완료
  - `MULTI_STREAM_CALLBACK`: tcp/tls/ws/wss 각각 `--runs 3 --result` 완주
    (`perf_linux_20260305_203722.txt`, `204229.txt`, `204414.txt`, `204603.txt`)
  - `MULTI_STREAM_LEN32BE`: tcp/tls/ws/wss 각각 `--runs 3 --result` 완주
    (`perf_linux_20260305_204756.txt`, `204941.txt`, `205127.txt`, `205314.txt`)
- [x] `MULTI_SPOT` ws `runs=3`에서 64B 0-throughput 재현 이슈 수정
  - Spot peer-ready 대기 추가(`SpotNode.pubPeers()/subPeers()`)
  - 재검증 결과(`perf_linux_20260305_210534.txt`)에서 ws 전 사이즈 throughput>0 확인
- [x] `MULTI_DEALER_DEALER` 서버 종료 경로 리팩토링
  - stop-token 의존 제거, 서버 active-window 기반 자체 종료로 `no_data/hang` 해소
  - `runs=3`, all transport/size 완주 확인(`perf_linux_20260305_230432.txt`)
- [ ] 기본설정 `--suite multi` 전체 패턴(`runs=3`) 완주
  - 결과: `perf_linux_20260306_003438.txt` (`META,status,complete`, `expected=576`, `actual=576`)
- [ ] single 패턴 8개를 동일 기준으로 점검/리팩토링하고 6-1~6-4 전체 체크 완료

**6-6. 객체지향 리팩토링 계약 (Java, 2026-03-06 추가)**

> 목적: C-API 포팅 스타일 코드를 Java 스타일로 정리하되, 벤치마크 의미/성능 특성은 불변.

- 대상 경로(1차): `bindings/java/perf/multi/Zlink.PerfBench/src/main/java/dev/kairoscode/zlink/integration/bench/src/`
- 대상 패턴(1차): `MULTI_PUBSUB`, `MULTI_DEALER_DEALER`

**측정 로직 불변 규칙**
- [ ] warmup/settle/active phase 의미 유지
- [ ] throughput/latency/p95/p99 계산식 유지
- [ ] RESULT 출력 형식 유지
- [ ] 기존 exit code/실패 판정 유지 (실패 원인 출력은 강화)

**성능 규칙**
- [ ] 측정 루프 내 힙 할당 0
- [ ] 측정 루프 내 불필요한 복사 0
- [ ] 측정 루프 내 로그/문자열 생성 0
- [ ] 측정 루프 내 sleep/과도 busy-wait 추가 금지
- [ ] send/recv 버퍼 루프 밖 1회 할당 후 재사용

**구조 규칙**
- [ ] send/recv 공통화는 각 패턴 파일 내부 private helper까지만 허용
- [ ] 패턴 간 과도한 공통 유틸 추출 금지
- [ ] 핵심 send/recv 루프는 패턴 파일에서 명시적으로 보이도록 유지

**객체지향 정리 규칙**
- [ ] Config/Result/Phase 개념을 타입으로 명확화
- [ ] 긴 메서드를 phase 단위 메서드로 분리 (`runWarmup`/`runSettle`/`runActive`)
- [ ] 리소스 생명주기(소켓/컨텍스트) 코드 경계 명확화
- [ ] 루프 내부 try/catch 난립 금지, 상위 레벨 일관 처리
- [ ] 매직 넘버 상수화

**금지 사항**
- [ ] client cap/retry budget/fallback 으로 실패를 숨기지 않는다
- [ ] 실패를 성공처럼 보이게 만드는 우회 로직 금지
- [ ] 문서/DoD와 충돌하는 동작 변경 금지

**I/O 실행 정책 (callback-only recv 모델)**
- [ ] `single send`: blocking 단발 호출만 허용 (`SendFlag.NONE`/`SNDMORE`)
- [ ] `multi send`: recv callback 내 `DONTWAIT` send + EAGAIN 시 역할별 backpressure:
  - echo 서버 (소켓 1개 × 클라이언트 N개): per-socket pending 큐 + `setSendReadyHandler()` drain
  - echo 클라이언트 (per-socket, inflight 1): `boolean sendPending` 플래그 + `setSendReadyHandler()` 재전송
  - one-way sender: `boolean sendPending` 플래그 + `setSendReadyHandler()` 재전송
  - one-way receiver: send 없음, backpressure 불필요
- [ ] `multi backpressure`: `setSendReadyHandler()` 기반, `PollOut` 미사용. EAGAIN 은 perf 환경(HWM 100, inflight 1/peer)에서 사실상 미발생
- [ ] `send` 실패(`EAGAIN` 제외) 시 즉시 실패 처리하고 원인(errno/message)을 그대로 노출한다
- [ ] `single recv`: blocking recv 1회 후 `DONTWAIT` nonblocking drain
- [ ] `multi recv`: callback-only recv (poller 미사용). 소켓 생성 시 recv handler 등록, I/O thread 에서 콜백 호출
- [ ] one-way recv active 측정은 pre-active backlog(warmup/settle 잔여분)를 drain 한 뒤 첫 active frame 도착 시점부터 시작한다
- [ ] active-frame 탐색은 고정 cap 으로 끊지 않고, backlog 가 계속 drain 되는 동안에는 계속 진행한다
- [ ] `PERF_MULTI_RECV_BATCH`류 제어 변수/우회 옵션을 도입하지 않는다
- [ ] `core/perf`와 달리 send/recv 공통화는 패턴 간 공유 유틸로 추출하지 않고, 각 패턴 파일 내부 `private helper`까지만 허용한다
- [ ] 핵심 recv callback/send-ready handler 는 각 패턴 파일에서 샘플 코드처럼 명시적으로 읽히도록 유지한다
- [ ] STREAM 계열 검증은 큰 backlog/HWM 조건에서도 callback ownership/echo 경로가 유지되는지 확인한다

**6-6 진행 현황 (1차, 2026-03-06)**
- [x] 대상 패턴 1차 적용: `MULTI_PUBSUB`, `MULTI_DEALER_DEALER`
- [x] `Config/Phase/Result` 타입 도입 + phase 메서드 분리
- [x] `MULTI_PUBSUB` client cap/fallback 제거 (`PERF_PUBSUB_*_CLIENTS` 미사용)
- [x] 실패 원인 stderr 노출 강화 (`ERROR,<pattern>,<role>,<reason>`)
- [x] `:perf-multi:classes` 빌드 통과
- [x] 스모크 통과: `MULTI_DEALER_DEALER tcp/64`, `MULTI_PUBSUB tls/262144`
- [x] `Poller` 네이티브 레이아웃 보정: `zlink_pollitem_t` struct 크기/offset 하드코딩 제거(동적 layout 계산)로 `Poller.poll` SIGSEGV 제거
- [x] `MULTI_GATEWAY` 토폴로지 보정: server=`Receiver(bind)+Gateway(sendTo)`, client=`Gateway(send)+Receiver(router recv)`로 정렬
- [x] `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER` 서버 multipart 수신/드레인 보강(2프레임 경계 확인 + trailing frame drain)
- [x] 멀티 핵심 패턴 `tcp/64` 스모크 통과:
  `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`,
  `MULTI_PUBSUB`, `MULTI_GATEWAY`, `MULTI_SPOT`
- [x] `MULTI_SPOT` poller 기준 정렬:
  - `Spot` service instance + `Poller.addSpotPub()/addSpotSub()` 사용
  - public 방향을 socket handover 가 아니라 service instance poller 로 고정
  - `core/v4.0.0` / runtime sync `da1d308a` 기준으로 문서 정렬

**v4.0.0 검증 상태**
- [x] core targeted tests 통과
- [x] C++ bindings 70개 테스트 통과
- [x] .NET 99개 테스트 통과
- [x] Java `test` / `integrationTest` 통과
- [x] Node 24개 테스트 통과
- [x] Python 37개 테스트 통과
- [ ] 나머지 패턴(멀티 전체/싱글 전체)에 동일 규칙 확장 적용

---

### 정책 준수 검증 (§15, §16.1)

> Phase 2~4 완료 후 수행.

**정적 검증**
- [x] C API / internal 패키지 직접 호출 0건
  ```bash
  rg -n "\\bNative\\." bindings/java/perf --glob '*.java'           # 0건
  rg -n "\\bNativeMsg\\." bindings/java/perf --glob '*.java'        # 0건
  rg -n "\\bdev\\.kairoscode\\.zlink\\.internal\\b" bindings/java/perf --glob '*.java'  # 0건
  ```
- [x] Java 측 STREAM client 구현 없음
  ```bash
  rg -n "StreamClient" bindings/java/perf --glob '*.java'           # 0건
  ```
- [x] TLS 인증서 경로 — `bindings/java/tests/certs/` 만 참조
  ```bash
  rg -n "tests/certs" bindings/java/perf --glob '*.java'            # java/tests/certs 만
  ```
- [x] Retry 로직 — EAGAIN/EINTR 외 재시도 없음
- [x] Inflight/Outstanding 옵션 사용 없음 — HWM 만 사용

---

### Smoke 테스트 (§16.3)

> Phase 4 완료 후 수행.

- [x] **single tcp smoke** 통과
  ```bash
  PERF_SINGLE_DURATION_SECONDS=1 python3 bindings/perf/run_policy_bench.py \
    --binding java --suite single --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --reuse-build
  ```
- [x] **multi tcp smoke** 통과
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite multi --pattern MULTI_DEALER_DEALER --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 10 --reuse-build
  ```
- [x] **stream tcp smoke** 통과
  ```bash
  python3 bindings/perf/run_policy_bench.py \
    --binding java --suite multi --pattern MULTI_STREAM_CALLBACK --transports tcp --msg-sizes 64 \
    --runs 1 --multi-duration-seconds 1 --multi-clients 100 --reuse-build
  ```
- [x] **single tls smoke** 통과
  ```bash
  PERF_SINGLE_DURATION_SECONDS=1 python3 bindings/perf/run_policy_bench.py \
    --binding java --suite single --pattern PAIR --transports tls --msg-sizes 64 --runs 1 --reuse-build
  ```

---

### 메트릭 정확성 검증 (§16.4)

- [x] 모든 RESULT 라인에 5개 메트릭 존재: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`
  - 확인: `perf_linux_20260306_003438.txt` 기준 192 조합, 누락 0건
- [x] Multi RESULT 라인에 추가 메트릭 존재: `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb`
- [ ] Bandwidth 계산식 정합 (허용 오차 ±1%)
  - [ ] single: `bandwidth ≈ throughput × size / 1,000,000`
  - [ ] multi echo: `bandwidth ≈ throughput × size × 2 / 1,000,000`
  - [ ] multi one-way: `bandwidth ≈ throughput × size / 1,000,000`
- [x] Percentile 일관성: `latency_p95 >= latency`, `latency_p99 >= latency_p95`
- [ ] 값 범위: `throughput > 0`, `bandwidth > 0`, `latency > 0`, `cpu_pct >= 0`, `mem_mb > 0`
  - 현황: `MULTI_PUBSUB` 일부 조합(주로 64KiB+)에서 0값 관측 (`perf_linux_20260306_003438.txt`)

---

### 사이즈별 순차 실행 검증 (§16.5)

- [ ] 3개 사이즈 (64, 1024, 65536) 순차 실행 시 각 사이즈의 5개 RESULT 라인 존재
- [ ] 콘솔에 `Testing tcp | 64B:`, `1024B:`, `65536B:` 순서대로 출력
- [ ] report 파일에 모든 사이즈 테이블 행 저장됨

---

### 기본 설정 전체 실행 무실패 검증 (§16.6)

**Single 전체 실행**
- [x] `python3 bindings/perf/run_policy_bench.py --binding java --suite single --result` 종료코드 0
- [x] 콘솔: `status: complete`
- [x] 콘솔: `warning: status=partial` 없음
- [x] `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합 — inproc/ipc 에서 GATEWAY/SPOT 만 허용
- [x] report 파일 생성됨 (테이블만)

**Multi 전체 실행**
- [x] `python3 bindings/perf/run_policy_bench.py --binding java --suite multi --result` 종료코드 0
- [x] 콘솔: `status: complete`
- [x] 콘솔: `warning: status=partial` 없음
- [x] `fail` 조합 0건
- [x] report 파일 생성됨

---

### 최종 완료 기준 (§17)

- [x] 디렉토리/파일 구조가 core/perf `common/` + `src/` 분리와 동일
- [x] Multi server/client 가 별도 파일로 분리
- [x] runner 옵션/기본값/결과 형식이 `run_policy_bench.py` 정책과 동등
- [x] single/multi 모든 패턴 클래스가 빌드됨
- [x] STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용
- [x] perf 소스 내 C API / internal 패키지 직접 호출 0건
- [x] TLS 인증서 `bindings/java/tests/certs/` 독립 관리
- [x] retry 로직 / 우회 wrapper / 비정책 실행 경로 없음
- [x] send/recv 공통화는 패턴 파일 내부(private helper)만 사용, `common/` 공유 루프 없음
- [x] 메트릭 헤더: single=SPF1, multi=MPF1
- [ ] 메트릭 정확성 검증 통과
- [x] 기본 설정 전체 실행 `status: complete`
- [x] `run_policy_bench.py` 수정 반영 완료
- [ ] 코드 품질 리뷰 완료
- [ ] 주석 정리 완료
