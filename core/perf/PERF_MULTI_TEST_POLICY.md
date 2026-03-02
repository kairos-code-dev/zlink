# zlink Multi Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — multi-client 벤치마크
> **Policy Version**: 1.6
> **Date**: 2026-02-27
> **Scope**: zlink multi-client 성능 테스트 정책
>
> 본 정책은 `perf/multi`의 C++ 벤치마크뿐 아니라 모든 바인딩 라이브러리(`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, `bindings/python`)의 multi-client 성능 테스트에도 동일하게 적용된다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md) — 공통 디렉터리 구조, 통합 실행, 비교 스크립트
> **관련 문서**: [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) — single-client 성능 테스트

---

## 1. 측정 기준

| 항목 | 기준 |
|------|------|
| 측정 모델 | time-based, 패턴별 phase: echo/one-way 모두 2-phase(throughput→latency) |
| throughput | `recv_count / duration_seconds` — echo 패턴: `ops/s`, one-way 패턴: `msg/s` |
| latency | echo/one-way 모두 latency 전용 phase에서 측정 |
| 대표값 | median (runs > 1) |
| 기본 runs | 3 |
| 결과 출력 | RESULT line |

### 1.1 프로세스 모델

Multi 벤치마크는 **server/client 별도 프로세스**로 동작한다.

| 역할 | 바이너리 | 책임 |
|------|----------|------|
| server | `perf_multi_<pattern>_server(.exe)` | bind, relay/echo, server-side 리소스 보고 |
| client | `perf_multi_<pattern>_client(.exe)` | connect, 패턴별 phase 정책에 따라 throughput/latency 측정, client-side 리소스 보고 |

```text
┌─ server process ─────────────────────┐    ┌─ client process ──────────────────────┐
│  bind(endpoint)                      │    │  connect(endpoint) × N clients        │
│  relay/echo received messages        │◄──►│  phase별 throughput/latency 측정         │
│  RESULT: server_cpu_pct, server_mem_mb│    │  RESULT: throughput, latency, p95/p99, │
│  READY/DONE protocol on stdout       │    │         client_cpu_pct, client_mem_mb  │
└──────────────────────────────────────┘    └───────────────────────────────────────┘
                        ▲                                      ▲
                        └────── 스크립트가 양쪽 프로세스를 관리 ──┘
```

#### 프로세스 간 조정 프로토콜

| 단계 | 동작 |
|------|------|
| 1. server 시작 | 스크립트가 server 바이너리를 spawn |
| 2. server READY | server가 bind 완료 후 stdout에 `READY,<endpoint>` 출력 |
| 3. client 시작 | 스크립트가 READY를 읽은 후 client 바이너리를 spawn (`--endpoint <endpoint>`) |
| 4. 측정 수행 | client가 패턴별 phase 정책(echo 2-phase / one-way 2-phase)으로 측정 |
| 5. client 종료 | client가 RESULT line 출력 후 종료 (exit code 0) |
| 6. server 종료 | 스크립트가 server에 SIGTERM (Linux) / TerminateProcess (Windows) 전송, server가 RESULT line 출력 후 종료 |

- server는 client 종료까지 상시 대기하며 relay/echo를 수행한다.
- phase 전환은 패턴별로 제어한다: echo는 client가 phase를 제어하고 server는 relay/echo 대기, one-way(server-push)는 server/client가 동일 순서의 phase를 수행한다.
- 스크립트는 양쪽 프로세스의 stdout을 수집하고, 종료 코드를 확인하여 결과를 합산한다.

#### 소스 파일 구조

```text
perf/multi/
├── common/
│   ├── perf_common.hpp                # 공통 (settings, result, utilities)
│   └── perf_common_multi.hpp          # multi 설정
├── current/
│   ├── perf_multi_<pattern>_server.cpp    # server (server)
│   ├── perf_multi_<pattern>_client.cpp    # client (client)
│   └── ...
```

- 모든 패턴은 `_server.cpp` / `_client.cpp` **별도 소스 파일 / 별도 바이너리**로 작성한다.
- 공통 로직(settings 해석, RESULT 출력, TLS 설정 등)은 `perf_common_multi.hpp`에 유지한다.

---

## 2. 운영 모드

| 모드 | 목적 | baseline | 기본 runs | 판정 |
|------|------|----------|-----------|------|
| Observe | 수치 수집 | 불필요 | 3 | 실행 오류만 fail |
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

throughput 예시: (130000 - 150000) / 150000 × 100 = -13.33%
  → warning(-10%) 초과 → WARNING

latency 예시: (52.0 - 45.0) / 45.0 × 100 = +15.56%
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
  "MULTI_DEALER_DEALER/tcp/throughput": { "warning": -15, "fail": -20 },
  "MULTI_STREAM/*/latency": { "warning": 15, "fail": 20 },
  "*/wss/throughput": { "warning": -12, "fail": -18 }
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
| runs | `--runs N` | — | Observe=3, Trend=3, Gate=5 |
| rolling N | — | `PERF_ROLLING_N` | 10 |
| 임계치 | — | thresholds.json (2.2) | 섹션 2.1 기본값 |
| msg sizes | `--msg-sizes` | `PERF_MSG_SIZES` | 표준 6종 |
| transports | `--transports` | `PERF_TRANSPORTS` | `tcp,tls,ws,wss` |
| clients | `--multi-clients` | `PERF_MULTI_CLIENTS` | 1000 |

- **CLI 인자 > 환경 변수 > 모드 기본값** 순으로 적용한다.
- `--runs`를 생략하면 현재 모드의 기본 runs를 사용한다. `--runs`를 명시하면 모드 기본값을 무시한다.

---

## 3. 테스트 유효성 기준

### 3.1 결과 상태 분류

| 상태 | 조건 | 집계 |
|------|------|------|
| success | RESULT line 정상 출력 | 유효 결과 |
| unsupported | 패턴-transport 조합 미지원 | 결과 제외, fail 아님 |
| skip | 환경 미충족 (OS, 아키텍처, nofile limit 등) | 결과 제외, fail 아님 |
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
- MULTI_STREAM 계열에서 테스트 모델 위반(예: non-STREAM server 사용, zlink STREAM client `connect()` 경로 사용)은 `UNSUPPORTED`/`SKIP` 대상이 아니다.
- 해당 구현 경로는 코드에서 삭제하고, `zlink STREAM server(bind-only) + raw client(connect)` 모델로 재구현해야 한다.
- **UNSUPPORTED 오용 금지**: §11.3에 정의된 transport가 실행 시 실패하면 반드시 `fail`로 보고한다. 정의된 transport를 `UNSUPPORTED`로 보고하여 실패를 숨기는 것을 금지한다. `UNSUPPORTED`는 정책에 정의되지 않은 pattern-transport 조합에만 사용한다. 상세 규칙은 [PERF_POLICY.md § 8.4](PERF_POLICY.md)을 참조한다.

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
for pattern in [MULTI_DEALER_DEALER, MULTI_PUBSUB, ...]:
    for transport in [tcp, tls, ws, wss]:
        for run in 1..N:
            spawn server(pattern, transport)         # server 프로세스 시작
            wait READY                               # server stdout에서 READY,<endpoint> 대기
            spawn client(pattern, transport, all_sizes, endpoint)  # client 프로세스 시작 (전체 size 순회)
            wait client exit                         # client 종료 대기, RESULT line 수집
            stop server                              # server 종료, server RESULT line 수집
            run_cooldown                             # 3s (PERF_MULTI_RUN_COOLDOWN_MS)
        transport_transition_cooldown                # 3s (PERF_MULTI_TRANSPORT_TRANSITION_MS)
    pattern_transition_cooldown                      # 3s (PERF_MULTI_PATTERN_TRANSITION_MS)
```

| 전환 구간 | 기본값 | 환경 변수 | 이유 |
|-----------|--------|-----------|------|
| run 간 cooldown | 3000ms | `PERF_MULTI_RUN_COOLDOWN_MS` | 동일 조합 반복 run 사이 안정화 |
| transport 전환 | 3000ms | `PERF_MULTI_TRANSPORT_TRANSITION_MS` | 이전 transport 소켓 정리 (TIME_WAIT 해소) |
| pattern 전환 | 3000ms | `PERF_MULTI_PATTERN_TRANSITION_MS` | 이전 패턴의 전체 클라이언트 소켓 정리 |

- Multi 벤치마크는 대량의 클라이언트 소켓(1000~10000)을 사용하므로, transport/pattern 전환 시 OS 소켓 리소스 해제를 위한 충분한 대기가 필요하다.
- 전환 cooldown은 이전 server/client 프로세스 종료 후 다음 server 실행 전에 **스크립트 레벨**에서 `sleep`으로 수행한다.

### 3.4 종료 코드

| 종료 코드 | 의미 | 상황 |
|-----------|------|------|
| 0 | 성공 | 모든 조합 complete, Gate 임계치 통과 |
| 1 | 실행 오류 | 빌드 실패, 바이너리 미존재, baseline 파일 미존재, partial에서 `--save` 시도 |
| 2 | Gate 실패 | 실행은 complete이나 Gate 모드에서 fail 임계치 초과 |

- Observe/Trend 모드에서는 임계치 초과가 종료 코드에 영향을 주지 않는다 (항상 0 또는 1).
- partial 상태 자체는 종료 코드 0이다 (`--save` 미사용 시). `--save`과 함께 partial이면 종료 코드 1.
- 여러 오류 조건이 동시에 발생하면 가장 높은 종료 코드를 반환한다.

### 3.5 실패 처리: Retry 금지

실패한 조합을 자동으로 재시도하지 않는다. 상세 정책은 [PERF_POLICY.md § 8](PERF_POLICY.md)을 참조한다.

### 3.6 코어 로직 인라인 원칙

각 벤치마크 소스 파일은 해당 패턴의 zlink API 사용법을 명시적으로 보여주는 샘플 역할을 해야 한다. 소켓 생성, bind/connect, send/recv 루프, phase 제어는 각 파일에 인라인으로 존재해야 하며, 외부 공통 함수 한 줄로 위임하는 것을 금지한다. 단, **동일 파일 내 extract method(의미 단위 함수 분리)** 는 허용/권장한다. 상세 규칙은 [PERF_POLICY.md § 8.5](PERF_POLICY.md)를 참조한다.

예외: STREAM client(`core/perf/common/streamclient/`)는 검증 인프라 코드로
분류하며 공통 모듈화를 허용한다. 단, multi 실행 경로/phase 정책 준수는
`multi` suite에서 보장해야 한다.

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
META,runs,3
META,patterns,MULTI_DEALER_DEALER
META,transports,tcp
META,msg_sizes,64,256
META,clients,1000
META,pin_cpu,off
META,warmup_seconds,3
META,duration_seconds,5
META,status,complete
META,expected,6
META,actual,6
RESULT,current,MULTI_DEALER_DEALER,tcp,64,throughput,150000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,64,bandwidth,9.60
RESULT,current,MULTI_DEALER_DEALER,tcp,64,latency,45.23
RESULT,current,MULTI_DEALER_DEALER,tcp,64,client_cpu_pct,48.20
RESULT,current,MULTI_DEALER_DEALER,tcp,64,client_mem_mb,128.40
RESULT,current,MULTI_DEALER_DEALER,tcp,64,server_cpu_pct,35.10
RESULT,current,MULTI_DEALER_DEALER,tcp,64,server_mem_mb,64.20
RESULT,current,MULTI_DEALER_DEALER,tcp,256,throughput,135000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,256,bandwidth,34.56
RESULT,current,MULTI_DEALER_DEALER,tcp,256,latency,52.10
RESULT,current,MULTI_DEALER_DEALER,tcp,256,client_cpu_pct,52.10
RESULT,current,MULTI_DEALER_DEALER,tcp,256,client_mem_mb,135.20
RESULT,current,MULTI_DEALER_DEALER,tcp,256,server_cpu_pct,38.50
RESULT,current,MULTI_DEALER_DEALER,tcp,256,server_mem_mb,66.80
TABLE
## Execution Options
| Option           | Value                              |
|------------------|------------------------------------|
| mode             | observe                            |
| runs             | 3                                  |
| patterns         | MULTI_DEALER_DEALER                |
| transports       | tcp                                |
| msg_sizes        | 64, 256                            |
| clients          | 1000                               |
| pin_cpu          | off                                |
| warmup_seconds   | 3                                  |
| duration_seconds | 5                                  |

===============================================================================

## PATTERN: MULTI_DEALER_DEALER (one-way)
### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |  9.6 MB/s |    45.23 us  |    61.40 us  |    79.85 us  |  35.1  |   64.2   |
| 256B     |   135.00 Kmsg/s  | 34.6 MB/s |    52.10 us  |    70.55 us  |    92.10 us  |  38.5  |   66.8   |
```

- META → RESULT → TABLE 세 영역으로 구성된다.
- `TABLE` 마커 이후에 `## Execution Options` 헤더(실행 옵션 테이블)를 먼저 출력하고, 이어서 패턴별 결과 테이블을 출력한다 (섹션 6.2 참조).
- 기계 파싱 시 `META,`와 `RESULT,`로 시작하는 라인만 처리하면 TABLE 영역은 자연히 무시된다.

#### report/ (사람이 읽는 용도)

```text
## Execution Options
| Option           | Value                              |
|------------------|------------------------------------|
| mode             | observe                            |
| runs             | 3                                  |
| patterns         | MULTI_DEALER_DEALER                |
| transports       | tcp                                |
| msg_sizes        | 64, 256                            |
| clients          | 1000                               |
| pin_cpu          | off                                |
| warmup_seconds   | 3                                  |
| duration_seconds | 5                                  |

===============================================================================

## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |  9.6 MB/s |    45.23 us  |    61.40 us  |    79.85 us  |  35.1  |   64.2   |
| 256B     |   135.00 Kmsg/s  | 34.6 MB/s |    52.10 us  |    70.55 us  |    92.10 us  |  38.5  |   66.8   |
```

- **실행 옵션 헤더 + TABLE**을 저장한다. META/RESULT 라인은 포함하지 않는다. `TABLE` 마커도 생략한다.
- `## Execution Options` 섹션은 실행 시 사용된 옵션을 테이블로 출력한다.
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
| `patterns` | MUST | 실행된 패턴 목록 (쉼표 구분) |
| `transports` | MUST | 실행된 transport 목록 (쉼표 구분) |
| `msg_sizes` | MUST | 메시지 크기 목록 (쉼표 구분) |
| `clients` | MUST | 클라이언트 소켓 수 |
| `pin_cpu` | MUST | CPU pinning 사용 여부 (`on`/`off`) |
| `warmup_seconds` | MUST | warmup 시간(초) |
| `duration_seconds` | MUST | 측정 시간(초) |
| `status` | MUST | 완료 상태: `complete` 또는 `partial` |
| `expected` | MUST | 예상 RESULT 라인 수 (unsupported/skip 제외) |
| `actual` | MUST | 실제 RESULT 라인 수 |

### 4.3 저장 구조

```text
perf/results/
└── multi/
    ├── tmp/                                              # 임시 저장 (항상)
    │   ├── perf_linux_20260224_081152.txt
    │   ├── perf_linux_20260224_093000_mytag.txt
    │   └── ...
    ├── report/                                           # 레포트 (--result, complete/partial)
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
- **저장 단위**: 스크립트(`run_benchmarks_multi.sh` / `.ps1`) 1회 실행 = 1개 결과 파일. 실행에서 측정된 모든 패턴/transport/size 조합의 결과가 하나의 파일에 기록된다.
- 날짜별 하위 디렉터리를 만들지 않는다. 파일명에 날짜/시간이 포함되어 있으므로 `ls -t`로 시간순 확인이 가능하다.
- `<platform>`: `linux`, `windows`, `macos`
- `<tag>`: `--results-tag` 옵션으로 지정 (선택)

| 동작 | 옵션 | 저장 위치 | 저장 형식 | 조건 |
|------|------|-----------|-----------|------|
| 임시 저장 | (항상) | `tmp/` | META + RESULT + TABLE | complete/partial 무관 |
| 레포트 생성 | `--result` | `report/` | **실행 옵션 헤더 + TABLE** | complete/partial 무관 |
| baseline 저장 | `--save [VER]` | `baseline/` | META + RESULT + TABLE | complete만 (Gate 비교 대상) |

### 4.4 결과 저장 흐름

#### 임시 저장 (항상)

```text
실행 완료 (complete/partial 무관)
    → results/multi/tmp/ 에 항상 저장 (META + RESULT + TABLE)
```

- 옵션 없이 항상 수행된다.
- Trend 모드에서 rolling baseline 소스로 사용된다 (`status=complete` 파일만 필터링).

#### `--result` (레포트 생성)

```text
실행 완료
    → results/multi/report/ 에 실행 옵션 헤더 + TABLE 저장 (complete/partial 무관)
```

- 용도: 사람이 결과를 확인하기 위한 레포트
- `report/`에는 **실행 옵션 헤더 + TABLE**을 저장한다 (META/RESULT 라인 미포함).
- `status=partial`인 경우에도 저장한다. 실패한 조합의 결과가 누락된 채로 저장되며, 실패 요약(§ 6.4)이 포함된다.

#### `--save [VER]` (baseline 저장)

```text
실행 완료 + complete 필수
    → VER 지정 시: results/multi/baseline/<VER>.txt 에 저장
    → VER 미지정 시: results/multi/baseline/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt 에 저장
    → results/multi/baseline/latest.txt symlink 갱신
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
    → report/ 에 항상 저장 (complete/partial 무관)
    → complete → baseline/ 에도 저장
    → partial  → baseline/ 에는 저장하지 않음
```

- `--result`과 `--save`는 동시 사용 가능.

#### 저장 옵션 조합 매트릭스

| 옵션 조합 | status=complete | status=partial |
|-----------|-----------------|----------------|
| (없음) | tmp/ 저장 | tmp/ 저장 |
| `--result` | tmp/ + report/ 저장 | tmp/ + report/ 저장 |
| `--save [V]` | tmp/ + baseline/ 저장 | tmp/ 저장 (baseline/ 에러, exit code 1) |
| `--result --save [V]` | tmp/ + report/ + baseline/ 저장 | tmp/ + report/ 저장 (baseline/ 에러, exit code 1) |

### 4.5 Baseline 관리

| 모드 | baseline 소스 | 생성 방법 |
|------|--------------|-----------|
| Observe | 없음 | — |
| Trend | rolling (최근 N회 median) | `tmp/`에서 `status=complete` 파일 자동 조회 |
| Gate | 고정 (`baseline/`) | `--save [VER]` |

#### 완료 판정

`--result` 및 `--save` 시 실행 종료 시점에 아래 기준으로 완료 여부를 판정한다. `--result`는 complete/partial 무관하게 `report/`에 저장한다. `--save`는 **complete인 경우에만** `baseline/`에 저장한다.

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
- `expected`와 `actual`은 RESULT 라인 수 기준이다 (throughput + bandwidth + latency + latency_p95 + latency_p99 = 조합당 5줄).
- `partial`인 경우 `--result`는 report/에 그대로 저장하고, `--save`는 에러로 중단한다.

#### Rolling baseline (Trend 모드)

1. `results/multi/tmp/` 디렉터리에서 `META,status,complete`인 파일만 필터링한 뒤, 파일명 사전순 내림차순(= 최신순)으로 정렬하여 최근 N개(기본 10, `PERF_ROLLING_N`으로 override)를 수집한다. **mtime이 아닌 파일명 기준**이다 (파일명에 `YYYYMMDD_HHMMSS`가 포함되어 사전순 = 시간순이 보장됨).
2. 각 파일에서 동일 키(`pattern/transport/size/metric`)의 값을 추출한다.
3. 키별로 수집된 값의 **median**을 rolling baseline으로 사용한다.
4. `tmp/`에는 complete/partial이 혼재하므로 반드시 `META,status,complete` 필터링을 수행한다.
5. N개 미만의 complete 파일만 존재하는 경우 **존재하는 파일 전체**를 사용하여 baseline을 산출한다. complete 파일이 0개이면 baseline 비교를 건너뛰고 warning을 출력한다.
6. **키별 최소 샘플 수**: N개 파일을 수집했더라도 특정 키가 일부 파일에만 존재할 수 있다. 키별로 **1개 이상**의 값이 있으면 median을 산출한다. 0개이면 해당 키의 baseline 비교를 건너뛰고 warning을 출력한다.

```text
tmp/ 내 최근 complete 10개 파일에서 MULTI_DEALER_DEALER/tcp/64/throughput 값:
  [145000, 147000, 148000, 149000, 149500, 150000, 151000, 151500, 152000, 153000]
                            ↓
                    median = 149750
                            ↓
                  rolling baseline = 149750

오늘 측정 대표값: 150000
변동률: (150000 - 149750) / 149750 × 100 = +0.17%
→ 임계치(-10% warning) 이내 → PASS
```

#### 고정 baseline (Gate 모드)

1. `results/multi/baseline/<version>.txt` 또는 `latest.txt`(symlink)를 로드한다.
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
# core (Linux)
perf/run_benchmarks_multi.sh [options]

# core (Windows PowerShell)
perf/run_benchmarks_multi.ps1 [options]

# bindings (Linux, 예: python)
perf/multi/run_benchmarks.sh [options]
```

> **정책 준수 실행기**: core는 `run_benchmarks_multi.sh` / `.ps1`, bindings는 `multi/run_benchmarks.sh` / `.ps1`이 multi suite의 유일한 정책 준수 실행기이다 ([PERF_POLICY.md § 3.1](PERF_POLICY.md) 참조). core는 `multi/run_comparison.py`, bindings는 `bindings/perf/run_policy_bench.py --suite multi`를 내부 실행/비교 엔진으로 사용한다. `PERF_COMPARISON_SCRIPT` 등 환경 변수로 실행 스크립트를 우회하는 것은 허용하지 않는다.

#### 실행기 체인

```text
run_benchmarks_multi.sh / .ps1                             # 진입점: 옵션 파싱, 빌드/실행 준비
    → (core) multi/run_comparison.py
    → (bindings) bindings/perf/run_policy_bench.py --suite multi
        → perf_multi_*_server(.exe) / 언어별 server runner  # server 프로세스
        → perf_multi_*_client(.exe) / 언어별 client runner  # client 프로세스
```

- core wrapper는 `multi/run_comparison.py`, bindings wrapper는 `bindings/perf/run_policy_bench.py`를 호출한다.
- 스크립트는 각 pattern/transport 조합별로 **server → READY 대기 → client** 순서로 두 프로세스를 관리한다.
- server/client 양쪽 바이너리가 RESULT line을 stdout에 출력하고, 스크립트가 이를 합산 수집한다.

### 5.2 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분 가능) | 전체 MULTI_* 패턴 |
| `--mode MODE` | 운영 모드: `observe`, `trend`, `gate` (§ 2 참조) | `observe` |
| `--build-dir PATH` | 빌드 디렉터리 경로 | 자동 탐색 |
| `--runs N` | 패턴/transport/size 조합당 반복 횟수 (모드별 기본값: § 2 참조) | 3 (Observe/Trend), 5 (Gate) |
| `--reuse-build` | 기존 빌드 디렉터리 재사용 (configure/build 생략) | off |
| `--clean-build` | 빌드 디렉터리 삭제 후 클린 configure/build 수행 | off (기본은 증분 빌드) |
| `--pin-cpu` | CPU 고정 (Linux: taskset, Windows: processor affinity) | off |
| `--io-threads N` | context I/O threads 수 | 0 |
| `--msg-sizes LIST` | 메시지 크기 목록 (쉼표 구분). STREAM 계열은 § 11.2 참조 | `64,256,1024,65536,131072,262144` (STREAM: `64,256,1024,65536`) |
| `--transports LIST` | transport 목록 (쉼표 구분) | `tcp,tls,ws,wss` |
| `--output PATH` | 결과를 파일에 동시 출력 (tee) | stdout만 |
| `--result` | `results/multi/report/`에 TABLE 레포트 저장 (complete/partial 무관) | off |
| `--save [VER]` | 완료 시 `results/multi/baseline/`에 baseline 저장 | — |
| `--results-dir PATH` | 결과 저장 루트 디렉터리 override (`PATH/multi/` 하위 사용) | `perf/results` |
| `--results-tag NAME` | 결과 파일명에 태그 추가 | 없음 |
| `--baseline-file PATH` | Gate 모드 비교 대상 baseline 파일 | `latest.txt` |
| `--multi-warmup-seconds N` | warmup 시간(초) | 3 |
| `--multi-duration-seconds N` | 측정 시간(초) | 5 |
| `--multi-clients N` | 클라이언트 소켓 수 | 1000 |
| `--multi-hwm N` | 소켓 HWM | 1000 |
| `--multi-sndhwm N` | 소켓 송신 HWM | `--multi-hwm` |
| `--multi-rcvhwm N` | 소켓 수신 HWM | `--multi-hwm` |
| `--multi-sndtimeo-ms N` | 송신 타임아웃(ms) | 200 |
| `--multi-rcvtimeo-ms N` | 수신 타임아웃(ms) | 200 |
| `--multi-connect-concurrency N` | 동시 연결 수 | auto |
| `--multi-drain-ms N` | drain 대기(ms) | 패턴별 기본값 |
| `--multi-transport-transition-ms N` | transport 전환 cooldown(ms) | 3000 |
| `--multi-pattern-transition-ms N` | pattern 전환 cooldown(ms) | 3000 |

#### 옵션 관계

- `--output`: 임의 경로에 stdout을 tee. 저장 구조와 무관.
- 임시 저장(`tmp/`)은 옵션 없이 항상 수행된다.
- `--result`: `report/`에 실행 옵션 헤더 + TABLE 저장. **complete/partial 무관**하게 저장.
- `--save [VER]`: `baseline/`에 저장. **complete인 경우에만** 저장. partial이면 에러.
- `--result`과 `--save`는 동시 사용 가능.

#### 빌드 모드 동작

| 항목 | 기본 (증분 빌드) | `--reuse-build` | `--clean-build` |
|------|------------------|-----------------|-----------------|
| 빌드 디렉터리 삭제 | 생략 | 생략 | 실행 |
| CMake configure | 실행 | 생략 | 실행 |
| CMake build | 실행 | 생략 | 실행 |
| 빌드 디렉터리 미존재 시 | 생성 후 진행 | 에러 후 중단 | 생성 후 진행 |

- 기본 모드는 증분 빌드다.
- `--reuse-build`는 이미 빌드된 결과를 그대로 재사용할 때 지정한다.
- `--clean-build`는 빌드 디렉터리를 초기화한 뒤 완전 재빌드가 필요할 때 지정한다.

### 5.3 실행 예시

```bash
# 전체 멀티 패턴 실행 (stdout만)
perf/run_benchmarks_multi.sh

# 특정 패턴만 실행
perf/run_benchmarks_multi.sh --pattern MULTI_STREAM

# 여러 패턴
perf/run_benchmarks_multi.sh --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB

# 클라이언트 수/메시지 크기 제한
perf/run_benchmarks_multi.sh --multi-clients 1000 --msg-sizes 64,1024

# 레포트 저장 (report/)
perf/run_benchmarks_multi.sh --result

# 레포트 + 태그
perf/run_benchmarks_multi.sh --result --results-tag debug1

# baseline 저장 (baseline/, 타임스탬프 파일명)
perf/run_benchmarks_multi.sh --save

# baseline 저장 (baseline/v1.5.0.txt)
perf/run_benchmarks_multi.sh --runs 5 --save v1.5.0

# 레포트 + baseline 동시 저장
perf/run_benchmarks_multi.sh --result --save

# 5회 반복, CPU 고정, 레포트 저장
perf/run_benchmarks_multi.sh --runs 5 --pin-cpu --result

# 측정 시간 조정
perf/run_benchmarks_multi.sh --multi-warmup-seconds 5 --multi-duration-seconds 10
```

### 5.4 바이너리 직접 실행

개별 벤치마크 바이너리를 직접 실행할 수 있다. server/client를 별도 프로세스로 실행해야 한다.

```bash
# server 먼저 실행 (bind 후 READY,<endpoint> 출력)
<server_binary> <lib_name> <transport>

# client 실행 (server의 READY endpoint를 전달)
<client_binary> <lib_name> <transport> <size> --endpoint <endpoint>
```

```bash
# 예시: MULTI_DEALER_DEALER
# 터미널 1 (server)
./core/build/linux-x64/bin/perf_multi_dealer_dealer_server current tcp
# stdout: READY,tcp://0.0.0.0:15557

# 터미널 2 (client)
./core/build/linux-x64/bin/perf_multi_dealer_dealer_client current tcp 1024 --endpoint tcp://127.0.0.1:15557

# 예시: MULTI_STREAM 계열 (각각 별도 server/client 바이너리)
./core/build/linux-x64/bin/perf_multi_stream_server current tcp
./core/build/linux-x64/bin/perf_multi_stream_client current tcp 1024 --endpoint tcp://127.0.0.1:15557

./core/build/linux-x64/bin/perf_multi_stream_callback_server current tcp
./core/build/linux-x64/bin/perf_multi_stream_callback_client current tcp 1024 --endpoint tcp://127.0.0.1:15557

./core/build/linux-x64/bin/perf_multi_stream_len32be_server current tcp
./core/build/linux-x64/bin/perf_multi_stream_len32be_client current tcp 1024 --endpoint tcp://127.0.0.1:15557
```

| 인자 | 대상 | 설명 |
|------|------|------|
| `lib_name` | server/client | 라이브러리 식별자 (`current`) |
| `transport` | server/client | `tcp`, `tls`, `ws`, `wss` |
| `size` | client만 | 메시지 크기(bytes) |
| `--endpoint` | client만 | server가 READY로 출력한 endpoint 주소 |

---

## 6. 출력 형식

### 6.1 바이너리 RESULT line

각 바이너리는 `pattern/transport/size` 조합마다 아래 RESULT line을 stdout에 출력한다.

```text
# client 프로세스가 출력 (throughput, bandwidth, latency, client 리소스)
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,throughput,150000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,bandwidth,153.60
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency,45.23
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency_p95,61.40
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency_p99,79.85
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,client_cpu_pct,48.20
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,client_mem_mb,128.40

# server 프로세스가 출력 (server 리소스)
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,server_cpu_pct,35.10
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,server_mem_mb,64.20
```

| 필드 | 설명 |
|------|------|
| `lib` | 라이브러리 식별자 (`current`) |
| `pattern` | `MULTI_DEALER_DEALER`, `MULTI_STREAM` 등 |
| `transport` | `tcp`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`, `client_cpu_pct`, `client_mem_mb`, `server_cpu_pct`, `server_mem_mb` |
| `value` | 수치 값 (소수점 2자리) |

| metric | 출력 프로세스 | 설명 | 필수 |
|--------|-------------|------|------|
| `throughput` | client | echo 패턴: 왕복 완료 수 (`ops/s`), one-way 패턴: 단방향 수신 수 (`msg/s`) — 섹션 8.1 참조 | MUST |
| `bandwidth` | client | 네트워크 전송량 (MB/s) — 섹션 8.3 참조 | MUST |
| `latency` | client | 레이턴시 (us) | MUST |
| `latency_p95` | client | 95th percentile 레이턴시 (us) | MUST |
| `latency_p99` | client | 99th percentile 레이턴시 (us) | MUST |
| `client_cpu_pct` | client | client 프로세스 CPU 사용률 (%) | Linux/Windows |
| `client_mem_mb` | client | client 프로세스 메모리 (MB, RSS/WorkingSet 기준) | Linux/Windows |
| `server_cpu_pct` | server | server 프로세스 CPU 사용률 (%) | Linux/Windows |
| `server_mem_mb` | server | server 프로세스 메모리 (MB, RSS/WorkingSet 기준) | Linux/Windows |

- 리소스 메트릭은 server/client 프로세스별로 **독립 측정**한다.
- `client_cpu_pct`, `client_mem_mb`는 client 프로세스가 자체 PID를 대상으로 측정하여 출력한다.
- `server_cpu_pct`, `server_mem_mb`는 server 프로세스가 자체 PID를 대상으로 측정하여 출력한다.
- 리소스 메트릭이 누락되어도 완료 판정(`expected`/`actual`)에 영향을 주지 않는다.

### 6.2 스크립트 결과 테이블

> **구현 필수**: 스크립트는 RESULT line 파싱 외에 아래 형식의 사람이 읽을 수 있는 테이블을 **반드시 stdout에 출력하고, 결과 파일에도 기록**해야 한다. RESULT line만 출력하고 테이블을 생략하면 안 된다.

`run_benchmarks_multi.sh` 실행 시 패턴/transport별로 markdown table이 stdout에 출력되고, 결과 파일에도 기록된다. 디렉터리별 기록 형식:

| 디렉터리 | TABLE 기록 방식 |
|-----------|----------------|
| `tmp/` | META + RESULT 이후 `TABLE` 마커와 함께 기록 |
| `report/` | **실행 옵션 헤더 + TABLE** 기록 (META/RESULT 없음, `TABLE` 마커도 생략) |
| `baseline/` | META + RESULT 이후 `TABLE` 마커와 함께 기록 |

```text
## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |  9.6 MB/s |    45.23 us  |    61.40 us  |    79.85 us  |  35.1  |   64.2   |
| 1024B    |   120.30 Kmsg/s  |123.2 MB/s |    52.10 us  |    70.55 us  |    92.10 us  |  38.5  |   66.8   |
| 65536B   |    35.50 Kmsg/s  |2326.5 MB/s|   180.44 us  |  61.3  |  256.8   |  42.0  |   72.1   |


===============================================================================

## PATTERN: MULTI_STREAM (echo)

### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|--------|----------|
| 64B      |   320.00 Kops/s  | 41.0 MB/s |    32.10 us  |    45.20 us  |    61.40 us  |  30.8  |   58.4   |
| 1024B    |   280.50 Kops/s  |574.5 MB/s |    38.40 us  |    52.00 us  |    70.20 us  |  33.2  |   60.1   |
```

- **패턴 간 구분선**: 패턴이 바뀔 때 `===============================================================================` 구분선을 출력한다 (첫 번째 패턴 앞에는 출력하지 않음).
- throughput 단위: echo 패턴 `Kops/s` (ops/sec / 1000), one-way 패턴 `Kmsg/s` (msg/sec / 1000) — 섹션 8.1 참조
- bandwidth 단위: `MB/s` (메가바이트/초) — 섹션 8.3 참조
- latency 단위: `us` (마이크로초, mean/p95/p99)
- S.CPU% / S.Mem MB: server 프로세스 CPU 사용률 / 메모리
- transport 미지원 시: `N/A`
- 수집 실패 시: 리소스 컬럼은 `N/A`

### 6.3 진행 로그

실행 중 **사이즈별 결과 테이블 행을 즉시 출력**하여 진행 상황과 측정 데이터를 동시에 제공한다. 공통 규칙은 [PERF_POLICY.md § 5.2](PERF_POLICY.md)를 참조한다.

#### runs=1 출력 형식

`run N/M:` 및 `median:` 레이블 없이 테이블만 출력한다. 각 행은 client가 해당 size 측정을 완료한 즉시 출력된다.

```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,256B,1024B,65536B,131072B,262144B:
      | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
      |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
      | 64B      |    121.98 Kops/s |    15.61 MB/s |    812.10 us |   1012.22 us |   1258.44 us |    N/A |      N/A |
      | 256B     |    234.56 Kops/s |    60.05 MB/s |    745.01 us |    923.80 us |   1188.60 us |    N/A |      N/A |
      | 1024B    |    ...
      | 65536B   |    ...
      | 131072B  |    ...
      | 262144B  |    ...
    Testing tcp: Done
    [transport cooldown 3000ms]
    Testing tls | 64B,256B,1024B,65536B,131072B,262144B:
      | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
      |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
      | 64B      |    ...
```

#### runs > 1 출력 형식

각 run마다 테이블을 출력하고, 마지막에 `median:` 테이블을 출력한다.

```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,256B,1024B:
      run 1/3:
        | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
        | 64B      |    121.98 Kops/s |    15.61 MB/s |    812.10 us |   1012.22 us |   1258.44 us |    N/A |      N/A |
        | 256B     |    ...
        | 1024B    |    ...
      [cooldown 3000ms]
      run 2/3:
        | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
      [cooldown 3000ms]
      run 3/3:
        | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
      median:
        | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
    Testing tcp: Done
    [transport cooldown 3000ms]
```

- run 간 `[cooldown Nms]`, transport 간 `[transport cooldown Nms]` 표시
- 실패 발생 시: `(failures=N) Done`
- transport 미지원 시: `unsupported Done`

### 6.4 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- MULTI_STREAM current wss 65536B: timeout
```

### 6.5 결과 파일 저장

사용된 옵션에 따라 결과 파일이 아래 경로에 저장된다. 파일 형식은 섹션 4.1을 참조한다.

| 옵션 | 저장 경로 |
|------|-----------|
| (항상) | `perf/results/multi/tmp/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| `--result` | `perf/results/multi/report/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| `--save [VER]` | `perf/results/multi/baseline/<VER>.txt` 또는 `baseline/perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt` |

### 6.6 리소스 메트릭 수집

Multi 벤치마크는 server/client 별도 프로세스로 동작하므로, 각 프로세스가 **자체 PID를 대상으로** 리소스를 측정한다.

| 메트릭 | 출력 프로세스 | Linux 수집 소스 | Windows 수집 소스 | 계산 방식 |
|--------|-------------|----------------|-------------------|-----------|
| `client_cpu_pct` | client | `/proc/[pid]/stat` | `GetProcessTimes()` | `(user₂+kernel₂ - user₁-kernel₁) / (elapsed × nproc) × 100` |
| `client_mem_mb` | client | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 측정 종료 시점 1회 읽기, MB 변환 |
| `server_cpu_pct` | server | `/proc/[pid]/stat` | `GetProcessTimes()` | 동일 공식 (server 자체 PID) |
| `server_mem_mb` | server | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 동일 (server 자체 PID) |

```text
client 프로세스 내부:
[warmup] -> [settle] -> [throughput] -> [latency] -> [drain] -> [size_transition_drain]
                          ^            ^
                      샘플₁ 수집   샘플₂ 수집 → client_cpu_pct, client_mem_mb

server 프로세스:
[bind] -> [READY] -> [relay 대기] -> ... -> [종료 신호] -> server_cpu_pct, server_mem_mb 출력
                                                           (전체 실행 구간 기준)
```

- **client**: throughput phase 시작/종료 시점에 자체 PID를 대상으로 2회 샘플링하여 `client_cpu_pct`, `client_mem_mb`를 출력한다.
- **server**: 종료 신호 수신 시 bind~종료 전체 구간의 CPU/메모리를 측정하여 `server_cpu_pct`, `server_mem_mb`를 출력한다(전체 실행 구간 기준 1회).
- 리소스 메트릭은 정보성(informational)이므로 누락 시 완료 판정에 영향을 주지 않는다.

#### size별 귀속 규칙

client 프로세스는 1회 실행에서 모든 size를 순회한다. client 리소스 메트릭(`client_cpu_pct`, `client_mem_mb`)은 **size별로 독립 측정**한다.

| 항목 | 규칙 |
|------|------|
| `client_cpu_pct` | 각 size의 throughput phase 시작/종료 시점에 개별 샘플링 |
| `client_mem_mb` | 각 size의 throughput phase 종료 시점에 개별 읽기 |
| `server_cpu_pct` | 전체 실행 구간 1회 측정 (size별 분리 불가) |
| `server_mem_mb` | 종료 시점 1회 읽기 (size별 분리 불가) |

- client의 size 전환 시 `size_transition_drain`이 수행되므로 이전 size의 잔여 영향이 최소화된다.
- client의 size별 측정값이 아닌 바이너리 1회 실행 전체의 단일 측정값을 복제하는 것은 허용하지 않는다.
- server 리소스는 전체 실행 구간에 대한 값이므로 스크립트가 모든 size의 RESULT line에 동일한 server 값을 귀속시킨다.

---

## 7. Test Phase

### 7.0 전체 실행 구조

```text
┌─ pattern loop ──────────────────────────────────────────────────────────────┐
│  ┌─ transport loop ──────────────────────────────────────────────────────┐  │
│  │  ┌─ run loop ──────────────────────────────────────────────────────┐  │  │
│  │  │  [1] spawn server(pattern, transport)                           │  │  │
│  │  │  [2] wait READY,<endpoint>                                      │  │  │
│  │  │  [3] spawn client(pattern, transport, sizes, endpoint)          │  │  │
│  │  │      client 내부 (전체 size 순회):                               │  │  │
│  │  │        [warmup]-[settle]-[throughput]-[latency]-[drain]-[size_drain] │  │  │
│  │  │        [warmup]-[settle]-[throughput]-[latency]-[drain]-[size_drain] │  │  │
│  │  │        ...                                                      │  │  │
│  │  │  [4] client 종료, RESULT line 수집                              │  │  │
│  │  │  [5] server 종료, server RESULT line 수집                       │  │  │
│  │  │  → run_cooldown (3s)                                            │  │  │
│  │  └────────────────────────────────────────────────────────────────┘  │  │
│  │  → transport_transition_cooldown (3s)                                │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│  → pattern_transition_cooldown (3s)                                        │
└────────────────────────────────────────────────────────────────────────────┘
```

### 7.1 client 프로세스 내부 Phase (size 1개 기준)

```text
[warmup] -> [settle] -> [throughput] -> [latency] -> [drain] -> [size_transition_drain]
```

> echo는 client가 phase를 제어하며 server는 relay/echo 대기한다. one-way(server-push)는 server/client가 동일 순서의 phase를 수행한다.

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | time-based | 3s | `PERF_MULTI_WARMUP_SECONDS` |
| settle | time-based | 500ms | `PERF_MULTI_SETTLE_MS` |
| throughput | time-based | 5s | `PERF_MULTI_DURATION_SECONDS` |
| latency | time-based | 5s | `PERF_MULTI_DURATION_SECONDS` |
| drain | time-based | 패턴별 | `PERF_MULTI_DRAIN_MS` |
| size_transition_drain | time-based | 300ms | `PERF_MULTI_SIZE_TRANSITION_DRAIN_MS` |

### 7.2 스크립트 레벨 전환 cooldown

| 전환 구간 | 기본값 | 환경 변수 | 설명 |
|-----------|--------|-----------|------|
| run 간 cooldown | 3000ms | `PERF_MULTI_RUN_COOLDOWN_MS` | 동일 transport에서 반복 run 사이 안정화 |
| transport 전환 | 3000ms | `PERF_MULTI_TRANSPORT_TRANSITION_MS` | transport 변경 시 소켓 정리 대기 |
| pattern 전환 | 3000ms | `PERF_MULTI_PATTERN_TRANSITION_MS` | 패턴 변경 시 전체 클라이언트 소켓 정리 대기 |

- 전환 cooldown은 이전 run의 server/client 프로세스 양쪽 종료 후 다음 run의 server 실행 전에 스크립트에서 `sleep`으로 수행한다.
- Multi는 대량의 클라이언트 소켓(1000~10000)을 사용하므로 OS의 TIME_WAIT 소켓 해제를 위해 전환 cooldown이 필수적이다.
- 마지막 transport/pattern 이후에는 전환 cooldown을 수행하지 않는다 (불필요).

### 7.3 drain 패턴별 기본값

| 패턴 | drain 기본값 |
|------|-------------|
| MULTI_DEALER_DEALER | 300ms |
| MULTI_DEALER_ROUTER | 300ms |
| MULTI_ROUTER_ROUTER | 300ms |
| MULTI_PUBSUB | 300ms |
| MULTI_STREAM | 300ms |
| MULTI_STREAM_CALLBACK | 300ms |
| MULTI_STREAM_LEN32BE | 300ms |
| MULTI_GATEWAY | 0ms |
| MULTI_SPOT | 0ms |

### 7.4 warmup 모드

| 모드 | 설명 | 환경 변수 |
|------|------|-----------|
| passive (기본) | 송신 비활성 상태에서 시간 대기 | `PERF_MULTI_ACTIVE_WARMUP=0` |
| active | 송신/수신 활성 상태로 워밍업 | `PERF_MULTI_ACTIVE_WARMUP=1` |

active warmup 시 pre-measure drain: `PERF_MULTI_WARMUP_DRAIN_MS` (기본 `max(PERF_MULTI_DRAIN_MS, 1000)`)

---

## 8. Throughput 측정

### 8.1 패턴 방향 분류

각 패턴은 메시지 흐름 방향에 따라 **echo(왕복)** 또는 **one-way(단방향)**으로 분류되며, throughput 단위가 다르다.

| 방향 | 단위 | 의미 | 측정 지점 | 패턴 |
|------|------|------|-----------|------|
| echo | `ops/s` | 왕복 완료 수/초 | client 측 recv | MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_GATEWAY, MULTI_STREAM, MULTI_STREAM_CALLBACK, MULTI_STREAM_LEN32BE |
| one-way | `msg/s` | 단방향 수신 수/초 | receiver 측 recv | MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT |

- echo 패턴: client가 send → server echo → client recv. 1 rtt = 2 message hops. client가 echo를 수신한 횟수를 카운트한다.
- one-way 패턴: sender가 송신한 메시지를 receiver가 수신한다(서버 relay 또는 server push 포함). 1 msg = 1 message hop으로 보고, receiver 수신 수를 카운트한다.
- 동일 단위의 패턴 간에만 throughput을 직접 비교할 수 있다.

### 8.2 계산

1. duration 구간의 수신량으로 계산한다.
2. `throughput = recv_count / duration_seconds`
3. warmup/drain/size_transition_drain 구간의 데이터는 계산에서 제외한다.

### 8.3 Bandwidth (네트워크 전송량)

throughput과 메시지 크기로부터 실제 네트워크 전송량(MB/s)을 계산한다. 패턴 방향에 따라 계산이 다르다.

| 방향 | 계산식 | 의미 |
|------|--------|------|
| echo (`ops/s`) | `throughput × msg_size × 2 / 1,000,000` | 양방향 총 전송량 (send + recv) |
| one-way (`msg/s`) | `throughput × msg_size / 1,000,000` | 단방향 전송량 |

- 단위: `MB/s` (1 MB = 1,000,000 bytes, SI 기준)
- echo 패턴은 send/recv 양방향 데이터가 이동하므로 `×2`를 적용한다.
- bandwidth는 throughput 단위(ops/s vs msg/s)가 다른 패턴 간에도 **실제 데이터 처리량**으로 직접 비교할 수 있는 공통 지표이다.

---

## 9. Latency 측정

latency는 패턴 유형에 따라 측정 방식을 분리한다.

### 9.0 phase 순서

각 size는 아래 순서로 측정한다.

1. echo 패턴: warmup → settle → throughput phase → latency phase → drain/size transition drain
2. one-way 패턴: warmup → settle → throughput phase → latency phase → drain/size transition drain

- echo/one-way 모두 throughput/latency phase를 동일 실행/동일 설정(transport, clients, socket options)에서 순차 수행한다.

### 9.1 패턴별 divisor 규칙

| 유형 | divisor | 적용 패턴 |
|------|---------|-----------|
| 양방향 RTT | `roundtrip_count * 2` | MULTI_DEALER_ROUTER, MULTI_ROUTER_*, MULTI_GATEWAY, MULTI_STREAM, MULTI_STREAM_CALLBACK, MULTI_STREAM_LEN32BE |
| 단방향 | `received_count` | MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT |

### 9.2 계산식

- mean: 측정 phase에서 수집한 샘플의 산술 평균
- p95: 샘플의 95th percentile
- p99: 샘플의 99th percentile

- RTT 샘플(echo): `sample_us = elapsed_us / (roundtrip_count * 2)`
- 단방향 샘플(one-way): 수신 메시지에 포함된 송신 타임스탬프 기준 `now_us - sent_us`
- warmup/drain 구간의 데이터는 계산에서 제외한다.

### 9.3 one-way server-push 보정 규칙

`MULTI_DEALER_DEALER`, `MULTI_PUBSUB`, `MULTI_SPOT`는 server가 지속적으로 publish/push하므로, 큐 backlog가 곧바로 샘플 왜곡으로 이어질 수 있다. 이를 완화하기 위해 아래 규칙을 적용한다.

- one-way latency 샘플링 worker 수는 throughput 처리 worker와 동일한 `client_workers` 계산식을 사용한다(1 worker 강제 금지).
- 소켓 drain 루프에서 latency 샘플은 **drain cycle 내 최신 메시지 1개만** 반영한다(오래된 backlog 메시지 다중 반영 금지).
- mean은 전체 worker의 `lat_sum/lat_count`로 계산한다.
- p95/p99는 worker별 p95/p99를 각 worker `lat_count`로 가중 평균하여 합성한다.

---

## 10. Metric Tiers

### 10.1 Tier 1: 필수 (RESULT line 출력, 완료 판정 대상)

| 메트릭 | RESULT key | 단위 | 계산 방식 |
|--------|-----------|------|-----------|
| throughput | `throughput` | echo: `ops/s`, one-way: `msg/s` | `recv_count / duration_seconds` — 섹션 8.1 참조 |
| bandwidth | `bandwidth` | MB/s | 섹션 8.3 참조 |
| latency | `latency` | us | 패턴별 divisor 규칙 적용 (섹션 9.1) |
| latency p95 | `latency_p95` | us | 측정 샘플 95th percentile (echo/one-way 모두 latency phase) |
| latency p99 | `latency_p99` | us | 측정 샘플 99th percentile (echo/one-way 모두 latency phase) |

- Tier 1 메트릭이 누락되면 해당 조합은 fail로 처리한다.
- `expected`/`actual` 완료 판정은 Tier 1 RESULT line만 카운트한다 (조합당 5줄: throughput + bandwidth + latency + latency_p95 + latency_p99).

### 10.2 Tier 2: 권장 (RESULT line 미출력, 향후 확장 예약)

| 메트릭 | 단위 | 설명 |
|--------|------|------|
| `connect_ms` | ms | 전체 클라이언트 연결 완료 시간 |
| `ready_ms` | ms | 연결 후 준비 완료 대기 시간 |

- Tier 2 메트릭은 현재 RESULT line에 출력하지 않는다. 향후 구현 시 RESULT line에 추가할 수 있다.
- 누락 시 완료 판정에 영향 없음.

### 10.3 Tier 3: 정보성 (RESULT line 출력, 완료 판정 제외)

| 메트릭 | RESULT key | 출력 프로세스 | 단위 | 수집 방식 | 플랫폼 |
|--------|-----------|-------------|------|-----------|--------|
| client CPU | `client_cpu_pct` | client | % | Linux: `/proc/[pid]/stat`, Windows: `GetProcessTimes()` | Linux/Windows |
| client 메모리 | `client_mem_mb` | client | MB | Linux: `/proc/[pid]/status` VmRSS, Windows: `GetProcessMemoryInfo()` WorkingSetSize | Linux/Windows |
| server CPU | `server_cpu_pct` | server | % | 동일 (server 자체 PID) | Linux/Windows |
| server 메모리 | `server_mem_mb` | server | MB | 동일 (server 자체 PID) | Linux/Windows |

- server/client 별도 프로세스이므로 각 프로세스가 자체 PID를 대상으로 리소스를 측정한다.
- client 리소스는 size별 독립 측정, server 리소스는 전체 실행 구간 1회 측정 (섹션 6.6 참조).
- 누락 시 완료 판정에 영향 없음.
- 수집 실패 시 테이블에 `N/A`로 표시.

---

## 11. Pattern & Transport Matrix

### 11.1 지원 패턴

MULTI_DEALER_DEALER, MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_PUBSUB, MULTI_GATEWAY, MULTI_SPOT, MULTI_STREAM, MULTI_STREAM_CALLBACK, MULTI_STREAM_LEN32BE

#### 바인딩 소스 파일 명명 규칙

모든 벤치마크 소스 파일은 **`perf_`** 접두어를 사용한다. multi는 server/client 역할 분리를 필수로 하며, 소스 파일 분리는 권장이다. 소스 위치는 [PERF_POLICY.md § 2.0.2](PERF_POLICY.md)를 참조한다.

| 언어 | server 파일 | client 파일 | 예시 |
|------|-----------|-----------|------|
| Core (C++) | `perf_multi_<pattern>_server.cpp` | `perf_multi_<pattern>_client.cpp` | `perf_multi_stream_server.cpp` |
| C++ binding | `perf_multi_<pattern>_server.cpp` 또는 `perf_main.cpp --multi-server` | `perf_multi_<pattern>_client.cpp` 또는 `perf_main.cpp --multi-client` | `perf_multi_stream_server.cpp` |
| .NET | `PerfMulti<Pattern>Server.cs` 또는 `PerfMain --multi-server` | `PerfMulti<Pattern>Client.cs` 또는 `PerfMain --multi-client` | `PerfMultiStreamServer.cs` |
| Java | `PerfMulti<Pattern>Server.java` 또는 `PerfMain --multi-server` | `PerfMulti<Pattern>Client.java` 또는 `PerfMain --multi-client` | `PerfMultiStreamServer.java` |
| Node | `perf_multi_<pattern>_server.js` | `perf_multi_<pattern>_client.js` | `perf_multi_stream_server.js` |
| Python | `perf_multi_<pattern>_server.py` | `perf_multi_<pattern>_client.py` | `perf_multi_stream_server.py` |

- STREAM 계열은 각각 별도 server/client 파일: `stream`, `stream_callback`, `stream_len32be`
- 공통 유틸리티 파일도 `perf_` 접두어: `perf_common.hpp`, `PerfCommon.cs`, `PerfUtil.java`, `perf_common.py` 등
- 실행 스크립트: bindings는 `multi/run_benchmarks.sh` / `.ps1`, core는 `run_benchmarks_multi.sh` / `.ps1` ([PERF_POLICY.md § 3.1](PERF_POLICY.md) 참조)
- 파일 분리 대신 단일 runner를 사용하는 경우에도 실행 시점에서는 반드시 server/client 별도 프로세스로 동작해야 하며 READY/RESULT 프로토콜은 동일하게 준수한다.

#### 패턴별 소스 파일 / 바이너리 매핑 (Core)

server/client 분리 패턴은 **별도 소스 파일 / 별도 바이너리**로 작성하는 것을 원칙으로 한다. 기본 소스 경로: `perf/multi/current/`

| 패턴 | server 소스 | server 바이너리 | client 소스 | client 바이너리 |
|------|------------|----------------|------------|----------------|
| MULTI_DEALER_DEALER | `*_dealer_dealer_server.cpp` | `perf_multi_dealer_dealer_server` | `*_dealer_dealer_client.cpp` | `perf_multi_dealer_dealer_client` |
| MULTI_DEALER_ROUTER | `*_dealer_router_server.cpp` | `perf_multi_dealer_router_server` | `*_dealer_router_client.cpp` | `perf_multi_dealer_router_client` |
| MULTI_ROUTER_ROUTER | `*_router_router_server.cpp` | `perf_multi_router_router_server` | `*_router_router_client.cpp` | `perf_multi_router_router_client` |
| MULTI_PUBSUB | `*_pubsub_server.cpp` | `perf_multi_pubsub_server` | `*_pubsub_client.cpp` | `perf_multi_pubsub_client` |
| MULTI_GATEWAY | `*_gateway_server.cpp` | `perf_multi_gateway_server` | `*_gateway_client.cpp` | `perf_multi_gateway_client` |
| MULTI_SPOT | `*_spot_server.cpp` | `perf_multi_spot_server` | `*_spot_client.cpp` | `perf_multi_spot_client` |
| MULTI_STREAM | `*_stream_server.cpp` | `perf_multi_stream_server` | `perf/common/streamclient/perf_stream_client.cpp` (shared) | `perf_stream_client` (shared) |
| MULTI_STREAM_CALLBACK | `*_stream_callback_server.cpp` | `perf_multi_stream_callback_server` | `perf/common/streamclient/perf_stream_client.cpp` (shared) | `perf_stream_client` (shared) |
| MULTI_STREAM_LEN32BE | `*_stream_len32be_server.cpp` | `perf_multi_stream_len32be_server` | `perf/common/streamclient/perf_stream_client.cpp` (shared) | `perf_stream_client` (shared) |

> 위 표의 `*`는 `perf_multi`를 축약한 것이다 (예: `*_stream_server.cpp` = `perf_multi_stream_server.cpp`).
> STREAM client 예외(core): `MULTI_STREAM*` client는 [PERF_POLICY.md § 8.5](PERF_POLICY.md)의 STREAM client 예외에 따라 `perf/common/streamclient/` 공용 구현을 사용한다. server는 패턴별 분리를 유지해야 한다.

#### MULTI_STREAM 계열 패턴

> **STREAM 소켓은 multi suite에서만 테스트한다.** single suite에서는 STREAM 테스트를 수행하지 않는다. STREAM의 성능 특성은 대량 동시 연결 환경(multi)에서 평가하는 것이 의미 있으므로, 모든 STREAM 벤치마크는 multi suite에 집중한다.

| 패턴 | server 수신 방식 | 설명 |
|------|-----------------|------|
| MULTI_STREAM | 기본 recv 루프 | 기존 소켓 recv API(`zlink_recv`/`zmq_recv` 계열)로 메시지 수신 |
| MULTI_STREAM_CALLBACK | callback dispatch | stream dispatch callback API로 수신 |
| MULTI_STREAM_LEN32BE | callback + len32be framing | callback dispatch + 32-bit big-endian length-prefixed framing |

- 세 패턴은 동일한 transport, size, clients 설정을 공유한다.
- **Wire protocol**: client는 `[4B length (big-endian)][payload]` (len32be framing)으로 통일한다. server 수신 방식만 패턴별로 다르다. 상세는 [PERF_POLICY.md § 2.0.3 Wire Protocol](PERF_POLICY.md)을 참조한다.
- 수신 방식만 다르므로 throughput/latency 차이를 직접 비교할 수 있다.
- MULTI_STREAM 계열의 server 프로세스는 반드시 zlink STREAM 소켓으로 `bind`해야 하며, DEALER/ROUTER/PUBSUB 등 non-STREAM 소켓으로 대체할 수 없다.
- client 프로세스는 raw transport(`tcp`,`tls`,`ws`,`wss`)로 `connect`해야 하며, zlink STREAM 소켓의 client `connect()` 경로를 사용하지 않는다.
- 각 size 측정에서 `connect_ok`는 `target clients`와 동일해야 한다(100%). 하나라도 미달하면 해당 조합은 `fail`이다.
- 위 모델을 위반한 구현은 정책 위반이므로 해당 코드를 삭제하고 정책 모델로 다시 구현해야 한다.
- 위반 구현에서 나온 실행 결과는 정책 산출물로 인정하지 않는다.

### 11.2 표준 메시지 크기

| 패턴군 | 크기 |
|--------|------|
| MULTI_DEALER / MULTI_ROUTER / MULTI_PUBSUB | `[64, 256, 1024, 65536, 131072, 262144]` |
| MULTI_STREAM / MULTI_STREAM_CALLBACK / MULTI_STREAM_LEN32BE | `[64, 256, 1024, 65536]` |
| MULTI_GATEWAY / MULTI_SPOT | `[64, 256, 1024, 65536, 131072, 262144]` |

- STREAM 계열은 대량 동시 연결 환경에서 테스트하므로 65536B까지만 측정한다.

### 11.3 transport

| 패턴군 | transport |
|--------|-----------|
| 전체 | tcp, tls, ws, wss |

---

## 12. Environment Variables

### 12.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 0 |
| `PERF_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `PERF_TRANSPORTS` | 테스트 transport 목록 | `tcp,tls,ws,wss` |
| `PERF_TASKSET` | CPU pinning (`1`로 활성화, Linux: taskset, Windows: processor affinity) | 0 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`로 활성화) | 0 |

### 12.2 Phase 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_WARMUP_SECONDS` | warmup 시간(초) | 3 |
| `PERF_MULTI_DURATION_SECONDS` | 측정 시간(초) | 5 |
| `PERF_MULTI_SETTLE_MS` | settle 대기(ms) | 500 |
| `PERF_MULTI_DRAIN_MS` | drain 대기(ms) | 패턴별 |
| `PERF_MULTI_SIZE_TRANSITION_DRAIN_MS` | size 전환 drain(ms) | 300 |
| `PERF_MULTI_TRANSPORT_TRANSITION_MS` | transport 전환 cooldown(ms) | 3000 |
| `PERF_MULTI_PATTERN_TRANSITION_MS` | pattern 전환 cooldown(ms) | 3000 |
| `PERF_MULTI_ACTIVE_WARMUP` | active warmup 활성화 | 0 |
| `PERF_MULTI_WARMUP_DRAIN_MS` | active warmup 후 drain(ms) | `max(drain_ms, 1000)` |

### 12.3 클라이언트 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_CLIENTS` | 클라이언트 소켓 수 | 1000 |
| `PERF_MULTI_STREAM_MSG_SIZES` | STREAM 계열 전용 size 목록 | `64,256,1024,65536` |
| `PERF_MULTI_HWM` | 소켓 HWM | 1000 |
| `PERF_MULTI_SNDHWM` | 소켓 송신 HWM | `PERF_MULTI_HWM` |
| `PERF_MULTI_RCVHWM` | 소켓 수신 HWM | `PERF_MULTI_HWM` |
| `PERF_MULTI_CONNECT_CONCURRENCY` | 동시 연결 수 | 128 |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | 연결 준비 타임아웃(ms) | 5000 |

### 12.4 송수신 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_CLIENT_WORKERS` | 클라이언트 thread-pool 워커 수 (소켓 소유 스레드 수) | 4 |
| `PERF_MULTI_CLIENT_POLL_TIMEOUT_MS` | 클라이언트 워커 poll 타임아웃(ms) | 1 |
| `PERF_MULTI_SNDTIMEO_MS` | 송신 타임아웃(ms) | 200 |
| `PERF_MULTI_RCVTIMEO_MS` | 수신 타임아웃(ms) | 200 |
| `PERF_MULTI_MONITOR_HWM` | 모니터 소켓 HWM | 1000 |
| `PERF_MULTI_PUBSUB_XPUB_NODROP` | PUBSUB 서버의 `ZLINK_XPUB_NODROP` 기본값 | 1 |

- `PERF_MULTI_CLIENT_IDLE_SLEEP_US`, `PERF_MULTI_SEND_BACKOFF_US`, `PERF_MULTI_BLOCKING_SEND`는 삭제됐다 (항상 blocking send, 불필요 backoff 제거).
- `PERF_MULTI_RECV_BATCH`, `PERF_MULTI_SEND_WORKERS`, `PERF_SERVER_RECV_THREADS`는 삭제됐다. multi client는 소켓 소유 thread-pool 루프에서 readiness 기반으로 즉시 drain한다.

### 12.5 프로세스 조정

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_TIMEOUT_SECONDS` | client 실행 timeout(초) | auto (`duration`/`size` 기반) |
| `PERF_MULTI_SERVER_READY_TIMEOUT_MS` | server READY 대기 타임아웃(ms) | 10000 |
| `PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS` | server 종료 대기 타임아웃(ms) | 5000 |
| `PERF_MULTI_SERVER_BIND_PORT` | server bind 포트 (0=자동 할당) | 0 |

- server READY 타임아웃 초과 시 해당 run을 실패 처리하고 server 프로세스를 강제 종료한다.
- server 종료 대기 타임아웃 초과 시 SIGKILL (Linux) / TerminateProcess (Windows)로 강제 종료한다.
- `PERF_MULTI_SERVER_BIND_PORT=0`이면 OS가 사용 가능한 포트를 자동 할당한다. server는 실제 bind된 포트를 `READY,<endpoint>`에 포함하여 출력한다.

### 12.6 기타

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MAX_SOCKETS` | context max sockets | auto |
| `PERF_MULTI_RUN_COOLDOWN_MS` | run 간 cooldown(ms) | 3000 |
| `PERF_SKIP_NOFILE_CHECK` | nofile limit 검사 생략 | 0 |

> **삭제된 환경 변수**: `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS` 및 레거시 `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS`는 삭제 대상이다. 구현에 존재하면 제거해야 한다. Retry 금지 정책은 [PERF_POLICY.md § 8](PERF_POLICY.md)을 참조한다.
| `PERF_ROLLING_N` | rolling baseline 참조 파일 수 | 10 |
| `PERF_THRESHOLDS_FILE` | 임계치 override 설정 파일 경로 | `perf/thresholds.json` |

---

## 13. 구현 제약

### 13.1 측정 경로(hot path) lock 사용 금지

벤치마크 바이너리의 **측정 구간(active phase: throughput/latency)에서 실행되는 hot path**에 `std::mutex`, `std::condition_variable` 등 blocking synchronization primitive를 사용하지 않는다.

| 구분 | 허용 | 금지 |
|------|------|------|
| hot path (send/recv/callback 루프) | `std::atomic`, lock-free queue, SPSC ring buffer | `std::mutex`, `std::condition_variable`, `std::shared_mutex` |
| cold path (setup/teardown/결과 출력) | 제한 없음 | — |

- **이유**: lock contention이 throughput/latency 측정값에 포함되어 벤치마크 대상(라이브러리 성능)이 아닌 동기화 오버헤드를 측정하게 된다.
- **dispatch callback 패턴 (MULTI_STREAM_CALLBACK, MULTI_STREAM_LEN32BE)**: I/O 스레드에서 호출되는 dispatch callback과 측정 스레드 간 데이터 전달에 lock 대신 `std::atomic` 카운터 또는 lock-free queue를 사용한다.
- throughput 측정 시 callback에서 `atomic_fetch_add`로 카운트만 증가시키고, 패킷 복사/큐잉을 하지 않는 **direct count mode**를 기본으로 한다.
- latency 측정 등 패킷 내용이 필요한 경우에만 큐잉을 허용하되, lock-free 자료구조를 사용한다.
- **Multi의 sender/receiver 스레드 간 동기화**: 카운터·플래그 등은 `std::atomic`으로 구현하며, blocking lock을 사용하지 않는다.

### 13.2 불필요한 메모리 할당/복사 금지

벤치마크는 **라이브러리 자체의 성능만 온전히 측정**해야 한다. 측정 구간(active phase: throughput/latency)에서 벤치마크 코드가 유발하는 불필요한 메모리 할당·복사는 측정 결과를 왜곡하므로 금지한다.

| 구분 | 권장 | 금지 |
|------|------|------|
| 송신 버퍼 | warmup 전 사전 할당, duration 내 재사용 | 매 send마다 `std::vector` 생성/resize |
| 수신 버퍼 | 고정 크기 버퍼 또는 pool | 매 recv마다 동적 할당 |
| 수신 데이터 | 내용 검증 불필요 시 카운트만 증가 | 수신 payload를 별도 컨테이너에 복사 |
| dispatch callback | `atomic_fetch_add`로 카운트 (direct count mode) | 패킷을 `std::deque`에 push_back |
| routing_id | 필요 시 고정 버퍼에 1회 저장 | 매 메시지마다 `std::vector<unsigned char>` 할당 |
| 카운터/통계 | `std::atomic<int64_t>` | 구조체를 큐에 push |

- **원칙**: active phase(throughput/latency)에서 벤치마크 인프라 코드의 `malloc`/`new`/`vector::push_back` 호출이 0에 수렴해야 한다. 측정 결과에 라이브러리 외 오버헤드가 포함되면 패턴 간 비교(예: MULTI_STREAM vs MULTI_STREAM_CALLBACK)가 공정하지 않다.
- warmup phase 이전(setup/connect)과 drain 이후(결과 출력/정리)에서는 할당/복사에 제한이 없다.
- `zlink_msg_data()` 반환 포인터를 직접 참조하여 불필요한 복사를 피한다. 내용 검증이 필요 없는 throughput 측정에서는 payload를 읽지 않는다.
- Multi의 대량 클라이언트(1000~10000) 환경에서는 per-client 버퍼도 setup 시 사전 할당하고, duration 내에서 재사용한다.

### 13.3 연결 준비 확인: MONITOR 소켓 전용

client 프로세스가 server에 대한 **연결 완료(CONNECT READY)**를 확인할 때 반드시 **MONITOR 소켓**을 사용한다. Sleep, handshake barrier(첫 메시지 전송 성공 대기) 등 우회적 방법을 연결 확인 수단으로 사용하지 않는다.

| 항목 | 규칙 |
|------|------|
| 연결 확인 API | `zlink_socket_monitor_open()` + `zlink_monitor_recv()` |
| 감시 이벤트 | `ZLINK_EVENT_CONNECTION_READY` (필수), `ZLINK_EVENT_CONNECTED` · `ZLINK_EVENT_ACCEPTED` (호환용 선택) |
| 대기 방식 | `zlink_poll()` + 타임아웃 기반 — busy-wait/sleep 금지 |
| 타임아웃 | `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` (기본 5000ms) 초과 시 run 실패 처리 |
| Monitor HWM | `PERF_MULTI_MONITOR_HWM` (기본 1,000) — monitor event queue 상한 |

- **이유**: `ZLINK_EVENT_CONNECTION_READY`는 transport 레벨에서 연결이 확정된 시점을 정확히 통지한다. Sleep은 환경에 따라 불충분하거나 과다하고, handshake barrier는 메시지 전송 자체가 측정 인프라 오버헤드를 유발하여 정확한 준비 시점을 보장하지 못한다.
- 대기 함수 구현 시 `wait_connect_ready_count(monitor, expected_count, timeout_ms)` 형태로 N개 클라이언트의 연결 완료를 카운트 기반으로 확인한다.
- server 측에서 client 연결 수락 확인이 필요한 경우에도 동일하게 server 소켓에 MONITOR를 열어 `ZLINK_EVENT_ACCEPTED` / `ZLINK_EVENT_CONNECTION_READY`로 확인한다.

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

def bandwidth_mbps(throughput, msg_size, is_echo):
    """echo 패턴: 양방향(×2), one-way 패턴: 단방향"""
    multiplier = 2 if is_echo else 1
    return throughput * msg_size * multiplier / 1_000_000

def latency_rtt_us(elapsed_us, roundtrip_count):
    """MULTI_DEALER_ROUTER, MULTI_ROUTER_*, MULTI_GATEWAY, MULTI_STREAM*"""
    return elapsed_us / max(1, roundtrip_count * 2)

def latency_oneway_us(elapsed_us, count):
    """MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT: count=received_count"""
    return elapsed_us / max(1, count)
```
