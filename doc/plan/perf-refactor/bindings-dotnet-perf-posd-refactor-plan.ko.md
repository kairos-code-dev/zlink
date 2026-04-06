# `bindings/dotnet/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B-** — 양호한 기반이나 single/multi 중복과 global state 문제
> 대상: `bindings/dotnet/perf/`

## 1. 목표

.NET perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- single/multi 공통 유틸리티가 하나의 shared 프로젝트로 통합된다.
- TLS 설정이 한 곳에서만 정의된다.
- 패턴 추가 시 변경 포인트가 최소화된다 (switch dispatch → 패턴 레지스트리).
- callback hot path에서 blocking lock이 lock-free 연산으로 교체된다.
- global mutable poll state가 캡슐화된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 PerfCommon single/multi 80% 중복

- `single/.../common/PerfCommon.cs` (480줄)
- `multi/.../common/PerfCommon.cs` (425줄)
- `ReceiveBlocking()`, `SendBlocking()`, `WaitUntil()`, `TimestampUs()`,
  `ComputeLatencyStats()`, `ReservoirSample()`, `EndpointFor()` 등 80%가 동일

### 2.2 PerfTls 사실상 복사

- `single/.../common/PerfTls.cs` (123줄)
- `multi/.../common/PerfTls.cs` (129줄)
- `ConfigureTlsServerIfNeeded()`, `TryResolvePerfTlsPaths()` 등 전 함수 동일
- 차이: single은 실패 시 stderr 메시지, multi는 silent return;
  single은 `socket.set()`, multi는 `socket.set_option()` — 의미 없는 차이

### 2.3 switch dispatch로 인한 변경 증폭

패턴 추가 시 수정 필요한 곳:
1. `single/.../src/Perf<Pattern>.cs` 신규 파일
2. `multi/.../src/Perf<Pattern>Server.cs` 신규 파일
3. `multi/.../src/Perf<Pattern>Client.cs` 신규 파일
4. `single/PerfMain.cs` switch문
5. `multi/.../common/PerfCommonMulti.cs` recv mode 검증
6. PerfTls.cs에 TLS 설정 필요 시

총 6-8개 수정 포인트.

### 2.4 callback hot path의 blocking lock

`PerfSpotClient.cs` `SpotCallbackState.AddSample()`:
```csharp
lock (_samplesLock)
{
    _samples[_sampleWriteIndex] = latencyUs;
    _sampleWriteIndex++;
}
```
고빈도 메시지 수신 경로에 `lock` 사용.

### 2.5 global mutable poll state

`PerfClientHelpers.cs`에 static mutable 배열:
```csharp
private static PollEvents[]? _monitorPollEvents;
private static PollEvents[]? _monitorRevents;
```
스레드 안전성 암묵적 가정. 다중 실행 시 충돌 위험.

### 2.6 errno 상수 중복 정의

- `single/.../common/PerfCommon.cs`: `ErrnoEagain = 11`
- `multi/.../common/PerfCommon.cs`: `ErrnoEagain = 11`
- 같은 상수를 두 곳에서 재정의

### 2.7 옵션 해석 비일관성

- single: `ParseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200)`
- multi: `ParseFirstPositiveEnv(200, "PERF_SNDTIMEO_MS")`
- 함수명, env var prefix (`SINGLE_` vs 없음), fallback 동작이 다름

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- C# 관용 스타일 유지 (async/await 불필요한 곳에 강제하지 않음)
- 공통화는 Zlink.BindingBench.Common 프로젝트로 추출
- shallow config struct는 builder 패턴 또는 record로 통합
- **inline 코드 요구사항** (`PERF_POLICY.md` 715-727줄):
  패턴 파일에서 send/recv API 호출, EAGAIN 처리, 모델 선택, ready gate,
  소켓 생성이 직접 보여야 한다. 설정/TLS/결과 출력만 공통화 가능.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 4. 단계별 실행 계획

### 단계 0. 현황 동결

할 일:
- 파일별 중복 매핑 (single ↔ multi 함수 대응표)
- env var 전수 목록과 네이밍 불일치 표
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인
- STREAM 공유 클라이언트 경로 현황 확인
- direct native API 사용 현황 (P/Invoke 직접 호출 grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합/제거/이동 대상이 함수 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. phase drain/settle 삭제 (recv drain loop는 유지)

**중요 구분**: recv drain loop (POLLIN→nonblocking recv until EAGAIN)는 핵심
측정 메커니즘이므로 유지. 삭제 대상은 warmup→active 전환 전용 pseudo-phase뿐.

할 일:
- `PERF_SETTLE_MS` env var 해석 삭제 (`PerfCommonMulti.cs:124`)
- `ResolveMultiWarmupDrainMs()`, `ResolveMultiDrainMs()` 삭제
- `settleMs`, `drainMs`, `warmupDrainMs` 파라미터/필드 삭제
- `RunDrainPhase()` 함수 삭제 (`PerfRouterRouterClient.cs`)
- 모든 client에서 settle/drain phase 관련 호출부 삭제
- single `PerfCommon.cs`의 drain 루프(71-81줄)는 recv drain loop인지
  phase drain인지 확인 후 판단 (poller event loop 내 nonblocking recv이면 유지)
- warmup 메시지가 active latency에 혼입되지 않는지 확인
  (header phase 필드로 걸러지는지 확인)

완료 기준:
- phase drain 관련 함수/필드/env var 0건
- recv 모델의 nonblocking recv loop는 정상 유지
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. 공통 프로젝트 추출

할 일:
- `Zlink.BindingBench.Common` 프로젝트 생성 (또는 기존 구조 내 shared/ 디렉터리)
- PerfCommon에서 **측정 인프라만** 추출:
  - `TimestampUs()`, `ComputeLatencyStats()`, `ReservoirSample()`
  - `EndpointFor()`, errno 상수, TLS/결과 출력 유틸리티
- `ReceiveBlocking()`과 `SendBlocking()`은 **각 패턴 파일에 남겨
  send/recv API 호출이 직접 보이게 유지** (`PERF_POLICY.md` 715-727줄)
- PerfTls 통합: 하나의 `PerfTls.cs`로 merge
  - verbose 파라미터로 stderr 출력 여부 제어
- single/multi PerfCommon은 shared를 참조하고 suite-specific 로직만 유지

완료 기준:
- PerfCommon 중복 0건
- PerfTls 파일 1개
- errno 상수 정의 1곳

### 단계 3. 패턴 레지스트리

할 일:
- `IPerfPattern` 인터페이스 정의:

```csharp
interface IPerfPattern
{
    string Name { get; }
    void RunSingle(PerfConfig config);
    void RunMultiServer(PerfConfig config);
    void RunMultiClient(PerfConfig config);
}
```

- 각 패턴을 `IPerfPattern` 구현으로 변환
- PerfMain.cs의 switch dispatch를 `Dictionary<string, IPerfPattern>` lookup으로 교체

완료 기준:
- 패턴 추가 시 수정 포인트: 패턴 클래스 1개 + 레지스트리 등록 1줄

### 단계 4. callback hot path 개선

할 일:
- `SpotCallbackState.AddSample()`의 `lock`을 `Interlocked` 기반으로 교체:

```csharp
int index = Interlocked.Increment(ref _sampleWriteIndex) - 1;
if ((uint)index < (uint)_samples.Length)
    _samples[index] = latencyUs;
```

- fatal 에러 경로 분리 (hot path에서 exception 처리 제거)

완료 기준:
- callback hot path에 `lock` 키워드 0건
- latency 측정 정확도 유지

### 단계 5. global state 캡슐화

할 일:
- `PerfClientHelpers`의 static poll 배열을 `PollManager` 인스턴스로 캡슐화:

```csharp
class PollManager : IDisposable
{
    private PollEvents[] _pollEvents;
    private PollEvents[] _revents;
    // ...
}
```

- `PerfCommon`의 static IPC 레지스트리도 인스턴스 기반으로 전환

완료 기준:
- static mutable state 0건 (const 제외)
- 다중 실행 격리 보장

### 단계 6. 옵션 해석 통합

할 일:
- `PerfOptions` record 정의:

```csharp
record PerfOptions(
    int DurationSeconds,
    int WarmupSeconds,
    int SndTimeoutMs,
    int RcvTimeoutMs,
    string RecvMode,
    int Clients,
    // ...
);
```

- env var 해석을 1곳에 집중하되 **정책 문서의 기존 이름을 유지**
  (`PERF_*`, `PERF_SINGLE_*`, `PERF_MULTI_*`)

완료 기준:
- env var 읽기 포인트 1곳
- 네이밍 정책 일관

### 단계 7. 검증

할 일:
- `dotnet build` 확인
- single/multi 전 패턴 smoke
- recv/callback 양쪽 모드 정상 동작
- `core/perf` 대비 semantic parity 확인
- 결과 파일 `bindings/dotnet/perf/results/` 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지

## 5. 완료 정의

- single/multi PerfCommon 중복이 공통 프로젝트로 통합됨
- PerfTls가 1개 파일로 통합됨
- 패턴 추가 시 변경 포인트 6개 → 2개
- callback hot path에 blocking lock 0건
- global mutable state가 인스턴스로 캡슐화됨
- env var 해석이 1곳으로 집중됨
- `core/perf`와 동일한 측정 의미 유지
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약이 코드에서 강제됨
- 미지원 recv 모드 조합이 fail-fast
- direct P/Invoke 호출 0건 (wrapper API만 사용)
- 전체 패턴/전체 사이즈 정상 동작
