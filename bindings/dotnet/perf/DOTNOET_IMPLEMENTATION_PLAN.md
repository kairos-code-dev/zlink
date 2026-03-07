# DotNet Perf Benchmark Implementation Plan

> core/perf 벤치마크를 bindings/dotnet/perf 로 1:1 포팅한다.
> (core/perf 는 C++ 소스이나 내부적으로 C API — `zlink_ctx_new()`, `ZLINK_PAIR` 등 — 를 사용한다.)
> **C# binding API (`Zlink.*`, `Zlink.Service.*`) 만 사용하며, Native P/Invoke 직접 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.
> **실행 엔진**: 모든 바인딩(C++/Java/dotnet)은 `bindings/perf/run_policy_bench.py` 를 단일 실행 엔진으로 사용한다.
> 바인딩별 `run_comparison.py` 는 호환 어댑터(CLI 변환 후 policy runner 위임)이며, 독립 실행 엔진이 아니다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `src/` 분리 구조를 C# 프로젝트 컨벤션으로 그대로 반영한다.

```
bindings/dotnet/
├── tests/
│   └── certs/                              ← TLS 인증서 (바인딩 독립 관리)
│       ├── server.crt
│       ├── server.key
│       └── ca.crt
│
└── perf/
    ├── DOTNOET_IMPLEMENTATION_PLAN.md      ← 본 문서
    ├── README.md                           ← 사용법 안내
    ├── .gitignore                          ← bin/, obj/ 제외
    │
    ├── single/
    │   ├── Zlink.BindingBench/
    │   │   ├── Zlink.BindingBench.csproj   ← .NET 8.0 콘솔 프로젝트
    │   │   ├── GlobalUsings.cs             ← 공통 using 선언
    │   │   ├── PerfMain.cs                 ← 진입점 (pattern, transport, size)
    │   │   ├── common/                     ← ★ core/perf/single/common/ 대응
    │   │   │   ├── PerfCommon.cs           ← 공통 유틸 (retry, PrintResult, 엔드포인트 등)
    │   │   │   └── PerfTls.cs             ← TLS 인증서 경로 리졸버 (single)
    │   │   └── src/                    ← ★ core/perf/single/src/ 대응
    │   │       ├── PerfPair.cs
    │   │       ├── PerfPubSub.cs
    │   │       ├── PerfDealerDealer.cs
    │   │       ├── PerfDealerRouter.cs
    │   │       ├── PerfRouterRouter.cs
    │   │       ├── PerfRouterRouterPoll.cs
    │   │       ├── PerfGateway.cs
    │   │       └── PerfSpot.cs
    │   │
    │   │   ※ 위는 목표 구조 (common/ + src/ 분리)
    │   │     현재 상태: 프로젝트 루트에 모든 .cs 파일이 flat 배치됨
    │   │     §14 Phase 1 에서 마이그레이션 실행
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── multi/
    │   ├── Zlink.BindingBench.Multi/
    │   │   ├── Zlink.BindingBench.Multi.csproj  ← .NET 8.0 콘솔 프로젝트
    │   │   ├── GlobalUsings.cs
    │   │   ├── PerfMain.cs                 ← 진입점 (--multi-server / --multi-client)
    │   │   ├── common/                     ← ★ core/perf/multi/common/ 대응
    │   │   │   ├── PerfCommon.cs           ← 공통 유틸 (multi 전용)
    │   │   │   ├── PerfCommonMulti.cs      ← Multi 설정 리졸버
    │   │   │   ├── PerfServerEntry.cs ← 서버 디스패처 + CPU/MEM
    │   │   │   ├── PerfClientEntry.cs ← 클라이언트 디스패처 + CPU/MEM
    │   │   │   ├── PerfClientHelpers.cs ← 연결 준비/정리 보조 유틸 (send/recv 루프 공유 금지)
    │   │   │   ├── PerfTls.cs         ← TLS 인증서 경로 리졸버
    │   │   │   └── PerfStreamStopParser.cs ← len32be stop-token 파서
    │   │   └── src/                    ← ★ core/perf/multi/src/ 대응 (server/client 별도 파일)
    │   │       ├── PerfDealerDealerServer.cs
    │   │       ├── PerfDealerDealerClient.cs
    │   │       ├── PerfDealerRouterServer.cs
    │   │       ├── PerfDealerRouterClient.cs
    │   │       ├── PerfRouterRouterServer.cs
    │   │       ├── PerfRouterRouterClient.cs
    │   │       ├── PerfPubSubServer.cs
    │   │       ├── PerfPubSubClient.cs
    │   │       ├── PerfGatewayServer.cs
    │   │       ├── PerfGatewayClient.cs
    │   │       ├── PerfSpotServer.cs
    │   │       ├── PerfSpotClient.cs
    │   │       ├── PerfStreamServer.cs           ← 서버 only (클라이언트=공통 stream client)
    │   │       ├── PerfStreamCallbackServer.cs
    │   │       └── PerfStreamLen32BeServer.cs
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── common/
    │   └── PerfComparisonBase.py           ← single/multi 공통 Python 유틸 (선택)
    │                                         ※ core/perf/common/ 은 streamclient 인프라 중심이나,
    │                                           dotnet 은 C++ 공용 streamclient 를 재사용하므로
    │                                           이 디렉토리는 Python 스크립트 유틸 공간으로만 활용
    │
    ├── results/
    │   ├── single/
    │   │   └── report/
    │   └── multi/
    │       └── report/
    │
    ├── run_benchmarks.sh                   ← 루트 single 래퍼
    ├── run_benchmarks.ps1
    ├── run_benchmarks_multi.sh             ← 루트 multi 래퍼
    ├── run_benchmarks_multi.ps1
    └── run_comparison.py                   ← 루트 호환 어댑터 (→ run_policy_bench.py 위임)
```

### core/perf 대비 구조 매핑

| core/perf | dotnet/perf | 비고 |
|-----------|-------------|------|
| `single/common/bench_common.hpp` | `single/.../common/PerfCommon.cs` | static 유틸 클래스 |
| `single/common/perf_single_metric_header.hpp` | (해당없음 — C# 은 BinaryPrimitives 로 인라인 처리) | |
| `single/src/perf_pair.cpp` | `single/.../src/PerfPair.cs` | 1:1 매핑 |
| `single/src/perf_dealer_dealer.cpp` | `single/.../src/PerfDealerDealer.cs` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/.../common/PerfCommon.cs` | 패키지 분리 |
| `multi/common/perf_common_multi.hpp` | `multi/.../common/PerfCommonMulti.cs` | |
| `multi/common/perf_metric_header.hpp` | (해당없음 — C# 은 BinaryPrimitives 로 인라인 처리) | magic=MPF1, phase_drain 포함 |
| `multi/common/perf_client_helpers.hpp` | `multi/.../common/PerfClientHelpers.cs` | 연결 준비/정리 보조 유틸 (패턴 간 send/recv 루프 공유 금지) |
| `multi/common/perf_entry.hpp` | `multi/.../common/PerfServerEntry.cs` + `PerfClientEntry.cs` | |
| `multi/src/perf_dealer_dealer_server.cpp` | `multi/.../src/PerfDealerDealerServer.cs` | ★ server/client 별도 파일 (1:1 매핑) |
| `multi/src/perf_dealer_dealer_client.cpp` | `multi/.../src/PerfDealerDealerClient.cs` | |
| `multi/src/perf_stream_server.cpp` | `multi/.../src/PerfStreamServer.cs` | 서버 only |

---

## 2. 빌드 시스템

### 2.1 Zlink.sln (기존 솔루션에 추가)

```xml
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = "Zlink.BindingBench", "perf\single\Zlink.BindingBench\Zlink.BindingBench.csproj", "{NEW-GUID-1}"
EndProject
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = "Zlink.BindingBench.Multi", "perf\multi\Zlink.BindingBench.Multi\Zlink.BindingBench.Multi.csproj", "{NEW-GUID-2}"
EndProject
```

### 2.2 single csproj (`perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj`)

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\..\src\Zlink\Zlink.csproj" />
  </ItemGroup>
</Project>
```

### 2.3 multi csproj (`perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj`)

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\..\..\src\Zlink\Zlink.csproj" />
  </ItemGroup>
</Project>
```

### 2.4 빌드 명령

```bash
cd bindings/dotnet
dotnet build perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release
dotnet build perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release
```

- `run_policy_bench.py` 의 `build_binding_if_needed()` 가 동일한 명령으로 빌드를 호출한다.
- 산출물: `perf/single/Zlink.BindingBench/bin/Release/net8.0/` 및 `perf/multi/Zlink.BindingBench.Multi/bin/Release/net8.0/`

---

## 3. CLI 인터페이스 (run_comparison.py 호환)

### 3.1 Single 실행

```bash
dotnet run --project perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj \
  -c Release -- <PATTERN> <TRANSPORT> <SIZE>
```

또는 빌드 후 직접 실행:

```bash
./perf/single/Zlink.BindingBench/bin/Release/net8.0/Zlink.BindingBench \
  <PATTERN> <TRANSPORT> <SIZE>
```

- **PATTERN** (C# 바이너리 직접 실행 8종): `PAIR | PUBSUB | DEALER_DEALER | DEALER_ROUTER | ROUTER_ROUTER | ROUTER_ROUTER_POLL | GATEWAY | SPOT`
- **TRANSPORT**: `tcp | tls | ws | wss | inproc | ipc`
- **SIZE**: 양의 정수 (바이트)
- 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류

> **single suite 는 위 8종**이다. STREAM 3종(STREAM, STREAM_CALLBACK, STREAM_LEN32BE)은
> core/perf 기준 **multi 전용** 패턴이며, single suite 에는 포함되지 않는다 (`core/perf/run_comparison.py:84` 참조).

### 3.2 Multi 실행

**서버:**
```bash
./Zlink.BindingBench.Multi --multi-server <PATTERN> <TRANSPORT> <SIZE>
```

**클라이언트:**
```bash
./Zlink.BindingBench.Multi --multi-client <PATTERN> <TRANSPORT> <SIZE> --endpoint <endpoint>
```

### 3.2.1 Multi 패턴 네이밍 규약

> **핵심 원칙**: `MULTI_*` 는 `run_policy_bench.py` 내부의 **입력 토큰**일 뿐이다. 실제 런타임(바이너리 실행, RESULT 출력, 환경 변수)에서는 접두어 없는 패턴 이름을 사용한다.

| 계층 | 패턴 이름 | 예시 |
|------|----------|------|
| `run_policy_bench.py --pattern` (입력 토큰) | `MULTI_DEALER_DEALER` | single/multi 구분용 |
| C# 바이너리 `<PATTERN>` (런타임) | `DEALER_DEALER` | `MULTI_` strip 후 전달 |
| `RESULT,current,...` (출력) | `DEALER_DEALER` | 접두어 없는 패턴 |
| `PERF_PATTERN` 환경 변수 (런타임) | `DEALER_DEALER` | `set_perf_pattern_env()` 로 설정 |

**core 기준 바이너리 식별 (run_comparison.py)**:
- multi 바이너리 이름: `comp_src_{pattern}_{role}` (예: `comp_src_dealer_dealer_server`, `comp_src_pubsub_client`)
- single 바이너리 이름: `perf_{pattern}` (예: `perf_pair`, `perf_pubsub`)
- STREAM 클라이언트: `perf_stream_client` (공용 바이너리, 패턴 무관)

**`set_perf_pattern_env()`** (core/perf multi/common/perf_entry.hpp):
- 서버/클라이언트 진입 시 `PERF_PATTERN` 환경 변수에 패턴 이름(무접두어)을 설정
- dotnet 구현: `PerfServerEntry.cs` / `PerfClientEntry.cs` 에서 동일하게 `Environment.SetEnvironmentVariable("PERF_PATTERN", pattern)` 호출

### 3.3 STREAM 패턴 클라이언트

STREAM, STREAM_CALLBACK, STREAM_LEN32BE 패턴은:
- **서버**: C# 벤치마크가 직접 구현 (Socket.AttachStreamRaw / AttachStreamLen32Be API)
- **클라이언트**: `core/perf/common/streamclient/build/perf_stream_client` (C++ 공통 바이너리) 사용
- `run_policy_bench.py` 가 자동으로 공통 stream client 를 호출한다.

### 3.4 실행 엔진 아키텍처

모든 바인딩은 `bindings/perf/run_policy_bench.py` 를 단일 실행 엔진으로 사용한다.

```
bindings/dotnet/perf/run_benchmarks.sh          ← 사용자 진입점 (인자 파싱, 환경 설정)
  └─ bindings/dotnet/perf/run_comparison.py     ← 호환 어댑터 (CLI 변환 후 위임)
       └─ bindings/perf/run_policy_bench.py     ← 단일 실행 엔진 (빌드/실행/메트릭/결과)
```

`run_policy_bench.py` 에 dotnet 지원이 **이미 구현**되어 있다:

| 함수 | 위치 | dotnet 처리 내용 |
|------|------|-----------------|
| `build_binding_if_needed()` | run_policy_bench.py:590 | `dotnet build -c Release` 호출 |
| `binding_cmd_prefix()` | run_policy_bench.py:820 | `dotnet <DLL> <PATTERN>` 형식 (빌드된 DLL 경로) |
| `binding_multi_role_command()` | run_policy_bench.py:928 | `dotnet <Multi.DLL> --multi-server/--multi-client` 형식 |

바인딩별 `run_comparison.py` 는 core/perf CLI 형태를 policy runner CLI 로 변환하는 **호환 어댑터**이다.
빌드/실행/메트릭 수집 로직은 `run_policy_bench.py` 에만 존재하며, 바인딩별로 분산되지 않는다.

---

## 4. RESULT 출력 형식 (필드 형식은 core/perf 동일, dotnet latency 값은 ms)

```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,throughput,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,bandwidth,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p95,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p99,<value>
```

> dotnet 출력 정책: `latency`, `latency_p95`, `latency_p99` 값은 **ms 단위**로 출력한다.
> 내부 타임스탬프와 샘플 계산은 기존대로 `sent_ts_us` / microsecond 기준을 유지하고, `PrintResult()` 직전만 `us -> ms` 변환한다.

Single 정보성 메트릭 (없어도 complete 판정에 영향 없음):
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,snd_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,rcv_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,rcv_pending_end,<value>
```

Multi 정보성 메트릭 (없어도 complete 판정에 영향 없음, PerfServerEntry / PerfClientEntry 에서):
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,client_mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_snd_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,server_rcv_pending_end,<value>
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
| `PERF_WARMUP_COUNT` | 패턴별 (표준: 1000, GATEWAY/SPOT: 200) | 웜업 메시지 횟수 (count 기반, 시간 기반 아님). **SPOT clamp**: `msg_size ≥ 65536` 이면 최대 20 |
| `PERF_SINGLE_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 200000 | 레이턴시 reservoir sampling 캡 |
| `PERF_SINGLE_HWM` | **1000** | 소켓 HWM (send+recv 기본) |
| `PERF_SINGLE_SNDHWM` | PERF_SINGLE_HWM 값 상속 | 송신 HWM (미설정 시 PERF_SINGLE_HWM 사용) |
| `PERF_SINGLE_RCVHWM` | PERF_SINGLE_HWM 값 상속 | 수신 HWM (미설정 시 PERF_SINGLE_HWM 사용) |
| `PERF_SINGLE_SNDTIMEO_MS` | **200** | 송신 타임아웃 |
| `PERF_SINGLE_RCVTIMEO_MS` | **200** | 수신 타임아웃 |
| `PERF_MAX_SOCKETS` | 자동 | 최대 소켓 수 |
| `PERF_DEBUG` | (없으면 off) | 디버그 출력 활성화 |
| `PERF_SINGLE_PUBSUB_RCVTIMEO_MS` | RCVTIMEO_MS 상속 | PubSub 전용 수신 타임아웃 |
| `PERF_SINGLE_QUEUE_SAMPLE_MS` | 100 | 큐 샘플링 주기 (ms) |
| `PERF_SINGLE_QUEUE_SAMPLE_EVERY_MSGS` | 64 | 큐 샘플링 메시지 간격 |

### 5.2 Multi

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_CLIENTS` | **100** (STREAM: 10000) | 동시 클라이언트 수 |
| `PERF_WARMUP_SECONDS` | **2** | 웜업 기간 |
| `PERF_SETTLE_MS` | 500 | 측정 전 안정화 |
| `PERF_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_ACTIVE_WARMUP` | 0 | 0=sleep, 1=active |
| `PERF_HWM` | **100** (STREAM: 10) | 소켓 HWM |
| `PERF_SNDHWM` | PERF_HWM 값 상속 | 송신 HWM (미설정 시 PERF_HWM 사용) |
| `PERF_RCVHWM` | PERF_HWM 값 상속 | 수신 HWM (미설정 시 PERF_HWM 사용) |
| `PERF_SNDTIMEO_MS` | **200** | 송신 타임아웃 |
| `PERF_RCVTIMEO_MS` | **200** | 수신 타임아웃 |
| `PERF_CONNECT_READY_TIMEOUT_MS` | 5000 | 연결 대기 |
| `PERF_MONITOR_HWM` | **1000** | 모니터 소켓 HWM |
| `PERF_SERVER_BIND_PORT` | 0 (자동) | 서버 포트 고정 |
| `PERF_IO_THREADS` | 0 | IO 스레드 (서버/클라이언트 공통) |

---

## 6. 벤치마크 페이즈 (core/perf 동일)

### 6.1 Single 페이즈

```
[Setup(bind/connect + settle)] → [Warmup(count)] → [Active(duration)]
```

1. **Setup**: 소켓 생성, bind/connect 후 `settle()` (100ms, 코드 상수 `SETTLE_TIME_MS`) — 별도 측정 페이즈가 아니라 연결 설정의 일부
2. **Warmup** (`PERF_WARMUP_COUNT`, 표준: 1000, GATEWAY/SPOT: 200): 고정 횟수 send/recv 반복, `phase_warmup` 라벨 (시간 기반이 아님). **SPOT clamp**: `msg_size ≥ 65536` 이면 warmup 최대 20 (`perf_spot.cpp:806`)
3. **Active** (`PERF_SINGLE_DURATION_SECONDS`, 5초): duration 기반 throughput 측정 + reservoir sampling 으로 latency/p95/p99 동시 수집, `phase_active` 라벨. recv timeout (기본 200ms) 동안 무수신 시 루프 종료

> **core 구현 참고**: Active 페이즈에서 메시지 헤더의 `sent_ts_us` 를 기반으로 throughput 과 latency 를 동시에 측정한다.
> 수신 측에서 reservoir sampling 으로 p95/p99 를 수집한다.
> Single 에는 `phase_drain` 이 없다 (`perf_single_metric_header.hpp`: phase_unknown=0, phase_warmup=1, phase_active=2 만 존재).

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle] → [Active(duration)]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_WARMUP_SECONDS`, 2초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_SETTLE_MS`, 500ms): 안정화 sleep (one-way: phase_drain 라벨, echo: phase_warmup 라벨)
4. **Active** (`PERF_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)

---

## 7. 패턴별 상세 구현 계획

### 7.1 Single 패턴

> **참고**: run_policy_bench.py 기준 모든 single 패턴은 one-way 방향(`bandwidth 승수 = 1.0`).
> "소켓 동작" 열은 실제 send/recv 패턴(echo=양방향, one-way=단방향)을 나타낸다.

| # | 파일 | 패턴 | 소켓 타입 | 소켓 동작 | 트랜스포트 |
|---|------|------|-----------|----------|-----------|
| 1 | PerfPair.cs | PAIR | Pair×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 2 | PerfPubSub.cs | PUBSUB | Pub+Sub | one-way | tcp,tls,ws,wss,inproc,ipc |
| 3 | PerfDealerDealer.cs | DEALER_DEALER | Dealer×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 4 | PerfDealerRouter.cs | DEALER_ROUTER | Dealer+Router | echo | tcp,tls,ws,wss,inproc,ipc |
| 5 | PerfRouterRouter.cs | ROUTER_ROUTER | Router×2 | echo | tcp,tls,ws,wss,inproc,ipc |
| 6 | PerfRouterRouterPoll.cs | ROUTER_ROUTER_POLL | Router×2+Poller | echo | tcp,tls,ws,wss,inproc,ipc |
| 7 | PerfGateway.cs | GATEWAY | Gateway+Receiver | echo | tcp,tls,ws,wss |
| 8 | PerfSpot.cs | SPOT | Spot (pub/sub) | one-way | tcp,tls,ws,wss | ★ warmup clamp: size≥65536 → max 20 |

> core/perf 기준 single suite 는 **8개 패턴**이다 (`core/perf/run_comparison.py:84`).
> STREAM 3종(STREAM, STREAM_CALLBACK, STREAM_LEN32BE)은 **multi 전용** 패턴이며, single suite 에 포함되지 않는다.

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 를 별도 파일로 분리**한다. 각 패턴의 서버 로직과 클라이언트 로직이 독립된 소스 파일에 구현된다.

| # | 서버 파일 | 클라이언트 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|-----------|----------------|------|-----------|----------------|
| 1 | PerfDealerDealerServer.cs | PerfDealerDealerClient.cs | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | PerfDealerRouterServer.cs | PerfDealerRouterClient.cs | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | PerfRouterRouterServer.cs | PerfRouterRouterClient.cs | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | PerfPubSubServer.cs | PerfPubSubClient.cs | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | PerfGatewayServer.cs | PerfGatewayClient.cs | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | PerfSpotServer.cs | PerfSpotClient.cs | SPOT | Spot publish | Spot subscribe |
| 7 | PerfStreamServer.cs | (C++ stream client) | STREAM | STREAM bind, raw echo | C++ stream client |
| 8 | PerfStreamCallbackServer.cs | (C++ stream client) | STREAM_CALLBACK | AttachStreamRaw callback | C++ stream client |
| 9 | PerfStreamLen32BeServer.cs | (C++ stream client) | STREAM_LEN32BE | AttachStreamLen32Be | C++ stream client |

**Multi 서버 통신 프로토콜:**
- 서버 stdout 에 `READY,<endpoint>` 출력 → 스크립트가 클라이언트 시작
- 클라이언트 종료 시 서버에 stop-token 전송
- 서버 graceful shutdown 후 RESULT 메트릭 출력

---

## 8. 공통 유틸리티

### 8.1 Single common/ 패키지

**`PerfCommon.cs`** — `single/.../common/` (core/perf single/common/bench_common.hpp 대응)

```csharp
internal static class PerfCommon
{
    // 환경변수 파싱
    static int ParseEnv(string name, int defaultValue);
    static int ParseEnvNonNegative(string name, int defaultValue);

    // 소켓 옵션 적용
    static void ApplySingleContextOptions(Context ctx);
    static void ApplySingleSocketOptions(Socket socket);

    // Send/Receive helper
    // - send: blocking send only (retry 금지)
    // - recv: blocking recv + non-blocking drain 조합
    static int ReceiveBlocking(Socket socket, byte[] buffer);
    static int TryReceiveNonBlocking(Socket socket, byte[] buffer);
    static int DrainRemainingFramesNonBlocking(Socket socket);
    static int SendBlocking(Socket socket, byte[] buffer);

    // 폴링
    static bool WaitForInput(Socket socket, int timeoutMs);
    static bool WaitUntil(Func<bool> check, int timeoutMs);

    // 엔드포인트 생성
    static string EndpointFor(string transport, string name);

    // RESULT 출력 (bandwidth 승수 = 1.0 for all single)
    // latency 계열 출력 단위는 ms, 내부 계산은 us 유지
    static void PrintResult(string pattern, string transport, int size,
                           double throughput, double latencyUs,
                           double latencyP95Us, double latencyP99Us);

    // 메시지 카운트 리졸브
    static int ResolveMsgCount(int size);

    // 헤더 stamp/decode (BinaryPrimitives 기반)
    static void StampHeader(Span<byte> buffer, uint runId, uint phase,
                           uint msgSize, ulong seq);
    static bool DecodeHeader(ReadOnlySpan<byte> buffer, out uint runId,
                            out uint phase, out uint msgSize, out ulong seq,
                            out long sentTsUs);

    // Latency reservoir sampling
    static void ReservoirSample(ref List<double> samples, double value,
                               long count, int cap, ref uint rngState);
    static (double mean, double p95, double p99) ComputeLatencyStats(
                               List<double> samples);

    // Stopwatch 유틸
    static long TimestampUs();  // Stopwatch.GetTimestamp() → microseconds

    // ※ STREAM 헬퍼는 single/common 에 배치하지 않는다.
    //   single suite 에는 STREAM 패턴이 없으며,
    //   multi STREAM 서버가 필요로 하는 STREAM 소켓 유틸(StreamSend/Recv/ConnectEvent)은
    //   각 multi/.../src/PerfStream*Server.cs 에 인라인한다.
    //   → single/multi 경계를 유지하여 core 의 suite 분리 철학을 준수한다.
    //
    // ※ STREAM 벤치마크 클라이언트는 C# 로 구현하지 않는다.
    //   core/perf/common/streamclient 공용 C++ 바이너리를 사용한다.

    // Gateway / Spot 헬퍼
    static void GatewayReceiveProviderMessage(Socket router, byte[] routingIdBuf, byte[] payloadBuf);
    static int SpotReceivePayloadWithTimeout(Spot spot, byte[] payloadBuf, int timeoutMs);
}
```

**`PerfTls.cs`** — single 전용 TLS 인증서 리졸버

```csharp
internal static class PerfTls
{
    static void ConfigureTlsServerIfNeeded(Socket socket, string transport);
    static void ConfigureTlsClientIfNeeded(Socket socket, string transport);
    // bindings/dotnet/tests/certs/ 에서 server.crt, server.key, ca.crt 탐색
    static bool TryResolvePerfTlsPaths(out string certPath, out string keyPath, out string caPath);
}
```

### 8.2 Multi common/ 패키지

**`PerfCommon.cs`** — `multi/.../common/` (core/perf multi/common/perf_common.hpp 대응)

single 의 PerfCommon 유틸 중 필요한 것을 포함하되, multi 는 다음 정책으로 분리한다.

- recv: `Poller` + `PollIn` + non-blocking drain (`EAGAIN`까지, cap 없음)
- send: `DontWait` 1회 시도, `EAGAIN`이면 pending 상태만 유지
- `PollOut` 기본 OFF, pending send 소켓에만 ON
- `PollOut`에서 pending send가 비면 즉시 OFF
- `Thread.Sleep` / `Thread.Yield` / retry budget 으로 `would-block` 은폐 금지
- `Gateway`/`Receiver`/`Spot` poller 등록은 raw socket helper가 아니라 service instance 기준으로 구현한다 (`core/v4.0.0`)

**`PerfCommonMulti.cs`** — (core/perf multi/common/perf_common_multi.hpp 대응)

```csharp
internal static class PerfCommonMulti
{
    static int ResolveClients(string pattern);   // 비-STREAM: 100, STREAM: 10000
    static int ResolveHwm(string pattern);       // 비-STREAM: 100, STREAM: 10
    static int ResolveWarmupSeconds();
    static int ResolveDurationSeconds();
    static int ResolveSettleMs();
    static int ResolveWarmupDrainMs(string pattern);
    // ... 기타 환경변수 리졸버
}
```

**`PerfClientHelpers.cs`** — (core/perf multi/common/perf_client_helpers.hpp 대응)

```csharp
internal static class PerfClientHelpers
{
    static bool IsSupportedTransport(string transport);
    static string ParseEndpointArg(string[] args);
    static void WaitAllClientConnectReady(List<MonitorSocket> monitors, int timeoutMs);
    static void TrySendStopToken(...);          // stop-token 전송 보조
    static void DisposeAllQuietly(...);         // 자원 정리 보조
}
```

> **중요 (dotnet 인라이닝 정책)**: `PerfClientHelpers` 는 연결/정리 보조 유틸만 포함한다.
> 패턴 간 `send/recv` 측정 루프는 공유하지 않으며, 각 `src/Perf*Client.cs` 파일 내부에서만 공통화한다.
> **명시 규칙**: core/perf 의 file-local helper 방식과 동일하게, `multi/src/Perf*Server.cs` / `Perf*Client.cs` 각각에서
> send/recv 핵심 로직을 파일 내부 `private static` 헬퍼로만 공통화한다. `common/` 으로 이동하지 않는다.

**`PerfServerEntry.cs`** — (core/perf multi/common/perf_entry.hpp 서버 부분 대응)

```csharp
// 1. Environment.SetEnvironmentVariable("PERF_PATTERN", pattern)  ← set_perf_pattern_env() 대응
// 2. 패턴 디스패치 → RunServer(transport, size)
// 3. CPU/MEM 메트릭 수집
// 4. RESULT 출력: server_cpu_pct, server_mem_mb
```

**`PerfClientEntry.cs`** — (core/perf multi/common/perf_entry.hpp 클라이언트 부분 대응)

```csharp
// 1. Environment.SetEnvironmentVariable("PERF_PATTERN", pattern)  ← set_perf_pattern_env() 대응
// 2. 패턴 디스패치 → RunClient(transport, size, endpoint)
// 3. CPU/MEM 메트릭 수집
// 4. RESULT 출력: client_cpu_pct, client_mem_mb
```

**`PerfTls.cs`** — TLS 인증서 리졸버

```csharp
// bindings/dotnet/tests/certs/ 에서 탐색
// 상위 디렉토리 순회: bindings/dotnet/tests/certs → tests/certs
```

---

## 9. TLS 인증서 관리

### 9.1 독립 인증서 디렉토리

각 바인딩은 `bindings/<lang>/tests/certs/` 에서 인증서를 독립 관리한다.

```
bindings/dotnet/tests/certs/
├── server.crt          ← 서버 인증서 (localhost SAN 포함)
├── server.key          ← 서버 개인키
└── ca.crt              ← CA 인증서
```

> dotnet 은 이미 `bindings/dotnet/tests/certs/` 에 인증서 관리 중.

### 9.2 인증서 경로 탐색 로직

상위 디렉토리 순회 방식:

```csharp
// 탐색 순서:
// 1. bindings/dotnet/tests/certs/
// 2. tests/certs/ (상위 순회)
// AppContext.BaseDirectory 또는 Environment.CurrentDirectory 기준으로
// 상위 디렉토리를 순회하며 "bindings/dotnet/tests/certs" 또는
// "tests/certs" 하위에 server.crt, server.key, ca.crt 3개 파일이 모두 존재하는지 확인
```

### 9.3 소켓 옵션 설정

```csharp
// Socket option API 사용 (SocketOption 상수)
socket.SetOption(SocketOption.TlsCert, certPath);
socket.SetOption(SocketOption.TlsKey, keyPath);
socket.SetOption(SocketOption.TlsCa, caPath);
```

또는 서비스 API:
```csharp
// Gateway
gateway.SetTlsClient(caCert: caPath, hostname: "localhost", trustSystem: false);

// Receiver
receiver.SetTlsServer(cert: certPath, key: keyPath);
```

---

## 10. C# API 매핑

| Core C API | C# API |
|-------------|--------|
| `zlink_ctx_new()` | `new Context()` |
| `zlink_socket(ctx, type)` | `new Socket(ctx, SocketType.Pair)` 등 |
| `zlink_bind(s, endpoint)` | `socket.Bind(endpoint)` |
| `zlink_connect(s, endpoint)` | `socket.Connect(endpoint)` |
| `zlink_send(s, buf, len, flags)` | `socket.Send(ReadOnlySpan<byte>, SendFlags)` |
| `zlink_recv(s, buf, len, flags)` | `socket.Receive(Span<byte>, ReceiveFlags)` |
| `zlink_setsockopt(s, opt, val)` | `socket.SetOption(SocketOption.XXX, val)` |
| `zlink_getsockopt(s, opt)` | `socket.GetOption<int>(SocketOption.XXX)` |
| `zlink_poll(items, n, timeout)` | `Poller.Poll(timeoutMs)` |
| `zlink_ctx_set(ctx, IO_THREADS)` | `ctx.SetOption(ContextOption.IoThreads, n)` |
| STREAM attach raw | `socket.AttachStreamRaw(callback)` |
| STREAM attach len32be | `socket.AttachStreamLen32Be(callback)` |
| STREAM detach | `socket.DetachStream()` |
| STREAM send | `socket.StreamSend(routingId, payload)` |
| Gateway | `new Gateway(ctx, discovery)` |
| Receiver | `new Receiver(ctx, ...)` |
| Spot | `new Spot(spotNode)` |
| MonitorSocket | `socket.MonitorOpen(events)` |
| Message 생성 | `new Message(size)` / `new Message(data)` |
| Message 읽기 | `msg.AsReadOnlySpan()` |

**C# 특화 성능 기법:**
- `Span<byte>` / `ReadOnlySpan<byte>` 로 zero-copy send/recv
- `stackalloc byte[]` 로 소규모 버퍼 스택 할당
- `ArrayPool<byte>.Shared` 로 대규모 버퍼 풀링
- `BinaryPrimitives.WriteUInt32BigEndian()` 등으로 헤더 인코딩
- `Stopwatch.GetTimestamp()` + `Stopwatch.Frequency` 로 고정밀 타이밍

---

## 11. 리소스 메트릭 수집 (C#)

### 11.1 CPU 사용률

```csharp
// Process 클래스 활용
using var process = Process.GetCurrentProcess();
TimeSpan cpuBefore = process.TotalProcessorTime;
// ... 측정 ...
process.Refresh();
TimeSpan cpuAfter = process.TotalProcessorTime;
double cpuPct = (cpuAfter - cpuBefore).TotalMilliseconds
              / (elapsedMs * Environment.ProcessorCount) * 100.0;
```

### 11.2 메모리 사용량

```csharp
// Process.WorkingSet64 (RSS 상당)
using var process = Process.GetCurrentProcess();
process.Refresh();
double memMb = process.WorkingSet64 / (1024.0 * 1024.0);
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

내부적으로 `run_comparison.py` → `run_policy_bench.py --binding dotnet --suite single` 체인으로 실행한다.

### 12.2 run_benchmarks_multi.sh (루트)

```bash
./run_benchmarks_multi.sh [options]

추가 Options:
  --clients N                 클라이언트 수 (기본: 100, STREAM: 10000)
  --warmup N                  웜업 초 (기본: 2)
  --transport-transition-ms N 트랜스포트 전환 대기 (기본: 3000)
  --pattern-transition-ms N   패턴 전환 대기 (기본: 3000)
```

내부: `run_comparison.py` → `run_policy_bench.py --binding dotnet --suite multi` 체인으로 실행한다.

### 12.3 run_benchmarks.ps1 / run_benchmarks_multi.ps1

동일 인터페이스, PowerShell 구현.

---

## 13. 산출물

### 13.1 결과 파일

```
bindings/dotnet/perf/results/single/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
bindings/dotnet/perf/results/multi/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
```

`run_policy_bench.py` 는 `results/{suite}/report/` 에 결과 파일을 생성한다:

**결과 파일 형식:**
```
## PATTERN: PAIR
### tcp
| Size | Throughput | Bandwidth | Latency(ms) | Latency_p95(ms) | Latency_p99(ms) |
|------|-----------|-----------|---------|-------------|-------------|
| 64   | 523401.23 | 33.50     | 12.35   | 18.22       | 25.10       |
...

## Completion
- status: complete
- expected_result_lines: N
- actual_result_lines: N
```

> **single vs multi 완료 형식 차이:**
> - **single**: 섹션 `## Completion`, 키 `expected_result_lines` / `actual_result_lines` (밑줄)
> - **multi**: 섹션 `## Status Summary`, 키 `expected result lines` / `actual result lines` (공백) + `success/unsupported/skip/fail` 카운트 포함

---

## 14. 구현 순서

### Phase 0: 인증서 및 스크립트 준비

1. `bindings/dotnet/tests/certs/` 인증서 확인 (server.crt, server.key, ca.crt — 이미 존재)
2. `run_policy_bench.py` dotnet 지원 확인 (이미 구현됨: build/cmd_prefix/multi_role_command)
3. `.gitignore` 파일

### Phase 1: 인프라 (빌드, 공통, 진입점)

4. **[마이그레이션] single flat → common/src 분리**: 현재 프로젝트 루트에 flat 배치된 파일들을 common/, src/ 로 이동
   - `PerfCommon.cs`, `PerfTls.cs` → `common/`
   - `PerfPair.cs`, `PerfPubSub.cs`, `PerfDealerRouter.cs`, `PerfRouterRouter.cs`, `PerfGateway.cs`, `PerfSpot.cs` 등 패턴 파일 → `src/`
   - `PerfStream.cs`, `PerfStreamCallbackEcho.cs` → 삭제 (STREAM 은 multi suite only, 클라이언트는 C++ 바이너리)
   - `PerfMain.cs`, `GlobalUsings.cs` → 프로젝트 루트 유지
5. `perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj` 생성
6. `perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj` 생성
7. `Zlink.sln` 에 perf 프로젝트 참조 추가
8. `single/.../common/PerfCommon.cs` — 환경변수 파싱, blocking send/recv + drain 헬퍼, RESULT 출력, 헤더 stamp/decode, reservoir sampling
9. `single/.../common/PerfTls.cs` — single TLS 인증서 리졸버
10. `single/.../PerfMain.cs` — 패턴 디스패치 진입점
11. `multi/.../common/PerfCommon.cs` — multi 공통 유틸
12. `multi/.../common/PerfCommonMulti.cs` — multi 설정 리졸버
13. `multi/.../common/PerfClientHelpers.cs` — 연결/정리 보조 유틸 (패턴 간 send/recv 루프 공유 금지)
14. `multi/.../common/PerfServerEntry.cs` — 서버 디스패치 + 메트릭
15. `multi/.../common/PerfClientEntry.cs` — 클라이언트 디스패치 + 메트릭
16. `multi/.../common/PerfTls.cs` — TLS 인증서 리졸버
17. `multi/.../PerfMain.cs` — --multi-server / --multi-client 디스패치

### Phase 2: Single 소켓 패턴 (6개)

18. `single/.../src/PerfPair.cs`
19. `single/.../src/PerfPubSub.cs`
20. `single/.../src/PerfDealerDealer.cs`
21. `single/.../src/PerfDealerRouter.cs`
22. `single/.../src/PerfRouterRouter.cs`
23. `single/.../src/PerfRouterRouterPoll.cs`

### Phase 3: Single 서비스 패턴 (2개)

24. `single/.../src/PerfGateway.cs`
25. `single/.../src/PerfSpot.cs`

### Phase 4: Multi 패턴 — server/client 별도 파일 (6×Server + 6×Client + 3 서버 only)

27. `multi/.../src/PerfDealerDealerClient.cs`
28. `multi/.../src/PerfDealerRouterServer.cs`
29. `multi/.../src/PerfDealerRouterClient.cs`
30. `multi/.../src/PerfRouterRouterServer.cs`
31. `multi/.../src/PerfRouterRouterClient.cs`
32. `multi/.../src/PerfPubSubServer.cs`
33. `multi/.../src/PerfPubSubClient.cs`
34. `multi/.../src/PerfGatewayServer.cs`
35. `multi/.../src/PerfGatewayClient.cs`
36. `multi/.../src/PerfSpotServer.cs`
37. `multi/.../src/PerfSpotClient.cs`
38. `multi/.../src/PerfStreamServer.cs` (서버 only; 클라이언트=core/perf/common/streamclient C++ 바이너리)
39. `multi/.../src/PerfStreamCallbackServer.cs` (서버 only; 클라이언트=C++ 바이너리)
40. `multi/.../src/PerfStreamLen32BeServer.cs` (서버 only; 클라이언트=C++ 바이너리)
41. `multi/.../common/PerfStreamStopParser.cs`

### Phase 5: 스크립트 및 마무리

42. `run_benchmarks.sh` / `.ps1` (루트 + single/ + multi/)
43. `run_benchmarks_multi.sh` / `.ps1`
44. `run_comparison.py` (호환 어댑터 — run_policy_bench.py 위임)
45. `README.md`
46. 빌드 검증 (`dotnet build -c Release`)
47. `run_policy_bench.py` → `run_comparison.py` 체인 통합 검증

### Phase 6: 코드 품질 리뷰 및 리팩토링

> 모든 패턴 구현과 스크립트 완성 후, 코드 전체에 대한 품질 리뷰와 개선을 수행한다.
> 성능 벤치마크 코드이므로 불필요한 오버헤드에 특히 엄격히 대응한다.

#### Phase 6-A. OOP 리팩토링 실행 스코프 (2026-03-06)

- `LANG`: `.NET`
- `TARGET_PATHS`:
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/common`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src`
- `PATTERNS`:
  - `MULTI_GATEWAY`
  - `MULTI_SPOT`

**핵심 제약(고정):**
- warmup/settle/active 의미, throughput/latency/p95/p99 계산식, `RESULT,current,...` 출력 형식은 변경하지 않는다.
- 측정 루프 내 힙 할당/불필요 복사/로그 문자열 생성을 추가하지 않는다.
- send/recv 버퍼는 루프 밖에서 1회 할당 후 재사용한다.
- core/perf 와 달리 dotnet 은 패턴 간 send/recv 공통화를 금지하고, **각 패턴 파일 내부 private helper** 까지만 허용한다.
- client cap/retry budget/fallback 으로 실패를 성공처럼 보이게 만드는 동작을 금지한다.
- send 정책은 고정한다: single=`blocking send 1회`, multi=`DontWait 1회 + pending 시 PollOut`.
- recv 정책은 고정한다: single=`blocking recv + non-blocking drain`, multi=`poller + non-blocking drain(무제한, cap 없음)`.
- hot loop 에서는 `Thread.Sleep` / `Thread.Yield` 를 금지하고, settle/drain 같은 phase 경계 sleep 만 허용한다.
- `core/v4.0.0` 이후 public poller 방향은 raw socket helper가 아니라 **service instance poller** 기준이다.
- `Gateway`/`Receiver`/`Spot`은 `Poller.AddGateway`, `Poller.AddReceiver`, `Poller.AddSpotPub`, `Poller.AddSpotSub`로 등록한다.
- `Gateway.CreateRouterSocket()` 또는 `SpotNode.GetPubSocket()/GetSubSocket()`는 internal/debug transport 경로로만 취급하고, perf poller 등록 대상으로 쓰지 않는다.
- raw socket helper를 직접 획득한 경우에만 pollable transport mode로 간주하며, 그때 facade I/O 호출은 `EFSM`이 정상이다.

**실행 단계(이번 리팩토링):**
1. Config/Result/Phase 개념을 파일 로컬 타입으로 명확화한다.
2. 긴 메서드를 `RunWarmup` / `RunSettle` / `RunActive` 단계로 분리한다.
3. 리소스 생명주기(`Context/Socket/Service`)를 상위 `Run()`에 명시한다.
4. 루프 내부 `try/catch` 난립을 helper 단으로 수렴하고 상위에서 일관 처리한다.
5. retry-budget 기반 stop-token 전송 루프를 제거한다.
6. 빌드/실행 검증 결과와 성능 안전성 체크리스트를 보고한다.

48. **[x] Dead Code / 미사용 파일 정리**
    - 사용되지 않는 using, 변수, 메서드, 클래스 전부 삭제
    - 의미 없는 주석 (TODO 잔재, 복사 흔적, 주석 처리된 코드) 전부 삭제
    - 빈 파일, 미사용 설정 파일 삭제
49. **[x] 가독성 리팩토링**
    - 메서드/변수 네이밍 일관성 검토 (core/perf 와 대응 관계 명확화)
    - 과도한 중첩 / 긴 메서드 분리 (단, 벤치마크 인라인 정책 범위 내)
    - 매직 넘버 → 상수 추출 (타임아웃, 버퍼 크기, 재시도 한도 등)
    - 패턴 파일 간 구조 일관성 확보 (동일 페이즈 순서, 동일 변수명 컨벤션)
50. **[x] 성능 리뷰 (벤치마크 오버헤드 제거)**
    - **불필요한 할당**: 측정 루프 내 `new byte[]`, `new string()`, 박싱 (`object`) 등
    - **불필요한 복사**: `ToArray()`, `Array.Copy` 가 회피 가능한 경우
    - **불필요한 대기**: 측정 루프 내 `Thread.Sleep`, busy-wait 이 과도한 경우
    - **GC 압박**: 측정 구간에서 단명 객체 반복 생성 여부 (Gen0 GC 유발)
    - **I/O 플러시**: `Console.WriteLine` 이 측정 루프 내에 포함되지 않는지 확인
    - **버퍼 재사용**: send/recv 버퍼가 루프 밖에서 1회 할당 후 재사용되는지 확인
    - **Span 활용**: `Span<byte>` / `stackalloc` 로 힙 할당 회피 가능한 곳 확인
    - 리뷰 체크리스트 (파일별):
      ```
      [x] 측정 루프 내 힙 할당 0건
      [x] 측정 루프 내 불필요한 복사 0건
      [x] 측정 루프 내 Thread.Sleep / 과도한 busy-wait 없음
      [x] 측정 루프 밖에서 I/O 출력
      [x] send/recv 버퍼 루프 밖 할당 + 재사용
      [x] 박싱/언박싱 없음 (value type 직접 사용)
      [x] Span<byte> / stackalloc 활용 최적화
      ```
51. **개선 사항 적용 후 주석 추가**
    - 리팩토링/성능 개선 완료 후, 코드 이해를 돕는 적절한 수준의 주석 추가
    - 주석 대상:
      - 각 패턴 파일 상단: 패턴 설명, 소켓 구성, 측정 방식 요약 (1-3줄)
      - 페이즈 전환 지점: `// --- Warmup ---`, `// --- Active measurement ---` 등
      - 비자명 로직: stop-token 처리, routing-id 관리, echo 루프 구조 등
      - 성능 관련 설계 결정: 버퍼 사전 할당 이유, DontWait 플래그 사용 이유 등
    - 주석 금지 대상:
      - 자명한 코드 (`i++`, `socket.Dispose()` 등)
      - API 호출 단순 설명 (XML Doc 참조로 충분한 경우)
      - 이력/변경 로그 스타일 주석

---

## 15. 정책 준수 사항

### 15.1 금지 사항

- **Native P/Invoke 직접 호출 금지**: `NativeMethods.*`, `NativeTypes.*`, `NativeHelpers.*` 일체 금지
- **internal 네임스페이스 접근 금지**: `Zlink.Native` 패키지 직접 참조 금지
- **Send retry 금지** (정책): send 실패 시 즉시 실패 처리, retry budget/fallback 으로 은닉 금지
- **Drain cap 금지** (정책): non-blocking drain 에 임의 cap/retry budget 을 두어 실패를 은닉하지 않음
- **Inflight/Outstanding 옵션 금지**: 백프레셔 한도 = 소켓 HWM 만

### 15.2 필수 사항

- 각 벤치마크 소스에 **소켓 생성, bind/connect, send/recv 루프, 페이즈 컨트롤** 인라인
- single send 경로는 **blocking send(backpressure 존중)** 로 구현
- multi send 경로는 **DontWait 1회 + pending 시 PollOut** 으로 구현
- single recv 경로는 **blocking recv + non-blocking drain** 구조
- multi recv 경로는 **poller + non-blocking drain(무제한, cap 없음)** 구조
- `SPOT` multi send 는 현재 service API 제약상 `Publish(DontWait)` 대신 `PollOut` readiness 확인 후 `Publish(None)`를 호출하는 예외 구현을 사용한다.
- `Gateway`/`Receiver`/`Spot` multi poller 대상은 raw socket helper가 아니라 service instance다
- `RESULT,current,...` 형식의 stdout 출력
- STREAM 서버는 stop-token `__zlink_perf_stop__` 수신 시 정상 종료
- Multi 서버는 `READY,<endpoint>` stdout 출력 후 클라이언트 대기
- TLS 인증서는 `bindings/dotnet/tests/certs/` 경로 사용

### 15.3 코드 인라이닝 정책

> **핵심 원칙 (core/perf 차이점)**: core/perf 는 C++ 파일 내 anonymous namespace 로 send/recv 헬퍼를
> 파일 로컬 함수로 정의한다. dotnet 에서도 동일하게 **각 패턴 파일 내에서만 send/recv 로직을 공통화**한다.
> 다른 패턴 파일과 공유하지 않는다. 이렇게 하면 각 패턴 파일이 **벤치마크 샘플 코드처럼 독립적으로 읽히며**,
> 핵심 측정 루프가 한 파일 안에서 완전히 파악된다.
> 특히 multi 는 core/perf 와 동일하게 `PerfDealerDealerServer.cs`, `PerfDealerDealerClient.cs` 같은 패턴별 파일 안에서만
> send/recv 루프를 정리한다(패턴 간 공용 send/recv 유틸 금지).
> 구현 상태: `single/src/*.cs`, `multi/src/*.cs` 는 패턴별 독립 클래스(`PerfPair`, `PerfDealerDealerClient` 등)로 구성하고,
> `common/` 의 `PerfRunner` 는 패턴 무관 공통 유틸만 제공한다.

**common/ 으로 추출 허용 (패턴 무관 유틸리티만)**:
- 환경변수 파싱 (`ParseEnv`, `ResolveWarmupCount`)
- `PrintResult` (RESULT 라인 stdout 출력)
- 엔드포인트 생성 (`EndpointFor`)
- 헤더 stamp/decode (`StampHeader`, `DecodeHeader`)
- `TimestampUs` (Stopwatch 기반 마이크로초)
- reservoir sampling (`ReservoirSample`, `ComputeLatencyStats`)
- TLS 인증서 경로 리졸버
- Multi 클라이언트 연결 준비/정리 보조 유틸 (`PerfClientHelpers`)

**패턴 파일 내부에만 정의 (다른 파일과 공유 금지)**:
- single send 루프 (warmup / active phase, blocking send, send retry 없음)
- multi send 루프 (`DontWait` 1회, pending send 발생 시에만 `PollOut` 등록)
- single recv 루프 (blocking recv + non-blocking drain)
- multi recv 루프 (poller + non-blocking drain, drain cap 없음)
- 메시지 파싱/매칭 (header decode + phase/run_id 검증)
- latency 수집 (sent_ts_us 기반 계산)

**Single 패턴 예시 (`PerfPair.cs` 구조)**:

```csharp
// PerfPair.cs — 파일 내 모든 send/recv 로직 자체 완결
internal static class PerfPair
{
    // ── 파일 로컬 헬퍼 (다른 패턴과 공유하지 않음) ──────────

    static int ReceiveOneMessage(
        Socket receiver, Span<byte> buffer, int flags,
        int expectedSize, out PerfHeader header, out bool headerOk)
    {
        // zlink_msg_recv → size 검증 → DecodeHeader
        // return: 1=수신, 0=EAGAIN, -1=오류
    }

    static bool RunOnewayPhase(
        Socket sender, Socket receiver,
        byte[] payload, int payloadSize, int msgSize,
        uint runId, ref ulong seq,
        PerfPhase phase, int warmupCount, int durationS,
        int recvTimeoutMs, QueueProbe? probe,
        out ulong received, out LatencyStats? latency)
    {
        // ── sender 스레드 ──
        //   active: while (now < deadline) { StampHeader → Send }
        //   warmup: for (i < warmupCount) { StampHeader → Send }

        // ── receiver 스레드 ──
        //   while (true) {
        //       rc = ReceiveOneMessage(blocking)
        //       if (rc > 0) { account latency; burst drain(DONTWAIT) }
        //       if (senderDone && idle > timeout) break
        //   }
    }

    // ── 진입점 ──────────────────────────────────────────────

    public static void Run(string transport, int msgSize, string libName)
    {
        // 1. Setup: ctx, socket(PAIR×2), bind/connect, settle
        // 2. Warmup: RunOnewayPhase(phase_warmup, warmupCount)
        // 3. Active: RunOnewayPhase(phase_active, durationS)
        // 4. PrintResult(throughput, bandwidth, latency, p95, p99)
    }
}
```

**Multi 서버 예시 (`PerfDealerDealerServer.cs` 구조)**:

```csharp
// PerfDealerDealerServer.cs — 파일 내 recv/relay 로직 자체 완결
internal static class PerfDealerDealerServer
{
    // ── 파일 로컬 헬퍼 ──────────────────────────────────────

    static RecvResult ReceiveOneMessage(
        Socket server, int flags, int expectedSize,
        uint expectedRunId, PerfPhase expectedPhase,
        bool countMessage, bool collectLatency,
        ref long msgCount, ref double latSum, ref long latCount,
        LatencySampler? sampler)
    {
        // zlink_msg_recv → decode_and_match_header → latency 계산
    }

    static bool DrainNonBlocking(
        Socket server, int expectedSize,
        uint expectedRunId, PerfPhase expectedPhase,
        bool countMessage, bool collectLatency,
        ref long msgCount, ref double latSum, ref long latCount,
        LatencySampler? sampler)
    {
        // while: ReceiveOneMessage(DONTWAIT) → recv_none 까지 반복
    }

    // ── 진입점 ──────────────────────────────────────────────

    public static void RunServer(string transport, int msgSize)
    {
        // 1. set_perf_pattern_env("DEALER_DEALER")
        // 2. bind, READY 출력, 연결 대기
        // 3. Warmup(duration) → Settle → Active(duration)
        //    각 페이즈에서 ReceiveOneMessage + DrainNonBlocking 직접 호출
        // 4. PrintResult
    }
}
```

**금지**: 패턴 간 `SendLoop()` / `RecvLoop()` 공유 메서드를 `common/` 에 만드는 것
**허용**: 패턴 내부에서 `ReceiveOneMessage` → `DrainNonBlocking` 등 파일 내 분할은 자유

- STREAM 서버 인프라: stop-token 파서 모듈화 허용 (`common/PerfStreamStopParser`), STREAM 소켓 헬퍼는 각 서버 파일에 인라인
- STREAM 벤치마크 클라이언트: C# 구현 금지, `core/perf/common/streamclient` C++ 공용 바이너리만 사용

---

## 16. 검증 계획

### 16.1 정적 검증

- Native P/Invoke / internal 패키지 직접 호출 금지 검사:
  ```bash
  # C# 소스에서 NativeMethods, NativeTypes, NativeHelpers, Zlink.Native 직접 호출이 없어야 함
  rg -n "\\bNativeMethods\\." bindings/dotnet/perf --glob '*.cs'
  rg -n "\\bNativeTypes\\." bindings/dotnet/perf --glob '*.cs'
  rg -n "\\bNativeHelpers\\." bindings/dotnet/perf --glob '*.cs'
  rg -n "\\bZlink\\.Native\\b" bindings/dotnet/perf --glob '*.cs'
  # 기대 결과: 각각 0건
  ```
- stream client 공유 경로 확인:
  ```bash
  rg -n "core/perf/common/streamclient" bindings/dotnet/perf/run_comparison.py
  # 기대 결과: 공용 경로만 참조
  ```
- TLS 인증서 경로 확인:
  ```bash
  rg -n "tests/certs" bindings/dotnet/perf --glob '*.cs'
  # 기대 결과: bindings/dotnet/tests/certs 경로만 사용, core/tests/certs 참조 없음
  ```

### 16.2 빌드 검증

- [x] `dotnet build perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release` 빌드 성공 (종료코드 0)
- [x] `dotnet build perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release` 빌드 성공 (종료코드 0)
- [x] `bindings/dotnet/tests/certs/` 인증서 파일 3개 존재 (server.crt, server.key, ca.crt)

### 16.3 기능 smoke 테스트

- single smoke:
  ```bash
  python3 bindings/dotnet/perf/run_comparison.py \
    --binding dotnet --suite single \
    --pattern PAIR --transports tcp --msg-sizes 64 \
    --runs 1 --duration 1 --reuse-build
  ```
- multi smoke:
  ```bash
  python3 bindings/dotnet/perf/run_comparison.py \
    --binding dotnet --suite multi \
    --pattern MULTI_DEALER_DEALER --transports tcp --msg-sizes 64 \
    --runs 1 --duration 1 --clients 10 --reuse-build
  ```
- stream smoke:
  ```bash
  python3 bindings/dotnet/perf/run_comparison.py \
    --binding dotnet --suite multi \
    --pattern MULTI_STREAM --transports tcp --msg-sizes 64 \
    --runs 1 --duration 1 --clients 100 --reuse-build
  ```
- TLS smoke:
  ```bash
  python3 bindings/dotnet/perf/run_comparison.py \
    --binding dotnet --suite single \
    --pattern PAIR --transports tls --msg-sizes 64 \
    --runs 1 --duration 1 --reuse-build
  ```

### 16.4 메트릭 정확성 검증

각 RESULT 라인의 메트릭 값이 논리적으로 정확한지 검증한다.

**필수 메트릭 존재 검증 (complete 판정 기준, 조합별):**

> **core 기준** (`core/perf/run_comparison.py:28`): 모든 바이너리는 **5개** 필수 메트릭을 출력한다:
> `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`

- **dotnet 정책**: core 기준과 동일하게 5개 메트릭 필수. 모든 pattern/transport/size 조합에 5개 메트릭이 존재해야 `complete` 판정
- dotnet 출력 단위: `latency`, `latency_p95`, `latency_p99` 는 **ms**
- single: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`
- multi: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`

> **`run_policy_bench.py` 참고**: 현재 `run_policy_bench.py` 는 `required_metrics = 5 if cfg.binding == "dotnet" else 3` 으로 바인딩별 분기가 있다.
> 이는 C++/Java 바인딩이 `latency_p95`/`latency_p99` 출력을 아직 구현하지 않은 **과도기적 예외**이며,
> core 기준(5개)이 정규 사양이다. dotnet 은 처음부터 core 기준에 맞춰 5개를 구현한다.

**정보성 메트릭 존재 검증 (품질 기준, 없어도 complete 판정에 영향 없음):**
- single: `cpu_pct`, `mem_mb`, `snd_pending_max`, `rcv_pending_max`, `rcv_pending_end`
- multi: `server_cpu_pct`, `server_mem_mb`, `client_cpu_pct`, `client_mem_mb`, `server_snd_pending_max`, `server_rcv_pending_max`, `server_rcv_pending_end`
- 정보성 메트릭이 누락되어도 테스트는 pass 처리하되, 누락 시 경고(warning) 로그를 남긴다

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
- `latency > 0` (유효한 레이턴시)
- single: `cpu_pct >= 0 && cpu_pct <= 100 × nCores`, `mem_mb > 0`
- multi: `server_cpu_pct >= 0 && server_cpu_pct <= 100 × nCores`, `client_cpu_pct >= 0 && client_cpu_pct <= 100 × nCores`
- multi: `server_mem_mb > 0`, `client_mem_mb > 0`

**검증 방법:**
```bash
# single smoke 결과 파일에서 RESULT 라인 추출 후 검증
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024 \
  --runs 1 --duration 2 --reuse-build --result

# 결과 파일에서 bandwidth 계산 검증
# RESULT,current,PAIR,tcp,64,throughput,X
# RESULT,current,PAIR,tcp,64,bandwidth,Y
# 검증: abs(Y - X * 64 / 1000000) / Y < 0.01
```

**multi 메트릭 검증 예시:**
```bash
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite multi \
  --pattern MULTI_DEALER_ROUTER --transports tcp --msg-sizes 64,1024 \
  --runs 1 --duration 2 --clients 10 --reuse-build --result

# echo 패턴이므로 bandwidth = throughput × size × 2 / 1,000,000
```

### 16.5 사이즈별 테이블 즉시 출력 검증

**동작 원리:**
- C# 벤치마크 바이너리가 측정 완료 시 `RESULT,current,...` 라인을 stdout 으로 출력한다.
- `run_policy_bench.py` 가 바이너리 stdout 을 실시간 파싱하여 RESULT 라인을 수집한다.
- 사용자에게는 pattern/transport/size 조합별로 순차적으로 결과가 표시된다.
- 모든 size 출력이 완료된 후에야 다음 transport 또는 pattern 으로 전환된다.

**검증 방법:**
```bash
# 3개 사이즈로 실행하고 콘솔 로그를 파일로 수집
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --duration 1 --reuse-build \
  --output bindings/dotnet/perf/results/single/report/size_progress.txt
```

**로그 검증 기준:**
1. `RESULT,current,PAIR,tcp,64,...` 라인이 먼저 출력됨
2. `RESULT,current,PAIR,tcp,1024,...` 라인이 그 다음 출력됨
3. `RESULT,current,PAIR,tcp,65536,...` 라인이 마지막에 출력됨
4. 각 사이즈의 5개 메트릭 (throughput, bandwidth, latency, latency_p95, latency_p99) 이 연속으로 출력됨
5. 모든 RESULT 라인이 완료 섹션 (`## Completion`) 보다 앞에 위치함

### 16.6 기본 설정 전체 실행 무실패 검증

기본 옵션(옵션 미지정)으로 single/multi 전체를 실행하여 모든 패턴이 실패 없이 완료되는지 확인한다.

**single 기본 실행:**
```bash
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite single --result
```

**multi 기본 실행:**
```bash
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite multi --result
```

**합격 기준:**
- [ ] 두 실행 모두 프로세스 종료코드 `0`
- [ ] **single** tmp 결과 파일(`results/single/tmp/perf_*.txt`): `META,status,complete` 이고 `META,expected == META,actual`
- [ ] **multi** tmp 결과 파일(`results/multi/tmp/perf_*.txt`): `META,status,complete` 이고 `META,expected == META,actual`
- [ ] 결과 파일/콘솔 로그에 `## Failures` 섹션이 없어야 함
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)

**기본 조합 수 예상 (single, Linux 기준):**
- socket 패턴 (6종: PAIR~ROUTER_ROUTER_POLL) × 6 transport (tcp,tls,ws,wss,inproc,ipc) × 6 size = 216 조합
- GATEWAY/SPOT (2종) × 4 transport (tcp,tls,ws,wss) × 6 size = 48 조합
- 총: **264** 조합 → UNSUPPORTED 제외한 나머지 전부 success

**기본 조합 수 예상 (multi):**
- 비-STREAM 패턴 (6종) × 4 transport × 6 size = 144 조합
- STREAM 패턴 (3종) × 4 transport × 4 size = 48 조합
- 총: 192 조합 → UNSUPPORTED 제외한 나머지 전부 success

**결과 파일 확인:**
```bash
# 결과 파일이 생성되었는지 확인
ls -la bindings/dotnet/perf/results/single/report/perf_*.txt
ls -la bindings/dotnet/perf/results/multi/report/perf_*.txt

# single tmp 결과 파일 completion 확인
head -20 bindings/dotnet/perf/results/single/tmp/perf_*.txt
# 기대 출력:
# META,status,complete
# META,expected,N
# META,actual,N

# multi tmp 결과 파일 completion 확인
head -20 bindings/dotnet/perf/results/multi/tmp/perf_*.txt
# 기대 출력:
# META,status,complete
# META,expected,N
# META,actual,N
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `src/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 별도 파일로 분리 (`*Server.cs` / `*Client.cs`).
- runner 옵션/기본값/결과 형식이 core 와 동등하게 동작.
- single/multi 모든 패턴 클래스가 `bindings/dotnet/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 Native P/Invoke / internal 네임스페이스 직접 호출 0건.
- TLS 인증서는 `bindings/dotnet/tests/certs/` 에서 독립 관리.
- retry 로직/우회 wrapper/비정책 실행 경로가 없음.
- **메트릭 정확성**(필수 메트릭 존재/bandwidth 계산식/값 범위)이 검증됨.
- **사이즈별 테스트 완료 시점마다** RESULT 행이 즉시 출력됨이 검증됨.
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- `run_policy_bench.py` dotnet 지원 확인: 빌드/실행 경로/메트릭 수집 정상 동작.
- **코드 품질 리뷰 완료**: dead code/미사용 주석 0건, 측정 루프 내 불필요한 할당/복사/대기 0건.
- **주석 정리 완료**: 패턴 설명, 페이즈 전환, 비자명 로직에 적절한 수준의 주석 추가.

---

## 18. 구현 체크리스트

> 각 항목을 순서대로 확인하며 진행한다. `[x]` 로 완료를 표시한다.
> 참조 섹션 번호를 함께 표기하여 상세 내용을 바로 찾을 수 있도록 한다.

---

### CL-0. 구조 철학 준수 확인 (착수 전 필수 확인)

- [x] **CL-0.1** single/multi 경계 분리: single/common 에 STREAM 헬퍼 없음 (§8.1)
- [x] **CL-0.2** STREAM 벤치마크 클라이언트: C# 구현 없음, `core/perf/common/streamclient` C++ 바이너리만 사용 (§3.3, §15.3)
- [x] **CL-0.3** 루트 `perf/common/` 역할: Python 스크립트 유틸 공간, streamclient 인프라 아님 (§1)
- [x] **CL-0.4** core/perf 구조 매핑 표에 `perf_metric_header.hpp` 포함 확인 (§1 매핑 표)
- [x] **CL-0.5** single 프로젝트 `common/` + `src/` 폴더 분리 (flat 배치 아님) (§1, §14 Phase 1)
- [x] **CL-0.6** multi 프로젝트 `common/` + `src/` 폴더 분리 확인 (§1)
- [x] **CL-0.7** `PerfStreamClient.cs` (C# raw transport client) 삭제 확인 — 파일 존재하면 안 됨 (§15.3)

---

### CL-1. 디렉토리 구조 (§1)

**single 파일 존재 확인:**

- [x] **CL-1.1** `single/Zlink.BindingBench/Zlink.BindingBench.csproj` 존재
- [x] **CL-1.2** `single/Zlink.BindingBench/GlobalUsings.cs` 존재
- [x] **CL-1.3** `single/Zlink.BindingBench/PerfMain.cs` 존재
- [x] **CL-1.4** `single/Zlink.BindingBench/common/PerfCommon.cs` 존재
- [x] **CL-1.5** `single/Zlink.BindingBench/common/PerfTls.cs` 존재
- [x] **CL-1.6** `single/Zlink.BindingBench/src/PerfPair.cs` 존재
- [x] **CL-1.7** `single/Zlink.BindingBench/src/PerfPubSub.cs` 존재
- [x] **CL-1.8** `single/Zlink.BindingBench/src/PerfDealerDealer.cs` 존재
- [x] **CL-1.9** `single/Zlink.BindingBench/src/PerfDealerRouter.cs` 존재
- [x] **CL-1.10** `single/Zlink.BindingBench/src/PerfRouterRouter.cs` 존재
- [x] **CL-1.11** `single/Zlink.BindingBench/src/PerfRouterRouterPoll.cs` 존재
- [x] **CL-1.12** `single/Zlink.BindingBench/src/PerfGateway.cs` 존재
- [x] **CL-1.13** `single/Zlink.BindingBench/src/PerfSpot.cs` 존재
- [x] **CL-1.14** single 프로젝트 루트에 패턴 파일 flat 배치 없음 (PerfPair.cs 등이 루트에 없어야 함)
- [x] **CL-1.15** `PerfStream.cs`, `PerfStreamCallbackEcho.cs` single 프로젝트에서 삭제됨 (STREAM 은 multi 전용 패턴이므로 single 에 불필요)

**multi 파일 존재 확인:**

- [x] **CL-1.16** `multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj` 존재
- [x] **CL-1.17** `multi/Zlink.BindingBench.Multi/GlobalUsings.cs` 존재
- [x] **CL-1.18** `multi/Zlink.BindingBench.Multi/PerfMain.cs` 존재
- [x] **CL-1.19** `multi/Zlink.BindingBench.Multi/common/PerfCommon.cs` 존재
- [x] **CL-1.20** `multi/Zlink.BindingBench.Multi/common/PerfCommonMulti.cs` 존재
- [x] **CL-1.21** `multi/Zlink.BindingBench.Multi/common/PerfServerEntry.cs` 존재
- [x] **CL-1.22** `multi/Zlink.BindingBench.Multi/common/PerfClientEntry.cs` 존재
- [x] **CL-1.23** `multi/Zlink.BindingBench.Multi/common/PerfClientHelpers.cs` 존재
- [x] **CL-1.24** `multi/Zlink.BindingBench.Multi/common/PerfTls.cs` 존재
- [x] **CL-1.25** `multi/Zlink.BindingBench.Multi/common/PerfStreamStopParser.cs` 존재
- [x] **CL-1.26** `multi/Zlink.BindingBench.Multi/common/PerfStreamClient.cs` 존재하지 않음 (삭제됨)
- [x] **CL-1.27** `multi/.../src/PerfDealerDealerServer.cs` 존재
- [x] **CL-1.28** `multi/.../src/PerfDealerDealerClient.cs` 존재
- [x] **CL-1.29** `multi/.../src/PerfDealerRouterServer.cs` 존재
- [x] **CL-1.30** `multi/.../src/PerfDealerRouterClient.cs` 존재
- [x] **CL-1.31** `multi/.../src/PerfRouterRouterServer.cs` 존재
- [x] **CL-1.32** `multi/.../src/PerfRouterRouterClient.cs` 존재
- [x] **CL-1.33** `multi/.../src/PerfPubSubServer.cs` 존재
- [x] **CL-1.34** `multi/.../src/PerfPubSubClient.cs` 존재
- [x] **CL-1.35** `multi/.../src/PerfGatewayServer.cs` 존재
- [x] **CL-1.36** `multi/.../src/PerfGatewayClient.cs` 존재
- [x] **CL-1.37** `multi/.../src/PerfSpotServer.cs` 존재
- [x] **CL-1.38** `multi/.../src/PerfSpotClient.cs` 존재
- [x] **CL-1.39** `multi/.../src/PerfStreamServer.cs` 존재
- [x] **CL-1.40** `multi/.../src/PerfStreamCallbackServer.cs` 존재
- [x] **CL-1.41** `multi/.../src/PerfStreamLen32BeServer.cs` 존재

**공통/스크립트/결과 디렉토리:**

- [x] **CL-1.42** `perf/.gitignore` 존재 (bin/, obj/ 제외)
- [x] **CL-1.43** `perf/results/single/report/` 디렉토리 존재
- [x] **CL-1.44** `perf/results/multi/report/` 디렉토리 존재

---

### CL-2. 빌드 시스템 (§2)

- [x] **CL-2.1** `Zlink.sln` 에 `Zlink.BindingBench` 프로젝트 참조 포함
- [x] **CL-2.2** `Zlink.sln` 에 `Zlink.BindingBench.Multi` 프로젝트 참조 포함
- [x] **CL-2.3** single csproj: `net8.0`, `Exe`, `AllowUnsafeBlocks=true`, Zlink.csproj 참조
- [x] **CL-2.4** multi csproj: `net8.0`, `Exe`, `AllowUnsafeBlocks=true`, Zlink.csproj 참조
- [x] **CL-2.5** single csproj 에 multi 프로젝트 참조 없음 (독립 빌드)
- [x] **CL-2.6** multi csproj 에 single 프로젝트 참조 없음 (독립 빌드)
- [x] **CL-2.7** `dotnet build perf/single/.../Zlink.BindingBench.csproj -c Release` 빌드 성공
- [x] **CL-2.8** `dotnet build perf/multi/.../Zlink.BindingBench.Multi.csproj -c Release` 빌드 성공

---

### CL-3. CLI 인터페이스 (§3)

**single CLI:**

- [ ] **CL-3.1** `Zlink.BindingBench <PATTERN> <TRANSPORT> <SIZE>` 형식 동작
- [ ] **CL-3.2** 지원 PATTERN 8종: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT
- [ ] **CL-3.3** 지원 TRANSPORT 6종: tcp, tls, ws, wss, inproc, ipc
- [ ] **CL-3.4** 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류

**multi CLI:**

- [ ] **CL-3.5** `--multi-server <PATTERN> <TRANSPORT> <SIZE>` 형식 동작
- [ ] **CL-3.6** `--multi-client <PATTERN> <TRANSPORT> <SIZE> --endpoint <ep>` 형식 동작
- [x] **CL-3.7** MULTI_ 접두사 strip 규약: run_policy_bench.py MULTI_DEALER_DEALER → 바이너리 DEALER_DEALER (§3.2.1)

**STREAM 클라이언트:**

- [x] **CL-3.8** STREAM 3종 서버: C# 구현 (AttachStreamRaw / AttachStreamLen32Be)
- [x] **CL-3.9** STREAM 3종 클라이언트: `core/perf/common/streamclient` C++ 바이너리 사용
- [x] **CL-3.10** C# 자체 STREAM 클라이언트 구현 없음 확인

**run_policy_bench.py dotnet 지원 확인:**

- [x] **CL-3.11** `binding_cmd_prefix()`: dotnet DLL 경로 지원 (이미 구현 — 경로 정합성 확인)
- [x] **CL-3.12** `binding_multi_role_command()`: server/client DLL 경로 분리 (이미 구현 — 경로 정합성 확인)
- [x] **CL-3.13** `build_binding_if_needed()`: `dotnet build -c Release` 호출 (이미 구현 — 빌드 동작 확인)

---

### CL-4. RESULT 출력 형식 (§4)

**필수 메트릭 (5개):**

- [x] **CL-4.1** `RESULT,current,<PAT>,<TR>,<SZ>,throughput,<v>` 출력
- [x] **CL-4.2** `RESULT,current,<PAT>,<TR>,<SZ>,bandwidth,<v>` 출력
- [x] **CL-4.3** `RESULT,current,<PAT>,<TR>,<SZ>,latency,<v>` 출력
- [x] **CL-4.4** `RESULT,current,<PAT>,<TR>,<SZ>,latency_p95,<v>` 출력
- [x] **CL-4.5** `RESULT,current,<PAT>,<TR>,<SZ>,latency_p99,<v>` 출력

**정보성 메트릭 (single):**

- [x] **CL-4.6** `cpu_pct`, `mem_mb` 출력
- [ ] **CL-4.7** `snd_pending_max`, `rcv_pending_max`, `rcv_pending_end` 출력

**정보성 메트릭 (multi):**

- [x] **CL-4.8** `server_cpu_pct`, `server_mem_mb` 출력
- [x] **CL-4.9** `client_cpu_pct`, `client_mem_mb` 출력
- [ ] **CL-4.10** `server_snd_pending_max`, `server_rcv_pending_max`, `server_rcv_pending_end` 출력

**Bandwidth 계산:**

- [x] **CL-4.11** single 전체: 승수 = 1.0
- [x] **CL-4.12** multi echo (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*): 승수 = 2.0
- [x] **CL-4.13** multi one-way (DEALER_DEALER, PUBSUB, SPOT): 승수 = 1.0

---

### CL-5. 환경 변수 (§5)

**single 환경 변수:**

- [x] **CL-5.1** `PERF_IO_THREADS` 파싱 (기본 0)
- [x] **CL-5.2** `PERF_WARMUP_COUNT` 파싱 (표준 1000, GATEWAY/SPOT 200)
- [x] **CL-5.3** `PERF_SINGLE_DURATION_SECONDS` 파싱 (기본 5)
- [ ] **CL-5.4** `PERF_SINGLE_LATENCY_SAMPLE_CAP` 파싱 (기본 200000) — reservoir sampling 최대 샘플 수
- [x] **CL-5.5** `PERF_SINGLE_HWM` / `PERF_SINGLE_SNDHWM` / `PERF_SINGLE_RCVHWM` 파싱
- [x] **CL-5.6** `PERF_SINGLE_SNDTIMEO_MS` / `PERF_SINGLE_RCVTIMEO_MS` 파싱 (기본 200)
- [x] **CL-5.7** `PERF_MAX_SOCKETS` 파싱

**multi 환경 변수:**

- [x] **CL-5.8** `PERF_CLIENTS` 파싱 (비-STREAM 100, STREAM 10000)
- [x] **CL-5.9** `PERF_WARMUP_SECONDS` 파싱 (기본 2)
- [x] **CL-5.10** `PERF_SETTLE_MS` 파싱 (기본 500)
- [x] **CL-5.11** `PERF_DURATION_SECONDS` 파싱 (기본 5)
- [x] **CL-5.12** `PERF_ACTIVE_WARMUP` 파싱 (기본 0)
- [x] **CL-5.13** `PERF_HWM` / `PERF_SNDHWM` / `PERF_RCVHWM` 파싱
- [x] **CL-5.14** `PERF_SNDTIMEO_MS` / `PERF_RCVTIMEO_MS` 파싱 (기본 200)
- [x] **CL-5.15** `PERF_CONNECT_READY_TIMEOUT_MS` 파싱 (기본 5000)
- [ ] **CL-5.16** `PERF_MONITOR_HWM` 파싱 (기본 1000)
- [x] **CL-5.17** `PERF_SERVER_BIND_PORT` 파싱 (기본 0)
- [x] **CL-5.18** `PERF_IO_THREADS` 파싱 (서버/클라이언트 공통, 기본 0)

---

### CL-6. 벤치마크 페이즈 (§6)

**single 페이즈:**

- [ ] **CL-6.1** Setup: bind/connect 후 `settle()` (100ms, `SETTLE_TIME_MS` 상수) — 별도 측정 페이즈가 아닌 연결 설정의 일부
- [ ] **CL-6.2** Warmup: count 기반 (시간 기반 아님), `PERF_WARMUP_COUNT` 적용, `phase_warmup` 라벨
- [ ] **CL-6.3** Active: duration 기반 throughput + reservoir sampling latency 동시 측정, `phase_active` 라벨
- [ ] **CL-6.4** Active 종료: sender 완료 후 receiver 가 recv timeout (기본 200ms) 동안 무수신 시 루프 종료 — 별도 drain 페이즈 없음
- [ ] **CL-6.5** Active 에서 `sent_ts_us` 기반 latency 측정 (single 에 `phase_drain` 없음)

**multi 페이즈:**

- [ ] **CL-6.6** Connect: N 클라이언트 + MonitorSocket 연결 확인
- [ ] **CL-6.7** Warmup: duration 기반, phase_warmup 라벨
- [ ] **CL-6.8** Settle: sleep, one-way=phase_drain / echo=phase_warmup 라벨
- [ ] **CL-6.9** Active: 라운드로빈 분산, phase_active 라벨, 메트릭 수집

---

### CL-7. 패턴별 구현 (§7)

**single 패턴 (8종: 6 소켓 + 2 서비스):**

- [ ] **CL-7.1** PerfPair: PAIR×2, echo, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.2** PerfPubSub: Pub+Sub, one-way, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.3** PerfDealerDealer: Dealer×2, echo, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.4** PerfDealerRouter: Dealer+Router, echo, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.5** PerfRouterRouter: Router×2, echo, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.6** PerfRouterRouterPoll: Router×2+Poller, echo, tcp/tls/ws/wss/inproc/ipc
- [ ] **CL-7.7** PerfGateway: Gateway+Receiver, echo, tcp/tls/ws/wss (inproc/ipc 미지원)
- [ ] **CL-7.8** PerfSpot: Spot pub/sub, one-way, tcp/tls/ws/wss (inproc/ipc 미지원), warmup clamp: `msg_size ≥ 65536` → max 20
**multi 패턴 (9종: 6 소켓/서비스 + 3 STREAM):**

- [ ] **CL-7.10** PerfDealerDealer: Server(DEALER bind, relay) + Client(DEALER connect, one-way)
- [ ] **CL-7.11** PerfDealerRouter: Server(ROUTER bind, echo) + Client(DEALER connect, send+recv)
- [ ] **CL-7.12** PerfRouterRouter: Server(ROUTER bind, echo) + Client(ROUTER connect, send+recv)
- [ ] **CL-7.13** PerfPubSub: Server(PUB bind, publish) + Client(SUB connect, recv)
- [ ] **CL-7.14** PerfGateway: Server(Receiver bind, echo) + Client(Gateway connect, send+recv)
- [ ] **CL-7.15** PerfSpot: Server(Spot publish) + Client(Spot subscribe)
- [ ] **CL-7.16** PerfStream: Server(STREAM bind, raw echo) + Client(C++ 바이너리)
- [ ] **CL-7.17** PerfStreamCallback: Server(AttachStreamRaw callback) + Client(C++ 바이너리)
- [ ] **CL-7.18** PerfStreamLen32Be: Server(AttachStreamLen32Be) + Client(C++ 바이너리)
- [ ] **CL-7.19** server/client 별도 파일 분리 확인 (각 패턴 *Server.cs / *Client.cs)

**multi 서버 프로토콜:**

- [ ] **CL-7.20** 서버 stdout `READY,<endpoint>` 출력
- [ ] **CL-7.21** 클라이언트 종료 시 stop-token 전송
- [ ] **CL-7.22** STREAM 서버 stop-token: `__zlink_perf_stop__`
- [ ] **CL-7.23** 서버 graceful shutdown 후 RESULT 출력

---

### CL-8. 공통 유틸리티 (§8)

**single common/ (PerfCommon.cs):**

- [ ] **CL-8.1** `ParseEnv` / `ParseEnvNonNegative` 환경변수 파싱
- [ ] **CL-8.2** `ApplySingleContextOptions` / `ApplySingleSocketOptions` 소켓 옵션
- [ ] **CL-8.3** `ReceiveBlocking` + `TryReceiveNonBlocking` + `DrainRemainingFramesNonBlocking` + `SendBlocking`
- [ ] **CL-8.4** `WaitForInput` / `WaitUntil` 폴링
- [ ] **CL-8.5** `EndpointFor` 엔드포인트 생성
- [ ] **CL-8.6** `PrintResult` RESULT 출력 (bandwidth 승수 = 1.0)
- [ ] **CL-8.7** `StampHeader` / `DecodeHeader` BinaryPrimitives 기반 헤더
- [ ] **CL-8.8** `ReservoirSample` / `ComputeLatencyStats` latency 통계 (cap: `PERF_SINGLE_LATENCY_SAMPLE_CAP`, 기본 200000)
- [ ] **CL-8.9** `TimestampUs` Stopwatch 기반 마이크로초
- [ ] **CL-8.10** STREAM 헬퍼(StreamSend/Recv/ConnectEvent) 가 single/common 에 없음 확인
- [ ] **CL-8.11** `GatewayReceiveProviderMessage` 헬퍼

**single common/ (PerfTls.cs):**

- [ ] **CL-8.12** `ConfigureTlsServerIfNeeded` / `ConfigureTlsClientIfNeeded`
- [ ] **CL-8.13** `TryResolvePerfTlsPaths` — `bindings/dotnet/tests/certs/` 기준 탐색

**multi common/ (PerfCommon.cs):**

- [ ] **CL-8.14** single PerfCommon 유틸 중 필요 항목 포함 + multi 전용 기능

**multi common/ (PerfCommonMulti.cs):**

- [ ] **CL-8.15** `ResolveClients` (비-STREAM 100, STREAM 10000)
- [ ] **CL-8.16** `ResolveHwm` (비-STREAM 100, STREAM 10)
- [ ] **CL-8.17** `ResolveWarmupSeconds` / `ResolveDurationSeconds` / `ResolveSettleMs`

**multi common/ (PerfClientHelpers.cs):**

- [ ] **CL-8.18** `IsSupportedTransport` / `ParseEndpointArg`
- [ ] **CL-8.19** `WaitAllClientConnectReady` MonitorSocket 기반
- [ ] **CL-8.20** `PerfClientHelpers` 는 연결 준비/정리 보조만 포함 (공통 send/recv 측정 루프 없음)

**multi common/ (PerfServerEntry.cs / PerfClientEntry.cs):**

- [ ] **CL-8.21** 서버: `SetEnvironmentVariable("PERF_PATTERN", pattern)` + 패턴 디스패치 → RunServer + CPU/MEM 메트릭 + RESULT 출력
- [ ] **CL-8.22** 클라이언트: `SetEnvironmentVariable("PERF_PATTERN", pattern)` + 패턴 디스패치 → RunClient + CPU/MEM 메트릭 + RESULT 출력

**multi common/ (PerfTls.cs):**

- [ ] **CL-8.23** `bindings/dotnet/tests/certs/` 기준 상위 디렉토리 순회 탐색

**multi common/ (PerfStreamStopParser.cs):**

- [ ] **CL-8.24** len32be stop-token 파서 동작

---

### CL-9. TLS 인증서 관리 (§9)

- [ ] **CL-9.1** `bindings/dotnet/tests/certs/server.crt` 존재
- [ ] **CL-9.2** `bindings/dotnet/tests/certs/server.key` 존재
- [ ] **CL-9.3** `bindings/dotnet/tests/certs/ca.crt` 존재
- [ ] **CL-9.4** 상위 디렉토리 순회 탐색 로직 구현 (§9.2)
- [ ] **CL-9.5** Socket option API 로 TlsCert/TlsKey/TlsCa 설정 (§9.3)
- [ ] **CL-9.6** Gateway SetTlsClient / Receiver SetTlsServer API 사용 (§9.3)
- [ ] **CL-9.7** `core/tests/certs/` 참조 없음 확인 (바인딩 독립 관리)

---

### CL-10. C# API 매핑 (§10)

- [ ] **CL-10.1** Context, Socket, Bind, Connect, Send, Receive API 사용
- [ ] **CL-10.2** SetOption / GetOption API 사용
- [ ] **CL-10.3** Poller.Poll API 사용 (ROUTER_ROUTER_POLL)
- [ ] **CL-10.4** STREAM: AttachStreamRaw / AttachStreamLen32Be / DetachStream / StreamSend API 사용
- [ ] **CL-10.5** Gateway / Receiver / Spot 서비스 API 사용
- [ ] **CL-10.6** MonitorSocket 사용 (multi connect ready)
- [ ] **CL-10.7** Message 생성/읽기 API 사용
- [ ] **CL-10.8** Span/ReadOnlySpan zero-copy 활용
- [ ] **CL-10.9** BinaryPrimitives 헤더 인코딩
- [ ] **CL-10.10** Stopwatch.GetTimestamp() 고정밀 타이밍

---

### CL-11. 리소스 메트릭 수집 (§11)

- [ ] **CL-11.1** CPU 사용률: Process.TotalProcessorTime 기반 계산 구현
- [ ] **CL-11.2** 메모리 사용량: Process.WorkingSet64 기반 계산 구현

---

### CL-12. 스크립트 (§12)

**루트 스크립트:**

- [ ] **CL-12.1** `perf/run_benchmarks.sh` 존재, `run_comparison.py` → `run_policy_bench.py --binding dotnet --suite single` 체인 호출
- [ ] **CL-12.2** `perf/run_benchmarks.ps1` 존재
- [ ] **CL-12.3** `perf/run_benchmarks_multi.sh` 존재, `run_comparison.py` → `run_policy_bench.py --binding dotnet --suite multi` 체인 호출
- [ ] **CL-12.4** `perf/run_benchmarks_multi.ps1` 존재

**single 스크립트:**

- [ ] **CL-12.5** `perf/single/run_benchmarks.sh` 존재
- [ ] **CL-12.6** `perf/single/run_benchmarks.ps1` 존재
- [ ] **CL-12.7** `perf/single/run_comparison.py` 존재

**multi 스크립트:**

- [ ] **CL-12.8** `perf/multi/run_benchmarks.sh` 존재
- [ ] **CL-12.9** `perf/multi/run_benchmarks.ps1` 존재
- [ ] **CL-12.10** `perf/multi/run_comparison.py` 존재

**CLI 옵션:**

- [ ] **CL-12.11** --pattern, --runs, --duration, --reuse-build, --clean-build 지원
- [ ] **CL-12.12** --output, --results-dir, --results-tag 지원
- [ ] **CL-12.13** --msg-sizes, --transports, --hwm, --sndtimeo, --rcvtimeo 지원
- [ ] **CL-12.14** multi 전용: --clients, --warmup, --transport-transition-ms, --pattern-transition-ms 지원

---

### CL-13. 산출물 (§13)

- [ ] **CL-13.1** single 결과 파일 경로: `results/single/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt`
- [ ] **CL-13.2** multi 결과 파일 경로: `results/multi/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt`
- [ ] **CL-13.3** 결과 파일 형식: 테이블 + 완료 상태 요약
- [ ] **CL-13.4** single 완료 형식: `## Completion`, `expected_result_lines` / `actual_result_lines` (밑줄)
- [ ] **CL-13.5** multi 완료 형식: `## Status Summary`, `expected result lines` / `actual result lines` (공백)

---

### CL-14. 구현 순서 — Phase 실행 (§14)

**Phase 0: 인증서 및 스크립트 준비**

- [ ] **CL-14.1** `bindings/dotnet/tests/certs/` 인증서 3개 확인
- [ ] **CL-14.2** `run_policy_bench.py` dotnet 지원 확인 (이미 구현됨 — 경로/빌드 정합성 검증)
- [ ] **CL-14.3** `.gitignore` 파일 생성

**Phase 1: 인프라**

- [ ] **CL-14.4** [마이그레이션] single flat → common/src 분리 완료
  - [ ] PerfCommon.cs, PerfTls.cs → common/
  - [ ] 패턴 파일 → src/
  - [ ] PerfStream.cs, PerfStreamCallbackEcho.cs 삭제 (STREAM 은 multi 전용 패턴)
  - [ ] PerfMain.cs, GlobalUsings.cs → 프로젝트 루트 유지
- [ ] **CL-14.5** single csproj 생성 (또는 업데이트)
- [ ] **CL-14.6** multi csproj 생성 (또는 업데이트)
- [ ] **CL-14.7** Zlink.sln 에 perf 프로젝트 추가
- [ ] **CL-14.8** single/common/PerfCommon.cs 구현
- [ ] **CL-14.9** single/common/PerfTls.cs 구현
- [ ] **CL-14.10** single PerfMain.cs 패턴 디스패치 구현
- [ ] **CL-14.11** multi/common/PerfCommon.cs 구현
- [ ] **CL-14.12** multi/common/PerfCommonMulti.cs 구현
- [ ] **CL-14.13** multi/common/PerfClientHelpers.cs 구현
- [ ] **CL-14.14** multi/common/PerfServerEntry.cs 구현
- [ ] **CL-14.15** multi/common/PerfClientEntry.cs 구현
- [ ] **CL-14.16** multi/common/PerfTls.cs 구현
- [ ] **CL-14.17** multi PerfMain.cs --multi-server/--multi-client 디스패치 구현

**Phase 2: Single 소켓 패턴 (6개)**

- [ ] **CL-14.18** PerfPair.cs 구현
- [ ] **CL-14.19** PerfPubSub.cs 구현
- [ ] **CL-14.20** PerfDealerDealer.cs 구현
- [ ] **CL-14.21** PerfDealerRouter.cs 구현
- [ ] **CL-14.22** PerfRouterRouter.cs 구현
- [ ] **CL-14.23** PerfRouterRouterPoll.cs 구현

**Phase 3: Single 서비스 패턴 (2개)**

- [ ] **CL-14.24** PerfGateway.cs 구현
- [ ] **CL-14.25** PerfSpot.cs 구현

**Phase 4: Multi 패턴 (6×Server + 6×Client + 3 서버 only + 1 파서)**

- [ ] **CL-14.26** PerfDealerDealerServer.cs + Client.cs 구현
- [ ] **CL-14.27** PerfDealerRouterServer.cs + Client.cs 구현
- [ ] **CL-14.28** PerfRouterRouterServer.cs + Client.cs 구현
- [ ] **CL-14.29** PerfPubSubServer.cs + Client.cs 구현
- [ ] **CL-14.30** PerfGatewayServer.cs + Client.cs 구현
- [ ] **CL-14.31** PerfSpotServer.cs + Client.cs 구현
- [ ] **CL-14.32** PerfStreamServer.cs 구현 (서버 only)
- [ ] **CL-14.33** PerfStreamCallbackServer.cs 구현 (서버 only)
- [ ] **CL-14.34** PerfStreamLen32BeServer.cs 구현 (서버 only)
- [ ] **CL-14.35** PerfStreamStopParser.cs 구현

**Phase 5: 스크립트 및 마무리**

- [ ] **CL-14.36** run_benchmarks.sh / .ps1 (루트 + single/ + multi/) 구현
- [ ] **CL-14.37** run_benchmarks_multi.sh / .ps1 구현
- [ ] **CL-14.38** run_comparison.py 호환 어댑터 구현 (run_policy_bench.py 위임)
- [ ] **CL-14.39** README.md 작성
- [x] **CL-14.40** 빌드 검증 통과 (single + multi)
- [ ] **CL-14.41** run_policy_bench.py → run_comparison.py 체인 통합 검증 통과

**Phase 6: 코드 품질 리뷰 및 리팩토링**

- [x] **CL-14.42** Dead code / 미사용 파일 정리
  - [x] 미사용 using 0건
  - [x] 미사용 변수/메서드/클래스 0건
  - [x] 의미 없는 주석 0건
  - [x] 빈 파일/미사용 설정 파일 0건
- [x] **CL-14.43** 가독성 리팩토링
  - [x] 네이밍 일관성 (core/perf 대응 관계)
  - [x] 매직 넘버 → 상수 추출
  - [x] 패턴 파일 간 구조 일관성
- [x] **CL-14.44** 성능 리뷰 (파일별)
  - [x] 측정 루프 내 힙 할당 0건
  - [x] 측정 루프 내 불필요한 복사 0건
  - [x] 측정 루프 내 Thread.Sleep / 과도한 busy-wait 없음
  - [x] 측정 루프 밖에서 I/O 출력
  - [x] send/recv 버퍼 루프 밖 할당 + 재사용
  - [x] 박싱/언박싱 없음
  - [x] Span / stackalloc 활용
- [ ] **CL-14.45** 주석 추가
  - [ ] 패턴 파일 상단 설명 (1-3줄)
  - [ ] 페이즈 전환 지점 주석
  - [ ] 비자명 로직 주석
  - [ ] 자명한 코드에 불필요한 주석 없음

---

### CL-15. 정책 준수 (§15)

**금지 사항:**

- [x] **CL-15.1** NativeMethods / NativeTypes / NativeHelpers 직접 호출 0건
- [x] **CL-15.2** Zlink.Native 네임스페이스 참조 0건
- [x] **CL-15.3** send retry 없음 + drain cap/retry budget 없음 (실패 은닉 금지)
- [x] **CL-15.4** Inflight/Outstanding 옵션 사용 0건

**필수 사항:**

- [x] **CL-15.5** 소켓 생성, bind/connect, send/recv 루프, 페이즈 컨트롤 인라인
- [x] **CL-15.6** `RESULT,current,...` 형식 stdout 출력
- [x] **CL-15.7** STREAM 서버 stop-token `__zlink_perf_stop__` 처리
- [x] **CL-15.8** Multi 서버 `READY,<endpoint>` stdout 출력
- [x] **CL-15.9** TLS 인증서 `bindings/dotnet/tests/certs/` 경로 사용

**코드 인라이닝 정책:**

- [x] **CL-15.10** 메인 루프 로직 각 패턴 파일에 인라인
- [x] **CL-15.11** common/ 추출 범위: 환경변수, PrintResult, 엔드포인트, 헤더, TimestampUs, ReservoirSample/ComputeLatencyStats, TLS 경로 리졸버, PerfClientHelpers(연결/정리 보조만)
- [x] **CL-15.12** 패턴 간 SendLoop()/RecvLoop() 공유 메서드 common/ 에 없음 (금지)
- [x] **CL-15.13** Multi 클라이언트 → PerfClientHelpers 는 연결/정리만 위임, send/recv 루프는 각 패턴 파일 내부 유지
- [x] **CL-15.14** STREAM stop-token 파서 → PerfStreamStopParser 모듈화
- [x] **CL-15.15** STREAM 소켓 헬퍼 → 각 서버 파일에 인라인
- [x] **CL-15.16** STREAM 벤치마크 클라이언트 C# 구현 없음
- [x] **CL-15.17** single send 경로는 blocking send 1회 호출 기준, send retry 없음
- [x] **CL-15.18** single recv 구조: blocking recv + non-blocking drain
- [x] **CL-15.19** multi recv 구조: poller + non-blocking drain(무제한, cap 없음)
- [x] **CL-15.20** multi send 구조: `DontWait` 1회 시도 + pending send 발생 시에만 `PollOut` ON
- [x] **CL-15.21** multi `PollOut` 상시 등록 없음, pending drain 완료 시 즉시 OFF

---

### CL-16. 검증 (§16)

**정적 검증:**

- [x] **CL-16.1** `rg NativeMethods` / `NativeTypes` / `NativeHelpers` / `Zlink.Native` → 각 0건
- [ ] **CL-16.2** `rg core/perf/common/streamclient` run_comparison.py → 공용 경로 참조 확인
- [x] **CL-16.3** `rg tests/certs` *.cs → `bindings/dotnet/tests/certs` 만 사용

**빌드 검증:**

- [x] **CL-16.4** single `dotnet build -c Release` 종료코드 0
- [x] **CL-16.5** multi `dotnet build -c Release` 종료코드 0
- [x] **CL-16.6** 인증서 파일 3개 존재 (server.crt, server.key, ca.crt)

**기능 smoke 테스트:**

- [x] **CL-16.7** single smoke: PAIR / tcp / 64 → 성공
- [x] **CL-16.8** multi smoke: MULTI_DEALER_DEALER / tcp / 64 / 10 clients → 성공
- [ ] **CL-16.9** stream smoke: MULTI_STREAM / tcp / 64 / 100 clients → 성공
- [ ] **CL-16.10** TLS smoke: PAIR / tls / 64 → 성공

**메트릭 정확성:**

- [ ] **CL-16.11** 필수 메트릭 5개 모든 조합 존재 (single)
- [ ] **CL-16.12** 필수 메트릭 5개 모든 조합 존재 (multi)
- [ ] **CL-16.13** bandwidth 계산식 오차 ±1% 이내
- [ ] **CL-16.14** latency_p95 >= latency, latency_p99 >= latency_p95
- [ ] **CL-16.15** throughput > 0, bandwidth > 0, latency > 0
- [ ] **CL-16.16** 정보성 메트릭 누락 시 warning 로그

**사이즈별 즉시 출력:**

- [ ] **CL-16.17** size 완료마다 RESULT 행 즉시 stdout 출력
- [ ] **CL-16.18** 사이즈별 5개 메트릭 연속 출력
- [ ] **CL-16.19** 모든 RESULT 라인이 Completion 섹션보다 앞에 위치

**기본 설정 전체 실행:**

- [ ] **CL-16.20** single 전체 실행 종료코드 0, status: complete
- [ ] **CL-16.21** multi 전체 실행 종료코드 0, status: complete
- [ ] **CL-16.22** `## Failures` 섹션 없음
- [ ] **CL-16.23** fail 조합 0건
- [ ] **CL-16.24** UNSUPPORTED 조합은 정책 범위 내에서만 허용

---

### CL-17. 완료 기준 최종 확인 (§17)

- [ ] **CL-17.1** 디렉토리/파일 구조가 core/perf common/src 분리 구조와 동일
- [x] **CL-17.2** multi server/client 별도 파일 분리
- [ ] **CL-17.3** runner 옵션/기본값/결과 형식이 core 와 동등
- [x] **CL-17.4** single/multi 모든 패턴 빌드 성공
- [ ] **CL-17.5** STREAM client = core/perf/common/streamclient 공용 바이너리만 사용
- [x] **CL-17.6** Native P/Invoke / internal 네임스페이스 직접 호출 0건
- [x] **CL-17.7** TLS 인증서 bindings/dotnet/tests/certs/ 독립 관리
- [ ] **CL-17.8** retry 로직/우회 wrapper/비정책 실행 경로 없음
- [ ] **CL-17.9** 메트릭 정확성 검증 완료
- [ ] **CL-17.10** 사이즈별 RESULT 즉시 출력 검증 완료
- [ ] **CL-17.11** 기본 설정 전체 실행 status: complete
- [x] **CL-17.12** run_policy_bench.py dotnet 빌드/실행 경로 지원 확인
- [ ] **CL-17.13** 코드 품질 리뷰 완료 (dead code 0건, 측정 루프 오버헤드 0건)
- [ ] **CL-17.14** 주석 정리 완료
