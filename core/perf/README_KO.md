# Core PERF 벤치마크 스크립트

zlink 벤치마크 스위트를 구동하는 두 개의 셸 스크립트:

- `run_benchmarks.sh`: 단일 패턴 실행기 (PAIR, PUBSUB, DEALER_DEALER 등)
- `run_benchmarks_multi.sh`: 다중 패턴 래퍼 (다중 소켓 패턴)

## run_benchmarks.sh

현재 zlink의 단일 패턴 성능을 측정한다. 프로젝트를 빌드(또는 기존 빌드를 재사용)하고,
Python 비교 스크립트를 호출하여 결과를 저장한다.

### 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | `ALL` | 패턴 목록 (쉼표 구분) 또는 `ALL`. ALL 확장: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT |
| `--build` | 비활성 | CMake 클린 빌드 강제. 기본은 reuse-build (바이너리가 있으면 스킵) |
| `--build-dir PATH` | 자동 | 빌드 디렉터리 지정. 자동 감지: `core/build/<platform>-<arch>` |
| `--output PATH` | — | 콘솔 출력을 파일로 tee |
| `--save [VERSION]` | 비활성 | `results/<suite>/baseline/`에 베이스라인 저장. 선택적 버전 태그 |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 디렉터리 지정 |
| `--results-tag NAME` | — | 결과 파일명에 추가될 태그 |
| `--runs N` | 모드별 | 패턴/트랜스포트/크기별 반복 횟수. 기본: observe=1, trend=3, gate=5 |
| `--duration N` | `5` | 측정 시간 (초) |
| `--hwm N` | — | `PERF_SINGLE_HWM`으로 송수신 HWM 공통 fallback 설정 |
| `--send-hwm N` | — | `PERF_SINGLE_SNDHWM` 설정 (송신 큐 HWM) |
| `--recv-hwm N` | — | `PERF_SINGLE_RCVHWM` 설정 (수신 큐 HWM) |
| `--pin-cpu` | 비활성 | Linux `taskset`으로 CPU 코어 고정 |
| `--io-threads N` | — | 벤치마크 바이너리의 `PERF_IO_THREADS` 설정 |
| `--msg-sizes LIST` | — | 페이로드 크기 목록 (쉼표 구분, 예: `64,1024,65536`) |
| `--transports LIST` | — | 트랜스포트 목록 (쉼표 구분, 예: `tcp,tls`) |

### 정책 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--mode MODE` | `observe` | `observe`, `trend`, `gate` 중 선택 |
| `--baseline-file PATH` | — | gate 모드용 베이스라인 파일 |
| `--rolling-n N` | `10` | trend 모드의 롤링 베이스라인 윈도우 |

### 모드

| 모드 | 설명 |
|------|------|
| `observe` | 메트릭 수집만, 베이스라인 비교 없음. 기본 runs=1 |
| `trend` | 롤링 베이스라인 대비 비교 (경고만). 기본 runs=3 |
| `gate` | 고정 베이스라인 대비 비교 (경고 + 실패). 기본 runs=5 |

### 실행 흐름

```
1. 플랫폼 (linux/macos/windows) 및 아키텍처 (x64/arm64) 감지
2. 빌드 디렉터리 결정
3. 빌드 또는 기존 빌드 재사용 (CMake Release, 벤치마크 ON)
4. 오래된 결과 디렉터리 정리 (90일 보존)
5. Python 비교 스크립트 호출:
   - 패턴 목록, 빌드 디렉터리, 반복 횟수, 측정 시간
   - 모드, rolling-n, baseline-file
   - result-file (tmp 출력)
6. 환경 변수 전달:
   PERF_IO_THREADS, PERF_MSG_SIZES, PERF_TRANSPORTS,
   PERF_SINGLE_DURATION_SECONDS, PERF_SINGLE_HWM,
   PERF_SINGLE_SNDHWM, PERF_SINGLE_RCVHWM, PERF_NO_AUTOBUILD
7. 종료 시 총 경과 시간 출력
```

### 결과 저장

```
results/
  single/
    tmp/          ← 항상 저장 (perf_<platform>_<timestamp>[_<tag>].txt)
    report/       ← 리포트 저장 (항상 활성)
    baseline/     ← --save [VERSION] 시에만 저장
  multi/
    tmp/          ← multi 패턴 저장 위치
    report/
    baseline/
```

### 제약 사항

- `PERF_ALLOW_MULTI=1` 설정 없이 MULTI_* 패턴 사용 불가
- 단일 패턴과 multi 패턴을 한 실행에서 혼합 불가
- 빌드 디렉터리는 레포 루트 내에 위치해야 함

---

## run_benchmarks_multi.sh

다중 소켓 벤치마크 패턴 전용 래퍼. multi 전용 환경 변수를 설정하고
`PERF_ALLOW_MULTI=1`로 `run_benchmarks.sh`에 위임한다.

### 기본 패턴

```
DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER,
PUBSUB, GATEWAY, SPOT,
STREAM, STREAM_CALLBACK, STREAM_LEN32BE
```

기본 트랜스포트: `tcp,tls,ws,wss`

### 옵션

공유 옵션 (`run_benchmarks.sh`로 전달):

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | 전체 패턴 | 패턴 목록 (쉼표 구분) 또는 `ALL`. `MULTI_` 접두어 생략 가능 |
| `--build` | 비활성 | 클린 빌드 강제 |
| `--build-dir PATH` | 자동 | 빌드 디렉터리 지정 |
| `--output PATH` | — | 결과를 파일로 tee |
| `--save [VER]` | 비활성 | `results/multi/baseline/`에 베이스라인 저장 |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 디렉터리 지정 |
| `--results-tag NAME` | — | 파일명 태그 |
| `--runs N` | `1` | 설정별 반복 횟수 |
| `--pin-cpu` | 비활성 | CPU 코어 고정 |
| `--io-threads N` | — | I/O 워커 스레드 수 |
| `--msg-sizes LIST` | — | 메시지 크기 목록 (쉼표 구분) |
| `--transports LIST` | `tcp,tls,ws,wss` | 트랜스포트 목록 (쉼표 구분) |

정책 옵션:

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--mode MODE` | `observe` | `observe`, `trend`, `gate` |
| `--rolling-n N` | `10` | 롤링 베이스라인 윈도우 |
| `--baseline-file PATH` | — | gate 모드용 고정 베이스라인 |
| `--warn-throughput-pct N` | `10` | 처리량 경고 하락 임계값 (%) |
| `--fail-throughput-pct N` | `15` | 처리량 실패 하락 임계값 (%) |
| `--warn-latency-pct N` | `10` | 레이턴시 경고 상승 임계값 (%) |
| `--fail-latency-pct N` | `15` | 레이턴시 실패 상승 임계값 (%) |

Multi 전용 옵션:

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--warmup N` | `3` | 워밍업 시간 (초) |
| `--duration N` | `5` | 측정 시간 (초) |
| `--clients N` | `1000` | 패턴당 클라이언트 소켓 수 |
| `--hwm N` | `1000` | HWM (송수신 큐 깊이) |
| `--send-hwm N` | `--hwm` | 송신 큐 HWM (`PERF_MULTI_SNDHWM`) |
| `--recv-hwm N` | `--hwm` | 수신 큐 HWM (`PERF_MULTI_RCVHWM`) |
| `--send-timeout-ms N` | `5000` | 송신 타임아웃 (ms) |
| `--recv-timeout-ms N` | `5000` | 수신 타임아웃 (ms) |
| `--connect-concurrency N` | 자동 | 동시 연결 수. 자동: 128 (< 1만 클라이언트), 1024 (>= 1만) |
| `--drain-ms N` | 패턴별 | 측정 후 드레인 대기 (ms). 대부분 300, GATEWAY/SPOT은 0 |
| `--transport-transition-ms N` | `3000` | 트랜스포트 전환 간 대기 (ms) |
| `--pattern-transition-ms N` | `3000` | 패턴 전환 간 대기 (ms) |
| `--server-ready-timeout-ms N` | `10000` | 서버 준비 대기 (ms) |
| `--connect-ready-timeout-ms N` | `5000` | 연결 준비 대기 (ms) |
| `--monitor-hwm N` | `1000` | 모니터 HWM |
| `--server-shutdown-timeout-ms N` | `5000` | 서버 종료 유예 (ms) |
| `--server-bind-port N` | `0` (자동) | 서버 바인드 포트 (0 = OS 자동 할당) |

### 사전 검사: nofile 제한

각 패턴 실행 전에 OS 파일 디스크립터 제한을 확인한다:

```
필요량 = clients × 3 + 4096
```

소프트 제한이 부족하면 `ulimit -Sn`으로 하드 제한까지 상향을 시도한다.
그래도 부족하면 해당 패턴은 **스킵** (실패가 아님)된다.
`PERF_SKIP_NOFILE_CHECK=1`로 비활성화 가능.

### 실행 흐름

```
1. CLI 옵션 파싱 및 검증
2. 패턴 목록 결정 (기본: 9개 패턴 전체, MULTI_ 접두어 자동 부여)
3. 각 패턴에 대해:
   a. 클라이언트 수 결정 (--clients 또는 패턴 기본값 1000)
   b. nofile 제한 사전 검사 → 실패 시 스킵
4. 환경 변수 배열 구성 (PERF_* 접두사)
5. run_benchmarks.sh 호출:
   - PERF_ALLOW_MULTI=1
   - 모든 multi 전용 환경 변수
   - 병합된 패턴 목록을 단일 --pattern 인자로
6. 스킵/실패 패턴 보고
7. 총 경과 시간 출력
```

### 환경 변수

모든 옵션은 `PERF_` 접두사 환경 변수로 설정 가능하다.
CLI 옵션이 우선한다.

| 환경 변수 | CLI 대응 |
|-----------|----------|
| `PERF_MULTI_CLIENTS` | `--clients` |
| `PERF_MULTI_HWM` | `--hwm` |
| `PERF_MULTI_SNDHWM` | `--send-hwm` |
| `PERF_MULTI_RCVHWM` | `--recv-hwm` |
| `PERF_MULTI_SNDTIMEO_MS` | `--send-timeout-ms` |
| `PERF_MULTI_RCVTIMEO_MS` | `--recv-timeout-ms` |
| `PERF_MULTI_CONNECT_CONCURRENCY` | `--connect-concurrency` |
| `PERF_MULTI_DRAIN_MS` | `--drain-ms` |
| `PERF_MULTI_WARMUP_SECONDS` | `--warmup` |
| `PERF_MULTI_DURATION_SECONDS` | `--duration` |
| `PERF_MULTI_TRANSPORT_TRANSITION_MS` | `--transport-transition-ms` |
| `PERF_MULTI_PATTERN_TRANSITION_MS` | `--pattern-transition-ms` |
| `PERF_MULTI_SERVER_READY_TIMEOUT_MS` | `--server-ready-timeout-ms` |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | `--connect-ready-timeout-ms` |
| `PERF_MULTI_MONITOR_HWM` | `--monitor-hwm` |
| `PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS` | `--server-shutdown-timeout-ms` |
| `PERF_MULTI_SERVER_BIND_PORT` | `--server-bind-port` |
| `PERF_MULTI_TIMEOUT_SECONDS` | — (env 전용 client timeout override) |
| `PERF_MULTI_DEFAULT_CLIENTS` | — (--clients 미설정 시 기본 클라이언트 수) |
| `PERF_MULTI_DEFAULT_STREAM_CLIENTS` | — (STREAM 패턴의 기본 클라이언트 수) |
| `PERF_SKIP_NOFILE_CHECK` | — (nofile 사전 검사 비활성화) |
| `PERF_RESULTS_RETENTION_DAYS` | — (오래된 결과 정리 임계값, 기본 90일) |

### 실행 예시

전체 multi 패턴 기본 실행:

```bash
./core/perf/run_benchmarks_multi.sh
```

특정 패턴, 커스텀 클라이언트 수 및 측정 시간:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --clients 5000 \
  --duration 10 \
  --transports tcp
```

gate 모드 + 명시적 베이스라인:

```bash
./core/perf/run_benchmarks_multi.sh \
  --mode gate \
  --baseline-file core/perf/results/multi/baseline/v1.0.txt \
  --fail-throughput-pct 20
```

베이스라인 저장:

```bash
./core/perf/run_benchmarks_multi.sh --save v2.0
```

단일 패턴 벤치마크 실행:

```bash
./core/perf/run_benchmarks.sh --pattern PAIR --duration 10 --runs 3
```

전체 표준 단일 패턴 실행:

```bash
./core/perf/run_benchmarks.sh
```
