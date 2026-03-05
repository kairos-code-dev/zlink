# Java Perf Benchmark Implementation Plan

> core/perf (C++) 벤치마크를 bindings/java/perf 로 1:1 포팅한다.
> **Java binding API (`dev.kairoscode.zlink.*`) 만 사용하며, zlink C API / wrapper 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `current/` 분리 구조를 Java 패키지 컨벤션으로 그대로 반영한다.

```
bindings/java/
├── tests/
│   └── certs/                              ← TLS 인증서 (바인딩 독립 관리)
│       ├── server.crt
│       ├── server.key
│       └── ca.crt
│
└── perf/
    ├── IMPLEMENTATION_PLAN.md              ← 본 문서
    ├── README.md                           ← 사용법 안내
    ├── .gitignore                          ← build/, tmp/ 제외
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
    │   │       └── current/                ← ★ core/perf/single/current/ 대응
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
    │   │       │   ├── PerfMultiClientHelpers.java ← 공통 client 루프 헬퍼
    │   │       │   ├── PerfMultiMetricHeader.java ← MPF1 페이로드 헤더 stamp/decode
    │   │       │   ├── PerfMultiTls.java          ← TLS 인증서 경로 리졸버
    │   │       │   ├── PerfMultiStreamClient.java ← Raw transport stream client
    │   │       │   └── PerfMultiStreamStopParser.java ← len32be stop-token 파서
    │   │       └── current/                ← ★ core/perf/multi/current/ 대응
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
    │   │   ├── report/.gitkeep
    │   │   └── tmp/.gitkeep
    │   └── multi/
    │       ├── report/.gitkeep
    │       └── tmp/.gitkeep
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
| `single/current/perf_pair.cpp` | `single/.../current/PerfPair.java` | 1:1 매핑 |
| `single/current/perf_dealer_dealer.cpp` | `single/.../current/PerfDealerDealer.java` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/.../common/PerfCommon.java` | 패키지 분리 |
| `multi/common/perf_common_multi.hpp` | `multi/.../common/PerfMultiCommon.java` | |
| `multi/common/perf_multi_metric_header.hpp` | `multi/.../common/PerfMultiMetricHeader.java` | MPF1 (0x4D504631) 페이로드 헤더 stamp/decode |
| `multi/common/perf_multi_client_helpers.hpp` | `multi/.../common/PerfMultiClientHelpers.java` | |
| `multi/common/perf_multi_entry.hpp` | `multi/.../common/PerfMultiServerEntry.java` + `PerfMultiClientEntry.java` | |
| `multi/current/perf_multi_dealer_dealer_server.cpp` | `multi/.../current/PerfMultiDealerDealerServer.java` | ★ server/client 분리 유지 |
| `multi/current/perf_multi_dealer_dealer_client.cpp` | `multi/.../current/PerfMultiDealerDealerClient.java` | |
| `multi/current/perf_multi_stream_server.cpp` | `multi/.../current/PerfMultiStreamServer.java` | 서버 only |

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

- **PATTERN**: `PAIR | PUBSUB | DEALER_DEALER | DEALER_ROUTER | ROUTER_ROUTER | ROUTER_ROUTER_POLL | GATEWAY | SPOT`
- **TRANSPORT**: `tcp | tls | ws | wss | inproc | ipc`
- **SIZE**: 양의 정수 (바이트)
- 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류

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

### 3.4 run_policy_bench.py 필수 수정 사항

현재 `run_policy_bench.py` 에 Java 관련 수정이 필요한 항목:

| 위치 | 현재 | 수정 필요 |
|------|------|----------|
| `binding_cmd_prefix()` (L873) | `[java, "-cp", cp, ...]` cp=`java/build/classes/java/main:test:resources` | `[java, "--enable-native-access=ALL-UNNAMED", "-cp", cp, ...]` cp=`java/perf/single/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main` |
| `binding_multi_role_command()` (L1049,1061) | 동일 cp 경로 + `--enable-native-access` 미포함 | `--enable-native-access=ALL-UNNAMED` 추가 + cp=`java/perf/multi/Zlink.PerfBench/build/classes/java/main:java/build/classes/java/main:java/build/resources/main` |
| `build_binding_if_needed()` (L673-687) | `"java" / "build" / "classes" / "java" / "test" / ...` | `"java" / "perf" / suite_dir / "Zlink.PerfBench" / "build" / "classes" / "java" / "main" / ...` |

---

## 4. RESULT 출력 형식 (core/perf 동일)

```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,throughput,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,bandwidth,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency,<value>
```

> **참고**: `run_policy_bench.py` 파서는 `throughput`, `bandwidth`, `latency` 3개 메트릭만 수집하며,
> 완료 판정도 조합당 3개 메트릭 기준이다 (`expected = (total - unsupported - skipped) * 3`).
> `latency_p95`, `latency_p99` 는 벤치마크가 stdout 에 출력하되, 런너가 수집·검증하지 않는 참고 메트릭이다.
> 런너 파서 수정 없이 p95/p99 를 수집하려면 파서의 허용 메트릭 목록과 완료 기준을 함께 변경해야 한다.

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
| Multi echo (DEALER_ROUTER, ROUTER_ROUTER, STREAM*) | 2.0 | 요청+응답 양방향 |
| Multi one-way (DEALER_DEALER, PUBSUB, GATEWAY, SPOT) | 1.0 | 단방향 |

---

## 5. 환경 변수 (core/perf 동일)

### 5.1 Single

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_IO_THREADS` | 0 (기본) | Context IO 스레드 |
| `PERF_WARMUP_COUNT` | 1000 | 웜업 메시지 횟수 (count 기반, 시간 기반 아님) |
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
| `PERF_MULTI_CLIENTS` | **100** (STREAM: 10000) | 동시 클라이언트 수 |
| `PERF_MULTI_WARMUP_SECONDS` | **2** | 웜업 기간 |
| `PERF_MULTI_SETTLE_MS` | 500 | 측정 전 안정화 |
| `PERF_MULTI_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_MULTI_DRAIN_MS` | 패턴별 (echo: 300, one-way: 0) | 드레인 |
| `PERF_MULTI_ACTIVE_WARMUP` | 0 | 0=sleep, 1=active |
| `PERF_MULTI_HWM` | **100** (STREAM: 10) | 소켓 HWM |
| `PERF_MULTI_SNDHWM` | 0 (HWM fallback) | 송신 HWM |
| `PERF_MULTI_RCVHWM` | 0 (HWM fallback) | 수신 HWM |
| `PERF_MULTI_SNDTIMEO_MS` | **200** | 송신 타임아웃 |
| `PERF_MULTI_RCVTIMEO_MS` | **200** | 수신 타임아웃 |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | 5000 | 연결 대기 |
| `PERF_MULTI_MONITOR_HWM` | **1000** | 모니터 소켓 HWM |
| `PERF_MULTI_SERVER_BIND_PORT` | 0 (자동) | 서버 포트 고정 |
| `PERF_IO_THREADS` | 0 | IO 스레드 |
| `PERF_MULTI_SERVER_IO_THREADS` | 0 | 서버 전용 IO 스레드 |
| `PERF_MULTI_CLIENT_IO_THREADS` | 0 | 클라이언트 전용 IO 스레드 |
| `PERF_MULTI_CLIENT_POLL_TIMEOUT_MS` | 0 | 클라이언트 poll 타임아웃 |
| `PERF_CTX_BLOCKY` | 미설정 | Context blocky 모드 (설정 시 적용) |
| `PERF_CTX_TERM` | 미설정 | Context termination 모드 (1=full term) |

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
> 수신 측에서 reservoir sampling (`latency_stats_builder_t`) 으로 p95/p99 를 수집한다.

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle] → [Active(duration)] → [Drain]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_MULTI_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_MULTI_WARMUP_SECONDS`, 2초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_MULTI_SETTLE_MS`, 500ms): 안정화 sleep (one-way: phase_drain 라벨, echo: phase_warmup 라벨)
4. **Active** (`PERF_MULTI_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)
5. **Drain** (`PERF_MULTI_DRAIN_MS`, echo: 300ms, one-way: 0ms): 인플라이트 메시지 대기

---

## 7. 패턴별 상세 구현 계획

### 7.1 Single 패턴

> **참고**: run_policy_bench.py 기준 모든 single 패턴은 one-way 방향(`bandwidth 승수 = 1.0`).
> "소켓 동작" 열은 실제 send/recv 패턴(echo=양방향, one-way=단방향)을 나타낸다.

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

> **참고**: STREAM 3종(STREAM, STREAM_CALLBACK, STREAM_LEN32BE)은 Java 에서 multi suite 서버만 구현한다.
> 단, `run_policy_bench.py` 의 `SINGLE_PATTERNS` 에 STREAM 3종이 포함되어 있으며,
> Java 는 `supports_split_multi()=true` 이므로 런너가 single STREAM 을 multi server 경로로 위임 실행한다.
> 따라서 single 기본 패턴은 11개(PAIR~SPOT + STREAM 3종)이며, STREAM 서버 구현 완료 시 모두 성공해야 한다.

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 별도 파일**로 분리한다.

| # | 서버 파일 | 클라이언트 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|----------|---------------|------|-----------|----------------|
| 1 | PerfMultiDealerDealerServer | PerfMultiDealerDealerClient | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | PerfMultiDealerRouterServer | PerfMultiDealerRouterClient | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | PerfMultiRouterRouterServer | PerfMultiRouterRouterClient | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | PerfMultiPubSubServer | PerfMultiPubSubClient | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | PerfMultiGatewayServer | PerfMultiGatewayClient | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | PerfMultiSpotServer | PerfMultiSpotClient | SPOT | Spot publish | Spot subscribe |
| 7 | PerfMultiStreamServer | (공통 stream client) | STREAM | STREAM bind, raw echo | C++ stream client |
| 8 | PerfMultiStreamCallbackServer | (공통 stream client) | STREAM_CALLBACK | attachStream callback | C++ stream client |
| 9 | PerfMultiStreamLen32BeServer | (공통 stream client) | STREAM_LEN32BE | attachStreamLen32be | C++ stream client |

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
    // throughput, bandwidth, latency — 3개 필수 메트릭 출력 (런너 파서 수집 대상)
    // latency_p95, latency_p99 — 참고용 stdout 출력 (런너 파서 미수집)
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
    static int resolveMultiClients(String pattern);   // 비-STREAM: 100, STREAM: 10000
    static int resolveMultiHwm(String pattern);       // 비-STREAM: 100, STREAM: 10
    static int resolveMultiWarmupSeconds();
    static int resolveMultiDurationSeconds();
    static int resolveMultiSettleMs();
    static int resolveMultiDrainMs(String pattern);   // echo: 300, one-way: 0
    static int resolveMultiWarmupDrainMs(String pattern);
    // ... 기타 환경변수 리졸버
}
```

**`PerfMultiClientHelpers.java`** — (core/perf multi/common/perf_multi_client_helpers.hpp 대응)

```java
public final class PerfMultiClientHelpers {
    static boolean isSupportedTransport(String transport);
    static String parseEndpointArg(String[] args);
    static void waitAllClientConnectReady(List<MonitorSocket> monitors, int timeoutMs);
    static void runMultiEchoClientBenchmark(...);    // 공통 echo 클라이언트 루프
    static void runMultiOnewayClientBenchmark(...);  // 공통 one-way 클라이언트 루프
}
```

**`PerfMultiServerEntry.java`** — (core/perf multi/common/perf_multi_entry.hpp 서버 부분 대응)

```java
// 1. 패턴 디스패치 → runServer(transport, size)
// 2. CPU/MEM 메트릭 수집
// 3. RESULT 출력: server_cpu_pct, server_mem_mb
```

**`PerfMultiClientEntry.java`** — (core/perf multi/common/perf_multi_entry.hpp 클라이언트 부분 대응)

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

**헤더 구조** (core `perf_single_metric_header.hpp` / `perf_multi_metric_header.hpp` 동일):

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

core/tests/certs/gen/ 의 인증서를 복사하거나, 동일 OpenSSL 명령으로 재생성:

```bash
# core/tests/certs/gen/ 에서 복사
cp core/tests/certs/gen/server.crt bindings/java/tests/certs/
cp core/tests/certs/gen/server.key bindings/java/tests/certs/
cp core/tests/certs/gen/ca.crt     bindings/java/tests/certs/
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

| Core C++ API | Java API |
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
| Gateway / Receiver / Spot | `new Gateway(...)` / `new Receiver(...)` / `new Spot(...)` |
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

> **주의**: report 파일에는 RESULT 라인이나 Completion 섹션이 포함되지 않는다.
> 완료 상태 확인 및 메트릭 추출은 tmp 파일을 사용해야 한다.

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
13. `multi/.../common/PerfMultiClientHelpers.java` — 공통 client 루프 헬퍼
14. `multi/.../common/PerfMultiServerEntry.java` — 서버 디스패치 + 메트릭
15. `multi/.../common/PerfMultiClientEntry.java` — 클라이언트 디스패치 + 메트릭
16. `multi/.../common/PerfMultiTls.java` — TLS 인증서 리졸버
17. `multi/.../PerfMultiMain.java` — --multi-server / --multi-client 디스패치

### Phase 2: Single 소켓 패턴 (6개)

18. `single/.../current/PerfPair.java`
19. `single/.../current/PerfPubSub.java`
20. `single/.../current/PerfDealerDealer.java`
21. `single/.../current/PerfDealerRouter.java`
22. `single/.../current/PerfRouterRouter.java`
23. `single/.../current/PerfRouterRouterPoll.java`

### Phase 3: Single 서비스 패턴 (2개)

24. `single/.../current/PerfGateway.java`
25. `single/.../current/PerfSpot.java`

### Phase 4: Multi 패턴 — server/client 분리 (6×2 + 3 서버 only)

26. `multi/.../current/PerfMultiDealerDealerServer.java` + `PerfMultiDealerDealerClient.java`
27. `multi/.../current/PerfMultiDealerRouterServer.java` + `PerfMultiDealerRouterClient.java`
28. `multi/.../current/PerfMultiRouterRouterServer.java` + `PerfMultiRouterRouterClient.java`
29. `multi/.../current/PerfMultiPubSubServer.java` + `PerfMultiPubSubClient.java`
30. `multi/.../current/PerfMultiGatewayServer.java` + `PerfMultiGatewayClient.java`
31. `multi/.../current/PerfMultiSpotServer.java` + `PerfMultiSpotClient.java`
32. `multi/.../current/PerfMultiStreamServer.java` (서버 only)
33. `multi/.../current/PerfMultiStreamCallbackServer.java` (서버 only)
34. `multi/.../current/PerfMultiStreamLen32BeServer.java` (서버 only)
35. `multi/.../common/PerfMultiStreamClient.java` (Raw transport)
36. `multi/.../common/PerfMultiStreamStopParser.java`

### Phase 5: 스크립트 및 마무리

37. `run_benchmarks.sh` / `.ps1` (루트 + single/ + multi/)
38. `run_benchmarks_multi.sh` / `.ps1`
39. `run_comparison.py`
40. `README.md`
41. 빌드 검증 (`./gradlew :perf-single:classes :perf-multi:classes`)
42. `run_policy_bench.py` 통합 검증

### Phase 6: 코드 품질 리뷰 및 리팩토링

> 모든 패턴 구현과 스크립트 완성 후, 코드 전체에 대한 품질 리뷰와 개선을 수행한다.
> 성능 벤치마크 코드이므로 불필요한 오버헤드에 특히 엄격히 대응한다.

43. **Dead Code / 미사용 파일 정리**
    - 사용되지 않는 import, 변수, 메서드, 클래스 전부 삭제
    - 의미 없는 주석 (TODO 잔재, 복사 흔적, 주석 처리된 코드) 전부 삭제
    - 빈 파일, 미사용 설정 파일 삭제
44. **가독성 리팩토링**
    - 메서드/변수 네이밍 일관성 검토 (core/perf 와 대응 관계 명확화)
    - 과도한 중첩 / 긴 메서드 분리 (단, 벤치마크 인라인 정책 범위 내)
    - 매직 넘버 → 상수 추출 (타임아웃, 버퍼 크기, 재시도 한도 등)
    - 패턴 파일 간 구조 일관성 확보 (동일 페이즈 순서, 동일 변수명 컨벤션)
45. **성능 리뷰 (벤치마크 오버헤드 제거)**
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
46. **개선 사항 적용 후 주석 추가**
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

### 15.3 코드 인라이닝 정책

- 각 패턴 파일에 메인 루프 로직 인라인 (core/perf 동일)
- `common/PerfCommon` 으로 추출 허용: 환경변수 파싱, retry, printResult, 엔드포인트 생성
- Multi 클라이언트는 `common/PerfMultiClientHelpers` 의 공통 루프 위임 허용
- STREAM 서버 인프라는 모듈화 허용 (`common/PerfMultiStreamClient`, `common/PerfMultiStreamStopParser`)

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
- stream client 공유 경로 확인:
  ```bash
  rg -n "core/perf/common/streamclient" bindings/java/perf/run_comparison.py
  # 기대 결과: 공용 경로만 참조
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
    --pattern MULTI_STREAM --transports tcp --msg-sizes 64 \
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

**필수 메트릭 존재 검증 (조합별):**
- single: `throughput`, `bandwidth`, `latency` — 3개 메트릭이 모든 pattern/transport/size 조합에 존재 (런너 파서 수집 대상)
- multi: `throughput`, `bandwidth`, `latency` + `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb`
- 참고 메트릭: `latency_p95`, `latency_p99` 는 벤치마크 stdout 에 출력되지만 런너 파서가 수집하지 않으므로 완료 판정에 영향 없음

**대역폭 계산식 검증:**
```
bandwidth_mbps = throughput × size × multiplier / 1,000,000
```
- single 전체 (one-way 방향): `bandwidth ≈ throughput × size / 1,000,000`
- multi echo 패턴 (DEALER_ROUTER, ROUTER_ROUTER, STREAM*): `bandwidth ≈ throughput × size × 2 / 1,000,000`
- multi one-way 패턴 (DEALER_DEALER, PUBSUB, GATEWAY, SPOT): `bandwidth ≈ throughput × size / 1,000,000`
- 허용 오차: ±1%

**Percentile 일관성 검증:**
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
- RESULT 라인은 tmp 파일에 저장되며, report 파일에는 포함되지 않는다.

**검증 대상: 벤치마크 자체의 RESULT 출력 정확성**
- 벤치마크 프로세스가 stdout 에 `RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,<metric>,<value>` 형식의 라인을 올바르게 출력하는지 확인한다.
- 각 조합의 3개 필수 메트릭 (throughput, bandwidth, latency) 이 모두 출력되는지 확인한다.

**검증 방법:**
```bash
# 3개 사이즈로 실행하고 콘솔 로그를 파일로 수집
PERF_SINGLE_DURATION_SECONDS=1 \
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --reuse-build \
  --output bindings/java/perf/results/single/tmp/size_progress.log
```

**로그 검증 기준:**
1. 콘솔에 `Testing tcp | 64B:`, `Testing tcp | 1024B:`, `Testing tcp | 65536B:` 가 순서대로 출력됨
2. 최종 테이블에 3개 사이즈 행이 모두 포함됨
3. tmp 파일에 각 사이즈의 3개 RESULT 라인이 존재함

**자동 검증 스크립트 (선택):**
```python
# tmp 파일에서 RESULT 라인의 size 값 순서 및 메트릭 완전성 검증
import glob, re
results = {}
for path in sorted(glob.glob("bindings/java/perf/results/single/tmp/perf_*.txt")):
    with open(path) as f:
        for line in f:
            m = re.match(r"RESULT,current,PAIR,tcp,(\d+),(\w+),", line)
            if m:
                size, metric = int(m.group(1)), m.group(2)
                results.setdefault(size, set()).add(metric)
for size in [64, 1024, 65536]:
    assert size in results, f"missing size: {size}"
    assert results[size] >= {"throughput", "bandwidth", "latency"}, f"missing metrics for {size}"
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
- [ ] tmp 파일의 `META,status,complete` 라인 확인 (런너는 `compute_completion_status()` 결과를 tmp 파일 메타에 저장)
- [ ] tmp 파일의 `META,expected,N` 과 `META,actual,N` 이 동일 (조합당 3개 메트릭 × success 조합 수)
- [ ] 콘솔에 `warning: status=partial` 경고가 없어야 함
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)
- [ ] `--result` 사용 시 report 파일 생성 확인 (테이블만 포함, META/RESULT 없음)

**기본 조합 수 예상 (single):**

> `run_policy_bench.py` 의 `SINGLE_PATTERNS` 에는 STREAM 3종이 포함되어 있다 (11종).
> Java 는 `supports_split_multi()` 가 `true` 를 반환하므로, 런너가 single STREAM 패턴을
> multi server 경로로 위임 실행한다 (`SINGLE_TO_MULTI_STREAM_PATTERN` 매핑 사용).
> 즉, multi stream 서버 구현이 완료되면 single STREAM 48 조합도 성공 대상이 된다.

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
# tmp 파일 확인 (META + RESULT + TABLE 포함, 항상 저장됨)
ls -la bindings/java/perf/results/single/tmp/perf_*.txt
ls -la bindings/java/perf/results/multi/tmp/perf_*.txt

# report 파일 확인 (--result + status=complete 시에만 저장, 테이블만 포함)
ls -la bindings/java/perf/results/single/report/perf_*.txt
ls -la bindings/java/perf/results/multi/report/perf_*.txt

# tmp 파일에서 META 및 RESULT 라인 확인
grep "^META," bindings/java/perf/results/single/tmp/perf_*.txt
grep "^RESULT," bindings/java/perf/results/single/tmp/perf_*.txt | head -10
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `current/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 별도 파일로 분리.
- runner 옵션/기본값/결과 형식이 `run_policy_bench.py` 정책 기준으로 동등하게 동작 (3개 필수 메트릭 수집, report 는 table-only, tmp 는 META CSV + RESULT CSV + TABLE).
- single/multi 모든 패턴 클래스가 `bindings/java/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 C API / internal 패키지 직접 호출 0건.
- TLS 인증서는 `bindings/java/tests/certs/` 에서 독립 관리.
- retry 로직/우회 wrapper/비정책 실행 경로가 없음.
- **메트릭 헤더**: single 은 SPF1 (0x53504631), multi 는 MPF1 (0x4D504631) 페이로드 헤더를 stamp/decode 하여 phase 필터링 및 latency 측정에 사용.
- **메트릭 정확성**(필수 3개 메트릭 존재/bandwidth 계산식/값 범위)이 검증됨.
- **벤치마크 RESULT 출력 정확성**: 각 조합의 3개 필수 메트릭이 stdout 에 올바르게 출력되고 tmp 파일에 저장됨이 검증됨.
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- `run_policy_bench.py` 수정 반영: `--enable-native-access=ALL-UNNAMED`, artifact 경로 갱신.
- **코드 품질 리뷰 완료**: dead code/미사용 주석 0건, 측정 루프 내 불필요한 할당/복사/대기 0건.
- **주석 정리 완료**: 패턴 설명, 페이즈 전환, 비자명 로직에 적절한 수준의 주석 추가.
