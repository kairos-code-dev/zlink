# zlink Single Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — single-client 벤치마크
> **Policy Version**: 1.5
> **Date**: 2026-02-24
> **Scope**: zlink single-client 성능 테스트 정책
>
> 본 정책은 `perf/single`의 C++ 벤치마크뿐 아니라 모든 바인딩 라이브러리(`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, `bindings/python`)의 single-client 성능 테스트에도 동일하게 적용된다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md) — 공통 디렉터리 구조, 통합 실행, 비교 스크립트
> **관련 문서**: [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md) — multi-client 성능 테스트

---

## 1. 측정 기준

| 항목 | 기준 |
|------|------|
| 측정 모델 | hybrid: throughput(duration) + latency(count) |
| throughput | `recv_count / duration_seconds` — 전체 one-way: `msg/s` |
| latency | count phase (패턴별 제수 적용) |
| 대표값 | median (runs > 1) |
| 결과 출력 | RESULT line |

---

## 2. 운영 모드

| 모드 | 목적 | baseline | 기본 runs | 판정 |
|------|------|----------|-----------|------|
| Observe | 수치 수집 | 불필요 | 1 | 실행 오류만 fail |
| Trend | 회귀 감지 | rolling (최근 N회 median) | 3 | threshold 초과 시 warning |
| Gate | 릴리즈 승인 | 고정 (릴리즈 시점 저장) | 5 | threshold 초과 시 fail |

- 기본 모드: **Observe**
- Baseline comparison은 Trend/Gate 모드에서만 수행한다.
- Rolling baseline(최근 N회 median)을 기본으로 하며, 고정 baseline은 릴리즈 시점에만 사용한다.

### 2.1 임계치 기본값

| 메트릭 | warning | fail |
|--------|---------|------|
| throughput | -10% | -15% |
| latency | +10% | +15% |

- Observe: 임계치 미적용
- Trend: warning만 적용
- Gate: warning + fail 적용
- 패턴/transport별 개별 임계치는 설정 파일에서 override 가능 (아래 2.2 참조)

#### 임계치 부호 규칙

| 메트릭 | 부호 의미 | 판정 방향 | fail 예시 |
|--------|-----------|-----------|-----------|
| throughput | 음수 = 성능 저하 | 변동률 ≤ 임계치 → 트리거 | 변동률 -16% ≤ -15% → fail |
| latency | 양수 = 성능 저하 | 변동률 ≥ 임계치 → 트리거 | 변동률 +16% ≥ +15% → fail |

```text
변동률 = (측정값 - baseline) / baseline × 100

throughput 예시: (140000 - 150000) / 150000 × 100 = -6.67%
  → warning(-10%) 이내 → PASS

latency 예시: (14.0 - 12.0) / 12.0 × 100 = +16.67%
  → fail(+15%) 초과 → FAIL
```

- 비교식은 단일 공식이며 부호가 방향을 결정한다.
- throughput: 변동률이 warning 값 **이하**이면 warning, fail 값 **이하**이면 fail.
- latency: 변동률이 warning 값 **이상**이면 warning, fail 값 **이상**이면 fail.

### 2.2 임계치 Override 설정 파일

| 항목 | 값 |
|------|---|
| 파일 경로 | `perf/thresholds.json` |
| 경로 override | `PERF_THRESHOLDS_FILE` 환경 변수 |
| 포맷 | JSON |

```json
{
  "PAIR/tcp/throughput": { "warning": -15, "fail": -20 },
  "PAIR/tcp/latency": { "warning": 15, "fail": 20 },
  "STREAM/*/throughput": { "warning": -8, "fail": -12 }
}
```

| 키 형식 | 설명 |
|---------|------|
| `<pattern>/<transport>/<metric>` | 특정 조합 |
| `<pattern>/*/<metric>` | 해당 패턴의 모든 transport |
| `*/<transport>/<metric>` | 모든 패턴의 해당 transport |
| `*/*/<metric>` | 전체 메트릭 기본값 override |

**우선순위** (높은 순, 구체도 기준):

| 순위 | 키 형식 | 설명 |
|------|---------|------|
| 1 | `<pattern>/<transport>/<metric>` | 정확 매칭 (최우선) |
| 2 | `<pattern>/*/<metric>` | transport만 와일드카드 |
| 3 | `*/<transport>/<metric>` | pattern만 와일드카드 |
| 4 | `*/*/<metric>` | 전체 와일드카드 (최저) |
| 5 | (없음) | 섹션 2.1 기본값 |

- 동일 구체도의 키가 충돌하면 **파일 내 먼저 나타나는 키**를 사용한다 (JSON 객체 순서).
- thresholds.json에 `warning`만 지정하고 `fail`을 생략하면 해당 조합의 `fail`은 섹션 2.1 기본값을 사용한다 (부분 override 허용). 반대도 동일.

| 상황 | 동작 |
|------|------|
| 파일 없음 | 기본값 사용 (warning/에러 없음) |
| 파싱 실패 | warning 출력 후 기본값 사용 |
| 키 누락 | 해당 조합은 기본값 사용 |

#### 스키마 유효성 검사

| 검사 항목 | 규칙 | 실패 시 |
|-----------|------|---------|
| 키 형식 | `<pattern_or_*>/<transport_or_*>/<metric>` 3-segment 필수 | warning 출력, 해당 키 무시 |
| warning/fail 값 | 숫자 (정수 또는 소수) | warning 출력, 해당 키 무시 |
| throughput warning/fail | 음수 또는 0이어야 함 | 양수이면 warning 출력 후 부호 반전 적용 |
| latency warning/fail | 양수 또는 0이어야 함 | 음수이면 warning 출력 후 부호 반전 적용 |
| unknown 필드 | `warning`, `fail` 외의 키 | 무시 (warning 없음) |

---

### 2.3 옵션 우선순위

실행 옵션이 여러 경로로 지정될 수 있는 경우 아래 우선순위를 따른다 (높은 순).

| 옵션 | CLI 인자 | 환경 변수 | 모드 기본값 |
|------|----------|-----------|------------|
| runs | `--runs N` | — | Observe=1, Trend=3, Gate=5 |
| rolling N | — | `PERF_ROLLING_N` | 10 |
| 임계치 | — | thresholds.json (2.2) | 섹션 2.1 기본값 |
| msg sizes | `--msg-sizes` | `PERF_MSG_SIZES` | 표준 6종 |
| transports | `--transports` | `PERF_TRANSPORTS` | 패턴별 기본값 |

- **CLI 인자 > 환경 변수 > 모드 기본값** 순으로 적용한다.
- `--runs`를 생략하면 현재 모드의 기본 runs를 사용한다. `--runs`를 명시하면 모드 기본값을 무시한다.

---

## 3. 테스트 유효성 기준

### 3.1 결과 상태 분류

| 상태 | 조건 | 집계 |
|------|------|------|
| success | RESULT line 정상 출력 | 유효 결과 |
| unsupported | 패턴-transport 조합 미지원 | 결과 제외, fail 아님 |
| skip | 환경 미충족 (OS, 아키텍처 등) | 결과 제외, fail 아님 |
| fail | timeout / no_data / non-zero exit | 무효 처리 |

#### 상태 판정 토큰

스크립트는 바이너리의 stdout과 종료 코드를 조합하여 상태를 판정한다.

| 상태 | 판정 기준 |
|------|-----------|
| success | exit code 0 + RESULT line 존재 |
| unsupported | stdout에 `UNSUPPORTED` 토큰 출력 + exit code 0 |
| skip | stdout에 `SKIP` 토큰 출력 + exit code 0 |
| fail | exit code ≠ 0, 또는 timeout, 또는 RESULT line 미출력 (exit 0이나 데이터 없음 = no_data) |

- `UNSUPPORTED` 토큰 형식: `UNSUPPORTED,<lib>,<pattern>,<transport>`
- `SKIP` 토큰 형식: `SKIP,<lib>,<pattern>,<transport>,<reason>`
- 동일 조합에서 RESULT line과 UNSUPPORTED/SKIP 토큰이 동시에 출력되면 **RESULT line을 우선**한다.
- STREAM 계열에서 테스트 모델 위반(예: non-STREAM server 사용, zlink STREAM client `connect()` 경로 사용)은 `UNSUPPORTED`/`SKIP` 대상이 아니다.
- 해당 구현 경로는 코드에서 삭제하고, `zlink STREAM server(bind-only) + raw client(connect)` 모델로 재구현해야 한다.
- **UNSUPPORTED 오용 금지**: §10.3에 정의된 transport가 실행 시 실패하면 반드시 `fail`로 보고한다. 정의된 transport를 `UNSUPPORTED`로 보고하여 실패를 숨기는 것을 금지한다. `UNSUPPORTED`는 정책에 정의되지 않은 pattern-transport 조합에만 사용한다. 상세 규칙은 [PERF_POLICY.md § 8.3](PERF_POLICY.md)을 참조한다.

### 3.2 유효성 규칙

1. 모든 `pattern/transport/size` 조합에서 RESULT line이 출력되어야 한다.
2. `unsupported`는 fail 집계에서 제외한다.
3. `skip`은 fail 집계에서 제외한다.
4. runs > 1인 경우 대표값은 **median**을 사용한다.
5. 동일 `pattern/transport/size/metric` 조합의 RESULT line이 **중복** 출력되면 **마지막 값**을 사용한다. 중복 자체는 에러가 아니며 warning을 출력한다.
6. RESULT line의 필드 수가 7개가 아니면 해당 라인을 무시하고 warning을 출력한다.

### 3.3 실행 순서

스크립트 1회 실행으로 요청된 모든 패턴/transport를 순차 측정한다.

```text
for pattern in [PAIR, PUBSUB, STREAM, ...]:
    for transport in [tcp, inproc, ipc, ...]:
        for size in [64, 256, 1024, ...]:
            for run in 1..N:
                binary(pattern, transport, size)   # 바이너리 1회 호출 = size 1개
            # run 간 cooldown 없음 (single은 경량 프로세스)
        # transport 전환: cooldown 없음
    # pattern 전환: cooldown 없음
```

- Single 벤치마크는 1:1 소켓이므로 전환 cooldown이 불필요하다.

### 3.4 종료 코드

| 종료 코드 | 의미 | 상황 |
|-----------|------|------|
| 0 | 성공 | 모든 조합 complete, Gate 임계치 통과 |
| 1 | 실행 오류 | 빌드 실패, 바이너리 미존재, baseline 파일 미존재, partial에서 `--save` 시도 |
| 2 | Gate 실패 | 실행은 complete이나 Gate 모드에서 fail 임계치 초과 |

- Observe/Trend 모드에서는 임계치 초과가 종료 코드에 영향을 주지 않는다 (항상 0 또는 1).
- partial 상태 자체는 종료 코드 0이다 (`--save` 미사용 시). `--save`와 함께 partial이면 종료 코드 1.
- 여러 오류 조건이 동시에 발생하면 가장 높은 종료 코드를 반환한다.

### 3.5 실패 처리: Retry 금지

실패한 조합을 자동으로 재시도하지 않는다. 상세 정책은 [PERF_POLICY.md § 8](PERF_POLICY.md)을 참조한다.

---

## 4. 결과 산출물

### 4.1 결과 파일 형식

디렉터리별로 저장 형식이 다르다.

#### tmp/ · baseline/ (기계 파싱 + 사람 열람)

```text
META,os,Linux 6.6.87.2-microsoft-standard-WSL2
META,cpu,AMD Ryzen 9 7950X
META,cores,32
META,build,Release
META,commit,abc1234
META,timestamp,2026-02-23T23:30:00+09:00
META,load_avg,0.52 0.48 0.45
META,mode,observe
META,runs,1
META,status,complete
META,expected,6
META,actual,6
RESULT,current,PAIR,tcp,64,throughput,523401.23
RESULT,current,PAIR,tcp,64,bandwidth,33.50
RESULT,current,PAIR,tcp,64,latency,12.35
RESULT,current,PAIR,tcp,64,cpu_pct,48.20
RESULT,current,PAIR,tcp,64,mem_mb,12.30
RESULT,current,PAIR,tcp,256,throughput,480123.45
RESULT,current,PAIR,tcp,256,bandwidth,122.91
RESULT,current,PAIR,tcp,256,latency,14.20
RESULT,current,PAIR,tcp,256,cpu_pct,51.30
RESULT,current,PAIR,tcp,256,mem_mb,13.10
TABLE
## PATTERN: PAIR (one-way)
### Transport: tcp
| Size   |       Throughput | Bandwidth |     Latency | CPU% |  Mem MB |
|--------|------------------|-----------|-------------|------|---------|
| 64B    |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us  | 48.2 |   12.3  |
| 256B   |   480.12 Kmsg/s  | 122.9 MB/s|   14.20 us  | 49.8 |   12.5  |
```

- META → RESULT → TABLE 세 영역으로 구성된다.
- `TABLE` 마커 이후의 내용은 RESULT 데이터를 사람이 읽을 수 있는 형식으로 포맷한 것이다 (섹션 6.2 참조).
- 기계 파싱 시 `META,`와 `RESULT,`로 시작하는 라인만 처리하면 TABLE 영역은 자연히 무시된다.

#### report/ (사람이 읽는 용도)

```text
## PATTERN: PAIR (one-way)

### Transport: tcp
| Size   |       Throughput | Bandwidth |     Latency | CPU% |  Mem MB |
|--------|------------------|-----------|-------------|------|---------|
| 64B    |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us  | 48.2 |   12.3  |
| 256B   |   480.12 Kmsg/s  | 122.9 MB/s|   14.20 us  | 49.8 |   12.5  |
```

- **TABLE만** 저장한다. META/RESULT 라인은 포함하지 않는다. `TABLE` 마커도 생략한다.
- 기계 파싱용 데이터가 필요하면 동일 실행의 `tmp/` 파일을 참조한다.

| 라인 유형 | 형식 | 설명 |
|-----------|------|------|
| `META` | `META,<key>,<value>` | 실행 환경 및 완료 상태 메타데이터 |
| `RESULT` | `RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>` | 측정 결과 |

### 4.2 META 필수 키

| 키 | 필수 | 설명 |
|----|------|------|
| `os` | MUST | OS 및 커널 버전 |
| `cpu` | MUST | CPU 모델명 |
| `cores` | MUST | 논리 코어 수 |
| `build` | MUST | 빌드 타입 (Release/Debug) |
| `commit` | MUST | git commit SHA |
| `timestamp` | MUST | 실행 시각 (ISO 8601, 로컬 시간대 offset 포함) |
| `load_avg` | SHOULD | 실행 시점 load average |
| `mode` | MUST | 운영 모드 (observe/trend/gate) |
| `runs` | MUST | 반복 횟수 |
| `status` | MUST | 완료 상태: `complete` 또는 `partial` |
| `expected` | MUST | 예상 RESULT 라인 수 (unsupported/skip 제외) |
| `actual` | MUST | 실제 RESULT 라인 수 |

### 4.3 저장 구조

```text
perf/results/
└── single/
    ├── tmp/                                              # 임시 저장 (항상)
    │   ├── perf_linux_20260224_081152.txt
    │   ├── perf_linux_20260224_093000_mytag.txt
    │   └── ...
    ├── report/                                           # 레포트 (--result, complete만)
    │   ├── perf_linux_20260224_091530.txt
    │   ├── perf_linux_20260224_143000_release.txt
    │   └── ...
    └── baseline/                                         # baseline (--save)
        ├── latest.txt                                    # 최근 baseline (symlink)
        ├── v1.5.0.txt
        ├── perf_linux_20260224_150000.txt
        └── ...
```

파일명 형식: `perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt`

- **시간대**: `YYYYMMDD_HHMMSS`는 **로컬 시간** 기준이다. 결과 파일은 로컬 머신에만 저장되므로 로컬 시간이 직관적이다.
- **태그 포함 정렬**: 태그는 타임스탬프 뒤에 위치하므로 사전순 정렬 시 동일 시각의 파일 간 순서만 영향받는다. 동일 시각+다른 태그는 태그 사전순으로 정렬된다.
- **저장 단위**: 스크립트(`run_benchmarks.sh` / `.ps1`) 1회 실행 = 1개 결과 파일. 실행에서 측정된 모든 패턴/transport/size 조합의 결과가 하나의 파일에 기록된다.
- 날짜별 하위 디렉터리를 만들지 않는다. 파일명에 날짜/시간이 포함되어 있으므로 `ls -t`로 시간순 확인이 가능하다.
- `<platform>`: `linux`, `windows`, `macos`
- `<tag>`: `--results-tag` 옵션으로 지정 (선택)

| 동작 | 옵션 | 저장 위치 | 저장 형식 | 조건 |
|------|------|-----------|-----------|------|
| 임시 저장 | (항상) | `tmp/` | META + RESULT + TABLE | complete/partial 무관 |
| 레포트 생성 | `--result` | `report/` | **TABLE만** | complete만 |
| baseline 저장 | `--save [VER]` | `baseline/` | META + RESULT + TABLE | complete만 (Gate 비교 대상) |

### 4.4 결과 저장 흐름

#### 임시 저장 (항상)

```text
실행 완료 (complete/partial 무관)
    → results/single/tmp/ 에 항상 저장 (META + RESULT + TABLE)
```

- 옵션 없이 항상 수행된다.
- Trend 모드에서 rolling baseline 소스로 사용된다 (`status=complete` 파일만 필터링).

#### `--result` (레포트 생성)

```text
실행 완료
    → status 판정 (expected == actual ?)
    → complete → results/single/report/ 에 TABLE만 저장
    → partial  → 저장하지 않음 (stdout 출력만)
```

- 용도: 사람이 결과를 확인하기 위한 레포트
- `report/`에는 **TABLE만** 저장한다 (META/RESULT 라인 미포함).
- `status=complete`인 경우에만 `report/`에 저장한다.

#### `--save [VER]` (baseline 저장)

```text
실행 완료 + complete 필수
    → VER 지정 시: results/single/baseline/<VER>.txt 에 저장
    → VER 미지정 시: results/single/baseline/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt 에 저장
    → results/single/baseline/latest.txt symlink 갱신
    → partial인 경우 에러로 중단 (baseline 저장 거부)
```

- 용도: Gate 모드에서 비교 대상 baseline
- `status=complete`인 경우에만 저장한다. `partial`이면 에러 메시지를 출력하고 저장하지 않는다.
- `latest.txt`는 가장 최근 저장된 baseline 파일에 대한 symlink이며, 저장 시 자동 갱신된다.
- **동일 버전 덮어쓰기**: `baseline/<VER>.txt`가 이미 존재하면 덮어쓴다. 부분 갱신 불가, 항상 전체 파일을 교체한다.

#### `--result --save` (동시 사용)

```text
실행 완료
    → tmp/ 에 항상 저장
    → complete → report/ + baseline/ 에도 저장
    → partial  → report/, baseline/ 에는 저장하지 않음
```

- `--result`과 `--save`는 동시 사용 가능.

#### 저장 옵션 조합 매트릭스

| 옵션 조합 | status=complete | status=partial |
|-----------|-----------------|----------------|
| (없음) | tmp/ 저장 | tmp/ 저장 |
| `--result` | tmp/ + report/ 저장 | tmp/ 저장 (report/ 미저장, warning) |
| `--save [V]` | tmp/ + baseline/ 저장 | tmp/ 저장 (baseline/ 에러, exit code 1) |
| `--result --save [V]` | tmp/ + report/ + baseline/ 저장 | tmp/ 저장 (report/ 미저장, baseline/ 에러, exit code 1) |

### 4.5 Baseline 관리

| 모드 | baseline 소스 | 생성 방법 |
|------|--------------|-----------|
| Observe | 없음 | — |
| Trend | rolling (최근 N회 median) | `tmp/`에서 `status=complete` 파일 자동 조회 |
| Gate | 고정 (`baseline/`) | `--save [VER]` |

#### 완료 판정

`--result` 및 `--save` 시 실행 종료 시점에 아래 기준으로 완료 여부를 판정한다. **complete인 경우에만** `report/` 또는 `baseline/`에 저장한다.

```text
expected = 요청된 전체 조합 수 - unsupported 수 - skip 수
actual   = 성공적으로 출력된 RESULT 라인 수

status = (expected == actual) ? "complete" : "partial"
```

| status | 조건 | 의미 |
|--------|------|------|
| `complete` | `expected == actual` | 요청된 모든 유효 조합의 결과가 정상 출력됨 |
| `partial` | `expected != actual` | 일부 조합이 timeout/no_data/fail로 누락됨 |

- `unsupported`와 `skip`은 `expected`에서 제외한다 (fail이 아니므로).
- `expected`와 `actual`은 RESULT 라인 수 기준이다 (throughput + bandwidth + latency = 조합당 3줄).
- `partial`인 경우 `--result`는 report/에 저장하지 않고, `--save`는 에러로 중단한다.

#### Rolling baseline (Trend 모드)

1. `results/single/tmp/` 디렉터리에서 `META,status,complete`인 파일만 필터링한 뒤, 파일명 사전순 내림차순(= 최신순)으로 정렬하여 최근 N개(기본 10, `PERF_ROLLING_N`으로 override)를 수집한다. **mtime이 아닌 파일명 기준**이다 (파일명에 `YYYYMMDD_HHMMSS`가 포함되어 사전순 = 시간순이 보장됨).
2. 각 파일에서 동일 키(`pattern/transport/size/metric`)의 값을 추출한다.
3. 키별로 수집된 값의 **median**을 rolling baseline으로 사용한다.
4. `tmp/`에는 complete/partial이 혼재하므로 반드시 `META,status,complete` 필터링을 수행한다.
5. N개 미만의 complete 파일만 존재하는 경우 **존재하는 파일 전체**를 사용하여 baseline을 산출한다. complete 파일이 0개이면 baseline 비교를 건너뛰고 warning을 출력한다.
6. **키별 최소 샘플 수**: N개 파일을 수집했더라도 특정 키가 일부 파일에만 존재할 수 있다. 키별로 **1개 이상**의 값이 있으면 median을 산출한다. 0개이면 해당 키의 baseline 비교를 건너뛰고 warning을 출력한다.

```text
tmp/ 내 최근 complete 10개 파일에서 PAIR/tcp/64/throughput 값:
  [147000, 148000, 148500, 149000, 149500, 150000, 151000, 151500, 152000, 153000]
                            ↓
                    median = 149750
                            ↓
                  rolling baseline = 149750

오늘 측정 대표값: 150000
변동률: (150000 - 149750) / 149750 × 100 = +0.17%
→ 임계치(-10% warning) 이내 → PASS
```

#### 고정 baseline (Gate 모드)

1. `results/single/baseline/<version>.txt` 또는 `latest.txt`(symlink)를 로드한다.
2. 동일 키(`pattern/transport/size/metric`)로 1:1 매칭하여 비교한다.
3. 매칭되지 않는 키는 비교에서 제외하고 warning을 출력한다.
4. baseline 파일이 존재하지 않으면 (`latest.txt` 포함) Gate 모드 실행을 **에러로 중단**한다 (exit code 1). baseline 없이 Gate 판정은 불가능하다.

### 4.6 보존 정책

| 디렉터리 | 최대 파일 수 | 초과 시 처리 |
|-----------|-------------|-------------|
| `tmp/` | 100 | 오래된 순 자동 삭제 |
| `report/` | 100 | 오래된 순 자동 삭제 |
| `baseline/` | 100 | 오래된 순 자동 삭제 (`latest.txt` symlink 제외) |

- 파일 수 검사는 새 파일 저장 시 수행한다.
- 삭제 대상 선정: **파일명 사전순 오름차순**(= 가장 오래된 파일)부터 삭제. mtime이 아닌 파일명 기준이다.
- `baseline/latest.txt` symlink는 삭제 대상에서 제외한다.
- rolling baseline 참조 범위: 최근 10개 (기본값, `PERF_ROLLING_N`으로 override).

---

## 5. 실행 방법

### 5.1 스크립트 실행

```bash
# Linux
perf/run_benchmarks.sh [options]

# Windows (PowerShell)
perf/run_benchmarks.ps1 [options]
```

> **정책 준수 실행기**: `run_benchmarks.sh` (Linux) / `run_benchmarks.ps1` (Windows)가 single suite의 유일한 정책 준수 실행기이다. core는 `single/run_comparison.py`, bindings는 `bindings/perf/run_policy_bench.py --suite single`을 내부 실행/비교 엔진으로 사용한다. `PERF_COMPARISON_SCRIPT` 등 환경 변수로 실행 스크립트를 우회하는 것은 허용하지 않는다.

#### 실행기 체인

```text
run_benchmarks.sh / .ps1                   # 진입점: 옵션 파싱, 빌드/실행 준비
    → (core) single/run_comparison.py
    → (bindings) bindings/perf/run_policy_bench.py --suite single
        → perf_*(.exe) / 언어별 perf runner  # 단일 pattern/transport/size 측정
```

- core wrapper는 `single/run_comparison.py`, bindings wrapper는 `bindings/perf/run_policy_bench.py`를 호출한다.
- 스크립트는 각 조합별로 벤치마크 바이너리를 호출한다.
- 바이너리는 RESULT line을 stdout에 출력하고, 스크립트가 이를 수집한다.

### 5.2 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분 가능) | `ALL` (전체 single 패턴) |
| `--mode MODE` | 운영 모드: `observe`, `trend`, `gate` (§ 2 참조) | `observe` |
| `--build-dir PATH` | 빌드 디렉터리 경로 | 자동 탐색 |
| `--runs N` | 패턴/transport/size 조합당 반복 횟수 (모드별 기본값: § 2 참조) | 1 (Observe), 3 (Trend), 5 (Gate) |
| `--reuse-build` | 기존 빌드 디렉터리 재사용 (CMake 재실행 생략) | off |
| `--pin-cpu` | CPU 고정 (Linux: taskset, Windows: processor affinity) | off |
| `--io-threads N` | context I/O threads 수 | 0 |
| `--msg-sizes LIST` | 메시지 크기 목록 (쉼표 구분). STREAM 계열은 § 10.2 참조 | `64,256,1024,65536,131072,262144` (STREAM: `64,256,1024,65536`) |
| `--transports LIST` | transport 목록 (쉼표 구분) | 패턴별 기본값 |
| `--output PATH` | 결과를 파일에 동시 출력 (tee) | stdout만 |
| `--result` | 완료 시 `results/single/report/`에 TABLE 레포트 저장 | off |
| `--save [VER]` | 완료 시 `results/single/baseline/`에 baseline 저장 | — |
| `--results-dir PATH` | 결과 저장 루트 디렉터리 override (`PATH/single/` 하위 사용) | `perf/results` |
| `--results-tag NAME` | 결과 파일명에 태그 추가 | 없음 |
| `--baseline-file PATH` | Gate 모드 비교 대상 baseline 파일 | `latest.txt` |

#### 옵션 관계

- `--output`: 임의 경로에 stdout을 tee. 저장 구조와 무관.
- 임시 저장(`tmp/`)은 옵션 없이 항상 수행된다.
- `--result`: `report/`에 TABLE만 저장. **complete인 경우에만** 저장.
- `--save [VER]`: `baseline/`에 저장. **complete인 경우에만** 저장. partial이면 에러.
- `--result`과 `--save`는 동시 사용 가능.

#### `--reuse-build` 동작

| 항목 | `--reuse-build` off (기본) | `--reuse-build` on |
|------|---------------------------|---------------------|
| CMake configure | 실행 | 생략 |
| CMake build | 실행 | 생략 |
| 바이너리 존재 확인 | build 후 자동 보장 | **실행 전 검증** |
| 바이너리 미존재 시 | — | 에러 메시지 출력 후 중단 (exit code 1) |

- `--reuse-build`는 이미 빌드된 바이너리를 그대로 사용할 때 지정한다.
- 빌드 디렉터리가 존재하지 않거나 필요한 바이너리가 누락된 경우 에러로 중단한다.

### 5.3 실행 예시

```bash
# 전체 패턴 실행 (stdout만)
perf/run_benchmarks.sh

# 특정 패턴만 실행
perf/run_benchmarks.sh --pattern PAIR

# 여러 패턴
perf/run_benchmarks.sh --pattern PAIR,PUBSUB,STREAM

# 메시지 크기/transport 제한
perf/run_benchmarks.sh --pattern PAIR --msg-sizes 64,1024 --transports tcp,inproc

# 레포트 저장 (report/)
perf/run_benchmarks.sh --result

# 레포트 + 태그
perf/run_benchmarks.sh --result --results-tag debug1

# baseline 저장 (baseline/, 타임스탬프 파일명)
perf/run_benchmarks.sh --save

# baseline 저장 (baseline/v1.5.0.txt)
perf/run_benchmarks.sh --runs 5 --save v1.5.0

# 레포트 + baseline 동시 저장
perf/run_benchmarks.sh --result --save

# 3회 반복, CPU 고정, 레포트 저장
perf/run_benchmarks.sh --runs 3 --pin-cpu --result

# 기존 빌드 재사용
perf/run_benchmarks.sh --reuse-build --pattern STREAM
```

### 5.4 바이너리 직접 실행

개별 벤치마크 바이너리를 직접 실행할 수 있다.

```bash
<binary> <lib_name> <transport> <size>
```

```bash
# 예시
./core/build/linux-x64/bin/perf_pair current tcp 1024

# STREAM 계열 바이너리 (각각 별도 바이너리)
./core/build/linux-x64/bin/perf_stream current tcp 1024
./core/build/linux-x64/bin/perf_stream_callback current tcp 1024
./core/build/linux-x64/bin/perf_stream_len32be current tcp 1024
```

| 인자 | 설명 |
|------|------|
| `lib_name` | 라이브러리 식별자 (`current`) |
| `transport` | `tcp`, `inproc`, `ipc`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |

---

## 6. 출력 형식

### 6.1 바이너리 RESULT line

각 바이너리는 `pattern/transport/size` 조합마다 아래 RESULT line을 stdout에 출력한다.

```text
RESULT,current,PAIR,tcp,1024,throughput,523401.23
RESULT,current,PAIR,tcp,1024,bandwidth,535.96
RESULT,current,PAIR,tcp,1024,latency,12.35
RESULT,current,PAIR,tcp,1024,cpu_pct,48.20
RESULT,current,PAIR,tcp,1024,mem_mb,12.30
```

| 필드 | 설명 |
|------|------|
| `lib` | 라이브러리 식별자 (`current`) |
| `pattern` | `PAIR`, `PUBSUB`, `STREAM`, `GATEWAY` 등 |
| `transport` | `tcp`, `inproc`, `ipc`, `ws`, `wss`, `tls` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput`, `bandwidth`, `latency`, `cpu_pct`, `mem_mb` |
| `value` | 수치 값 (소수점 2자리) |

| metric | 설명 | 필수 |
|--------|------|------|
| `throughput` | 단방향 수신 수 (`msg/s`) — 섹션 8.1 참조 | MUST |
| `bandwidth` | 네트워크 전송량 (MB/s) — 섹션 8.3 참조 | MUST |
| `latency` | 레이턴시 (us) | MUST |
| `cpu_pct` | 벤치마크 프로세스 CPU 사용률 (%) | Linux/Windows |
| `mem_mb` | 벤치마크 프로세스 메모리 (MB, RSS/WorkingSet 기준) | Linux/Windows |

- `cpu_pct`, `mem_mb`는 Linux와 Windows에서 수집한다.
- `cpu_pct`, `mem_mb`가 누락되어도 완료 판정(`expected`/`actual`)에 영향을 주지 않는다.

### 6.2 스크립트 결과 테이블

> **구현 필수**: 스크립트는 RESULT line 파싱 외에 아래 형식의 사람이 읽을 수 있는 테이블을 **반드시 stdout에 출력하고, 결과 파일에도 기록**해야 한다. RESULT line만 출력하고 테이블을 생략하면 안 된다.

`run_benchmarks.sh` 실행 시 패턴/transport별로 markdown table이 stdout에 출력되고, 결과 파일에도 기록된다. 디렉터리별 기록 형식:

| 디렉터리 | TABLE 기록 방식 |
|-----------|----------------|
| `tmp/` | META + RESULT 이후 `TABLE` 마커와 함께 기록 |
| `report/` | **TABLE만** 기록 (META/RESULT 없음, `TABLE` 마커도 생략) |
| `baseline/` | META + RESULT 이후 `TABLE` 마커와 함께 기록 |

```text
## PATTERN: PAIR (one-way)

### Transport: tcp
| Size   |       Throughput | Bandwidth |     Latency | CPU% |  Mem MB |
|--------|------------------|-----------|-------------|------|---------|
| 64B    |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us  | 48.2 |   12.3  |
| 256B   |   480.12 Kmsg/s  | 122.9 MB/s|   14.20 us  | 49.8 |   12.5  |
| 1024B  |   312.50 Kmsg/s  | 320.0 MB/s|   18.44 us  | 52.1 |   14.1  |


===============================================================================

## PATTERN: STREAM (one-way)

### Transport: tcp
| Size   |       Throughput | Bandwidth |     Latency | CPU% |  Mem MB |
|--------|------------------|-----------|-------------|------|---------|
| 64B    |   680.00 Kmsg/s  | 43.5 MB/s |   10.20 us  | 45.1 |   11.8  |
| 256B   |   620.30 Kmsg/s  | 158.8 MB/s|   11.80 us  | 46.5 |   12.0  |
...
```

- **패턴 간 구분선**: 패턴이 바뀔 때 `===============================================================================` 구분선을 출력한다 (첫 번째 패턴 앞에는 출력하지 않음).
- throughput 단위: `Kmsg/s` (msg/sec / 1000) — SINGLE은 전체 one-way (섹션 8.1 참조)
- bandwidth 단위: `MB/s` (메가바이트/초) — 섹션 8.3 참조
- latency 단위: `us` (마이크로초)
- CPU%: 벤치마크 프로세스 CPU 사용률
- Mem MB: 벤치마크 프로세스 메모리 (MB, RSS/WorkingSet 기준)
- transport 미지원 시: `N/A`
- 수집 실패 시: CPU%, Mem MB 컬럼은 `N/A`

### 6.3 진행 로그

실행 중 각 조합의 진행 상황이 출력된다.

```text
  > Benchmarking current for PAIR...
    Testing tcp | 64B: 1 Done
    Testing tcp | 256B: 1 Done
    Testing inproc | 64B: 1 Done
```

- `--runs 3` 시: `1 2 3 Done`
- 실패 발생 시: `(failures=1) Done`
- timeout 발생 시: failure로 기록
- transport 미지원 시: `unsupported Done`

### 6.4 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- PAIR current ipc 64B: timeout
- STREAM current wss 65536B: no_data
```

### 6.5 결과 파일 저장

사용된 옵션에 따라 결과 파일이 아래 경로에 저장된다. 파일 형식은 섹션 4.1을 참조한다.

| 옵션 | 저장 경로 |
|------|-----------|
| (항상) | `perf/results/single/tmp/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| `--result` | `perf/results/single/report/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| `--save [VER]` | `perf/results/single/baseline/<VER>.txt` 또는 `baseline/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |

### 6.6 리소스 메트릭 수집

스크립트는 각 벤치마크 프로세스의 CPU 사용률과 메모리를 측정한다.

| 메트릭 | Linux 수집 소스 | Windows 수집 소스 | 계산 방식 |
|--------|----------------|-------------------|-----------|
| `cpu_pct` | `/proc/[pid]/stat` | `GetProcessTimes()` | `(user₂+kernel₂ - user₁-kernel₁) / (elapsed × nproc) × 100` |
| `mem_mb` | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 측정 종료 시점 1회 읽기, MB 단위 변환 |

```text
[warmup] -> [settle] -> [throughput] -> [drain] -> [latency]
                         ^          ^
                     샘플₁ 수집   샘플₂ 수집
                    (stat 읽기)   (stat + RSS/WorkingSet 읽기)
```

- throughput phase 시작/종료 시점에 2회 샘플링한다.
- 리소스 메트릭은 정보성(informational)이므로 누락 시 완료 판정에 영향을 주지 않는다.

---

## 7. Test Phase

### 7.0 전체 실행 구조

```text
┌─ pattern loop ──────────────────────────────────────────────┐
│  ┌─ transport loop ──────────────────────────────────────┐  │
│  │  ┌─ size loop ─────────────────────────────────────┐  │  │
│  │  │  ┌─ run loop ───────────────────────────────┐   │  │  │
│  │  │  │  binary(pattern, transport, size)         │   │  │  │
│  │  │  │    [warmup]-[settle]-[throughput]-[drain] │   │  │  │
│  │  │  │    [latency]                              │   │  │  │
│  │  │  └───────────────────────────────────────────┘   │  │  │
│  │  └──────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

- Single은 1:1 소켓이므로 run/transport/pattern 전환 시 cooldown이 불필요하다.

### 7.1 바이너리 내부 Phase (size 1개 기준)

```text
[warmup] -> [settle] -> [throughput] -> [drain] -> [latency]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | time-based | 3s | `PERF_SINGLE_WARMUP_SECONDS` |
| settle | time-based | 300ms | `PERF_SINGLE_SETTLE_MS` |
| throughput | time-based | 5s | `PERF_SINGLE_DURATION_SECONDS` |
| drain | time-based | 300ms | `PERF_SINGLE_DRAIN_MS` |
| latency | count-based | 패턴별 | `PERF_LAT_COUNT` |

---

## 8. Throughput 측정

### 8.1 패턴 방향 분류

SINGLE 벤치마크는 모든 패턴의 throughput을 **one-way(단방향)**으로 측정한다. 1:1 소켓 쌍에서 한쪽이 send, 반대쪽이 recv하는 구조이다.

| 방향 | 단위 | 의미 | 측정 지점 | 패턴 |
|------|------|------|-----------|------|
| one-way | `msg/s` | 단방향 수신 수/초 | receiver 측 recv | 전체: PAIR, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, STREAM, STREAM_CALLBACK, STREAM_LEN32BE, PUBSUB, GATEWAY, SPOT |

- throughput: sender가 send → receiver가 recv. 1 msg = 1 message hop. receiver가 수신한 메시지 수를 카운트한다.
- latency는 별도 phase에서 RTT(왕복) 또는 단방향으로 측정한다 (섹션 9 참조).

### 8.2 계산

1. duration 구간의 수신량으로 계산한다.
2. `throughput = recv_count / duration_seconds`
3. warmup/settle/drain 구간의 데이터는 계산에서 제외한다.

### 8.3 Bandwidth (네트워크 전송량)

throughput과 메시지 크기로부터 실제 네트워크 전송량(MB/s)을 계산한다. SINGLE은 전체 one-way이므로 단일 계산식을 사용한다.

| 계산식 | 의미 |
|--------|------|
| `throughput × msg_size / 1,000,000` | 단방향 전송량 |

- 단위: `MB/s` (1 MB = 1,000,000 bytes, SI 기준)

---

## 9. Latency 측정

latency는 throughput과 분리된 count phase에서 측정한다.

### 9.1 패턴별 기본값

| 패턴 | LAT_COUNT | 제수(divisor) |
|------|-----------|---------------|
| PAIR | 500 | `lat_count * 2` |
| DEALER_DEALER | 500 | `lat_count * 2` |
| DEALER_ROUTER | 1000 | `lat_count * 2` |
| ROUTER_ROUTER | 1000 | `lat_count * 2` |
| ROUTER_ROUTER_POLL | 1000 | `lat_count * 2` |
| STREAM | 500 | `lat_count * 2` |
| STREAM_CALLBACK | 500 | `lat_count * 2` |
| STREAM_LEN32BE | 500 | `lat_count * 2` |
| PUBSUB | 500 | `received_count` |
| GATEWAY | 200 | `lat_count` |
| SPOT | 200 | `lat_count` |

### 9.2 divisor 규칙

| 유형 | divisor | 적용 패턴 |
|------|---------|-----------|
| 양방향 RTT | `lat_count * 2` | PAIR, DEALER_*, ROUTER_*, STREAM, STREAM_CALLBACK, STREAM_LEN32BE |
| 단방향 | `received_count` | PUBSUB |
| 단방향 멀티홉 | `lat_count` | GATEWAY, SPOT |

### 9.3 계산식

- RTT: `latency_us = elapsed_us / (lat_count * 2)`
- PUBSUB: `latency_us = elapsed_us / received_count`
- GATEWAY/SPOT: `latency_us = elapsed_us / lat_count`

---

## 10. Pattern & Transport Matrix

### 10.1 지원 패턴

PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, STREAM, STREAM_CALLBACK, STREAM_LEN32BE, GATEWAY, SPOT

#### 소스 파일 명명 규칙

소스 위치는 [PERF_POLICY.md § 2.0.2](PERF_POLICY.md)를 참조한다.

| 언어 | 파일 명명 패턴 | 예시 |
|------|--------------|------|
| Core (C++) | `perf_<pattern>.cpp` | `perf_stream.cpp`, `perf_pair.cpp` |
| C++ binding | `perf_<pattern>.cpp` | `perf_stream.cpp`, `perf_pair.cpp` |
| .NET | `Perf<Pattern>.cs` (PascalCase) | `PerfStream.cs`, `PerfPair.cs` |
| Java | `Perf<Pattern>.java` (PascalCase) | `PerfStream.java`, `PerfPair.java` |
| Node | `perf_<pattern>.js` (snake_case) | `perf_stream.js`, `perf_pair.js` |
| Python | `perf_<pattern>.py` (snake_case) | `perf_stream.py`, `perf_pair.py` |

- 모든 언어는 **`perf_`** 접두어를 사용한다 (PascalCase 언어는 `Perf`).
- STREAM 계열은 별도 파일 분리를 권장한다: `stream`, `stream_callback`, `stream_len32be`
- 실행 스크립트: `run_benchmarks.sh` / `.ps1` (모든 언어 동일)

#### STREAM 계열 패턴

| 패턴 | server 수신 방식 | 소스 파일 | 바이너리 |
|------|-----------------|-----------|----------|
| STREAM | 기본 recv | `perf_stream.cpp` | `perf_stream` |
| STREAM_CALLBACK | callback dispatch | `perf_stream_callback.cpp` | `perf_stream_callback` |
| STREAM_LEN32BE | callback + len32be framing | `perf_stream_len32be.cpp` | `perf_stream_len32be` |

- Core는 각 패턴을 **별도 소스 파일 / 별도 바이너리**로 작성한다.
- bindings는 동일 동작을 보장하는 범위에서 단일 runner 내부의 패턴별 분기 구현을 허용한다.
- 소스 경로: `perf/single/current/`
- 세 패턴은 동일한 transport, size 설정을 공유한다.
- **Wire protocol**: client는 `[4B length (big-endian)][payload]` (len32be framing)으로 통일한다. server 수신 방식만 패턴별로 다르다. 상세는 [PERF_POLICY.md § 2.0.3 Wire Protocol](PERF_POLICY.md)을 참조한다.
- 수신 방식만 다르므로 throughput/latency 차이를 직접 비교할 수 있다.
- STREAM 계열의 서버는 반드시 zlink STREAM 소켓으로 `bind`해야 하며, DEALER/ROUTER/PUBSUB 등으로 대체할 수 없다.
- 클라이언트는 raw transport(`tcp`,`tls`,`ws`,`wss`)로 `connect`해야 하며, zlink STREAM 소켓의 client `connect()` 경로를 사용하지 않는다.
- 위 모델을 위반한 구현은 정책 위반이므로 해당 코드를 삭제하고 정책 모델로 다시 구현해야 한다.
- 위반 구현에서 나온 실행 결과는 정책 산출물로 인정하지 않는다.

### 10.2 표준 메시지 크기

| 패턴군 | 크기 |
|--------|------|
| PAIR / PUBSUB / DEALER / ROUTER | `[64, 256, 1024, 65536, 131072, 262144]` |
| STREAM / STREAM_CALLBACK / STREAM_LEN32BE | `[64, 256, 1024, 65536]` |
| GATEWAY / SPOT | `[64, 256, 1024, 65536, 131072, 262144]` |

### 10.3 transport

| 패턴군 | transport |
|--------|-----------|
| PAIR / PUBSUB / DEALER / ROUTER | tcp, tls, ws, wss, inproc, ipc (Windows: ipc 제외) |
| STREAM / STREAM_CALLBACK / STREAM_LEN32BE | tcp, tls, ws, wss |
| GATEWAY / SPOT | tcp, tls, ws, wss |

---

## 11. Environment Variables

### 11.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 0 |
| `PERF_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `PERF_TRANSPORTS` | 테스트 transport 목록 | 패턴별 기본값 |
| `PERF_TASKSET` | CPU pinning (`1`로 활성화, Linux: taskset, Windows: processor affinity) | 0 |
| `PERF_LAT_COUNT` | latency count override | 패턴별 기본값 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`로 활성화) | 0 |

### 11.2 Phase 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_WARMUP_SECONDS` | warmup 시간(초) | 3 |
| `PERF_SINGLE_DURATION_SECONDS` | throughput 측정 시간(초) | 5 |
| `PERF_SINGLE_SETTLE_MS` | settle 대기(ms) | 300 |
| `PERF_SINGLE_DRAIN_MS` | drain 대기(ms) | 300 |

### 11.3 STREAM 전용

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_STREAM_TIMEOUT_MS` | socket timeout | 5000 |
| `PERF_STREAM_DRAIN_TIMEOUT_MS` | drain timeout | 5000 |
| `PERF_STREAM_HWM` | HWM | 100000 |
| `PERF_STREAM_SERVER_IO_THREADS` | server io threads | 4 |
| `PERF_STREAM_CLIENT_THREADS` | client worker threads | auto |

### 11.4 기타

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout | 120 |
| `PERF_MAX_SOCKETS` | context max sockets | auto |
| `PERF_ROLLING_N` | rolling baseline 참조 파일 수 | 10 |
| `PERF_THRESHOLDS_FILE` | 임계치 override 설정 파일 경로 | `perf/thresholds.json` |

---

## 12. 구현 제약

### 12.1 측정 경로(hot path) lock 사용 금지

벤치마크 바이너리의 **측정 구간(duration phase)에서 실행되는 hot path**에 `std::mutex`, `std::condition_variable` 등 blocking synchronization primitive를 사용하지 않는다.

| 구분 | 허용 | 금지 |
|------|------|------|
| hot path (send/recv/callback 루프) | `std::atomic`, lock-free queue, SPSC ring buffer | `std::mutex`, `std::condition_variable`, `std::shared_mutex` |
| cold path (setup/teardown/결과 출력) | 제한 없음 | — |

- **이유**: lock contention이 throughput/latency 측정값에 포함되어 벤치마크 대상(라이브러리 성능)이 아닌 동기화 오버헤드를 측정하게 된다.
- **dispatch callback 패턴 (STREAM_CALLBACK, STREAM_LEN32BE)**: I/O 스레드에서 호출되는 dispatch callback과 측정 스레드 간 데이터 전달에 lock 대신 `std::atomic` 카운터 또는 lock-free queue를 사용한다.
- throughput 측정 시 callback에서 `atomic_fetch_add`로 카운트만 증가시키고, 패킷 복사/큐잉을 하지 않는 **direct count mode**를 기본으로 한다.
- latency 측정 등 패킷 내용이 필요한 경우에만 큐잉을 허용하되, lock-free 자료구조를 사용한다.

### 12.2 불필요한 메모리 할당/복사 금지

벤치마크는 **라이브러리 자체의 성능만 온전히 측정**해야 한다. 측정 구간(duration phase)에서 벤치마크 코드가 유발하는 불필요한 메모리 할당·복사는 측정 결과를 왜곡하므로 금지한다.

| 구분 | 권장 | 금지 |
|------|------|------|
| 송신 버퍼 | warmup 전 사전 할당, duration 내 재사용 | 매 send마다 `std::vector` 생성/resize |
| 수신 버퍼 | 고정 크기 버퍼 또는 pool | 매 recv마다 동적 할당 |
| 수신 데이터 | 내용 검증 불필요 시 카운트만 증가 | 수신 payload를 별도 컨테이너에 복사 |
| dispatch callback | `atomic_fetch_add`로 카운트 (direct count mode) | 패킷을 `std::deque`에 push_back |
| routing_id | 필요 시 고정 버퍼에 1회 저장 | 매 메시지마다 `std::vector<unsigned char>` 할당 |
| 카운터/통계 | `std::atomic<int64_t>` | 구조체를 큐에 push |

- **원칙**: duration phase에서 벤치마크 인프라 코드의 `malloc`/`new`/`vector::push_back` 호출이 0에 수렴해야 한다. 측정 결과에 라이브러리 외 오버헤드가 포함되면 패턴 간 비교(예: STREAM vs STREAM_CALLBACK)가 공정하지 않다.
- warmup phase 이전(setup/connect)과 drain 이후(결과 출력/정리)에서는 할당/복사에 제한이 없다.
- `zlink_msg_data()` 반환 포인터를 직접 참조하여 불필요한 복사를 피한다. 내용 검증이 필요 없는 throughput 측정에서는 payload를 읽지 않는다.

---

## Appendix: 계산 레퍼런스

```python
import statistics

def aggregate_runs(values):
    """runs > 1인 경우 대표값 산출"""
    if not values:
        return 0.0
    return statistics.median(values)

def throughput_per_sec(recv_count, duration_seconds):
    return recv_count / max(1, duration_seconds)

def bandwidth_mbps(throughput, msg_size):
    """SINGLE은 전체 one-way: 단방향"""
    return throughput * msg_size / 1_000_000

def latency_rtt_us(elapsed_us, lat_count):
    """PAIR, DEALER_*, ROUTER_*, STREAM*"""
    return elapsed_us / max(1, lat_count * 2)

def latency_oneway_us(elapsed_us, count):
    """PUBSUB: count=received_count, GATEWAY/SPOT: count=lat_count"""
    return elapsed_us / max(1, count)
```
