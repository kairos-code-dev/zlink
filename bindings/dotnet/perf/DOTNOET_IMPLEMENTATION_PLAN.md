# DotNet Perf Benchmark Implementation Plan

> core/perf (C++) 벤치마크를 bindings/dotnet/perf 로 1:1 포팅한다.
> **C# binding API (`Zlink.*`, `Zlink.Service.*`) 만 사용하며, Native P/Invoke 직접 호출은 절대 금지.**
> STREAM 클라이언트는 공통 바이너리 `core/perf/common/streamclient` 를 재사용한다.

---

## 1. 디렉토리 구조

core/perf 의 `common/` + `current/` 분리 구조를 C# 프로젝트 컨벤션으로 그대로 반영한다.

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
    ├── .gitignore                          ← bin/, obj/, tmp/ 제외
    │
    ├── single/
    │   ├── Zlink.BindingBench/
    │   │   ├── Zlink.BindingBench.csproj   ← .NET 8.0 콘솔 프로젝트
    │   │   ├── GlobalUsings.cs             ← 공통 using 선언
    │   │   ├── PerfMain.cs                 ← 진입점 (pattern, transport, size)
    │   │   ├── common/                     ← ★ core/perf/single/common/ 대응
    │   │   │   ├── PerfCommon.cs           ← 공통 유틸 (retry, PrintResult, 엔드포인트 등)
    │   │   │   └── PerfTls.cs             ← TLS 인증서 경로 리졸버 (single)
    │   │   └── current/                    ← ★ core/perf/single/current/ 대응
    │   │       ├── PerfPair.cs
    │   │       ├── PerfPubSub.cs
    │   │       ├── PerfDealerDealer.cs
    │   │       ├── PerfDealerRouter.cs
    │   │       ├── PerfRouterRouter.cs
    │   │       ├── PerfRouterRouterPoll.cs
    │   │       ├── PerfGateway.cs
    │   │       └── PerfSpot.cs
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
    │   │   │   ├── PerfMultiCommon.cs      ← Multi 설정 리졸버
    │   │   │   ├── PerfMultiServerEntry.cs ← 서버 디스패처 + CPU/MEM
    │   │   │   ├── PerfMultiClientEntry.cs ← 클라이언트 디스패처 + CPU/MEM
    │   │   │   ├── PerfMultiClientHelpers.cs ← 공통 client 루프 헬퍼 (was PerfMultiClientHelpers)
    │   │   │   ├── PerfMultiTls.cs         ← TLS 인증서 경로 리졸버
    │   │   │   ├── PerfMultiStreamClient.cs  ← Raw transport stream client
    │   │   │   └── PerfMultiStreamStopParser.cs ← len32be stop-token 파서
    │   │   └── current/                    ← ★ core/perf/multi/current/ 대응
    │   │       ├── PerfMultiDealerDealer.cs     ← ★ server/client 동일 파일 내 분리
    │   │       ├── PerfMultiDealerRouter.cs
    │   │       ├── PerfMultiRouterRouter.cs
    │   │       ├── PerfMultiPubSub.cs
    │   │       ├── PerfMultiGateway.cs
    │   │       ├── PerfMultiSpot.cs
    │   │       ├── PerfMultiStream.cs           ← 서버 only (클라이언트=공통 stream client)
    │   │       ├── PerfMultiStreamCallback.cs
    │   │       └── PerfMultiStreamLen32Be.cs
    │   ├── run_benchmarks.sh
    │   ├── run_benchmarks.ps1
    │   └── run_comparison.py
    │
    ├── common/
    │   └── PerfComparisonBase.py           ← single/multi 공통 Python 유틸 (선택)
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
    ├── run_benchmarks.sh                   ← 루트 single 래퍼
    ├── run_benchmarks.ps1
    ├── run_benchmarks_multi.sh             ← 루트 multi 래퍼
    ├── run_benchmarks_multi.ps1
    └── run_comparison.py                   ← 루트 오케스트레이터
```

### core/perf 대비 구조 매핑

| core/perf | dotnet/perf | 비고 |
|-----------|-------------|------|
| `single/common/bench_common.hpp` | `single/.../common/PerfCommon.cs` | static 유틸 클래스 |
| `single/common/perf_single_metric_header.hpp` | (해당없음 — C# 은 BinaryPrimitives 로 인라인 처리) | |
| `single/current/perf_pair.cpp` | `single/.../current/PerfPair.cs` | 1:1 매핑 |
| `single/current/perf_dealer_dealer.cpp` | `single/.../current/PerfDealerDealer.cs` | 1:1 매핑 |
| `multi/common/perf_common.hpp` | `multi/.../common/PerfCommon.cs` | 패키지 분리 |
| `multi/common/perf_common_multi.hpp` | `multi/.../common/PerfMultiCommon.cs` | |
| `multi/common/perf_multi_client_helpers.hpp` | `multi/.../common/PerfMultiClientHelpers.cs` | |
| `multi/common/perf_multi_entry.hpp` | `multi/.../common/PerfMultiServerEntry.cs` + `PerfMultiClientEntry.cs` | |
| `multi/current/perf_multi_dealer_dealer_server.cpp` | `multi/.../current/PerfMultiDealerDealer.cs` (RunServer) | ★ server/client 동일 파일 |
| `multi/current/perf_multi_dealer_dealer_client.cpp` | `multi/.../current/PerfMultiDealerDealer.cs` (RunClient) | |
| `multi/current/perf_multi_stream_server.cpp` | `multi/.../current/PerfMultiStream.cs` | 서버 only |

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

- `run_comparison.py` 가 동일한 명령으로 빌드를 호출한다.
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

- **PATTERN**: `PAIR | PUBSUB | DEALER_DEALER | DEALER_ROUTER | ROUTER_ROUTER | ROUTER_ROUTER_POLL | GATEWAY | SPOT`
- **TRANSPORT**: `tcp | tls | ws | wss | inproc | ipc`
- **SIZE**: 양의 정수 (바이트)
- 종료코드: 0=성공, 1=인자 오류, 2=런타임 오류

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

`run_comparison.py` 의 `--pattern` 인자와 C# 바이너리의 `<PATTERN>` 인자는 네이밍이 다르다.

| run_comparison.py `--pattern` | C# 바이너리 `<PATTERN>` | 비고 |
|-------------------------------|------------------------|------|
| `MULTI_DEALER_DEALER` | `DEALER_DEALER` | `MULTI_` 접두사 strip |
| `MULTI_DEALER_ROUTER` | `DEALER_ROUTER` | |
| `MULTI_ROUTER_ROUTER` | `ROUTER_ROUTER` | |
| `MULTI_PUBSUB` | `PUBSUB` | |
| `MULTI_GATEWAY` | `GATEWAY` | |
| `MULTI_SPOT` | `SPOT` | |
| `MULTI_STREAM` | `STREAM` | |
| `MULTI_STREAM_CALLBACK` | `STREAM_CALLBACK` | |
| `MULTI_STREAM_LEN32BE` | `STREAM_LEN32BE` | |

**규칙**: `run_comparison.py` 는 `--suite multi` 일 때 `MULTI_` 접두사를 사용하여 single 패턴과 구분한다. 내부적으로 C# 바이너리 호출 시 `MULTI_` 접두사를 strip 하고 나머지를 `<PATTERN>` 인자로 전달한다.

### 3.3 STREAM 패턴 클라이언트

STREAM, STREAM_CALLBACK, STREAM_LEN32BE 패턴은:
- **서버**: C# 벤치마크가 직접 구현 (Socket.AttachStreamRaw / AttachStreamLen32Be API)
- **클라이언트**: `core/perf/common/streamclient/build/perf_stream_client` (C++ 공통 바이너리) 사용
- `run_comparison.py` 가 자동으로 공통 stream client 를 호출한다.

### 3.4 run_comparison.py 필수 수정 사항

현재 `run_comparison.py` 에 dotnet 관련 수정이 필요한 항목:

| 위치 | 현재 | 수정 필요 |
|------|------|----------|
| `binding_cmd_prefix()` | C++ 바이너리 직접 실행 | `dotnet run --project ... -c Release --` 또는 빌드 후 바이너리 경로 |
| `binding_multi_role_command()` | 동일 | server/client 바이너리 경로 분리 |
| `build_binding_if_needed()` | C++ CMake 빌드 | `dotnet build -c Release` 호출 |

---

## 4. RESULT 출력 형식 (core/perf 동일)

```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,throughput,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,bandwidth,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p95,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,latency_p99,<value>
```

Single 정보성 메트릭 (없어도 complete 판정에 영향 없음):
```
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,cpu_pct,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,mem_mb,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,snd_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,rcv_pending_max,<value>
RESULT,current,<PATTERN>,<TRANSPORT>,<SIZE>,rcv_pending_end,<value>
```

Multi 정보성 메트릭 (없어도 complete 판정에 영향 없음, PerfMultiServerEntry / PerfMultiClientEntry 에서):
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
| **Single 전체** | **1.0** | run_comparison.py 기준 모든 single 은 one-way 방향 |
| Multi echo (DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, STREAM*) | 2.0 | 요청+응답 양방향 |
| Multi one-way (DEALER_DEALER, PUBSUB, SPOT) | 1.0 | 단방향 |

---

## 5. 환경 변수 (core/perf 동일)

### 5.1 Single

| 변수 | 기본값 | 용도 |
|------|--------|------|
| `PERF_IO_THREADS` | 0 (기본) | Context IO 스레드 |
| `PERF_WARMUP_COUNT` | 패턴별 (표준: 1000, GATEWAY/SPOT: 200) | 웜업 메시지 횟수 (count 기반, 시간 기반 아님) |
| `PERF_SINGLE_DURATION_SECONDS` | 5 | 활성 측정 기간 |
| `PERF_LAT_COUNT` | 500 (GATEWAY,SPOT: 200) | 레이턴시 reservoir sampling 캡 |
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

---

## 6. 벤치마크 페이즈 (core/perf 동일)

### 6.1 Single 페이즈

```
[Warmup(count)] → [Settle(100ms)] → [Active(duration)] → [Drain idle]
```

1. **Warmup** (`PERF_WARMUP_COUNT`, 표준: 1000, GATEWAY/SPOT: 200): 고정 횟수 send/recv 반복으로 워밍업 (시간 기반이 아님)
2. **Settle** (100ms, 코드 상수 `SETTLE_TIME_MS`): 소켓 설정 완료 후 안정화 대기
3. **Active** (`PERF_SINGLE_DURATION_SECONDS`, 5초): duration 기반 throughput 측정 + reservoir sampling 으로 latency/p95/p99 동시 수집
4. **Drain idle**: active 종료 후 recv timeout (기본 200ms) 동안 무수신 시 종료

> **core 구현 참고**: Active 페이즈에서 메시지 헤더의 `sent_ts_us` 를 기반으로 throughput 과 latency 를 동시에 측정한다.
> 수신 측에서 reservoir sampling 으로 p95/p99 를 수집한다.

### 6.2 Multi 페이즈

```
[Connect] → [Warmup(duration)] → [Settle] → [Active(duration)]
```

1. **Connect**: N 클라이언트 생성, MonitorSocket 로 연결 확인 (`PERF_MULTI_CONNECT_READY_TIMEOUT_MS`)
2. **Warmup** (`PERF_MULTI_WARMUP_SECONDS`, 2초): duration 기반 send/recv 반복 (phase_warmup)
3. **Settle** (`PERF_MULTI_SETTLE_MS`, 500ms): 안정화 sleep (one-way: phase_drain 라벨, echo: phase_warmup 라벨)
4. **Active** (`PERF_MULTI_DURATION_SECONDS`, 5초): 라운드로빈 분산 send/recv, 메트릭 수집 (phase_active)

---

## 7. 패턴별 상세 구현 계획

### 7.1 Single 패턴

> **참고**: run_comparison.py 기준 모든 single 패턴은 one-way 방향(`bandwidth 승수 = 1.0`).
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
| 8 | PerfSpot.cs | SPOT | Spot (pub/sub) | one-way | tcp,tls,ws,wss |

> **참고**: STREAM 3종(STREAM, STREAM_CALLBACK, STREAM_LEN32BE)은 core/perf 와 동일하게 multi suite 에만 포함된다. single 기본 패턴은 8개(PAIR~SPOT)이다.

### 7.2 Multi 패턴

> ★ core/perf 와 동일하게 **server/client 로직 분리**한다. C# 에서는 동일 파일 내 `RunServer()` / `RunClient()` 정적 메서드로 분리한다.

| # | 파일 | 패턴 | 서버 역할 | 클라이언트 역할 |
|---|------|------|-----------|----------------|
| 1 | PerfMultiDealerDealer.cs | DEALER_DEALER | DEALER bind, relay | DEALER connect, send one-way |
| 2 | PerfMultiDealerRouter.cs | DEALER_ROUTER | ROUTER bind, echo | DEALER connect, send+recv |
| 3 | PerfMultiRouterRouter.cs | ROUTER_ROUTER | ROUTER bind, echo | ROUTER connect, send+recv |
| 4 | PerfMultiPubSub.cs | PUBSUB | PUB bind, publish | SUB connect, recv |
| 5 | PerfMultiGateway.cs | GATEWAY | Receiver bind, echo | Gateway connect, send+recv |
| 6 | PerfMultiSpot.cs | SPOT | Spot publish | Spot subscribe |
| 7 | PerfMultiStream.cs | STREAM | STREAM bind, raw echo | C++ stream client |
| 8 | PerfMultiStreamCallback.cs | STREAM_CALLBACK | AttachStreamRaw callback | C++ stream client |
| 9 | PerfMultiStreamLen32Be.cs | STREAM_LEN32BE | AttachStreamLen32Be | C++ stream client |

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

    // Send/Receive with retry (EAGAIN/EINTR only)
    static int ReceiveRetry(Socket socket, byte[] buffer);
    static int SendRetry(Socket socket, byte[] buffer);

    // 폴링
    static bool WaitForInput(Socket socket, int timeoutMs);
    static bool WaitUntil(Func<bool> check, int timeoutMs);

    // 엔드포인트 생성
    static string EndpointFor(string transport, string name);

    // RESULT 출력 (bandwidth 승수 = 1.0 for all single)
    // throughput, bandwidth, latency, latency_p95, latency_p99 — 5개 메트릭 출력
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

    // STREAM 헬퍼 — single suite 에는 STREAM 패턴이 없지만,
    // core/perf 구조와 동일하게 low-level STREAM 소켓 유틸을 single/common 에 배치한다.
    // multi STREAM 서버(PerfMultiStream 등)가 이 헬퍼를 참조한다.
    static int StreamExpectConnectEvent(Socket socket, byte[] idBuffer);
    static void StreamSend(Socket socket, byte[] id, byte[] payload);
    static int StreamRecvPayload(Socket socket, byte[] idBuffer, byte[] payloadBuffer);

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

single 의 PerfCommon 유틸 중 필요한 것을 포함하고 multi 전용 기능 추가.

**`PerfMultiCommon.cs`** — (core/perf multi/common/perf_common_multi.hpp 대응)

```csharp
internal static class PerfMultiCommon
{
    static int ResolveMultiClients(string pattern);   // 비-STREAM: 100, STREAM: 10000
    static int ResolveMultiHwm(string pattern);       // 비-STREAM: 100, STREAM: 10
    static int ResolveMultiWarmupSeconds();
    static int ResolveMultiDurationSeconds();
    static int ResolveMultiSettleMs();
    static int ResolveMultiWarmupDrainMs(string pattern);
    // ... 기타 환경변수 리졸버
}
```

**`PerfMultiClientHelpers.cs`** — (core/perf multi/common/perf_multi_client_helpers.hpp 대응)

```csharp
internal static class PerfMultiClientHelpers
{
    static bool IsSupportedTransport(string transport);
    static string ParseEndpointArg(string[] args);
    static void WaitAllClientConnectReady(List<MonitorSocket> monitors, int timeoutMs);
    static void RunMultiEchoClientBenchmark(...);    // 공통 echo 클라이언트 루프
    static void RunMultiOnewayClientBenchmark(...);  // 공통 one-way 클라이언트 루프
}
```

**`PerfMultiServerEntry.cs`** — (core/perf multi/common/perf_multi_entry.hpp 서버 부분 대응)

```csharp
// 1. 패턴 디스패치 → RunServer(transport, size)
// 2. CPU/MEM 메트릭 수집
// 3. RESULT 출력: server_cpu_pct, server_mem_mb
```

**`PerfMultiClientEntry.cs`** — (core/perf multi/common/perf_multi_entry.hpp 클라이언트 부분 대응)

```csharp
// 1. 패턴 디스패치 → RunClient(transport, size, endpoint)
// 2. CPU/MEM 메트릭 수집
// 3. RESULT 출력: client_cpu_pct, client_mem_mb
```

**`PerfMultiTls.cs`** — TLS 인증서 리졸버

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

| Core C++ API | C# API |
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

내부적으로 `run_comparison.py --binding dotnet --suite single` 을 호출한다.

### 12.2 run_benchmarks_multi.sh (루트)

```bash
./run_benchmarks_multi.sh [options]

추가 Options:
  --clients N                 클라이언트 수 (기본: 100, STREAM: 10000)
  --warmup N                  웜업 초 (기본: 2)
  --transport-transition-ms N 트랜스포트 전환 대기 (기본: 3000)
  --pattern-transition-ms N   패턴 전환 대기 (기본: 3000)
```

내부: `run_comparison.py --binding dotnet --suite multi`

### 12.3 run_benchmarks.ps1 / run_benchmarks_multi.ps1

동일 인터페이스, PowerShell 구현.

---

## 13. 산출물

### 13.1 결과 파일

```
bindings/dotnet/perf/results/single/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
bindings/dotnet/perf/results/multi/report/perf_linux_YYYYMMDD_HHMMSS[_tag].txt
```

파일 형식 (core/perf 동일):
```
## META
platform: linux
timestamp: 2026-03-05T12:00:00+09:00
binding: dotnet

## Effective Options
...

## PATTERN: PAIR
### tcp
| Size | Throughput | Bandwidth | Latency | Latency_p95 | Latency_p99 |
|------|-----------|-----------|---------|-------------|-------------|
| 64   | 523401.23 | 33.50     | 12.35   | 18.22       | 25.10       |

RESULT,current,PAIR,tcp,64,throughput,523401.23
RESULT,current,PAIR,tcp,64,bandwidth,33.50
RESULT,current,PAIR,tcp,64,latency,12.35
RESULT,current,PAIR,tcp,64,latency_p95,18.22
RESULT,current,PAIR,tcp,64,latency_p99,25.10
...

## Completion
- status: complete
- expected_result_lines: 330
- actual_result_lines: 330
```

> **single vs multi 완료 형식 차이:**
> - **single**: 섹션 `## Completion`, 키 `expected_result_lines` / `actual_result_lines` (밑줄)
> - **multi**: 섹션 `## Status Summary`, 키 `expected result lines` / `actual result lines` (공백) + `success/unsupported/skip/fail` 카운트 포함

### 13.2 결과 보존 정책

- 최대 100개 파일 per report/ 디렉토리
- 초과 시 파일명 정렬 기준 oldest 삭제 (FIFO)

---

## 14. 구현 순서

### Phase 0: 인증서 및 스크립트 준비

1. `bindings/dotnet/tests/certs/` 인증서 확인 (server.crt, server.key, ca.crt — 이미 존재)
2. `run_comparison.py` 수정: dotnet 빌드/실행 경로 지원 추가
3. `.gitignore`, `.gitkeep` 파일

### Phase 1: 인프라 (빌드, 공통, 진입점)

4. `perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj` 생성
5. `perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj` 생성
6. `Zlink.sln` 에 perf 프로젝트 참조 추가
7. `single/.../common/PerfCommon.cs` — 환경변수 파싱, retry 헬퍼, RESULT 출력, 헤더 stamp/decode, reservoir sampling
8. `single/.../common/PerfTls.cs` — single TLS 인증서 리졸버
9. `single/.../PerfMain.cs` — 패턴 디스패치 진입점
10. `multi/.../common/PerfCommon.cs` — multi 공통 유틸
11. `multi/.../common/PerfMultiCommon.cs` — multi 설정 리졸버
12. `multi/.../common/PerfMultiClientHelpers.cs` — 공통 client 루프 헬퍼
13. `multi/.../common/PerfMultiServerEntry.cs` — 서버 디스패치 + 메트릭
14. `multi/.../common/PerfMultiClientEntry.cs` — 클라이언트 디스패치 + 메트릭
15. `multi/.../common/PerfMultiTls.cs` — TLS 인증서 리졸버
16. `multi/.../PerfMain.cs` — --multi-server / --multi-client 디스패치

### Phase 2: Single 소켓 패턴 (6개)

17. `single/.../current/PerfPair.cs`
18. `single/.../current/PerfPubSub.cs`
19. `single/.../current/PerfDealerDealer.cs`
20. `single/.../current/PerfDealerRouter.cs`
21. `single/.../current/PerfRouterRouter.cs`
22. `single/.../current/PerfRouterRouterPoll.cs`

### Phase 3: Single 서비스 패턴 (2개)

23. `single/.../current/PerfGateway.cs`
24. `single/.../current/PerfSpot.cs`

### Phase 4: Multi 패턴 — server/client 분리 (6×RunServer/RunClient + 3 서버 only)

25. `multi/.../current/PerfMultiDealerDealer.cs` (RunServer + RunClient)
26. `multi/.../current/PerfMultiDealerRouter.cs` (RunServer + RunClient)
27. `multi/.../current/PerfMultiRouterRouter.cs` (RunServer + RunClient)
28. `multi/.../current/PerfMultiPubSub.cs` (RunServer + RunClient)
29. `multi/.../current/PerfMultiGateway.cs` (RunServer + RunClient)
30. `multi/.../current/PerfMultiSpot.cs` (RunServer + RunClient)
31. `multi/.../current/PerfMultiStream.cs` (서버 only)
32. `multi/.../current/PerfMultiStreamCallback.cs` (서버 only)
33. `multi/.../current/PerfMultiStreamLen32Be.cs` (서버 only)
34. `multi/.../common/PerfMultiStreamClient.cs` (Raw transport)
35. `multi/.../common/PerfMultiStreamStopParser.cs`

### Phase 5: 스크립트 및 마무리

36. `run_benchmarks.sh` / `.ps1` (루트 + single/ + multi/)
37. `run_benchmarks_multi.sh` / `.ps1`
38. `run_comparison.py`
39. `README.md`
40. 빌드 검증 (`dotnet build -c Release`)
41. `run_comparison.py` 통합 검증

### Phase 6: 코드 품질 리뷰 및 리팩토링

> 모든 패턴 구현과 스크립트 완성 후, 코드 전체에 대한 품질 리뷰와 개선을 수행한다.
> 성능 벤치마크 코드이므로 불필요한 오버헤드에 특히 엄격히 대응한다.

42. **Dead Code / 미사용 파일 정리**
    - 사용되지 않는 using, 변수, 메서드, 클래스 전부 삭제
    - 의미 없는 주석 (TODO 잔재, 복사 흔적, 주석 처리된 코드) 전부 삭제
    - 빈 파일, 미사용 설정 파일 삭제
43. **가독성 리팩토링**
    - 메서드/변수 네이밍 일관성 검토 (core/perf 와 대응 관계 명확화)
    - 과도한 중첩 / 긴 메서드 분리 (단, 벤치마크 인라인 정책 범위 내)
    - 매직 넘버 → 상수 추출 (타임아웃, 버퍼 크기, 재시도 한도 등)
    - 패턴 파일 간 구조 일관성 확보 (동일 페이즈 순서, 동일 변수명 컨벤션)
44. **성능 리뷰 (벤치마크 오버헤드 제거)**
    - **불필요한 할당**: 측정 루프 내 `new byte[]`, `new string()`, 박싱 (`object`) 등
    - **불필요한 복사**: `ToArray()`, `Array.Copy` 가 회피 가능한 경우
    - **불필요한 대기**: 측정 루프 내 `Thread.Sleep`, busy-wait 이 과도한 경우
    - **GC 압박**: 측정 구간에서 단명 객체 반복 생성 여부 (Gen0 GC 유발)
    - **I/O 플러시**: `Console.WriteLine` 이 측정 루프 내에 포함되지 않는지 확인
    - **버퍼 재사용**: send/recv 버퍼가 루프 밖에서 1회 할당 후 재사용되는지 확인
    - **Span 활용**: `Span<byte>` / `stackalloc` 로 힙 할당 회피 가능한 곳 확인
    - 리뷰 체크리스트 (파일별):
      ```
      [ ] 측정 루프 내 힙 할당 0건
      [ ] 측정 루프 내 불필요한 복사 0건
      [ ] 측정 루프 내 Thread.Sleep / 과도한 busy-wait 없음
      [ ] 측정 루프 밖에서 I/O 출력
      [ ] send/recv 버퍼 루프 밖 할당 + 재사용
      [ ] 박싱/언박싱 없음 (value type 직접 사용)
      [ ] Span<byte> / stackalloc 활용 최적화
      ```
45. **개선 사항 적용 후 주석 추가**
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
- **Retry 금지** (정책): send/recv 실패 시 재시도 없음 (EAGAIN/EINTR 은 예외적으로 루프)
- **Inflight/Outstanding 옵션 금지**: 백프레셔 한도 = 소켓 HWM 만

### 15.2 필수 사항

- 각 벤치마크 소스에 **소켓 생성, bind/connect, send/recv 루프, 페이즈 컨트롤** 인라인
- `RESULT,current,...` 형식의 stdout 출력
- STREAM 서버는 stop-token `__zlink_perf_stop__` 수신 시 정상 종료
- Multi 서버는 `READY,<endpoint>` stdout 출력 후 클라이언트 대기
- TLS 인증서는 `bindings/dotnet/tests/certs/` 경로 사용

### 15.3 코드 인라이닝 정책

- 각 패턴 파일에 메인 루프 로직 인라인 (core/perf 동일)
- `common/PerfCommon` 으로 추출 허용: 환경변수 파싱, retry, PrintResult, 엔드포인트 생성, 헤더 stamp/decode
- Multi 클라이언트는 `common/PerfMultiClientHelpers` 의 공통 루프 위임 허용
- STREAM 서버 인프라는 모듈화 허용 (`common/PerfMultiStreamClient`, `common/PerfMultiStreamStopParser`)

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

- [ ] `dotnet build perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release` 빌드 성공 (종료코드 0)
- [ ] `dotnet build perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release` 빌드 성공 (종료코드 0)
- [ ] `bindings/dotnet/tests/certs/` 인증서 파일 3개 존재 (server.crt, server.key, ca.crt)

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
- single: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99` — 5개 메트릭이 모든 pattern/transport/size 조합에 존재
- multi: `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99` — 5개 메트릭이 모든 pattern/transport/size 조합에 존재

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

**요구 동작:**
- `pattern/transport` 실행 중 각 `size` 테스트 완료 시 해당 RESULT 행이 즉시 stdout 에 출력되어야 한다.
- 모든 size 출력이 완료된 후에야 다음 transport 또는 pattern 으로 전환된다.
- 사용자가 실시간으로 진행 상황을 확인할 수 있어야 한다.

**검증 방법:**
```bash
# 3개 사이즈로 실행하고 콘솔 로그를 파일로 수집
python3 bindings/dotnet/perf/run_comparison.py \
  --binding dotnet --suite single \
  --pattern PAIR --transports tcp --msg-sizes 64,1024,65536 \
  --runs 1 --duration 1 --reuse-build \
  --output bindings/dotnet/perf/results/single/tmp/size_progress.log
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
- [ ] **single** 결과 파일: `## Completion` 섹션에서 `status: complete`, `expected_result_lines == actual_result_lines` (밑줄 키)
- [ ] **multi** 결과 파일: `## Status Summary` 섹션에서 `status: complete`, `expected result lines == actual result lines` (공백 키) + `fail` 카운트 0
- [ ] 결과 파일/콘솔 로그에 `## Failures` 섹션이 없어야 함
- [ ] 요청된 기본 조합 중 `fail` 조합 0건
- [ ] `UNSUPPORTED` 조합은 정책 정의 범위 내에서만 허용 (예: inproc/ipc 에서 GATEWAY/SPOT)

**기본 조합 수 예상 (single):**
- socket 패턴 (6종) × 6 transport × 6 size = 216 조합
- gateway/spot (2종) × 4 transport × 6 size = 48 조합
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

# single 결과 파일 completion 확인
tail -20 bindings/dotnet/perf/results/single/report/perf_*.txt
# 기대 출력:
# ## Completion
# status: complete
# expected_result_lines: N
# actual_result_lines: N

# multi 결과 파일 completion 확인
tail -20 bindings/dotnet/perf/results/multi/report/perf_*.txt
# 기대 출력:
# ## Status Summary
# status: complete
# expected result lines: N
# actual result lines: N
```

---

## 17. 완료 기준 (Definition of Done)

- 디렉토리/파일 구조가 core/perf 의 `common/` + `current/` 분리 구조와 동일.
- multi server/client 가 core/perf 와 동일하게 로직 분리 (동일 파일 내 RunServer/RunClient).
- runner 옵션/기본값/결과 형식이 core 와 동등하게 동작.
- single/multi 모든 패턴 클래스가 `bindings/dotnet/perf` 에서 빌드됨.
- STREAM client 는 `core/perf/common/streamclient` 공용 바이너리만 사용.
- perf 소스 내 Native P/Invoke / internal 네임스페이스 직접 호출 0건.
- TLS 인증서는 `bindings/dotnet/tests/certs/` 에서 독립 관리.
- retry 로직/우회 wrapper/비정책 실행 경로가 없음.
- **메트릭 정확성**(필수 메트릭 존재/bandwidth 계산식/값 범위)이 검증됨.
- **사이즈별 테스트 완료 시점마다** RESULT 행이 즉시 출력됨이 검증됨.
- **기본 설정 전체 실행**(single/multi)이 실패 없이 `status: complete` 로 종료됨.
- `run_comparison.py` 수정 반영: dotnet 빌드/실행 경로 지원.
- **코드 품질 리뷰 완료**: dead code/미사용 주석 0건, 측정 루프 내 불필요한 할당/복사/대기 0건.
- **주석 정리 완료**: 패턴 설명, 페이즈 전환, 비자명 로직에 적절한 수준의 주석 추가.
