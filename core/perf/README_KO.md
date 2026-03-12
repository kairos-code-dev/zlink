# Core PERF 벤치마크 스크립트

zlink perf는 아래 두 개의 진입 스크립트로 실행한다.

- `run_benchmarks.sh`: single 패턴 실행기
- `run_benchmarks_multi.sh`: multi 패턴 래퍼

공식 결과 파일은 항상 `core/perf/results/.../report/` 아래에 저장된다.

---

## run_benchmarks.sh

single 패턴(PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER,
ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT)의 성능을 측정한다.

### 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | `ALL` | 패턴 목록(쉼표 구분) 또는 `ALL` |
| `--reuse-build` | 비활성 | 기존 빌드 디렉터리 재사용(configure/build 생략) |
| `--clean-build` | 비활성 | 빌드 디렉터리 삭제 후 클린 빌드 |
| `--build-dir PATH` | 자동 | 빌드 디렉터리 지정 |
| `--output PATH` | — | 콘솔 출력을 파일로 tee |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 경로 지정 |
| `--results-tag NAME` | — | 결과 파일명 태그 |
| `--runs N` | `1` | pattern/transport/size별 반복 횟수 |
| `--duration N` | `5` | active 측정 시간(초) |
| `--hwm N` | — | `PERF_SINGLE_HWM` fallback 설정 |
| `--send-hwm N` | — | `PERF_SINGLE_SNDHWM` 설정 |
| `--recv-hwm N` | — | `PERF_SINGLE_RCVHWM` 설정 |
| `--pin-cpu` | 비활성 | CPU 고정(Linux taskset) |
| `--io-threads N` | — | `PERF_IO_THREADS` 설정 |
| `--msg-sizes LIST` | — | 메시지 크기 목록(쉼표 구분) |
| `--transports LIST` | — | 트랜스포트 목록(쉼표 구분) |

참고: `pgm`/`epgm`은 single perf에서 현재 비활성화 상태다.

### 실행 모델(single)

- `pattern/transport/size/run` 조합마다 바이너리를 별도 프로세스로 실행
- 바이너리 phase: `warmup(count) -> active(duration)`
- active 구간에서 throughput + latency를 **동시에** 측정
- 집계는 payload header 검증 성공 데이터만 사용(header 기반 집계)
- 재시도/드레인 단계 없음

### 결과 저장

```text
results/
  single/
    report/
      perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
```

---

## run_benchmarks_multi.sh

multi 패턴 래퍼 스크립트다. multi 옵션을 정규화한 뒤 `PERF_ALLOW_MULTI=1`
환경으로 `run_benchmarks.sh`를 호출한다.

### 기본 패턴

`DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,GATEWAY,SPOT,STREAM,STREAM_CALLBACK,STREAM_LEN32BE`

### 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | 기본 전체 | 패턴 목록(쉼표 구분), `MULTI_` 접두어 생략 가능 |
| `--help` | — | 도움말 |
| `--reuse-build` | 비활성 | 기존 빌드 재사용 |
| `--clean-build` | 비활성 | 클린 빌드 |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 경로 |
| `--results-tag NAME` | — | 파일명 태그 |
| `--build-dir PATH` | 자동 | 빌드 디렉터리 지정 |
| `--output PATH` | — | 출력을 파일로 tee |
| `--runs N` | `1` | 설정별 반복 횟수 |
| `--pin-cpu` | 비활성 | CPU 고정 |
| `--io-threads N` | — | 서버/클라이언트 io thread 동시 설정 |
| `--server-io-threads N` | non-stream=`2`, stream=`4` | 서버 io threads |
| `--client-io-threads N` | non-stream=`2`, stream=`4` | 클라이언트 io threads |
| `--msg-sizes LIST` | env/기본값 | 메시지 크기 목록 |
| `--transports LIST` | `tcp,tls,ws,wss` | 트랜스포트 목록 |
| `--warmup N` | `2` | warmup 시간(초) |
| `--duration N` | `5` | active 측정 시간(초) |
| `--clients N` | `100` (`stream=10000`) | 패턴별 클라이언트 수 |
| `--hwm N` | env/바이너리 기본값 | `PERF_MULTI_HWM` 설정 |
| `--send-hwm N` | `--hwm` fallback | `PERF_MULTI_SNDHWM` 설정 |
| `--recv-hwm N` | `--hwm` fallback | `PERF_MULTI_RCVHWM` 설정 |
| `--sndtimeo N` / `--send-timeout-ms N` | `200` | `PERF_MULTI_SNDTIMEO_MS` |
| `--rcvtimeo N` / `--recv-timeout-ms N` | `200` | `PERF_MULTI_RCVTIMEO_MS` |
| `--connect-concurrency N` | 자동 | 동시 연결 수 |
| `--transport-transition-ms N` | `3000` | 트랜스포트 전환 대기 |
| `--pattern-transition-ms N` | `3000` | 패턴 전환 대기 |
| `--server-ready-timeout-ms N` | `10000` | 서버 준비 대기 |
| `--connect-ready-timeout-ms N` | `5000` | 연결 준비 대기 |
| `--monitor-hwm N` | `1000` | 모니터 HWM |
| `--server-shutdown-timeout-ms N` | `5000` | 서버 종료 대기 |
| `--server-bind-port N` | `0` | 서버 바인드 포트 |

### 결과 저장

```text
results/
  multi/
    report/
      perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
```

### 사전 검사

- nofile 가드 (`PERF_SKIP_NOFILE_CHECK=1`로 비활성화)
- 메모리 가드 (`PERF_SKIP_MEMORY_CHECK=1`로 비활성화)
  - `PERF_MULTI_MEMORY_BUDGET_PCT=70` — MemAvailable 대비 예산 비율(%)
  - `PERF_MULTI_MEMORY_BASE_MB=512` — 기본 메모리 예약(MB)
  - `PERF_MULTI_MEMORY_PER_CLIENT_KB=1024` — 클라이언트당 예상 메모리(KB)

---

## 공통 환경 변수

| 변수 | 의미 |
|------|------|
| `PERF_IO_THREADS` | I/O thread 수 |
| `PERF_MSG_SIZES` | 메시지 크기 override |
| `PERF_TRANSPORTS` | 트랜스포트 override |
| `PERF_RESULTS_DIR` | 결과 루트 경로 override |
| `PERF_RESULTS_TAG` | 파일명 태그 |
| `PERF_RESULTS_MAX_FILES` | report/ 디렉터리 최대 파일 수 (기본: 100) |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`) |
| `PERF_TASKSET` | CPU 고정 (`1`) |

single 상세 제약/정책은 `PERF_SINGLE_TEST_POLICY.md`,
multi 상세 제약/정책은 `PERF_MULTI_TEST_POLICY.md`를 따른다.

---

## 빠른 예시

single 전체 실행:

```bash
./core/perf/run_benchmarks.sh
```

single 제한 실행:

```bash
./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5
```

multi 전체 실행:

```bash
./core/perf/run_benchmarks_multi.sh
```

multi STREAM만 실행:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --clients 5000 \
  --duration 10 \
  --transports tcp
```
