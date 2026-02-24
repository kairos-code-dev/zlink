# zlink Performance Test Policy (통합)

> **적용 범위**: zlink 전체 (core + bindings)
> **Policy Version**: 1.5
> **Date**: 2026-02-24
> **Scope**: zlink 성능 테스트 통합 정책 — 공통 구조, 통합 실행, 비교 스크립트
>
> 본 정책은 `perf/`의 C++ 벤치마크뿐 아니라 모든 바인딩 라이브러리(`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, `bindings/python`)의 성능 테스트에도 동일하게 적용된다. 각 바인딩은 언어별 구현 차이가 있을 수 있으나, 측정 기준·결과 형식·운영 모드·임계치 등 본 정책에서 정의하는 규칙을 준수해야 한다.

---

## 1. 문서 구조

| 문서 | 설명 |
|------|------|
| **PERF_POLICY.md** (본 문서) | 공통 디렉터리 구조, 통합 실행, 비교 스크립트 |
| [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) | single-client 벤치마크 측정 기준, 패턴, phase, 환경 변수 |
| [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md) | multi-client 벤치마크 측정 기준, 패턴, phase, 환경 변수 |

- 측정 방법, 패턴별 상세, phase 제어, 환경 변수 등은 각 개별 정책 문서를 참조한다.
- 본 문서는 **공통 구조**와 **통합 실행 방법**만 기술한다.
- 각 바인딩의 실행 스크립트 경로는 `perf/single/` 및 `perf/multi/`에 위치한다.

---

## 2. 디렉터리 구조

### 2.0 core (C++ 레퍼런스 구현)

```text
perf/
├── PERF_POLICY.md                          # 통합 정책 (본 문서)
├── PERF_SINGLE_TEST_POLICY.md              # single 정책
├── PERF_MULTI_TEST_POLICY.md               # multi 정책
├── run_benchmarks.sh / .ps1                # single 전용 실행 스크립트 (Linux/Windows)
├── run_benchmarks_multi.sh / .ps1          # multi 전용 실행 스크립트 (Linux/Windows)
├── run_comparison.py                       # 통합 비교/실행 스크립트
├── single/                                 # single 벤치마크 소스
├── multi/                                  # multi 벤치마크 소스
└── results/                                # 결과 저장 루트
    ├── single/
    │   ├── tmp/                            # 항상 저장 (임시)
    │   ├── report/                         # --result (누적, complete만)
    │   └── baseline/                       # --save [VER] (고정)
    └── multi/
        ├── tmp/
        ├── report/
        └── baseline/
```

### 2.0.1 bindings (바인딩 라이브러리)

각 바인딩은 동일한 `perf/` 하위 구조를 따른다.

```text
perf/                                       # bindings/<lang>/perf/
├── single/
│   ├── run_benchmarks.sh / .ps1            # single 전용 실행 스크립트
│   └── ...                                 # 언어별 벤치마크 소스
├── multi/
│   ├── run_benchmarks.sh / .ps1            # multi 전용 실행 스크립트
│   └── ...
└── results/                                # 결과 저장 루트 (core와 동일 구조)
    ├── single/
    │   ├── tmp/
    │   ├── report/
    │   └── baseline/
    └── multi/
        ├── tmp/
        ├── report/
        └── baseline/
```

### 2.0.2 소스 파일 위치 및 명명 규칙

#### 소스 위치 규칙

- 모든 벤치마크 소스는 해당 suite의 디렉터리에 위치해야 한다:
  - single: `perf/single/`
  - multi: `perf/multi/`
- 언어별 빌드 시스템이 하위 프로젝트를 요구하는 경우 `perf/single/` 또는 `perf/multi/` 안에 프로젝트 디렉터리를 둔다 (예: .NET의 `Zlink.BindingBench/`, Java의 Gradle/Maven 모듈).
- 벤치마크 소스를 언어 메인 프로젝트의 테스트 트리(예: `src/test/`)에 두지 않는다.

#### 명명 접두어 규칙

- 모든 벤치마크 소스 파일은 **`perf_`** 접두어를 사용한다 (PascalCase 언어는 `Perf` 접두어).
- 기본 패턴: `perf_<pattern>` — 각 언어의 명명 컨벤션을 적용한다.
- 상세 파일명 규칙은 개별 정책 문서를 참조한다:
  - Single: [PERF_SINGLE_TEST_POLICY.md § 10.1](PERF_SINGLE_TEST_POLICY.md)
  - Multi: [PERF_MULTI_TEST_POLICY.md § 11.1](PERF_MULTI_TEST_POLICY.md)

#### 소스 위치 테이블

| 언어 | single 소스 위치 | multi 소스 위치 | 공통 유틸리티 |
|------|-----------------|----------------|-------------|
| Core (C++) | `perf/single/current/` | `perf/multi/current/` | `perf/single/common/`, `perf/multi/common/` |
| C++ binding | `perf/single/` | `perf/multi/` | `perf_dispatch.hpp` |
| .NET | `perf/single/Zlink.BindingBench/` | `perf/multi/<project>/` 또는 `perf/single/Zlink.BindingBench/` 내 multi role entrypoint | `PerfCommon.cs` |
| Java | `perf/single/<project>/` | `perf/multi/<project>/` 또는 `perf/single/<project>/` 내 multi role entrypoint | `PerfUtil.java` |
| Node | `perf/single/` | `perf/multi/` | (inline) |
| Python | `perf/single/` | `perf/multi/` | `perf_common.py` |

- 컴파일 언어 바인딩(C++/.NET/Java)은 소스 트리 분리 대신 단일 runner에서 `--multi-server`/`--multi-client` role 분기를 제공해도 된다. 이 경우에도 결과 형식, 운영 모드, server/client 프로세스 모델은 동일하게 준수해야 한다.

### 2.1 결과 저장 규칙

| 항목 | 규칙 |
|------|------|
| 파일명 형식 | `perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| 날짜 디렉터리 | 사용하지 않음 (파일명에 날짜/시간 포함) |
| `<platform>` | `linux`, `windows`, `macos` |
| `<tag>` | `--results-tag` 옵션으로 지정 (선택) |

### 2.2 보존 정책

| 디렉터리 | 최대 파일 수 | 초과 시 처리 |
|-----------|-------------|-------------|
| `tmp/` | 100 | 오래된 순 자동 삭제 |
| `report/` | 100 | 오래된 순 자동 삭제 |
| `baseline/` | 100 | 오래된 순 자동 삭제 (`latest.txt` symlink 제외) |

- `single/`과 `multi/` 각각 독립적으로 적용한다.
- 파일 수 검사는 새 파일 저장 시 수행한다.
- 삭제 대상 선정: **파일명 사전순 오름차순**(= 가장 오래된 파일)부터 삭제. mtime이 아닌 파일명 기준이다.

### 2.3 저장 단위

- 스크립트 1회 실행 = 1개 결과 파일. 실행에서 측정된 모든 패턴/transport/size 조합의 결과가 하나의 파일에 기록된다.

---

## 3. 실행 스크립트

### 3.1 개별 실행

| suite | core 스크립트 | bindings 스크립트 | 정책 문서 |
|-------|--------------|-------------------|-----------|
| single | `perf/run_benchmarks.sh` / `.ps1` | `perf/single/run_benchmarks.sh` / `.ps1` | [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) |
| multi | `perf/run_benchmarks_multi.sh` / `.ps1` | `perf/multi/run_benchmarks.sh` / `.ps1` | [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md) |

```bash
# core single만 실행
perf/run_benchmarks.sh --pattern PAIR --save

# core multi만 실행
perf/run_benchmarks_multi.sh --pattern MULTI_STREAM --save

# bindings 실행 (예: node)
perf/single/run_benchmarks.sh --pattern PAIR --save
perf/multi/run_benchmarks.sh --pattern MULTI_STREAM --save
```

각 스크립트의 상세 옵션은 개별 정책 문서의 섹션 5를 참조한다.

> **정책 준수 실행기**: 아래 스크립트가 각 suite의 유일한 정책 준수 실행기이다. core는 `run_comparison.py`를 내부 실행/비교 엔진으로 사용하고, bindings는 `bindings/perf/run_policy_bench.py`를 내부 실행/비교 엔진으로 사용한다. 환경 변수로 실행 스크립트를 우회하는 것은 허용하지 않는다.
>
> | suite | core | bindings |
> |-------|------|----------|
> | single | `perf/run_benchmarks.sh` | `perf/single/run_benchmarks.sh` |
> | multi | `perf/run_benchmarks_multi.sh` | `perf/multi/run_benchmarks.sh` |
>
> core는 single/multi 스크립트가 같은 디렉터리에 있으므로 `_multi` 접미어로 구분한다. bindings는 `single/`·`multi/` 디렉터리로 분리되므로 스크립트명은 `run_benchmarks.sh`로 동일하다.

### 3.2 통합 실행

실행 엔트리포인트는 wrapper 스크립트다 (§ 3.1 정책 준수 실행기 테이블 참조).
- core single: `run_benchmarks.sh` / `.ps1`
- core multi: `run_benchmarks_multi.sh` / `.ps1`
- bindings: `single/run_benchmarks.sh` / `multi/run_benchmarks.sh`
- 단일 실행에서 single/multi를 혼합하지 않는다.

```bash
# core: single만 실행
perf/run_benchmarks.sh --pattern ALL

# core: multi만 실행
perf/run_benchmarks_multi.sh --pattern ALL

# core: 특정 패턴만
perf/run_benchmarks.sh --pattern PAIR,STREAM

# core: 레포트 저장 (report/)
perf/run_benchmarks.sh --result

# core: baseline 저장 (baseline/, 타임스탬프 파일명)
perf/run_benchmarks.sh --save

# core: baseline 저장 (baseline/v1.5.0.txt)
perf/run_benchmarks.sh --save v1.5.0

# core: baseline 비교 (Trend 모드)
perf/run_benchmarks.sh --mode trend --result

# core: baseline 비교 (Gate 모드, 특정 baseline 파일)
perf/run_benchmarks.sh --mode gate --baseline-file perf/results/single/baseline/v1.5.0.txt

# bindings: 동일한 옵션 체계 적용 (예: python)
perf/single/run_benchmarks.sh --pattern ALL
perf/multi/run_benchmarks.sh --pattern ALL
```

### 3.3 통합 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분) | 전체 |
| `--mode MODE` | 운영 모드: `observe`, `trend`, `gate` | `observe` |
| `--runs N` | 반복 횟수 | suite별 기본값 |
| `--build-dir PATH` | 빌드 디렉터리 경로 | 자동 탐색 |
| `--reuse-build` | 기존 빌드 재사용 | single: off, multi: on |
| `--pin-cpu` | CPU pinning (Linux: taskset, Windows: processor affinity) | off |
| `--output PATH` | stdout tee 출력 | stdout만 |
| `--result` | `report/`에 TABLE 레포트 저장 (complete만) | off |
| `--save [VER]` | `baseline/`에 baseline 저장 (complete만) | — |
| `--results-dir PATH` | 결과 저장 루트 override | `perf/results` |
| `--results-tag NAME` | 결과 파일명 태그 | 없음 |
| `--baseline-file PATH` | Gate 모드 비교 대상 baseline 파일 | `latest.txt` |
| `--msg-sizes LIST` | 메시지 크기 목록 | suite별 기본값 |
| `--transports LIST` | transport 목록 | suite별 기본값 |

- suite별 고유 옵션(`--multi-clients` 등)은 환경 변수 또는 개별 스크립트 호출 시 전달한다.

---

## 4. 결과 파일 형식 (공통)

### 4.1 파일 구조

디렉터리별로 저장 형식이 다르다.

#### tmp/ · baseline/ (기계 파싱용)

```text
META,<key>,<value>
META,<key>,<value>
...
RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
...
TABLE
## PATTERN: PAIR (one-way)
### Transport: tcp
| Size     |       Throughput | Bandwidth |      Latency | CPU% | Mem MB |
...
```

- META → RESULT → TABLE 세 영역으로 구성된다.
- `TABLE` 마커 이후의 내용은 RESULT 데이터를 사람이 읽을 수 있는 markdown table로 포맷한 것이다.
- 기계 파싱 시 `META,`와 `RESULT,`로 시작하는 라인만 처리하면 TABLE 영역은 자연히 무시된다.

#### report/ (사람이 읽는 용도)

```text
## PATTERN: PAIR (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |      Latency | CPU% | Mem MB |
|----------|------------------|-----------|--------------|------|--------|
| 64B      |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us   | 48.2 |   12.3 |
| 1024B    |   120.30 Kmsg/s  | 123.2 MB/s|   52.10 us   | 52.1 |   14.1 |
```

- **TABLE만** 저장한다. META/RESULT 라인은 포함하지 않는다.
- 기계 파싱용 데이터가 필요하면 동일 실행의 `tmp/` 파일을 참조한다.

### 4.2 RESULT line 형식

```text
RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
```

| metric | 설명 | 필수 |
|--------|------|------|
| `throughput` | echo 패턴: 왕복 완료 수 (`ops/s`), one-way 패턴: 단방향 수신 수 (`msg/s`) | MUST |
| `bandwidth` | 네트워크 전송량 (MB/s) — echo: `throughput × size × 2 / 1,000,000`, one-way: `throughput × size / 1,000,000` | MUST |
| `latency` | 레이턴시 (us) | MUST |
| `cpu_pct` | 프로세스 CPU 사용률 (%) — single용 | Linux/Windows |
| `mem_mb` | 프로세스 메모리 (MB, RSS/WorkingSet 기준) — single용 | Linux/Windows |
| `client_cpu_pct` | client 프로세스 CPU (%) — multi용 | Linux/Windows |
| `client_mem_mb` | client 프로세스 메모리 (MB) — multi용 | Linux/Windows |
| `server_cpu_pct` | server 프로세스 CPU (%) — multi용 | Linux/Windows |
| `server_mem_mb` | server 프로세스 메모리 (MB) — multi용 | Linux/Windows |

- throughput 단위는 패턴의 메시지 흐름 방향에 따라 결정된다. echo(왕복) 패턴은 `ops/s`, one-way(단방향) 패턴은 `msg/s`. 상세 분류는 개별 정책 문서 섹션 8.1을 참조한다.
- bandwidth는 throughput 단위가 다른 패턴 간에도 실제 데이터 처리량으로 직접 비교할 수 있는 공통 지표이다. 상세 계산은 개별 정책 문서 섹션 8.3을 참조한다.
- `cpu_pct`, `mem_mb`는 정보성(informational) 메트릭이다. 누락 시 완료 판정에 영향 없음.
- 상세 META 키 및 패턴별 측정 방식은 개별 정책 문서를 참조한다.

### 4.3 저장 옵션

| 동작 | 옵션 | 저장 위치 | 저장 형식 | 조건 |
|------|------|-----------|-----------|------|
| 임시 저장 | (항상) | `<suite>/tmp/` | META + RESULT + TABLE | complete/partial 무관 |
| 레포트 생성 | `--result` | `<suite>/report/` | **TABLE만** | complete만 |
| baseline 저장 | `--save [VER]` | `<suite>/baseline/<VER>.txt` | META + RESULT + TABLE | complete만 (partial 시 에러) |

- 임시 저장(`tmp/`)은 옵션 없이 항상 수행된다.
- `report/`에는 TABLE만 저장한다. 기계 파싱용 데이터(META/RESULT)는 동일 실행의 `tmp/` 파일을 참조한다.
- `--result`과 `--save`는 동시 사용 가능.
- `--save` 버전 미지정 시 타임스탬프 기반 파일명으로 저장. 지정 시 `<VER>.txt`로 저장.
- `--save` 동일 버전 덮어쓰기: 기존 파일이 있으면 전체 교체. 부분 갱신 불가.
- 완료 판정 기준: `expected == actual` (throughput + bandwidth + latency RESULT line 기준, 조합당 3줄).

---

## 5. 출력 형식 (공통)

### 5.1 스크립트 결과 테이블

> **구현 필수**: 모든 실행 스크립트는 RESULT line 외에 아래 형식의 사람이 읽을 수 있는 테이블을 **반드시 stdout에 출력하고, 결과 파일에도 TABLE 영역으로 기록**해야 한다. RESULT line만 출력하고 테이블을 생략하면 안 된다.

```text
## PATTERN: PAIR (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |      Latency | CPU% | Mem MB |
|----------|------------------|-----------|--------------|------|--------|
| 64B      |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us   | 48.2 |   12.3 |
| 1024B    |   312.50 Kmsg/s  | 320.0 MB/s|   18.44 us   | 52.1 |   14.1 |

## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |      Latency | C.CPU% | C.Mem MB | S.CPU% | S.Mem MB |
|----------|------------------|-----------|--------------|--------|----------|--------|----------|
| 64B      |   150.00 Kmsg/s  |  9.6 MB/s |   45.23 us   | 48.2   |  128.4   | 35.1   |   64.2   |
| 1024B    |   120.30 Kmsg/s  | 123.2 MB/s|   52.10 us   | 52.1   |  135.2   | 38.5   |   66.8   |
```

| 컬럼 | 단위 | 비고 |
|------|------|------|
| Throughput | echo: `Kops/s`, one-way: `Kmsg/s` | 패턴 방향별 단위 — 개별 정책 문서 섹션 8.1 참조 |
| Bandwidth | `MB/s` | 네트워크 전송량 — 개별 정책 문서 섹션 8.3 참조 |
| Latency | `us` | 마이크로초 |
| CPU% | `%` | single 리소스 메트릭, 수집 실패 시 `N/A` |
| Mem MB | `MB` | single 리소스 메트릭, 수집 실패 시 `N/A` |
| C.CPU% / C.Mem MB | `%` / `MB` | multi client 리소스 메트릭 |
| S.CPU% / S.Mem MB | `%` / `MB` | multi server 리소스 메트릭 |

### 5.2 진행 로그

실행 중 진행 상황이 stdout에 출력된다. 형식은 suite별로 다르다.

**Single:**
```text
  > Benchmarking current for PAIR...
    Testing tcp | 64B: 1 Done
    Testing tcp | 256B: 1 Done
```

**Multi:**
```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B: 1 Done
    Testing tcp | 1024B: 1 Done
```

### 5.3 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- PAIR current ipc 64B: timeout
- MULTI_STREAM current wss 65536B: no_data
```

---

## 6. 운영 모드 (공통)

| 모드 | 목적 | baseline | 판정 |
|------|------|----------|------|
| Observe | 수치 수집 | 불필요 | 실행 오류만 fail |
| Trend | 회귀 감지 | rolling (최근 N회 median, `tmp/` 소스) | threshold 초과 시 warning |
| Gate | 릴리즈 승인 | 고정 (`baseline/`) | threshold 초과 시 fail |

### 6.1 임계치

| 메트릭 | warning | fail |
|--------|---------|------|
| throughput | -10% | -15% |
| latency | +10% | +15% |

| 모드 | warning | fail |
|------|---------|------|
| Observe | 미적용 | 미적용 |
| Trend | 적용 | 미적용 |
| Gate | 적용 | 적용 |

패턴/transport별 개별 임계치 override는 `perf/thresholds.json`에서 설정 가능 (`PERF_THRESHOLDS_FILE` 환경 변수로 경로 override). 바인딩도 동일한 thresholds.json을 사용하거나 `PERF_THRESHOLDS_FILE`로 별도 파일을 지정할 수 있다. 상세 사양은 개별 정책 문서의 섹션 2.2를 참조한다.

### 6.2 Rolling baseline

1. `tmp/` 디렉터리에서 `META,status,complete`인 파일만 필터링한 뒤, 파일명 사전순 내림차순(= 최신순)으로 정렬하여 최근 N개(기본 10, `PERF_ROLLING_N`으로 override)를 수집. **mtime이 아닌 파일명 기준**.
2. 동일 키(`pattern/transport/size/metric`)별 값의 **median**을 rolling baseline으로 사용.
3. `tmp/`에는 complete/partial이 혼재하므로 반드시 `META,status,complete` 필터링을 수행한다.
4. N개 미만 시 존재하는 complete 파일 전체를 사용. 0개이면 비교 건너뛰기 + warning.

### 6.3 고정 baseline

1. `baseline/<version>.txt` 또는 `latest.txt`(symlink) 로드.
2. 동일 키로 1:1 매칭 비교.
3. 매칭되지 않는 키는 비교 제외 + warning 출력.

---

## 7. 리소스 메트릭 수집

| 메트릭 | Linux 수집 소스 | Windows 수집 소스 | 계산 방식 |
|--------|----------------|-------------------|-----------|
| `cpu_pct` | `/proc/[pid]/stat` | `GetProcessTimes()` | `(user₂+kernel₂ - user₁-kernel₁) / (elapsed × nproc) × 100` |
| `mem_mb` | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 측정 종료 시점 1회 읽기, MB 변환 |

- 측정 phase 시작/종료 시점에 2회 샘플링.
- Single: 벤치마크 프로세스 대상. Multi: server/client 별도 프로세스별 독립 측정 (`client_cpu_pct`, `client_mem_mb`, `server_cpu_pct`, `server_mem_mb`).
- 정보성(informational) 메트릭이므로 누락 시 완료 판정에 영향 없음.

---

## 8. 환경 변수 (공통)

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 0 |
| `PERF_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `PERF_TRANSPORTS` | 테스트 transport 목록 | suite/패턴별 기본값 |
| `PERF_TASKSET` | CPU pinning (`1`로 활성화, Linux: taskset, Windows: processor affinity) | 0 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 | 0 |
| `PERF_MAX_SOCKETS` | context max sockets | auto |
| `PERF_ROLLING_N` | rolling baseline 참조 파일 수 | 10 |
| `PERF_THRESHOLDS_FILE` | 임계치 override 설정 파일 경로 | `perf/thresholds.json` |

- 위 환경 변수는 core와 모든 바인딩에서 동일하게 적용된다.
- suite별 고유 환경 변수는 개별 정책 문서를 참조한다:
  - Single: [PERF_SINGLE_TEST_POLICY.md § 11](PERF_SINGLE_TEST_POLICY.md)
  - Multi: [PERF_MULTI_TEST_POLICY.md § 12](PERF_MULTI_TEST_POLICY.md)
